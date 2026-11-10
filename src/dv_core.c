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
 *   SEQ             a new pattern from the machine */
#include "dv.h"
#include "dv_dsp.h"
#include "dv_gfx.h"

typedef struct {
    const dv_machine_t *machine;
    uint8_t page, mute;
    int16_t level;                 /* 0..100 */
} track_t;

static track_t tracks[DV_TRACKS];
static uint32_t held_buttons;
static uint8_t selected, playing;
static int16_t bpm = 134;
static volatile uint8_t step16;
static float to_next_step;         /* samples until the next 1/16 */
static volatile uint8_t restart;

void dv_init(void)
{
    static const dv_machine_t *const layout[DV_TRACKS] = {&DV_SISMO_BOX, 0, 0, 0, 0, 0};
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
        if (playing) restart = 1;
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

void dv_keys(uint32_t held) { (void)held; }

void dv_encoder(unsigned id, int delta)
{
    track_t *t = &tracks[selected];
    if (!delta) return;
    if (id == DV_ENC_SELECT) {
        selected = (uint8_t)clampi((int)selected + (delta > 0 ? 1 : -1), 0, (int)DV_TRACKS - 1);
    } else if (id == DV_ENC_ALGORITHM) {
        bpm = (int16_t)clampi(bpm + delta, 60, 200);
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

static void advance_step(void)
{
    step16 = (uint8_t)((step16 + 1u) & 15u);
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine) tracks[t].machine->step(step16, 0);
}

void dv_render(float stems[DV_TRACKS][DV_BLOCK], float *left, float *right)
{
    float step_len = (float)DV_FS * 15.0f / (float)bpm;   /* samples per 1/16 */
    unsigned done = 0;
    if (restart) {
        restart = 0;
        step16 = 15;
        to_next_step = 0.0f;
    }
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
        for (unsigned i = 0; i < DV_BLOCK; i++) {
            stems[t][i] *= gain;
            left[i] += stems[t][i];
        }
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

void dv_draw(uint16_t *fb)
{
    char buf[12];
    const track_t *t = &tracks[selected];
    const dv_machine_t *m = t->machine;
    dv_fill(fb, 0, 0, (int)DV_LCD, (int)DV_LCD, DV_C_BASE);

    /* header: machine name, tempo, transport */
    dv_text(fb, DV_FONT_MEDIUM, 8, 7, 0, m ? m->name : "EMPTY", m ? DV_C_LIME : DV_C_FAINT);
    dv_text(fb, DV_FONT_MEDIUM, 214, 7, 2, number(bpm, buf), DV_C_TEXT);
    dv_fill(fb, 222, 11, 10, 10, playing ? DV_C_LIME : DV_C_LINE);
    dv_fill(fb, 0, 32, (int)DV_LCD, 1, DV_C_LINE);

    /* track strip */
    for (unsigned k = 0; k < DV_TRACKS; k++) {
        int x = 8 + (int)k * 38;
        uint16_t ink = tracks[k].mute ? DV_C_FAINT : tracks[k].machine ? DV_C_TEXT : DV_C_FAINT;
        if (k == selected) {
            dv_fill(fb, x, 40, 34, 20, DV_C_LIME);
            ink = DV_C_BASE;
        } else {
            dv_frame(fb, x, 40, 34, 20, DV_C_LINE);
        }
        buf[0] = (char)('1' + k); buf[1] = 0;
        dv_text(fb, DV_FONT_SMALL, x + 17, 45, 1, tracks[k].mute ? "M" : buf, ink);
    }

    if (!m) {
        dv_text(fb, DV_FONT_SMALL, 120, 120, 1, "NO MACHINE ON THIS TRACK", DV_C_FAINT);
        return;
    }

    /* the machine's picture */
    dv_fill(fb, 8, 68, 224, 70, DV_C_PANEL);
    dv_frame(fb, 8, 68, 224, 70, DV_C_LINE);
    if (m->visual) m->visual(fb, 16, 74, 208, 58, playing ? step16 : 16u);

    /* page name and position, track level */
    dv_text(fb, DV_FONT_SMALL, 8, 148, 0, m->page_names[t->page], DV_C_DIM);
    for (unsigned p = 0; p < m->npages; p++)
        dv_fill(fb, 100 + (int)p * 12, 151, 8, 4, p == t->page ? DV_C_LIME : DV_C_LINE);
    dv_text(fb, DV_FONT_SMALL, 232, 148, 2, number(t->level, buf), DV_C_DIM);
    dv_text(fb, DV_FONT_SMALL, 232 - 30, 148, 2, "LVL", DV_C_FAINT);

    /* four parameters, one per knob */
    for (int kn = 0; kn < 4; kn++) {
        int x = 4 + kn * 58, p = m->layout[t->page][kn];
        dv_frame(fb, x, 166, 56, 68, DV_C_LINE);
        if (p < 0) continue;
        const dv_param_t *d = &m->params[p];
        int value = m->values[p];
        dv_text(fb, DV_FONT_SMALL, x + 28, 172, 1, d->name, DV_C_DIM);
        if (d->choices)
            dv_text(fb, DV_FONT_SMALL, x + 28, 195, 1, d->choices[value - d->min], DV_C_TEXT);
        else
            dv_text(fb, DV_FONT_MEDIUM, x + 28, 191, 1, number(value, buf), DV_C_TEXT);
        dv_fill(fb, x + 6, 222, 44, 3, DV_C_LINE);
        dv_fill(fb, x + 6, 222, 44 * (value - d->min) / (d->max - d->min), 3, DV_C_LIME);
    }
}
