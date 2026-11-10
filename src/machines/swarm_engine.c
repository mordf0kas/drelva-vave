/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Swarm Engine: four drone voices, each a centre oscillator plus three detuned ones
 * (16 oscillators). The voices sit at an interval set (unison, octaves, fifths, fourths,
 * cluster). Waves: band-limited saw, pulse, XOR-folded saw (metallic) and an XOR swarm
 * between neighbouring oscillators (1-bit ring mod).
 *
 * TONE is a low-pass on the whole swarm and MOVE sweeps it slowly, one turn every four bars.
 *
 * Mode: PLAY drones while the transport runs, KEYS only while a key is held, SYNC drones
 * with the phases reset on every beat. The keyboard sets the pitch.
 *
 * Ported from the Drelvarom program; there the four voices have an output each, here they
 * are summed into the track's channel. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum { TUNE, SPREAD, INTERVALS, WAVE, FOLD, MODE, TONE, MOVE, NPARAM };
static const char *const SETS[5] = {"UNISON", "OCT", "5TH", "4TH", "CLUSTR"};
static const char *const WAVES[4] = {"SAW", "PULSE", "FOLD", "XOR"};
static const char *const MODES[3] = {"PLAY", "KEYS", "SYNC"};
static const dv_param_t PARAMS[NPARAM] = {
    {"TUNE", -24, 24, 7, 0}, {"SPREAD", 0, 100, 28, 0}, {"INTVL", 0, 4, 2, SETS}, {"WAVE", 0, 3, 0, WAVES},
    {"FOLD", 0, 100, 40, 0}, {"MODE", 0, 2, 0, MODES},     {"TONE", 0, 100, 42, 0},  {"MOVE", 0, 100, 35, 0},
};
static const int8_t LAYOUT[2][4] = {{TUNE, SPREAD, INTERVALS, WAVE}, {FOLD, MODE, TONE, MOVE}};
static const char *const PAGES[2] = {"SWARM", "SHAPE"};

/* pitch units (1536 per octave) */
static const int16_t INTERVAL[5][4] = {
    {0, 0, 0, 0}, {0, 12 * 128, 24 * 128, -12 * 128}, {0, 7 * 128, 12 * 128, 19 * 128},
    {0, 5 * 128, 10 * 128, 15 * 128}, {0, 70, 150, 230}};
static const int16_t DETUNE[4] = {0, 26, -38, 64};   /* at 100 %: up to half a semitone */

static int16_t values[NPARAM];
static uint32_t phase[4][4], inc[4][4];
static float gain, sweep, sweep_inc;
static dv_svf2_t tone;
static volatile uint8_t key_held, sync_now;
static volatile int8_t transpose;

static void update(void)
{
    for (int v = 0; v < 4; v++)
        for (int o = 0; o < 4; o++) {
            float units = 3.0f * 1536.0f + (float)(values[TUNE] + transpose) * 128.0f + (float)INTERVAL[values[INTERVALS]][v]
                        + (float)DETUNE[o] * (float)values[SPREAD] * 0.01f;
            inc[v][o] = (uint32_t)(dv_pitch_hz(dv_clampf(units, 0.0f, 14000.0f)) * (4294967296.0f / (float)DV_FS));
        }
}

static void init(void)
{
    dv_rng_t rng = {0x7F4A7C15u};
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    for (int v = 0; v < 4; v++)
        for (int o = 0; o < 4; o++) phase[v][o] = dv_rng_u32(&rng);
    transpose = 0;
    update();
}

static void key(int note)
{
    key_held = note >= 0;
    if (note >= 0) {
        transpose = (int8_t)(note - 7);
        update();
    }
}

static void step(unsigned s16, float step_len)
{
    sweep_inc = 1.0f / (64.0f * step_len);        /* one turn in four bars */
    if ((s16 & 3u) == 0u && values[MODE] == 2) sync_now = 1;
}

static void render(float *out, unsigned n)
{
    int wave = values[WAVE];
    uint32_t fold = (uint32_t)values[FOLD] * 655u;
    float target = values[MODE] == 1 ? (key_held ? 1.0f : 0.0f) : (dv_is_playing() || key_held ? 1.0f : 0.0f);
    if (sync_now) {
        sync_now = 0;
        for (int v = 0; v < 4; v++)
            for (int o = 0; o < 4; o++) phase[v][o] = 0;
    }
    sweep += sweep_inc * (float)n;
    if (sweep >= 1.0f) sweep -= 1.0f;
    dv_svf2_set(&tone, 80.0f * dv_exp2((float)values[TONE] * 0.075f + dv_sin(sweep) * (float)values[MOVE] * 0.02f), 35.0f);
    for (unsigned i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int v = 0; v < 4; v++)
            for (int o = 0; o < 4; o++) {
                uint32_t p = phase[v][o] += inc[v][o];
                switch (wave) {
                case 0: sum += dv_saw_blep((float)p * (1.0f / 4294967296.0f), (float)inc[v][o] * (1.0f / 4294967296.0f)); break;
                case 1: sum += p < 0x80000000u ? 0.73f : -0.73f; break;
                case 2: sum += (float)(int32_t)((((p >> 16) ^ fold) & 0xFFFFu) - 32768u) * (1.0f / 32768.0f); break;
                default: sum += (float)(int32_t)((((p ^ phase[v][(o + 1) & 3]) >> 16) & 0xFFFFu) - 32768u) * (1.0f / 32768.0f); break;
                }
            }
        gain += (target - gain) * 0.004f;            /* declicked, about 6 ms */
        out[i] = dv_softclip(dv_svf2_lp(&tone, sum * 0.125f) * 1.6f * gain);
    }
}

/* one ring per voice; its four dots are the oscillators' phases, so you see the swarm
 * drift apart (spread) or lock (unison, sync) */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    (void)s16;
    for (int v = 0; v < 4; v++) {
        float cx = (float)x + (float)w * (0.125f + 0.25f * (float)v), cy = (float)y + (float)h * 0.5f, r = (float)h * 0.5f - 5.0f;
        dv_arc(fb, cx, cy, r, 2.0f, 0.0f, 1.0f, DV_C_LINE);
        for (int o = 0; o < 4; o++) {
            float a = (float)phase[v][o] * (1.0f / 4294967296.0f);
            dv_disc(fb, cx + r * dv_sin(a), cy - r * dv_sin(a + 0.25f), o == 0 ? 3.5f : 2.5f, o == 0 ? DV_C_TEXT : DV_C_LIME);
        }
    }
}

const dv_machine_t DV_SWARM_ENGINE = {
    "SWARM ENGINE", "SWARM", 5, NPARAM, 2, PARAMS, PAGES, LAYOUT, values, init, update, 0, step, render, 0, key, visual,
};
