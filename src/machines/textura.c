/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Textura: the air around the groove. A band of filtered noise whose colour drifts slowly,
 * a low hum under it and a little crackle on top, all of it breathing with the kick: PUMP
 * pushes the texture down on every beat and RELEASE lets it rise back. It sounds while the
 * transport runs (and while a key is held). A new machine. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum { COLOR, WIDTH, DRIFT, RATE, PUMP, RELEASE, CRACKLE, HUM, NPARAM };
static const dv_param_t PARAMS[NPARAM] = {
    {"COLOR", 0, 100, 55, 0}, {"WIDTH", 0, 100, 45, 0},   {"DRIFT", 0, 100, 40, 0},   {"RATE", 0, 100, 30, 0},
    {"PUMP", 0, 100, 65, 0},  {"RELEASE", 0, 100, 45, 0}, {"CRACKLE", 0, 100, 25, 0}, {"HUM", 0, 100, 20, 0},
};
static const int8_t LAYOUT[2][4] = {{COLOR, WIDTH, DRIFT, RATE}, {PUMP, RELEASE, CRACKLE, HUM}};
static const char *const PAGES[2] = {"TEXTURE", "MOTION"};

static int16_t values[NPARAM];
static volatile uint8_t key_held;
static struct { float centre_oct, damping, drift_oct, drift_inc, pump, release, crackle, hum; } k;
static struct {
    float gain, duck, drift_phase, drift_from, drift_to, hum_a, hum_b, click;
    dv_svf_t band, ring;
    dv_rng_t noise;
} v;
static volatile float shown_hz = 1000.0f, shown_duck;

static void update(void)
{
    k.centre_oct = (float)values[COLOR] * 0.065f;                 /* 100 Hz .. 9 kHz */
    k.damping = 1.3f - (float)values[WIDTH] * 0.0115f;            /* wide and soft .. narrow and whistling */
    k.drift_oct = (float)values[DRIFT] * 0.02f;                   /* up to +-2 octaves */
    k.drift_inc = 0.02f * dv_exp2((float)values[RATE] * 0.0664f) / (float)DV_FS;   /* 0.02 .. 2 Hz */
    k.pump = (float)values[PUMP] * 0.01f;
    k.release = dv_settle_ms(80.0f + (float)values[RELEASE] * 5.0f);
    k.crackle = (float)values[CRACKLE] * (float)values[CRACKLE] * 0.004f / (float)DV_FS;   /* up to 40 per second */
    k.hum = (float)values[HUM] * 0.006f;
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0x3C6EF372u;
    update();
}

static void key(int note) { key_held = note >= 0; }

static void step(unsigned s16, float step_len)
{
    (void)step_len;
    if ((s16 & 3u) == 0u) v.duck = 1.0f;       /* every beat pushes the texture down */
}

static void render(float *out, unsigned n)
{
    float target = dv_is_playing() || key_held ? 1.0f : 0.0f;
    /* the colour wanders between random points, eased */
    v.drift_phase += k.drift_inc * (float)n;
    if (v.drift_phase >= 1.0f) {
        v.drift_phase -= 1.0f;
        v.drift_from = v.drift_to;
        v.drift_to = dv_rng_bipolar(&v.noise);
    }
    float ease = v.drift_phase * v.drift_phase * (3.0f - 2.0f * v.drift_phase);
    float drift = v.drift_from + (v.drift_to - v.drift_from) * ease;
    float hz = dv_clampf(100.0f * dv_exp2(k.centre_oct + drift * k.drift_oct), 60.0f, 5200.0f);
    dv_svf_set(&v.band, hz, k.damping);
    dv_svf_set(&v.ring, dv_clampf(hz * 2.7f, 800.0f, 5200.0f), 0.12f);
    shown_hz = hz;
    for (unsigned i = 0; i < n; i++) {
        float noise = dv_rng_bipolar(&v.noise);
        dv_svf_lp(&v.band, noise);
        float air = v.band.band * (0.35f + 0.5f * k.damping);     /* narrow bands ring louder: even them out */

        /* crackle: rare impulses of random size ringing in a bright resonator */
        float impulse = 0.0f;
        if ((float)(dv_rng_u32(&v.noise) >> 8) * (1.0f / 16777216.0f) < k.crackle) impulse = dv_rng_bipolar(&v.noise) * 2.0f;
        dv_svf_lp(&v.ring, impulse);
        float click = v.ring.band * 0.7f;

        v.hum_a += 98.0f / (float)DV_FS;                          /* G2 and a slightly flat fifth above */
        v.hum_b += 146.4f / (float)DV_FS;
        if (v.hum_a >= 1.0f) v.hum_a -= 1.0f;
        if (v.hum_b >= 1.0f) v.hum_b -= 1.0f;
        float hum = (dv_sin(v.hum_a) + 0.5f * dv_sin(v.hum_b)) * k.hum;

        v.duck *= k.release;
        v.gain += (target - v.gain) * 0.0008f;                    /* fades in and out in about 30 ms */
        out[i] = dv_softclip((air + click + hum) * (1.0f - v.duck * k.pump) * v.gain);
    }
    shown_duck = v.duck * k.pump;
}

/* the band on a 60 Hz .. 6 kHz axis: where it sits, how wide it is, how far it is pushed down */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    (void)s16;
    float hz = shown_hz, oct = 0.0f;
    while (hz > 120.0f) { hz *= 0.5f; oct += 1.0f; }
    oct += hz / 60.0f - 1.0f;                                      /* octaves above 60 Hz, roughly */
    float centre = oct / 6.6f * 34.0f, spread = 2.0f + (1.3f - k.damping) * -7.0f + 9.0f;
    float open = 1.0f - shown_duck;
    for (int i = 0; i < 34; i++) {
        float d = ((float)i - centre) / spread;
        float shape = 1.0f / (1.0f + d * d * d * d);
        int bh = 3 + (int)(shape * open * (float)(h - 6));
        dv_rrect(fb, x + i * 6, y + h - bh, 4, bh, 2, shape > 0.6f ? DV_C_LIME : shape > 0.15f ? DV_C_DEEP : DV_C_LINE);
    }
    (void)w;
}

const dv_machine_t DV_TEXTURA = {
    "TEXTURA", "TEXTUR", 7, NPARAM, 2, PARAMS, PAGES, LAYOUT, values, init, update, 0, step, render, 0, key, visual,
};
