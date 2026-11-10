/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Sismo Box: the Sismo plugin idea as a machine. A synthesized kick on every beat plus a
 * synthesized low rumble: a decaying sine burst on each 1/16 step, shaped by the Sismo groove
 * figures, through a moving low-pass, saturated and ducked by the kick.
 *
 * The kick's attack has three parts: it starts at the top of the sine (a hard edge), a very
 * fast extra pitch drop (the tick) and a short bright noise burst (the click). Ported from the
 * Drelvarom program of the same name; times and depths are the same, the arithmetic is float
 * at 44.1 kHz instead of Q15 at 16.7 kHz. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum { RUMBLE, GROOVE, WEIGHT, GRIT, PUMP, PITCH, DECAY, TAIL, CLICK, SNAP, NPARAM };

static const char *const GROOVES[6] = {"ROLL", "OFFBT", "GALLP", "TRIPL", "WAREH", "GEN"};
static const dv_param_t PARAMS[NPARAM] = {
    {"RUMBLE", 0, 100, 70, 0}, {"GROOVE", 0, 5, 4, GROOVES}, {"WEIGHT", 0, 100, 50, 0},
    {"GRIT", 0, 100, 35, 0},   {"PUMP", 0, 100, 70, 0},      {"PITCH", -12, 12, 0, 0},
    {"DECAY", 0, 100, 40, 0},  {"TAIL", 0, 100, 50, 0},      {"CLICK", 0, 100, 60, 0},
    {"SNAP", 0, 100, 50, 0},
};
static const int8_t LAYOUT[3][4] = {
    {RUMBLE, GROOVE, WEIGHT, GRIT}, {PUMP, PITCH, DECAY, TAIL}, {CLICK, SNAP, -1, -1}};
static const char *const PAGES[3] = {"RUMBLE", "KICK", "PUNCH"};

#define KICK_BASE_HZ 49.0f      /* about G1 at PITCH 0 */
#define PITCH_CEILING_HZ 6000.0f

static int16_t values[NPARAM];
static uint8_t groove[16];
static uint32_t seed = 1;
static volatile uint8_t regenerate;

/* coefficients, written by update() and read by the audio */
static struct {
    float base_hz, kick_decay, burst_decay, duck_release, pitch_drop, tick_drop, click_decay;
    float click_amt, tick_oct, snap_oct, cutoff_hz, cutoff_oct, rumble, grit, pump;
} k;
/* voice state, audio only */
static struct {
    float kick_phase, rumble_phase, kick_env, pitch_env, tick_env, click_env, burst_env, duck;
    float last_noise, burst_cutoff_hz;
    dv_svf_t filter;
    dv_rng_t noise;
} v;

/* Groove figures from the Sismo plugin (beat accents repeated over the bar), or a seeded
 * figure after the Pyramidal Decode rumble loops. */
static void generate(void)
{
    static const uint8_t fixed[5][4] = {
        {255, 179, 217, 179},  /* Rolling 16th */
        {89, 140, 255, 204},   /* Offbeat Sync */
        {230, 38, 217, 255},   /* Gallop 3-4 */
        {128, 255, 153, 242},  /* Triplet Push */
        {102, 255, 191, 217},  /* Warehouse Tail */
    };
    int g = values[GROOVE];
    regenerate = 0;
    if (g < 5) {
        for (int i = 0; i < 16; i++) groove[i] = fixed[g][i & 3];
        return;
    }
    dv_rng_t rng = {seed * 2654435761u + 0x9E3779B9u};
    int cycle = (dv_rng_u32(&rng) & 3u) == 0u ? 4 : ((dv_rng_u32(&rng) & 1u) ? 8 : 16);
    uint8_t fig[16];
    for (int i = 0; i < cycle; i++) {
        uint32_t u = dv_rng_u32(&rng) & 0xFFFFu;
        if (i % 4 == 0) fig[i] = (uint8_t)(128u + u % 100u);            /* under the duck */
        else if ((u & 0xFFu) < 72u) fig[i] = (uint8_t)(8u + u % 24u);   /* hole */
        else fig[i] = (uint8_t)(115u + u % 140u);
    }
    for (int b = 0; b < cycle; b += 4) {   /* one strong off-beat per beat */
        int best = b + 1;
        for (int i = b + 2; i < b + 4 && i < cycle; i++)
            if (fig[i] > fig[best]) best = i;
        if (fig[best] < 217) fig[best] = 217;
    }
    for (int i = 0; i < 16; i++) groove[i] = fig[i % cycle];
}

static void update(void)
{
    float weight = (float)values[WEIGHT] * 0.01f;
    k.base_hz = KICK_BASE_HZ * dv_exp2((float)values[PITCH] / 12.0f);
    k.kick_decay = dv_decay_ms(60.0f + (float)values[DECAY] * 6.0f);
    k.burst_decay = dv_decay_ms(40.0f + (float)values[TAIL] * 5.0f);
    k.duck_release = dv_decay_ms(60.0f + (float)values[PUMP] * 3.0f);
    k.pitch_drop = dv_decay_ms(4.4f);
    k.tick_drop = dv_decay_ms(0.44f);
    k.click_decay = dv_decay_ms(0.66f);
    k.click_amt = (float)values[CLICK] * 0.01f;
    k.tick_oct = (float)values[CLICK] * 0.04f;          /* up to +4 oct for about 1 ms */
    k.snap_oct = 1.0f + (float)values[SNAP] * 0.04f;    /* +1 .. +5 oct, 50 = +3 */
    k.cutoff_hz = 50.0f * dv_exp2(weight * 3.8f);       /* about 50 Hz .. 700 Hz */
    k.cutoff_oct = 1.0f + weight * 1.95f;               /* opening per step accent */
    k.rumble = (float)values[RUMBLE] * 0.01f;
    k.grit = (float)values[GRIT] * (1.0f / 32.0f);
    k.pump = (float)values[PUMP] * 0.01f;
    if (regenerate || groove[0] == 0) generate();
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0x1234ABCDu;
    v.burst_cutoff_hz = 100.0f;
    regenerate = 1;
    update();
}

static void action(void)
{
    seed += 7919u;
    regenerate = 1;
    update();
}

static void step(unsigned s, int accent)
{
    if ((s & 3u) == 0u) {              /* kick on every beat */
        v.kick_phase = 0.25f;          /* top of the sine: the body starts on a hard edge */
        v.kick_env = accent ? 1.1f : 1.0f;
        v.pitch_env = 1.0f;
        v.tick_env = 1.0f;
        v.click_env = accent ? 1.0f : 0.79f;
    }
    float level = (float)groove[s & 15u] * (1.0f / 255.0f);
    v.burst_env = level * k.rumble;
    v.burst_cutoff_hz = k.cutoff_hz * dv_exp2(level * k.cutoff_oct);
}

static void render(float *out, unsigned n)
{
    float inv_fs = 1.0f / (float)DV_FS;
    dv_svf_set(&v.filter, dv_clampf(v.burst_cutoff_hz, 30.0f, 5000.0f), 0.8f);
    for (unsigned i = 0; i < n; i++) {
        /* kick: sine with a fast pitch drop and an exponential decay */
        v.pitch_env *= k.pitch_drop;
        v.tick_env *= k.tick_drop;
        float hz = k.base_hz * dv_exp2(v.pitch_env * k.snap_oct + v.tick_env * k.tick_oct);
        if (hz > PITCH_CEILING_HZ) hz = PITCH_CEILING_HZ;
        v.kick_phase += hz * inv_fs;
        if (v.kick_phase >= 1.0f) v.kick_phase -= 1.0f;
        v.kick_env *= k.kick_decay;
        float kick = dv_sin(v.kick_phase) * v.kick_env;
        /* click: a short burst of high-passed noise on the attack */
        v.click_env *= k.click_decay;
        float noise = dv_rng_bipolar(&v.noise);
        float click = (noise - v.last_noise) * 0.5f * v.click_env * k.click_amt;
        v.last_noise = noise;
        kick = dv_softclip(kick * 1.5f + click);   /* a little drive */

        /* rumble: low sine burst through a moving low-pass, saturated and ducked */
        v.rumble_phase += k.base_hz * inv_fs;
        if (v.rumble_phase >= 1.0f) v.rumble_phase -= 1.0f;
        v.burst_env *= k.burst_decay;
        float r = dv_sin(v.rumble_phase) * v.burst_env;
        r += dv_rng_bipolar(&v.noise) * v.burst_env * (1.0f / 16.0f);   /* a touch of air */
        r = dv_svf_lp(&v.filter, r);
        r = dv_softclip(r + r * k.grit);
        v.duck = v.kick_env > v.duck ? v.kick_env : v.duck * k.duck_release;
        r *= dv_clampf(1.0f - dv_clampf(v.duck, 0.0f, 1.0f) * k.pump, 0.0f, 1.0f);

        out[i] = dv_softclip(kick + r);
    }
}

/* 16 steps grouped by beat: bar height = groove level, a block marks each kick */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s)
{
    int bw = (w - 3 * 8) / 16, top = h - 12;
    for (int i = 0; i < 16; i++) {
        int bx = x + i * bw + (i / 4) * 8;
        int bh = groove[i] * top / 255;
        int now = i == (int)(s & 15u);
        dv_fill(fb, bx, y + top - bh, bw - 3, bh, now ? DV_C_TEXT : DV_C_LIME);
        if ((i & 3) == 0) dv_fill(fb, bx, y + top + 4, bw - 3, 5, now ? DV_C_TEXT : DV_C_DEEP);
        else dv_fill(fb, bx, y + top + 6, bw - 3, 1, DV_C_LINE);
    }
}

const dv_machine_t DV_SISMO_BOX = {
    "SISMO BOX", NPARAM, 3, PARAMS, PAGES, LAYOUT, values, init, update, step, render, action, visual,
};
