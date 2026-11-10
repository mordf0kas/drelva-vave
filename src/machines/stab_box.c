/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Stab Box: dub techno chord stabs. Four band-limited saws play a chord, a resonant low-pass
 * pings open on each stab and closes with the amp envelope. The stabs follow a rhythm figure
 * (or SEQ), and the root can walk a short progression, one chord per bar. Send it to the
 * master delay for the full dub sound; the keyboard moves the root.
 *
 * Ported from the Drelvarom program of the same name. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum { CHORD, RHYTHM, PROG, ROOT, CUTOFF, ENV, DECAY, RES, DETUNE, NPARAM };
#define NCHORD 7
#define NRHYTHM 6
#define NPROG 5
#define NBAR 4

/* semitones above the root for the four saws */
static const int8_t CHORDS[NCHORD][4] = {
    {0, 3, 7, 12},   /* Min */
    {0, 3, 7, 10},   /* Min7 */
    {0, 3, 10, 14},  /* Min9 (no fifth) */
    {0, 7, 10, 15},  /* Dub: the classic open minor seventh voicing */
    {0, 5, 7, 12},   /* Sus4 */
    {0, 7, 12, 19},  /* Fifths */
    {0, 3, 10, 17},  /* Min11 */
};
static const char *const CHORD_NAMES[NCHORD] = {"MIN", "MIN7", "MIN9", "DUB", "SUS4", "5TH", "MIN11"};
/* bit n = a stab on step n */
static const uint16_t RHYTHMS[NRHYTHM] = {
    0x4444,  /* Off: every off-beat */
    0x2404,  /* Dub: off-beat, then a late answer */
    0x4848,  /* Push: on the "a" of 2 and 4 */
    0x2492,  /* 3/16: every third step */
    0x0004,  /* One: a single stab per bar */
    0x0000,  /* Man: only SEQ */
};
static const char *const RHYTHM_NAMES[NRHYTHM] = {"OFF", "DUB", "PUSH", "3/16", "ONE", "MAN"};
/* root offset in semitones for each of four bars */
static const int8_t PROGS[NPROG][NBAR] = {
    {0, 0, 0, 0}, {0, 0, -4, -4}, {0, 0, 5, 5}, {0, -2, 0, -2}, {0, 0, -2, -5}};
static const char *const PROG_NAMES[NPROG] = {"STAY", "I-VI", "I-IV", "I-VII", "FALL"};

static const dv_param_t PARAMS[NPARAM] = {
    {"CHORD", 0, NCHORD - 1, 3, CHORD_NAMES}, {"RHYTHM", 0, NRHYTHM - 1, 1, RHYTHM_NAMES},
    {"PROG", 0, NPROG - 1, 0, PROG_NAMES},    {"ROOT", -12, 12, 0, 0},
    {"CUTOFF", 0, 100, 30, 0}, {"ENV", 0, 100, 55, 0}, {"DECAY", 0, 100, 35, 0}, {"RESO", 0, 100, 50, 0},
    {"DETUNE", 0, 100, 40, 0},
};
static const int8_t LAYOUT[3][4] = {{CHORD, RHYTHM, PROG, ROOT}, {CUTOFF, ENV, DECAY, RES}, {DETUNE, -1, -1, -1}};
static const char *const PAGES[3] = {"CHORD", "FILTER", "VOICE"};

static int16_t values[NPARAM];
static volatile uint8_t bar, stab_now, flash;
static volatile int8_t transpose;
static struct { float cutoff_units, env_units, res, amp_decay, filt_decay, detune_units; } k;
static struct {
    float phase[4], env, fenv;
    dv_svf2_t filter;
} v = {{0.0f, 0.25f, 0.5625f, 0.8125f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};   /* spread: no flam at the start */

static void update(void)
{
    /* 60 ms .. 1.5 s, exponential */
    float decay = 60.0f * dv_exp2((float)values[DECAY] * 0.01f * 4.6439f);
    k.cutoff_units = 6000.0f + (float)values[CUTOFF] * 70.0f;   /* about 120 Hz .. 1.6 kHz before the envelope */
    k.env_units = (float)values[ENV] * 60.0f;                   /* up to about 4 octaves of ping */
    k.res = (float)values[RES] * 1.2f;
    k.amp_decay = dv_settle_ms(decay);
    k.filt_decay = dv_settle_ms(decay * 0.5f);
    k.detune_units = (float)values[DETUNE] * 0.12f;             /* up to about 10 cents per voice */
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    bar = NBAR - 1;
    transpose = 0;
    update();
}

static void reset(void) { bar = NBAR - 1; }
static void action(void) { stab_now = 1; }
static void key(int note) { if (note >= 0) transpose = (int8_t)(note - 7); }

static void step(unsigned s16, float step_len)
{
    (void)step_len;
    if ((s16 & 15u) == 0u) bar = (uint8_t)((bar + 1u) % NBAR);
    if (RHYTHMS[values[RHYTHM]] & (1u << (s16 & 15u))) stab_now = 1;
}

static void render(float *out, unsigned n)
{
    float inv_fs = 1.0f / (float)DV_FS, inc[4];
    float root = 4.0f * 1536.0f + (float)(values[ROOT] + transpose + PROGS[values[PROG]][bar]) * 128.0f;   /* C3 */
    if (stab_now) {
        stab_now = 0;
        v.env = v.fenv = 1.0f;
        flash = 8;
    }
    for (int o = 0; o < 4; o++) {
        float units = root + (float)CHORDS[values[CHORD]][o] * 128.0f + (float)(o - 1) * k.detune_units;
        inc[o] = dv_pitch_hz(dv_clampf(units, 0.0f, 14000.0f)) * inv_fs;
    }
    for (unsigned i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int o = 0; o < 4; o++) {
            v.phase[o] += inc[o];
            if (v.phase[o] >= 1.0f) v.phase[o] -= 1.0f;
            sum += dv_saw_blep(v.phase[o], inc[o]);
        }
        v.env *= k.amp_decay;
        v.fenv *= k.filt_decay;
        dv_svf2_set(&v.filter, dv_pitch_hz(k.cutoff_units + v.fenv * k.env_units), k.res);
        float x = dv_svf2_lp(&v.filter, sum * 0.25f) * v.env;
        out[i] = dv_softclip(x * 1.5f);
    }
}

/* the chord as four bars by interval, the bar of the progression, the rhythm as 16 steps */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    (void)w;
    uint16_t lit = flash ? DV_C_TEXT : DV_C_LIME;
    if (flash) flash--;
    for (int o = 0; o < 4; o++) {
        int bh = 10 + CHORDS[values[CHORD]][o] * (h - 12) / 19;
        dv_rrect(fb, x + o * 14, y + h - bh, 10, bh, 3, o == 0 ? lit : DV_C_DEEP);
    }
    for (int b = 0; b < NBAR; b++)
        dv_rrect(fb, x + 70 + b * 34, y, 30, 5, 2, b == bar && s16 < 16u ? DV_C_LIME : DV_C_LINE);
    for (int i = 0; i < 16; i++) {
        int on = (RHYTHMS[values[RHYTHM]] >> i) & 1, now = i == (int)s16;
        int bx = x + 70 + i * 8 + (i / 4) * 2;
        dv_rrect(fb, bx, y + 16, 6, on ? h - 16 : 6, 2, now ? DV_C_TEXT : on ? DV_C_LIME : DV_C_LINE);
    }
}

const dv_machine_t DV_STAB_BOX = {
    "STAB BOX", NPARAM, 3, PARAMS, PAGES, LAYOUT, values, init, update, reset, step, render, action, key, visual,
};
