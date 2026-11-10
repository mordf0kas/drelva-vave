/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* The master section: a tempo-synced dub delay and a small reverb fed by the track sends,
 * then a one-knob filter and a drive on the whole mix. PUMP ducks every track except the one
 * holding Sismo Box on each beat, the sidechain of a techno mix. It shows up as a seventh "machine"
 * (ARP button) so its pages and knobs work like any track's. Only the stereo mix passes
 * through here; the per-track outputs stay dry.
 *
 * Memory: the delay line is 64 KB (16-bit), the reverb about 48 KB (float). */
#include "dv.h"
#include "dv_dsp.h"
#include "dv_gfx.h"

enum { D_TIME, D_FEEDBACK, D_TONE, D_RETURN, R_SIZE, R_DAMP, R_RETURN, O_FILTER, O_DRIVE, O_PUMP, NPARAM };
static const char *const TIMES[4] = {"1/16", "1/8", "3/16", "1/4"};
static const dv_param_t PARAMS[NPARAM] = {
    {"TIME", 0, 3, 2, TIMES},  {"FEEDBK", 0, 100, 48, 0}, {"TONE", 0, 100, 45, 0}, {"RETURN", 0, 100, 70, 0},
    {"SIZE", 0, 100, 68, 0},   {"DAMP", 0, 100, 50, 0},   {"RETURN", 0, 100, 60, 0},
    {"FILTER", -100, 100, 0, 0}, {"DRIVE", 0, 100, 12, 0}, {"PUMP", 0, 100, 35, 0},
};
static const int8_t LAYOUT[3][4] = {
    {D_TIME, D_FEEDBACK, D_TONE, D_RETURN}, {R_SIZE, R_DAMP, R_RETURN, -1}, {O_FILTER, O_DRIVE, O_PUMP, -1}};
static const char *const PAGES[3] = {"DELAY", "REVERB", "OUTPUT"};
static int16_t values[NPARAM];

#define DELAY_LEN 32768u
static int16_t delay_line[DELAY_LEN];
static uint32_t delay_pos;
static float delay_lp, delay_hp;

#define NCOMB 4
static const uint16_t COMB_LEN[NCOMB] = {1116, 1188, 1277, 1356}, AP_LEN[2] = {556, 441};
#define STEREO_SPREAD 23u
static float comb_buf[2][NCOMB][1356 + STEREO_SPREAD], ap_buf[2][2][556 + STEREO_SPREAD];
static float comb_lp[2][NCOMB];
static uint16_t comb_at[2][NCOMB], ap_at[2][2];

static dv_svf_t out_filter[2];
static struct { float fb, tone, d_return, room, damp, r_return, drive, makeup, f_hz; int f_mode; } k;

static void update(void)
{
    k.fb = (float)values[D_FEEDBACK] * 0.0092f;
    k.tone = dv_clampf(6.2831853f * 400.0f * dv_exp2((float)values[D_TONE] * 0.043f) / (float)DV_FS, 0.0f, 0.9f);
    k.d_return = (float)values[D_RETURN] * 0.01f;
    k.room = 0.70f + (float)values[R_SIZE] * 0.0027f;
    k.damp = 0.05f + (float)values[R_DAMP] * 0.0065f;
    k.r_return = (float)values[R_RETURN] * 0.01f;
    k.drive = 1.0f + (float)values[O_DRIVE] * 0.03f;
    k.makeup = 1.0f / (1.0f + (float)values[O_DRIVE] * 0.012f);
    int f = values[O_FILTER];
    k.f_mode = f < -2 ? -1 : f > 2 ? 1 : 0;
    /* left of centre: low-pass closing from 16 kHz to 150 Hz; right: high-pass opening 30 Hz to 3 kHz */
    k.f_hz = f < 0 ? 16000.0f * dv_exp2((float)f * 0.0674f) : 30.0f * dv_exp2((float)f * 0.0664f);
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    update();
}

float dv_fx_pump(void) { return (float)values[O_PUMP] * 0.01f; }

static float reverb_channel(unsigned c, float in)
{
    float out = 0.0f;
    for (unsigned i = 0; i < NCOMB; i++) {
        unsigned len = COMB_LEN[i] + c * STEREO_SPREAD;
        float y = comb_buf[c][i][comb_at[c][i]];
        comb_lp[c][i] += (y - comb_lp[c][i]) * (1.0f - k.damp);
        comb_buf[c][i][comb_at[c][i]] = in + comb_lp[c][i] * k.room;
        if (++comb_at[c][i] >= len) comb_at[c][i] = 0;
        out += y;
    }
    for (unsigned i = 0; i < 2; i++) {
        unsigned len = AP_LEN[i] + c * STEREO_SPREAD;
        float b = ap_buf[c][i][ap_at[c][i]];
        ap_buf[c][i][ap_at[c][i]] = out + b * 0.5f;
        if (++ap_at[c][i] >= len) ap_at[c][i] = 0;
        out = b - out * 0.5f;
    }
    return out;
}

void dv_fx_process(const float *to_delay, const float *to_reverb, float *left, float *right, float step_len)
{
    static const uint8_t steps[4] = {1, 2, 3, 4};
    uint32_t delay = (uint32_t)(step_len * (float)steps[values[D_TIME]]);
    if (delay > DELAY_LEN - 64u) delay = DELAY_LEN - 64u;
    if (delay < 32u) delay = 32u;
    if (k.f_mode) {
        dv_svf_set(&out_filter[0], k.f_hz, 1.1f);
        out_filter[1].f = out_filter[0].f;
        out_filter[1].q = out_filter[0].q;
    }
    for (unsigned i = 0; i < DV_BLOCK; i++) {
        /* delay: the repeats lose highs (TONE) and a little low end each time round */
        float echo = (float)delay_line[(delay_pos - delay) & (DELAY_LEN - 1u)] * (1.0f / 16384.0f);
        delay_lp += (echo - delay_lp) * k.tone;
        delay_hp += (delay_lp - delay_hp) * 0.017f;                  /* about 120 Hz */
        float back = dv_softclip(to_delay[i] + (delay_lp - delay_hp) * k.fb);
        delay_line[delay_pos & (DELAY_LEN - 1u)] = (int16_t)(back * 16383.0f);
        delay_pos++;

        float wet_in = (to_reverb[i] + echo * 0.15f) * 0.06f;
        float l = left[i] + echo * k.d_return + reverb_channel(0, wet_in) * k.r_return;
        float r = right[i] + echo * k.d_return + reverb_channel(1, wet_in) * k.r_return;

        if (k.f_mode) {
            float ll = dv_svf_lp(&out_filter[0], l), rl = dv_svf_lp(&out_filter[1], r);
            l = k.f_mode < 0 ? ll : l - ll - out_filter[0].q * out_filter[0].band;
            r = k.f_mode < 0 ? rl : r - rl - out_filter[1].q * out_filter[1].band;
        }
        left[i] = dv_softclip(l * k.drive) * k.makeup;
        right[i] = dv_softclip(r * k.drive) * k.makeup;
    }
}

/* the mixer at a glance: six track meters and the stereo output */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    (void)s16;
    for (unsigned t = 0; t < DV_TRACKS + 2u; t++) {
        int bx = x + (int)t * (w / 8) + (t >= DV_TRACKS ? 6 : 0), bw = w / 8 - 8;
        float level = t < DV_TRACKS ? dv_meter(t) : dv_meter_out(t - DV_TRACKS);
        int bh = (int)(dv_clampf(level * 1.2f, 0.0f, 1.0f) * (float)(h - 4));
        dv_rrect(fb, bx, y, bw, h, 3, DV_C_BASE);
        if (bh > 2) dv_rrect(fb, bx, y + h - bh, bw, bh, 3, level > 0.83f ? DV_C_TEXT : t < DV_TRACKS ? DV_C_DEEP : DV_C_LIME);
    }
}

const dv_machine_t DV_MASTER = {
    "MASTER", "MASTER", 100, NPARAM, 3, PARAMS, PAGES, LAYOUT, values, init, update, 0, 0, 0, 0, 0, visual,
};
