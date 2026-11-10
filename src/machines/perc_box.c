/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Perc Box: the top of the groove in one track. Three voices, each on its own Euclidean
 * pattern (HITS spread over the 16 steps, turned by ROTATE):
 *   Hat   metallic squares and noise through a high-pass; off-beat hits ring longer (OPEN)
 *   Clap  three quick noise bursts and a tail through a band-pass
 *   Perc  a two-operator FM hit with a short pitch drop: toms, rims, bells
 * ACCENT sets how much the off-beats stand out. A new machine, built on the Hat Box idea
 * from Drelvarom. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum {
    H_HITS, H_ROT, H_DECAY, H_OPEN, C_HITS, C_ROT, C_DECAY, C_TONE, P_HITS, P_ROT, P_PITCH, P_DECAY,
    H_TONE, P_FM, P_RATIO, ACCENT, L_HAT, L_CLAP, L_PERC, NPARAM
};
static const char *const RATIOS[4] = {"1.41", "2.00", "2.76", "3.50"};
static const float RATIO[4] = {1.41f, 2.0f, 2.76f, 3.5f};
static const dv_param_t PARAMS[NPARAM] = {
    {"HITS", 0, 16, 4, 0},   {"ROTATE", 0, 15, 2, 0}, {"DECAY", 0, 100, 30, 0},  {"OPEN", 0, 100, 60, 0},
    {"HITS", 0, 16, 2, 0},   {"ROTATE", 0, 15, 4, 0}, {"DECAY", 0, 100, 40, 0},  {"TONE", 0, 100, 50, 0},
    {"HITS", 0, 16, 5, 0},   {"ROTATE", 0, 15, 3, 0}, {"PITCH", -12, 24, 7, 0},  {"DECAY", 0, 100, 35, 0},
    {"HAT TON", 0, 100, 60, 0}, {"FM", 0, 100, 45, 0}, {"RATIO", 0, 3, 2, RATIOS}, {"ACCENT", 0, 100, 50, 0},
    {"HAT", 0, 100, 70, 0},  {"CLAP", 0, 100, 65, 0}, {"PERC", 0, 100, 70, 0},
};
static const int8_t LAYOUT[5][4] = {
    {H_HITS, H_ROT, H_DECAY, H_OPEN}, {C_HITS, C_ROT, C_DECAY, C_TONE}, {P_HITS, P_ROT, P_PITCH, P_DECAY},
    {H_TONE, P_FM, P_RATIO, ACCENT},  {L_HAT, L_CLAP, L_PERC, -1}};
static const char *const PAGES[5] = {"HAT", "CLAP", "PERC", "SHAPE", "LEVELS"};

static int16_t values[NPARAM];
static struct {
    float hat_closed, hat_open, hat_hp, clap_tail, clap_hz, perc_hz, perc_decay, fm, accent, level[3];
} k;
static struct {
    uint32_t square[4];
    float hat_env, hat_decay, hat_gain, hp_low, hp_band;
    float clap_t, clap_env, clap_gain, perc_env, perc_mod, perc_drop, perc_gain, carrier, modulator;
    dv_svf_t clap_filter;
    dv_rng_t noise;
} v;

/* Euclidean rhythm: `hits` spread as evenly as 16 steps allow, turned by `rotate` */
static int hit_on(int hits, int rotate, unsigned s16)
{
    return hits > 0 && ((s16 + 16u - (unsigned)rotate) * (unsigned)hits) % 16u < (unsigned)hits;
}

static void update(void)
{
    k.hat_closed = dv_settle_ms(15.0f + (float)values[H_DECAY] * 1.2f);
    k.hat_open = dv_settle_ms((15.0f + (float)values[H_DECAY] * 1.2f) * (1.0f + (float)values[H_OPEN] * 0.09f));
    k.hat_hp = dv_clampf(6.2831853f * 3000.0f * dv_exp2((float)values[H_TONE] * 0.0174f) / (float)DV_FS, 0.05f, 0.95f);
    k.clap_tail = dv_settle_ms(60.0f + (float)values[C_DECAY] * 3.4f);
    k.clap_hz = 900.0f * dv_exp2((float)values[C_TONE] * 0.0142f);            /* 900 Hz .. 2.4 kHz */
    k.perc_hz = 150.0f * dv_exp2((float)values[P_PITCH] / 12.0f);
    k.perc_decay = dv_settle_ms(40.0f + (float)values[P_DECAY] * 5.6f);
    k.fm = (float)values[P_FM] * 0.04f / 6.2831853f;                          /* index up to 4, in turns */
    k.accent = (float)values[ACCENT] * 0.006f;
    for (int i = 0; i < 3; i++) k.level[i] = (float)values[L_HAT + i] * 0.01f;
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0xB5297A4Du;
    v.clap_t = 1.0e6f;
    update();
}

static void step(unsigned s16, float step_len)
{
    (void)step_len;
    int off_beat = (s16 & 3u) == 2u;
    float strong = 1.0f, weak = 1.0f - k.accent;
    if (hit_on(values[H_HITS], values[H_ROT], s16 & 15u)) {
        v.hat_env = 1.0f;
        v.hat_decay = off_beat ? k.hat_open : k.hat_closed;
        v.hat_gain = off_beat ? strong : weak;
    }
    if (hit_on(values[C_HITS], values[C_ROT], s16 & 15u)) {
        v.clap_t = 0.0f;
        v.clap_gain = (s16 & 3u) == 0u ? strong : weak;
    }
    if (hit_on(values[P_HITS], values[P_ROT], s16 & 15u)) {
        v.perc_env = v.perc_mod = v.perc_drop = 1.0f;
        v.carrier = 0.0f;
        v.perc_gain = (s16 & 1u) ? weak : strong;
    }
}

static void render(float *out, unsigned n)
{
    /* inharmonic squares, the classic metal recipe */
    static const uint32_t SQUARE_INC[4] = {
        (uint32_t)(3155.0 * 4294967296.0 / DV_FS), (uint32_t)(4217.0 * 4294967296.0 / DV_FS),
        (uint32_t)(5328.0 * 4294967296.0 / DV_FS), (uint32_t)(6861.0 * 4294967296.0 / DV_FS)};
    const float ms = 1000.0f / (float)DV_FS;
    dv_svf_set(&v.clap_filter, k.clap_hz, 0.55f);
    for (unsigned i = 0; i < n; i++) {
        float noise = dv_rng_bipolar(&v.noise);

        /* hat */
        float metal = 0.0f;
        for (int o = 0; o < 4; o++) metal += (v.square[o] += SQUARE_INC[o]) < 0x80000000u ? 0.25f : -0.25f;
        float src = metal * 0.6f + noise * 0.5f;
        v.hp_low += k.hat_hp * v.hp_band;
        float high = src - v.hp_low - 1.2f * v.hp_band;
        v.hp_band += k.hat_hp * high;
        v.hat_env *= v.hat_decay;
        float hat = high * v.hat_env * v.hat_gain;

        /* clap: bursts at 0, 10 and 20 ms, then the tail */
        float burst = 0.0f;
        if (v.clap_t < 30.0f) {
            float t = v.clap_t;
            while (t >= 10.0f) t -= 10.0f;
            burst = dv_exp2(-t * 0.48f);                                   /* about 3 ms */
            v.clap_env = 0.8f;
        } else {
            v.clap_env *= k.clap_tail;
            burst = v.clap_env;
        }
        v.clap_t += ms;
        dv_svf_lp(&v.clap_filter, noise);
        float clap = v.clap_t < 2000.0f ? v.clap_filter.band * burst * v.clap_gain * 1.6f : 0.0f;

        /* perc: FM with the index and the pitch falling fast */
        v.perc_env *= k.perc_decay;
        v.perc_mod *= 0.99925f;                                            /* about 30 ms */
        v.perc_drop *= 0.99717f;                                           /* about 8 ms */
        float hz = k.perc_hz * (1.0f + 0.41f * v.perc_drop);
        v.carrier += hz / (float)DV_FS;
        v.modulator += hz * RATIO[values[P_RATIO]] / (float)DV_FS;
        if (v.carrier >= 1.0f) v.carrier -= 1.0f;
        if (v.modulator >= 1.0f) v.modulator -= 1.0f;
        float perc = dv_sin(v.carrier + k.fm * v.perc_mod * dv_sin(v.modulator) + 1.0f) * v.perc_env * v.perc_gain;

        out[i] = dv_softclip(hat * k.level[0] + clap * k.level[1] + perc * k.level[2] * 0.9f);
    }
}

/* three rows of sixteen steps: hat, clap, perc */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    static const char *const rows[3] = {"H", "C", "P"};
    static const uint8_t hits[3] = {H_HITS, C_HITS, P_HITS}, rots[3] = {H_ROT, C_ROT, P_ROT};
    int cell = (w - 18 - 9) / 16, rh = h / 3;
    for (int r = 0; r < 3; r++) {
        int ry = y + r * rh;
        dv_text(fb, DV_FONT_SMALL, x, ry + rh / 2 - 6, 0, rows[r], DV_C_DIM);
        for (int i = 0; i < 16; i++) {
            int on = hit_on(values[hits[r]], values[rots[r]], (unsigned)i), now = i == (int)s16;
            int bx = x + 18 + i * cell + (i / 4) * 3;
            dv_rrect(fb, bx, ry + 2, cell - 2, rh - 5, 3, on ? (now ? DV_C_TEXT : (i & 3) == 2 ? DV_C_LIME : DV_C_DEEP) : (now ? DV_C_LINE : DV_C_BASE));
        }
    }
}

const dv_machine_t DV_PERC_BOX = {
    "PERC BOX", "PERC", 6, NPARAM, 5, PARAMS, PAGES, LAYOUT, values, init, update, 0, step, render, 0, 0, visual,
};
