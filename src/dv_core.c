/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* The Drelva Vave instrument: clock, six tracks, the master section, presets and one screen.
 *
 * Panel:
 *   PLAY            start / stop (the patterns restart)
 *   FX SCL ENV LFO EDIT GLO   tracks 1..6; with HOME held: mute
 *   ARP             the master section (delay, reverb, output); again: SYSTEM (clock, links, panel)
 *   SELECT          track (or the preset slot while PRESETS is open)
 *   OCT- / OCT+     page; the last page of every track is MIX: level, the two sends, and
 *                   LOAD, which loads another machine on the track (one that is on
 *                   another track changes places with this one)
 *   KNOB 1..4       the four parameters of the page
 *   ALGORITHM       tempo
 *   PRESETS         level of the track
 *   SEQ             the machine's action (a new pattern, a hit)
 *   keys            go to the track's machine (transpose, gate)
 *   SAVE            opens PRESETS: SELECT picks one of 8, SAVE stores, OCT+ loads, OCT- closes
 *
 * Clock: internal by default. When MIDI clock arrives (USB, DIN or Bluetooth) the tempo
 * follows it: the tick period is measured over the last two beats, the internal 1/16 clock
 * runs at that tempo and is pulled gently towards the master's 1/16 grid (steps are counted
 * on both sides, so it cannot settle a step away). The jitter of a Bluetooth link is averaged
 * out instead of being played. MIDI Start, Continue and Stop drive the transport. */
#include "dv.h"
#include "dv_dsp.h"
#include "dv_gfx.h"

#define MASTER DV_TRACKS           /* the master section is selected like a seventh track */
#define SYSTEM (DV_TRACKS + 1u)     /* and the settings like an eighth */

/* every machine a track can load, in the order MACHINE shows them */
static const dv_machine_t *const MACHINES[] = {
    &DV_MINI_ATLANTIS, &DV_PERC_BOX, &DV_PHRASE_BOX, &DV_SISMO_BOX, &DV_STAB_BOX, &DV_SWARM_ENGINE, &DV_TEXTURA};
#define NMACHINE (sizeof MACHINES / sizeof MACHINES[0])
enum { MIX_LEVEL, MIX_DELAY, MIX_REVERB, NMIX };
static const dv_param_t MIX_PARAMS[NMIX] = {
    {"LEVEL", 0, 100, 80, 0}, {"DELAY", 0, 100, 0, 0}, {"REVERB", 0, 100, 0, 0}};

typedef struct {
    const dv_machine_t *machine;
    uint8_t page, mute;
    int16_t mix[NMIX];
} track_t;

static track_t tracks[DV_TRACKS + 2u];
static uint8_t last_track;
static uint32_t held_buttons, held_keys;
static uint8_t selected, playing;
static int16_t bpm = 134;
static volatile uint8_t step16;
static float to_next_step;         /* samples until the next 1/16 */
static float step_len_now;
static volatile uint8_t restart;
static float meter[DV_TRACKS], meter_out[2];
static float pump_env;             /* 1 on each beat, falling: the master's PUMP */

/* presets */
static uint8_t preset_open, preset_slot, preset_used;   /* preset_used: one bit per slot */
static const char *message;
static uint16_t message_frames;

/* external clock */
#define EXT_HISTORY 48u            /* ticks kept: two beats */
static uint32_t now;               /* samples rendered so far */
static uint32_t ext_times[EXT_HISTORY], ext_count;
static float ext_period;           /* samples per MIDI tick (24 per quarter) */
static uint8_t ext_locked, ext_tick;             /* ext_tick: 0..5 inside the 1/16 */
static volatile uint8_t ext_phase_pending, ext_first_step, ext_align;
static int32_t ext_steps, int_steps;             /* 1/16 boundaries: the master's, ours */

int dv_is_playing(void) { return playing; }
int dv_clock_is_external(void) { return ext_locked; }
int dv_machine_page(const dv_machine_t *m)
{
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine == m) return tracks[t].page;
    return -1;
}
float dv_meter(unsigned track) { return meter[track % DV_TRACKS]; }
float dv_meter_out(unsigned channel) { return meter_out[channel & 1u]; }

void dv_midi_realtime(uint8_t status)
{
    if (dv_setting(DV_SET_CLOCK_SOURCE) == 1) return;   /* internal only */
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

/* ----------------------------------------------------------------- presets --- */
/* "DV" version, tempo, then for each track and the master: machine id (0: none), parameter
 * count, mute, the three mix values and the parameters, 16-bit little-endian. A machine whose
 * parameter count differs from the stored one keeps its current values. */
#define PRESET_VERSION 2u
static uint8_t preset_data[DV_PRESET_BYTES];

static unsigned put16(uint8_t *p, unsigned at, int v)
{
    p[at] = (uint8_t)(v & 0xFF);
    p[at + 1u] = (uint8_t)((v >> 8) & 0xFF);
    return at + 2u;
}
static int get16(const uint8_t *p, unsigned at) { return (int16_t)(p[at] | p[at + 1u] << 8); }

static unsigned preset_pack(uint8_t *p)
{
    unsigned at = 0;
    p[at++] = 'D'; p[at++] = 'V'; p[at++] = PRESET_VERSION; p[at++] = 0;
    at = put16(p, at, bpm);
    for (unsigned t = 0; t <= DV_TRACKS; t++) {
        const dv_machine_t *m = tracks[t].machine;
        p[at++] = m ? m->id : 0;
        p[at++] = m ? m->nparams : 0;
        p[at++] = tracks[t].mute;
        for (unsigned i = 0; i < NMIX; i++) at = put16(p, at, tracks[t].mix[i]);
        for (unsigned i = 0; m && i < m->nparams; i++) at = put16(p, at, m->values[i]);
    }
    return at;
}

static int preset_unpack(const uint8_t *p, unsigned size)
{
    unsigned at = 4;
    if (size < 6u || p[0] != 'D' || p[1] != 'V' || p[2] != PRESET_VERSION) return 0;
    bpm = (int16_t)get16(p, at); at += 2u;
    if (bpm < 60 || bpm > 200) bpm = 134;
    for (unsigned t = 0; t <= DV_TRACKS && at + 3u + 2u * NMIX <= size; t++) {
        unsigned id = p[at++], count = p[at++];
        if (t < DV_TRACKS) {
            tracks[t].machine = 0;
            for (unsigned i = 0; i < NMACHINE; i++)
                if (MACHINES[i]->id == id) tracks[t].machine = MACHINES[i];
        }
        const dv_machine_t *m = tracks[t].machine;
        tracks[t].mute = p[at++] & 1u;
        for (unsigned i = 0; i < NMIX; i++, at += 2u) {
            int v = get16(p, at);
            tracks[t].mix[i] = (int16_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        }
        if (at + 2u * count > size) return 0;
        if (m && count == m->nparams) {
            for (unsigned i = 0; i < count; i++) {
                int v = get16(p, at + 2u * i);
                m->values[i] = (int16_t)(v < m->params[i].min ? m->params[i].min : v > m->params[i].max ? m->params[i].max : v);
            }
            m->update();
        }
        tracks[t].page = 0;
        at += 2u * count;
    }
    return 1;
}

static void say(const char *text) { message = text; message_frames = 90; }

static void preset_scan(void)
{
    preset_used = 0;
    for (unsigned s = 0; s < DV_PRESETS; s++)
        if (dv_store_read(s, preset_data, DV_PRESET_BYTES) >= 6u && preset_data[0] == 'D') preset_used |= (uint8_t)(1u << s);
}

/* -------------------------------------------------------------------- setup --- */
void dv_init(void)
{
    static const dv_machine_t *const layout[DV_TRACKS + 2u] = {
        &DV_SISMO_BOX, &DV_MINI_ATLANTIS, &DV_TEXTURA, &DV_SWARM_ENGINE, &DV_PHRASE_BOX, &DV_PERC_BOX, &DV_MASTER, &DV_SYSTEM};
    /* level, delay send, reverb send */
    static const int16_t mix[DV_TRACKS + 2u][NMIX] = {
        {90, 0, 0}, {62, 28, 10}, {50, 0, 35}, {42, 0, 50}, {58, 10, 35}, {70, 6, 12}, {100, 0, 0}, {100, 0, 0}};
    for (unsigned i = 0; i < NMACHINE; i++) MACHINES[i]->init();   /* also the ones no track has loaded */
    DV_MASTER.init();
    DV_SYSTEM.init();
    for (unsigned t = 0; t <= SYSTEM; t++) {
        tracks[t].machine = layout[t];
        tracks[t].page = 0;
        tracks[t].mute = 0;
        for (unsigned i = 0; i < NMIX; i++) tracks[t].mix[i] = mix[t][i];
    }
    selected = last_track = 0;
    playing = 0;
    step16 = 15;
    to_next_step = 0.0f;
    preset_open = 0;
    preset_scan();
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* the pages of a selection: the machine's, then MIX on the six tracks */
static unsigned page_count(const track_t *t)
{
    return (t->machine ? t->machine->npages : 0u) + (t < &tracks[MASTER] ? 1u : 0u);
}
static int is_mix_page(const track_t *t) { return t < &tracks[MASTER] && t->page == (t->machine ? t->machine->npages : 0u); }

/* where a machine sits in MACHINES, 0 for none, else index + 1 */
static unsigned machine_number(const dv_machine_t *m)
{
    for (unsigned i = 0; i < NMACHINE; i++)
        if (MACHINES[i] == m) return i + 1u;
    return 0;
}
/* LOAD on the MIX page: the next or previous machine on this track. Each machine exists
 * once, so one that another track has loaded changes places with this track's. */
static void load_machine(track_t *t, int direction)
{
    int number = (int)machine_number(t->machine) + (direction > 0 ? 1 : -1);
    if (number < 0 || number > (int)NMACHINE) return;
    const dv_machine_t *wanted = number ? MACHINES[number - 1] : 0, *old = t->machine;
    for (unsigned k = 0; wanted && k < DV_TRACKS; k++)
        if (tracks[k].machine == wanted) {
            tracks[k].machine = old;
            tracks[k].page = 0;
        }
    t->machine = wanted;
    t->page = (uint8_t)(wanted ? wanted->npages : 0u);   /* stay on MIX */
    if (wanted && wanted->reset) wanted->reset();
}

static void select(unsigned which)
{
    if (selected == SYSTEM && which != SYSTEM) dv_system_save();
    if (which < DV_TRACKS) last_track = (uint8_t)which;
    selected = (uint8_t)which;
}
/* the parameter under a knob: its definition and where its value lives; 0 if the knob is free */
static const dv_param_t *knob_param(track_t *t, unsigned kn, int16_t **value)
{
    if (is_mix_page(t)) {
        if (kn >= NMIX) return 0;
        *value = &t->mix[kn];
        return &MIX_PARAMS[kn];
    }
    if (!t->machine) return 0;
    int p = t->machine->layout[t->page][kn];
    if (p < 0) return 0;
    *value = &t->machine->values[p];
    return &t->machine->params[p];
}

void dv_buttons(uint32_t held)
{
    uint32_t pressed = held & ~held_buttons;
    held_buttons = held;
    if (pressed & (1u << DV_BTN_PLAY)) {
        playing = !playing;
        if (playing) { restart = 1; ext_align = 1; }
    }
    if (pressed & (1u << DV_BTN_SAVE)) {
        if (!preset_open) {
            preset_open = 1;
            preset_scan();
        } else {
            unsigned size = preset_pack(preset_data);
            if (dv_store_write(preset_slot, preset_data, size)) {
                preset_used |= (uint8_t)(1u << preset_slot);
                say("SAVED");
            } else {
                say("NOT SAVED");
            }
            preset_open = 0;
        }
    }
    if (preset_open) {
        if (pressed & (1u << DV_BTN_OCT_UP)) {
            unsigned size = dv_store_read(preset_slot, preset_data, DV_PRESET_BYTES);
            say(size && preset_unpack(preset_data, size) ? "LOADED" : "EMPTY");
            preset_open = 0;
        }
        if (pressed & (1u << DV_BTN_OCT_DN)) preset_open = 0;
        return;
    }
    for (unsigned k = 0; k < DV_TRACKS; k++) {
        if (!(pressed & (1u << (DV_BTN_FX + k)))) continue;
        if (held & (1u << DV_BTN_HOME)) tracks[k].mute = !tracks[k].mute;
        else select(k);
    }
    if (pressed & (1u << DV_BTN_ARP)) select(selected < MASTER ? MASTER : selected == MASTER ? SYSTEM : last_track);
    track_t *t = &tracks[selected];
    if ((pressed & (1u << DV_BTN_OCT_DN)) && t->page > 0) t->page--;
    if ((pressed & (1u << DV_BTN_OCT_UP)) && t->page + 1u < page_count(t)) t->page++;
    if ((pressed & (1u << DV_BTN_SEQ)) && t->machine && t->machine->action) t->machine->action();
}

void dv_keys(uint32_t held)
{
    uint32_t pressed = held & ~held_keys;
    const dv_machine_t *m = tracks[selected].machine;
    int released = held_keys && !held;
    held_keys = held;
    if (!m || !m->key) return;
    for (int note = 26; note >= 0; note--)
        if (pressed & (1u << note)) { m->key(note); break; }
    if (released) m->key(-1);
}

void dv_encoder(unsigned id, int delta)
{
    track_t *t = &tracks[selected];
    if (!delta) return;
    if (id == DV_ENC_SELECT) {
        if (preset_open) preset_slot = (uint8_t)clampi((int)preset_slot + (delta > 0 ? 1 : -1), 0, (int)DV_PRESETS - 1);
        else select((unsigned)clampi((int)selected + (delta > 0 ? 1 : -1), 0, (int)SYSTEM));
    } else if (id == DV_ENC_ALGORITHM) {
        if (!ext_locked) bpm = (int16_t)clampi(bpm + delta, 60, 200);
    } else if (id == DV_ENC_PRESETS) {
        if (selected < MASTER) t->mix[MIX_LEVEL] = (int16_t)clampi(t->mix[MIX_LEVEL] + delta, 0, 100);
    } else if (id >= DV_ENC_KNOB1 && id <= DV_ENC_KNOB4) {
        int16_t *value = 0;
        if (is_mix_page(t) && id == DV_ENC_KNOB4) {
            load_machine(t, delta);
            return;
        }
        const dv_param_t *d = knob_param(t, id - DV_ENC_KNOB1, &value);
        if (!d) return;
        int turn = delta * (delta > 2 || delta < -2 ? 3 : 1);   /* fast turns go further */
        int next = clampi(*value + turn, d->min, d->max);
        if (next != *value) {
            *value = (int16_t)next;
            if (!is_mix_page(t)) t->machine->update();
        }
    }
}

/* -------------------------------------------------------------------- audio --- */
static void reset_machines(void)
{
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine && tracks[t].machine->reset) tracks[t].machine->reset();
}

static void advance_step(void)
{
    step16 = (uint8_t)((step16 + 1u) & 15u);
    int_steps++;
    if ((step16 & 3u) == 0u) pump_env = 1.0f;
    for (unsigned t = 0; t < DV_TRACKS; t++)
        if (tracks[t].machine && tracks[t].machine->step) tracks[t].machine->step(step16, step_len_now);
}

void dv_render(float stems[DV_TRACKS][DV_BLOCK], float *left, float *right)
{
    static float to_delay[DV_BLOCK], to_reverb[DV_BLOCK];
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
            float error = to_next_step + (float)(ext_steps - int_steps) * step_len
                        + (float)dv_setting(DV_SET_CLOCK_OFFSET) * ((float)DV_FS / 1000.0f);
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
            if (tracks[t].machine && tracks[t].machine->render) {
                tracks[t].machine->render(stems[t] + done, n);
            } else {
                for (unsigned i = 0; i < n; i++) stems[t][done + i] = 0.0f;
            }
        }
        done += n;
    }
    for (unsigned i = 0; i < DV_BLOCK; i++) left[i] = to_delay[i] = to_reverb[i] = 0.0f;
    float duck = 1.0f - pump_env * dv_fx_pump();
    pump_env *= 0.9717f;                        /* per block: back up in about 230 ms */
    for (unsigned t = 0; t < DV_TRACKS; t++) {
        float gain = tracks[t].mute ? 0.0f : (float)tracks[t].mix[MIX_LEVEL] * 0.01f;
        float delay = (float)tracks[t].mix[MIX_DELAY] * 0.01f, reverb = (float)tracks[t].mix[MIX_REVERB] * 0.01f;
        gain *= gain;                                       /* a gentler taper */
        float peak = meter[t] * 0.985f;
        for (unsigned i = 0; i < DV_BLOCK; i++) {
            float x = stems[t][i] *= gain;         /* the track's own output: level and mute only */
            float mixed = tracks[t].machine == &DV_SISMO_BOX ? x : x * duck;
            left[i] += mixed;
            to_delay[i] += mixed * delay;
            to_reverb[i] += mixed * reverb;
            if (x < 0.0f) x = -x;
            if (x > peak) peak = x;
        }
        meter[t] = peak;
    }
    for (unsigned i = 0; i < DV_BLOCK; i++) right[i] = left[i];
    dv_fx_process(to_delay, to_reverb, left, right, step_len);
    for (unsigned c = 0; c < 2u; c++) {
        const float *out = c ? right : left;
        float peak = meter_out[c] * 0.985f;
        for (unsigned i = 0; i < DV_BLOCK; i++) {
            float x = out[i] < 0.0f ? -out[i] : out[i];
            if (x > peak) peak = x;
        }
        meter_out[c] = peak;
    }
}

uint32_t dv_led_buttons(void)
{
    return (playing ? 1u << DV_BTN_PLAY : 0u) | (preset_open ? 1u << DV_BTN_SAVE : 0u)
         | (selected >= MASTER ? 1u << DV_BTN_ARP : 1u << (DV_BTN_FX + selected));
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

static void draw_presets(uint16_t *fb)
{
    char buf[4];
    dv_text(fb, DV_FONT_SMALL, 10, 147, 0, "PRESETS", DV_C_LIME);
    for (unsigned s = 0; s < DV_PRESETS; s++) {
        int x = 8 + (int)(s % 4u) * 57, y = 164 + (int)(s / 4u) * 30, used = (preset_used >> s) & 1u;
        if (s == preset_slot) dv_rrect(fb, x, y, 53, 26, 6, DV_C_LIME);
        else dv_rframe(fb, x, y, 53, 26, 6, DV_C_LINE, used ? DV_C_PANEL : DV_C_BASE);
        buf[0] = (char)('1' + s); buf[1] = 0;
        dv_text(fb, DV_FONT_MEDIUM, x + 20, y + 4, 1, buf, s == preset_slot ? DV_C_BASE : used ? DV_C_TEXT : DV_C_FAINT);
        if (used) dv_disc(fb, (float)x + 40.5f, (float)y + 13.0f, 3.0f, s == preset_slot ? DV_C_BASE : DV_C_LIME);
    }
    dv_text(fb, DV_FONT_SMALL, 120, 225, 1, "SAVE STORE  OCT+ LOAD", DV_C_DIM);
}

void dv_draw(uint16_t *fb)
{
    char buf[12];
    track_t *t = &tracks[selected];
    const dv_machine_t *m = t->machine;
    dv_fill(fb, 0, 0, (int)DV_LCD, (int)DV_LCD, DV_C_BASE);

    /* header: machine name (or a message); tempo with its source; four beat lamps */
    int shown = ext_locked ? (int)((float)DV_FS * 2.5f / ext_period + 0.5f) : bpm;
    if (message_frames) {
        message_frames--;
        dv_text(fb, DV_FONT_MEDIUM, 8, 5, 0, message, DV_C_LIME);
    } else {
        dv_text(fb, DV_FONT_MEDIUM, 8, 5, 0, m ? m->name : "EMPTY", m ? DV_C_TEXT : DV_C_FAINT);
    }
    dv_text(fb, DV_FONT_MEDIUM, 232, 5, 2, number(shown, buf), ext_locked ? DV_C_LIME : DV_C_TEXT);
    if (dv_text_width(DV_FONT_MEDIUM, m ? m->name : "EMPTY") < 140)
        dv_text(fb, DV_FONT_SMALL, 232 - 44, 9, 2, ext_locked ? "EXT" : "BPM", ext_locked ? DV_C_LIME : DV_C_FAINT);
    for (unsigned b = 0; b < 4u; b++) {
        int on = playing && (unsigned)(step16 >> 2) == b;
        dv_rrect(fb, 8 + (int)b * 57, 27, 53, 3, 1, on ? DV_C_LIME : DV_C_LINE);
    }

    /* track strip: number, and a meter along the bottom; the master shows as all six framed */
    for (unsigned k = 0; k < DV_TRACKS; k++) {
        int x = 8 + (int)k * 38, sel = k == selected;
        uint16_t ink = sel ? DV_C_BASE : tracks[k].mute || !tracks[k].machine ? DV_C_FAINT : DV_C_TEXT;
        if (sel) dv_rrect(fb, x, 36, 34, 26, 6, DV_C_LIME);
        else dv_rframe(fb, x, 36, 34, 26, 6, selected >= MASTER ? DV_C_DEEP : DV_C_LINE, tracks[k].machine ? DV_C_PANEL : DV_C_BASE);
        buf[0] = (char)('1' + k); buf[1] = 0;
        dv_text(fb, DV_FONT_SMALL, x + 17, 40, 1, tracks[k].mute ? "M" : buf, ink);
        int level = (int)(dv_clampf(meter[k] * 1.4f, 0.0f, 1.0f) * 22.0f);
        dv_rrect(fb, x + 6, 55, 22, 3, 1, sel ? DV_C_DEEP : DV_C_LINE);
        if (level > 1) dv_rrect(fb, x + 6, 55, level, 3, 1, sel ? DV_C_BASE : DV_C_LIME);
    }

    /* the machine's picture */
    dv_rframe(fb, 8, 68, 224, 72, 8, DV_C_LINE, DV_C_PANEL);
    if (m && m->visual) m->visual(fb, 18, 76, 204, 56, playing ? step16 : 16u);
    else dv_text(fb, DV_FONT_SMALL, 120, 99, 1, "NO MACHINE YET", DV_C_FAINT);

    if (preset_open) {
        draw_presets(fb);
        return;
    }

    /* page name and position, track level */
    unsigned pages = page_count(t);
    dv_text(fb, DV_FONT_SMALL, 10, 147, 0, is_mix_page(t) ? "MIX" : m->page_names[t->page], DV_C_LIME);
    int pitch = pages > 9 ? 7 : pages > 5 ? 9 : 14;   /* many pages: smaller marks, clear of the level */
    for (unsigned p = 0; p < pages; p++)
        dv_rrect(fb, 120 - (int)pages * pitch / 2 + (int)p * pitch, 151, pitch - 4, 4, 2,
                 p == t->page ? DV_C_LIME : DV_C_LINE);
    if (selected < MASTER) {
        dv_text(fb, DV_FONT_SMALL, 230, 147, 2, number(t->mix[MIX_LEVEL], buf), DV_C_DIM);
        dv_text(fb, DV_FONT_SMALL, 230 - 30, 147, 2, "LVL", DV_C_FAINT);
    }

    /* four parameters, one per knob */
    for (unsigned kn = 0; kn < 4u; kn++) {
        int cx = 33 + (int)kn * 58;
        int16_t *value = 0;
        if (is_mix_page(t) && kn == 3u) {           /* which machine the track has loaded */
            static const dv_param_t which = {"LOAD", 0, (int16_t)NMACHINE, 0, 0};
            knob(fb, (float)cx, 184.0f, &which, (int)machine_number(m));
            dv_text(fb, DV_FONT_SMALL, cx, 206, 1, m ? m->short_name : "NONE", DV_C_TEXT);
            dv_text(fb, DV_FONT_SMALL, cx, 223, 1, which.name, DV_C_DIM);
            continue;
        }
        const dv_param_t *d = knob_param(t, kn, &value);
        if (!d) {
            dv_arc(fb, (float)cx, 184.0f, 15.0f, 4.0f, 0.625f, 1.375f, DV_C_PANEL);
            continue;
        }
        knob(fb, (float)cx, 184.0f, d, *value);
        if (d->choices)
            dv_text(fb, DV_FONT_SMALL, cx, 206, 1, d->choices[*value - d->min], DV_C_TEXT);
        else
            dv_text(fb, DV_FONT_MEDIUM, cx, 201, 1, number(*value, buf), DV_C_TEXT);
        dv_text(fb, DV_FONT_SMALL, cx, 223, 1, d->name, DV_C_DIM);
    }
}
