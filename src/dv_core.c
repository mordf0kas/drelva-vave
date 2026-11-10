/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* The Drelva Vave instrument: clock, six tracks, and one screen.
 *
 * Panel:
 *   PLAY            start / stop (the pattern restarts)
 *   FX SCL ENV LFO EDIT GLO   tracks 1..6; with HOME held: mute
 *   SELECT          track
 *   OCT- / OCT+     page of the track's machine
 *   KNOB 1..4       the four parameters of the page
 *   ALGORITHM       tempo
 *   PRESETS         level of the track
 *   SEQ             a new pattern from the machine
 *   keys            go to the track's machine (Mini Atlantis: transpose)
 *
 * Clock: internal by default. When MIDI clock arrives (USB, DIN or Bluetooth) the tempo
 * follows it: the tick period is measured over the last two beats, the internal 1/16 clock
 * runs at that tempo and is pulled gently towards the master's 1/16 grid (steps are counted
 * on both sides, so it cannot settle a step away). The jitter of a Bluetooth link is averaged
 * out instead of being played.
 * MIDI Start, Continue and Stop drive the transport. */
#include "dv.h"
#include "dv_dsp.h"
#include "dv_gfx.h"

typedef struct {
    const dv_machine_t *machine;
    uint8_t page, mute;
    int16_t level;                 /* 0..100 */
} track_t;

static track_t tracks[DV_TRACKS];
static uint32_t held_buttons, held_keys;
static uint8_t selected, playing;
static int16_t bpm = 134;
static volatile uint8_t step16;
static float to_next_step;         /* samples until the next 1/16 */
static volatile uint8_t restart;
static float meter[DV_TRACKS];     /* peak per track, for the screen */

/* external clock */
#define EXT_HISTORY 48u            /* ticks kept: two beats */
static uint32_t now;               /* samples rendered so far */
static uint32_t ext_times[EXT_HISTORY], ext_count;
static float ext_period;           /* samples per MIDI tick (24 per quarter) */
static uint8_t ext_locked, ext_tick;             /* ext_tick: 0..5 inside the 1/16 */
static volatile uint8_t ext_phase_pending, ext_first_step, ext_align;
static int32_t ext_steps, int_steps;             /* 1/16 boundaries: the master's, ours */

void dv_midi_realtime(uint8_t status)
{
    if (status == 0xFA) {                       /* start: the next tick is step 0 */
        playing = 1;
        ext_tick = 0;
        ext_first_step = 1;
    } else if (status == 0xFB) {
        playing = 1;
        ext_align = 1;
    } else if (status == 0xFC) {
        playing = 0;
    } else if (status == 0xF8) {
        uint32_t last = ext_count ? ext_times[(ext_count - 1u) % EXT_HISTORY] : 0u;
        if (ext_count && now - last > (uint32_t)DV_FS / 2u) {   /* the clock came back */
            ext_count = 0;
            ext_locked = 0;
            ext_align = 1;
        }
        ext_times[ext_count % EXT_HISTORY] = now;
        ext_count++;
        if (ext_count >= 7u) {                  /* a 1/16 of history at least */
            uint32_t span = ext_count < EXT_HISTORY ? ext_count - 1u : EXT_HISTORY - 1u;
            float measured = (float)(now - ext_times[(ext_count - 1u - span) % EXT_HISTORY]) / (float)span;
            ext_period = ext_locked ? ext_period + (measured - ext_period) * 0.2f : measured;
            ext_locked = 1;
        }
        if (ext_tick == 0) ext_phase_pending = 1;
        ext_tick = (uint8_t)((ext_tick + 1u) % 6u);
    }
}

void dv_init(void)
{
    static const dv_machine_t *const layout[DV_TRACKS] = {&DV_SISMO_BOX, &DV_MINI_ATLANTIS, 0, 0, 0, 0};
    for (unsigned t = 0; t < DV_TRACKS; t++) {
        tracks[t].machine = layout[t];
        tracks[t].page = 0;
        tracks[t].mute = 0;
        tracks[t].level = 80;
        if (layout[t]) layout[t]->init();
    }
    selected = 0;
    playing = 0;
    step16 = 15;
    to_next_step = 0.0f;
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

void dv_buttons(uint32_t held)
{
    uint32_t pressed = held & ~held_buttons;
    held_buttons = held;
    track_t *t = &tracks[selected];
    if (pressed & (1u << DV_BTN_PLAY)) {
        playing = !playing;
        if (playing) { restart = 1; ext_align = 1; }
    }
    for (unsigned k = 0; k < DV_TRACKS; k++) {
        if (!(pressed & (1u << (DV_BTN_FX + k)))) continue;
        if (held & (1u << DV_BTN_HOME)) tracks[k].mute = !tracks[k].mute;
        else selected = (uint8_t)k;
    }
    if (!t->machine) return;
    if ((pressed & (1u << DV_BTN_OCT_DN)) && t->page > 0) t->page--;
    if ((pressed & (1u << DV_BTN_OCT_UP)) && t->page + 1 < t->machine->npages) t->page++;
    if ((pressed & (1u << DV_BTN_SEQ)) && t->machine->action) t->machine->action();
}

void dv_keys(uint32_t held)
{
    uint32_t pressed = held & ~held_keys;
    const dv_machine_t *m = tracks[selected].machine;
    held_keys = held;
    if (!pressed || !m || !m->key) return;
    for (int note = 26; note >= 0; note--)
        if (pressed & (1u << note)) { m->key(note); break; }
}

void dv_encoder(unsigned id, int delta)
{
    track_t *t = &tracks[selected];
    if (!delta) return;
    if (id == DV_ENC_SELECT) {
        selected = (uint8_t)clampi((int)selected + (delta > 0 ? 1 : -1), 0, (int)DV_TRACKS - 1);
    } else if (id == DV_ENC_ALGORITHM) {
        if (!ext_locked) bpm = (int16_t)clampi(bpm + delta, 60, 200);
    } else if (id == DV_ENC_PRESETS) {
        t->level = (int16_t)clampi(t->level + delta, 0, 100);
    } else if (id >= DV_ENC_KNOB1 && id <= DV_ENC_KNOB4 && t->machine) {
        const dv_machine_t *m = t->machine;
        int p = m->layout[t->page][id - DV_ENC_KNOB1];
        if (p < 0) return;
        int step = delta * (delta > 2 || delta < -2 ? 3 : 1);   /* fast turns go further */
        int value = clampi(m->values[p] + step, m->params[p].min, m->params[p].max);
        if (value != m->values[p]) {
            m->values[p] = (int16_t)value;
            m->update();
        }
    }
}

static float step_len_now;
static void reset_machines(void)
{
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine && tracks[t].machine->reset) tracks[t].machine->reset();
}

static void advance_step(void)
{
    step16 = (uint8_t)((step16 + 1u) & 15u);
    int_steps++;
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine) tracks[t].machine->step(step16, step_len_now);
}

void dv_render(float stems[DV_TRACKS][DV_BLOCK], float *left, float *right)
{
    float step_len = (float)DV_FS * 15.0f / (float)bpm;   /* samples per 1/16 */
    unsigned done = 0;
    if (ext_locked && now - ext_times[(ext_count - 1u) % EXT_HISTORY] > (uint32_t)DV_FS) {
        ext_locked = 0;                         /* clock gone: back to our own tempo */
        ext_count = 0;
    }
    if (ext_locked) step_len = ext_period * 6.0f;
    step_len_now = step_len;
    if (restart) {
        restart = 0;
        step16 = 15;
        to_next_step = 0.0f;
        reset_machines();
    }
    if (ext_phase_pending) {                    /* a 1/16 boundary of the master is now */
        ext_phase_pending = 0;
        ext_steps++;
        if (ext_first_step) {                   /* after Start: step 0 falls on this tick */
            ext_first_step = 0;
            ext_align = 0;
            step16 = 15;
            to_next_step = 0.0f;
            reset_machines();
            ext_steps = int_steps;
        } else if (ext_align) {                 /* no Start seen: take the nearest boundary */
            ext_align = 0;
            ext_steps = to_next_step > step_len * 0.5f ? int_steps - 1 : int_steps;
        }
        if (playing && ext_locked) {
            /* samples until we play the step the master is playing now (negative: played already) */
            float error = to_next_step + (float)(ext_steps - int_steps) * step_len;
            to_next_step -= error * 0.15f;
        }
    }
    now += DV_BLOCK;
    while (done < DV_BLOCK) {
        unsigned n = DV_BLOCK - done;
        if (playing) {
            if (to_next_step < 1.0f) {
                advance_step();
                to_next_step += step_len;
            }
            if ((float)n > to_next_step) n = (unsigned)to_next_step;
            if (n == 0) n = 1;
            to_next_step -= (float)n;
        }
        for (unsigned t = 0; t < DV_TRACKS; t++) {
            if (tracks[t].machine) {
                tracks[t].machine->render(stems[t] + done, n);
            } else {
                for (unsigned i = 0; i < n; i++) stems[t][done + i] = 0.0f;
            }
        }
        done += n;
    }
    for (unsigned i = 0; i < DV_BLOCK; i++) left[i] = 0.0f;
    for (unsigned t = 0; t < DV_TRACKS; t++) {
        float gain = tracks[t].mute ? 0.0f : (float)tracks[t].level * 0.01f;
        gain *= gain;                                       /* a gentler taper */
        float peak = meter[t] * 0.985f;
        for (unsigned i = 0; i < DV_BLOCK; i++) {
            float x = stems[t][i] *= gain;
            left[i] += x;
            if (x < 0.0f) x = -x;
            if (x > peak) peak = x;
        }
        meter[t] = peak;
    }
    for (unsigned i = 0; i < DV_BLOCK; i++) left[i] = right[i] = dv_softclip(left[i]);
}

uint32_t dv_led_buttons(void)
{
    return (playing ? 1u << DV_BTN_PLAY : 0u) | 1u << (DV_BTN_FX + selected);
}
uint32_t dv_led_keys(void) { return 0; }

/* ------------------------------------------------------------------ screen --- */
static const char *number(int v, char *buf)
{
    char tmp[8];
    int n = 0, neg = v < 0;
    unsigned u = (unsigned)(neg ? -v : v);
    do { tmp[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    char *p = buf;
    if (neg) *p++ = '-';
    while (n) *p++ = tmp[--n];
    *p = 0;
    return buf;
}

/* a parameter as a house knob: a 270 degree track, the value in lime with a bright tip;
 * parameters that go below zero grow from the top */
static void knob(uint16_t *fb, float cx, float cy, const dv_param_t *d, int value)
{
    const float lo = 0.625f, sweep = 0.75f, r = 15.0f;
    float pos = (float)(value - d->min) / (float)(d->max - d->min);
    float zero = d->min < 0 ? (float)(0 - d->min) / (float)(d->max - d->min) : 0.0f;
    float a = lo + sweep * (pos < zero ? pos : zero), b = lo + sweep * (pos < zero ? zero : pos);
    dv_arc(fb, cx, cy, r, 4.0f, lo, lo + sweep, DV_C_LINE);
    if (b - a > 0.004f) dv_arc(fb, cx, cy, r, 4.0f, a, b, DV_C_LIME);
    float tip = lo + sweep * pos;
    dv_disc(fb, cx + r * dv_sin(tip), cy - r * dv_sin(tip + 0.25f), 2.6f, DV_C_TEXT);
}

void dv_draw(uint16_t *fb)
{
    char buf[12];
    const track_t *t = &tracks[selected];
    const dv_machine_t *m = t->machine;
    dv_fill(fb, 0, 0, (int)DV_LCD, (int)DV_LCD, DV_C_BASE);

    /* header: machine name; tempo with its source; four beat lamps */
    int shown = ext_locked ? (int)((float)DV_FS * 2.5f / ext_period + 0.5f) : bpm;
    dv_text(fb, DV_FONT_MEDIUM, 8, 5, 0, m ? m->name : "EMPTY", m ? DV_C_TEXT : DV_C_FAINT);
    dv_text(fb, DV_FONT_MEDIUM, 232, 5, 2, number(shown, buf), ext_locked ? DV_C_LIME : DV_C_TEXT);
    dv_text(fb, DV_FONT_SMALL, 232 - 44, 9, 2, ext_locked ? "EXT" : "BPM", ext_locked ? DV_C_LIME : DV_C_FAINT);
    for (unsigned b = 0; b < 4u; b++) {
        int on = playing && (unsigned)(step16 >> 2) == b;
        dv_rrect(fb, 8 + (int)b * 57, 27, 53, 3, 1, on ? DV_C_LIME : DV_C_LINE);
    }

    /* track strip: number, and a meter along the bottom */
    for (unsigned k = 0; k < DV_TRACKS; k++) {
        int x = 8 + (int)k * 38, sel = k == selected;
        uint16_t ink = sel ? DV_C_BASE : tracks[k].mute || !tracks[k].machine ? DV_C_FAINT : DV_C_TEXT;
        if (sel) dv_rrect(fb, x, 36, 34, 26, 6, DV_C_LIME);
        else dv_rframe(fb, x, 36, 34, 26, 6, DV_C_LINE, tracks[k].machine ? DV_C_PANEL : DV_C_BASE);
        buf[0] = (char)('1' + k); buf[1] = 0;
        dv_text(fb, DV_FONT_SMALL, x + 17, 40, 1, tracks[k].mute ? "M" : buf, ink);
        int level = (int)(dv_clampf(meter[k] * 1.4f, 0.0f, 1.0f) * 22.0f);
        dv_rrect(fb, x + 6, 55, 22, 3, 1, sel ? DV_C_DEEP : DV_C_LINE);
        if (level > 1) dv_rrect(fb, x + 6, 55, level, 3, 1, sel ? DV_C_BASE : DV_C_LIME);
    }

    if (!m) {
        dv_rframe(fb, 8, 68, 224, 166, 8, DV_C_LINE, DV_C_PANEL);
        dv_text(fb, DV_FONT_SMALL, 120, 144, 1, "NO MACHINE YET", DV_C_FAINT);
        return;
    }

    /* the machine's picture */
    dv_rframe(fb, 8, 68, 224, 72, 8, DV_C_LINE, DV_C_PANEL);
    if (m->visual) m->visual(fb, 18, 76, 204, 56, playing ? step16 : 16u);

    /* page name and position, track level */
    dv_text(fb, DV_FONT_SMALL, 10, 147, 0, m->page_names[t->page], DV_C_LIME);
    int pitch = m->npages > 5 ? 9 : 14;         /* many pages: smaller marks, clear of the level */
    for (unsigned p = 0; p < m->npages; p++)
        dv_rrect(fb, 126 - (int)m->npages * pitch / 2 + (int)p * pitch, 151, pitch - 4, 4, 2,
                 p == t->page ? DV_C_LIME : DV_C_LINE);
    dv_text(fb, DV_FONT_SMALL, 230, 147, 2, number(t->level, buf), DV_C_DIM);
    dv_text(fb, DV_FONT_SMALL, 230 - 30, 147, 2, "LVL", DV_C_FAINT);

    /* four parameters, one per knob */
    for (int kn = 0; kn < 4; kn++) {
        int cx = 33 + kn * 58, p = m->layout[t->page][kn];
        if (p < 0) {
            dv_arc(fb, (float)cx, 184.0f, 15.0f, 4.0f, 0.625f, 1.375f, DV_C_PANEL);
            continue;
        }
        const dv_param_t *d = &m->params[p];
        int value = m->values[p];
        knob(fb, (float)cx, 184.0f, d, value);
        if (d->choices)
            dv_text(fb, DV_FONT_SMALL, cx, 206, 1, d->choices[value - d->min], DV_C_TEXT);
        else
            dv_text(fb, DV_FONT_MEDIUM, cx, 201, 1, number(value, buf), DV_C_TEXT);
        dv_text(fb, DV_FONT_SMALL, cx, 223, 1, d->name, DV_C_DIM);
    }
}
