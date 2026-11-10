/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Small DSP helpers shared by the machines. Single precision only, no libm, no allocation. */
#ifndef DV_DSP_H
#define DV_DSP_H
#include <stdint.h>
#include "dv.h"

static inline float dv_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* 2^x for x in about -30..30; relative error below 1e-4 */
static inline float dv_exp2(float x)
{
    int i = (int)x;
    if (x < (float)i) i--;
    float f = x - (float)i;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    union { uint32_t u; float v; } s;
    i = i < -120 ? -120 : i > 120 ? 120 : i;
    s.u = (uint32_t)(i + 127) << 23;
    return p * s.v;
}

/* per-sample multiplier of an exponential decay with time constant ms */
static inline float dv_decay_ms(float ms) { return dv_exp2(-1.442695f * 1000.0f / (ms * (float)DV_FS)); }

/* sine of a phase in turns (0..1) */
static inline float dv_sin(float turns)
{
    float x = turns - (float)(int)turns;
    if (x < 0.0f) x += 1.0f;
    x = x < 0.5f ? x : x - 1.0f;                       /* -0.5..0.5 */
    float y = x * (8.0f - 16.0f * (x < 0.0f ? -x : x));  /* parabola through the peaks */
    return y * (0.775f + 0.225f * (y < 0.0f ? -y : y)); /* one correction step */
}

/* smooth saturation: unity gain near zero, flat at +-1 */
static inline float dv_softclip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - x * x * x * (4.0f / 27.0f);
}

typedef struct { uint32_t s; } dv_rng_t;
static inline uint32_t dv_rng_u32(dv_rng_t *r)
{
    uint32_t x = r->s ? r->s : 0x2545F491u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}
static inline float dv_rng_bipolar(dv_rng_t *r) { return (float)(int32_t)dv_rng_u32(r) * (1.0f / 2147483648.0f); }

/* state variable low-pass (Chamberlin); keep the cutoff under about fs / 8 */
typedef struct { float low, band, f, q; } dv_svf_t;
static inline void dv_svf_set(dv_svf_t *s, float hz, float damping)
{
    s->f = dv_clampf(6.2831853f * hz / (float)DV_FS, 0.001f, 0.75f);
    s->q = damping;
}
static inline float dv_svf_lp(dv_svf_t *s, float x)
{
    s->low += s->f * s->band;
    s->band += s->f * (x - s->low - s->q * s->band);
    return s->low;
}
#endif
