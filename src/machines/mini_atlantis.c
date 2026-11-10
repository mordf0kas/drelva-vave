/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Mini Atlantis: an SH-101 style mono voice (VCO with saw, pulse, sub and noise, 12 dB
 * low-pass, ADSR, VCA, LFO) played by a stage sequencer after the ML-185: up to 8 stages,
 * each with a pitch, a length in 1/16 pulses and a gate mode (rest / one / multi / hold),
 * with accents and slides. The pattern length is the sum of the stage lengths, so the rhythm
 * comes from lengths and gate modes, not from a step grid. SEQ rolls a new pattern in the
 * chosen scale; the keyboard transposes it.
 *
 * Ported from the Drelvarom voice and its Atlantis Seq program: same parameters and ranges,
 * float arithmetic at 44.1 kHz. The single-stage editor of the module is not here yet. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum {
    TUNE, FINE, PW, SUB_MODE, SAW_LVL, PULSE_LVL, SUB_LVL, NOISE_LVL, CUTOFF, RES, ENV_AMT, KEY_TRACK,
    ATTACK, DECAY, SUSTAIN, RELEASE, GLIDE, GLIDE_MODE, VCA_MODE, LFO_RATE, LFO_PW, LFO_CUTOFF, LFO_PITCH,
    STAGES, SLIDES, ACCENTS, SEED, SCALE, OCTS, ROOT, NPARAM
};
enum { REST, SINGLE, MULTI, HOLD };
enum { F_ACCENT = 1, F_SLIDE = 2 };
#define NSTAGE 8
#define NSCALE 6

static const char *const SUBS[3] = {"-1 OCT", "-2 OCT", "-2 PLS"};
static const char *const TRACKING[3] = {"0%", "50%", "100%"};
static const char *const GLIDES[2] = {"SLIDE", "ALWAYS"};
static const char *const VCAS[2] = {"ENV", "GATE"};
static const char *const SCALES[NSCALE] = {"MINOR", "PHRYG", "DORIAN", "PENTA", "ACID", "5THS"};
static const dv_param_t PARAMS[NPARAM] = {
    {"TUNE", -24, 24, 0, 0},   {"FINE", -50, 50, 0, 0},   {"PW", 5, 95, 50, 0},      {"SUB", 0, 2, 0, SUBS},
    {"SAW", 0, 100, 100, 0},   {"PULSE", 0, 100, 0, 0},   {"SUB", 0, 100, 40, 0},    {"NOISE", 0, 100, 0, 0},
    {"CUTOFF", 0, 127, 55, 0}, {"RESO", 0, 127, 60, 0},   {"ENV", -64, 63, 40, 0},   {"KEYTRK", 0, 2, 1, TRACKING},
    {"ATTACK", 0, 127, 0, 0},  {"DECAY", 0, 127, 70, 0},  {"SUS", 0, 100, 0, 0},     {"REL", 0, 127, 60, 0},
    {"GLIDE", 0, 127, 60, 0},  {"GL ON", 0, 1, 0, GLIDES}, {"VCA", 0, 1, 0, VCAS},   {"LFO", 0, 127, 76, 0},
    {"LFO PW", 0, 100, 0, 0},  {"LFO CUT", 0, 100, 0, 0}, {"LFO PIT", 0, 100, 0, 0},
    {"STAGES", 1, 8, 8, 0},    {"SLIDE", 0, 100, 20, 0},  {"ACCENT", 0, 100, 30, 0}, {"SEED", 0, 127, 5, 0},
    {"SCALE", 0, NSCALE - 1, 0, SCALES}, {"OCTS", 1, 2, 1, 0}, {"ROOT", -12, 12, 0, 0},
};
static const int8_t LAYOUT[9][4] = {
    {CUTOFF, RES, ENV_AMT, DECAY},
    {STAGES, SLIDES, ACCENTS, SEED},
    {SCALE, OCTS, ROOT, -1},
    {TUNE, FINE, PW, SUB_MODE},
    {SAW_LVL, PULSE_LVL, SUB_LVL, NOISE_LVL},
    {CUTOFF, RES, ENV_AMT, KEY_TRACK},
    {ATTACK, DECAY, SUSTAIN, RELEASE},
    {GLIDE, GLIDE_MODE, VCA_MODE, LFO_RATE},
    {LFO_PW, LFO_CUTOFF, LFO_PITCH, -1},
};
static const char *const PAGES[9] = {"PLAY", "SEQ", "GEN", "VCO", "MIXER", "FILTER", "ENV", "MOD", "LFO"};

typedef struct { int8_t pitch; uint8_t length, mode, flags; } stage_t;

static int16_t values[NPARAM];
static stage_t stages[NSTAGE];
static volatile uint8_t stage_now = NSTAGE - 1, pulse_now = 0xFF;
static volatile int8_t transpose;

/* coefficients from the parameters */
static struct {
    float level[4], k_attack, k_decay, k_release, sustain, k_glide, lfo_inc, damping;
} k;
/* voice state, audio only */
static struct {
    float phase, lfo, pitch, env, gate_slew, low, band, last_noise;
    uint32_t wraps;
    uint8_t env_stage, gate, accent, glide, tied, started;
    float note, gate_left;          /* gate_left: samples the gate stays high */
    dv_rng_t noise;
} v;
enum { E_IDLE, E_ATTACK, E_DECAY, E_SUSTAIN, E_RELEASE };

/* 0..127 to an exponential time between lo and hi */
static float time_from_index(int index, float lo, float hi)
{
    float ratio = hi / lo, log2r = 0.0f;
    while (ratio >= 2.0f) { ratio *= 0.5f; log2r += 1.0f; }
    log2r += (ratio - 1.0f) * (1.3465f - 0.3465f * (ratio - 1.0f));   /* log2 on 1..2 */
    return lo * dv_exp2(log2r * (float)index / 127.0f);
}
/* one-pole step size so that a step settles to about -40 dB in ms */
static float approach_ms(float ms, float settle)
{
    if (ms < 0.05f) return 1.0f;
    return 1.0f - dv_exp2(-1.442695f * settle * 1000.0f / (ms * (float)DV_FS));
}

/* New pattern from the seed in the chosen scale: root weighted, some rests, multis and
 * holds, accents and slides from the SEQ page. */
static void generate(void)
{
    static const int8_t scales[NSCALE][8] = {
        {0, 0, 2, 3, 5, 7, 8, 10},     /* minor */
        {0, 0, 1, 3, 5, 7, 8, 10},     /* phrygian */
        {0, 0, 2, 3, 5, 7, 9, 10},     /* dorian */
        {0, 3, 5, 7, 10, 12, 15, 17},  /* minor pentatonic */
        {0, 0, 3, 5, 7, 10, 12, 15},   /* acid */
        {0, 0, 0, 7, 7, 12, 12, -5},   /* root, fifths and octaves */
    };
    const int long_bias = 40, rest = 12, multi = 15, hold = 12;
    dv_rng_t rng = {0x9E3779B9u ^ ((uint32_t)values[SEED] * 2654435761u)};
    const int8_t *scale = scales[values[SCALE]];
    for (int i = 0; i < NSTAGE; i++) {
        stage_t *s = &stages[i];
        uint32_t r = dv_rng_u32(&rng);
        int pitch = scale[(r & 0xFFu) % 8u] + 12 * (int)(((r >> 8) & 0xFFu) % (uint32_t)values[OCTS]);
        s->pitch = (int8_t)(pitch < -12 ? -12 : pitch > 19 ? 19 : pitch);
        int lr = (int)((r >> 16) & 0x7Fu) * 100 / 128;      /* mostly 1-2, some longer */
        s->length = (uint8_t)(lr < long_bias / 3 ? 3u + ((r >> 23) & 3u) : lr < long_bias ? 2u : 1u);
        int mr = (int)(dv_rng_u32(&rng) % 100u);
        s->mode = (uint8_t)(mr < rest ? REST : mr < rest + multi ? MULTI : mr < rest + multi + hold ? HOLD : SINGLE);
        s->flags = 0;
        if ((int)(dv_rng_u32(&rng) % 100u) < values[ACCENTS]) s->flags |= F_ACCENT;
        if ((int)(dv_rng_u32(&rng) % 100u) < values[SLIDES]) s->flags |= F_SLIDE;
    }
    stages[0].pitch = 0;                                     /* starts on the root and sounds */
    if (stages[0].mode == REST) stages[0].mode = SINGLE;
}

static int16_t generated_for[4] = {-1, -1, -1, -1};
static void update(void)
{
    for (int i = 0; i < 4; i++) k.level[i] = (float)values[SAW_LVL + i] * 0.005f;   /* half scale */
    k.k_attack = approach_ms(time_from_index(values[ATTACK], 1.0f, 4000.0f), 1.8f);
    k.k_decay = approach_ms(time_from_index(values[DECAY], 1.0f, 4000.0f), 4.6f);
    k.k_release = approach_ms(time_from_index(values[RELEASE], 1.0f, 4000.0f), 4.6f);
    k.sustain = (float)values[SUSTAIN] * 0.01f;
    k.k_glide = approach_ms(time_from_index(values[GLIDE], 1.0f, 2000.0f), 4.6f);
    k.lfo_inc = time_from_index(values[LFO_RATE], 0.05f, 30.0f) / (float)DV_FS;
    k.damping = 1.2f - (float)values[RES] * (1.17f / 127.0f);
    int16_t key[4] = {values[SEED], values[SCALE], values[OCTS], values[SLIDES] * 128 + values[ACCENTS]};
    for (int i = 0; i < 4; i++)
        if (key[i] != generated_for[i]) {
            for (int j = 0; j < 4; j++) generated_for[j] = key[j];
            generate();
            break;
        }
}

static void reset(void)
{
    stage_now = (uint8_t)(values[STAGES] - 1);
    pulse_now = 0xFF;
    v.tied = 0;
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0x51ED270Bu;
    transpose = 0;
    update();
    reset();
}

static void action(void)
{
    values[SEED] = (int16_t)((values[SEED] + 1) & 127);
    update();
}

static void key(int note) { transpose = (int8_t)(note - 7); }   /* the first C plays the pattern as written */

static void step(unsigned s16, float step_len)
{
    (void)s16;
    uint8_t count = (uint8_t)values[STAGES];
    uint8_t stage = stage_now < count ? stage_now : 0, pulse = (uint8_t)(pulse_now + 1u);
    if (pulse >= stages[stage].length || stage_now >= count) {
        pulse = 0;
        stage = (uint8_t)(stage + 1u >= count ? 0u : stage + 1u);
    }
    stage_now = stage;
    pulse_now = pulse;
    const stage_t *s = &stages[stage], *next = &stages[stage + 1u >= count ? 0u : stage + 1u];
    int trigger = s->mode == MULTI || (s->mode != REST && pulse == 0);
    int hold = s->mode == HOLD, last = pulse + 1u >= s->length;
    /* a slide on the last pulse of a sounding stage ties into the next stage:
     * the gate stays high and the next note glides */
    int gate_high = s->mode == HOLD || s->mode == MULTI || s->length == 1;
    int tie = last && gate_high && (s->flags & F_SLIDE) && next->mode != REST;
    if (trigger) {
        v.note = (float)(values[ROOT] + s->pitch + transpose);
        v.accent = (s->flags & F_ACCENT) != 0;
        v.glide = v.tied;
        if (!v.tied) v.gate = 0;                 /* a new note retriggers the envelope */
    }
    float eighths = s->mode == REST ? 0.0f : tie ? 9.0f : hold ? (last ? 6.0f : 9.0f) : trigger ? 4.0f : 0.0f;
    v.gate_left = eighths * 0.125f * step_len;
    v.tied = (uint8_t)tie;
    v.started = 1;
}

static inline float saw_blep(float p, float inc)
{
    float s = 2.0f * p - 1.0f;
    if (p < inc) { float t = p / inc; s -= t + t - t * t - 1.0f; }
    else if (p > 1.0f - inc) { float t = (p - 1.0f) / inc; s -= t * t + t + t + 1.0f; }
    return s;
}

static void render(float *out, unsigned n)
{
    float inv_fs = 1.0f / (float)DV_FS;
    for (unsigned i = 0; i < n; i++) {
        int gate = v.gate_left > 0.0f;
        if (gate) v.gate_left -= 1.0f;

        v.lfo += k.lfo_inc;
        if (v.lfo >= 1.0f) v.lfo -= 1.0f;
        float lfo = v.lfo < 0.5f ? 4.0f * v.lfo - 1.0f : 3.0f - 4.0f * v.lfo;

        /* pitch in semitones above C2, with glide and vibrato (up to +-2 semitones) */
        float target = v.note + (float)values[TUNE] + (float)values[FINE] * 0.01f
                     + lfo * (float)values[LFO_PITCH] * 0.02f;
        v.pitch += (target - v.pitch) * ((values[GLIDE_MODE] == 1 || v.glide) ? k.k_glide : 1.0f);
        float inc = 65.406f * dv_exp2(v.pitch * (1.0f / 12.0f)) * inv_fs;
        if (inc > 0.45f) inc = 0.45f;

        v.phase += inc;
        if (v.phase >= 1.0f) { v.phase -= 1.0f; v.wraps++; }
        float pw = dv_clampf((float)values[PW] * 0.01f + lfo * (float)values[LFO_PW] * 0.0045f, 0.05f, 0.95f);
        float p2 = v.phase + pw;
        if (p2 >= 1.0f) p2 -= 1.0f;
        float saw = saw_blep(v.phase, inc);
        float pulse = (saw - saw_blep(p2, inc)) * 0.5f;
        float sub = values[SUB_MODE] == 0 ? ((v.wraps & 1u) ? 1.0f : -1.0f)
                  : values[SUB_MODE] == 1 ? ((v.wraps & 2u) ? 1.0f : -1.0f)
                  : ((v.wraps & 3u) == 0u ? 1.0f : -1.0f);
        float mix = dv_softclip(saw * k.level[0] + pulse * k.level[1] + sub * k.level[2]
                              + dv_rng_bipolar(&v.noise) * k.level[3]);

        /* envelope */
        if (gate && !v.gate) v.env_stage = E_ATTACK;
        if (!gate && v.gate) v.env_stage = E_RELEASE;
        v.gate = (uint8_t)gate;
        switch (v.env_stage) {
        case E_ATTACK:
            v.env += (1.2f - v.env) * k.k_attack;
            if (v.env >= 1.0f) { v.env = 1.0f; v.env_stage = E_DECAY; }
            break;
        case E_DECAY:
        case E_SUSTAIN:
            v.env += (k.sustain - v.env) * k.k_decay;
            break;
        case E_RELEASE:
            v.env += (-0.008f - v.env) * k.k_release;
            if (v.env <= 0.0f) { v.env = 0.0f; v.env_stage = E_IDLE; }
            break;
        default:
            v.env = 0.0f;
            break;
        }

        /* cutoff in octaves above 20 Hz: knob, envelope, key track, LFO */
        float env_amount = (float)values[ENV_AMT];
        if (v.accent) env_amount += env_amount * 0.5f + 8.0f;
        float octaves = (float)values[CUTOFF] * (96.0f / 1536.0f) + v.env * env_amount * (144.0f / 1536.0f)
                      + v.pitch * (1.0f / 12.0f) * (float)values[KEY_TRACK] * 0.5f
                      + lfo * (float)values[LFO_CUTOFF] * (46.0f / 1536.0f);
        float hz = 20.0f * dv_exp2(dv_clampf(octaves, 0.0f, 9.3f));       /* up to about 12.6 kHz */
        float f = 3.14159265f * hz * inv_fs;                              /* run twice per sample */
        for (int pass = 0; pass < 2; pass++) {
            v.low += f * v.band;
            v.band += f * (mix - v.low - k.damping * v.band);
        }
        v.band = dv_clampf(v.band, -4.0f, 4.0f);

        /* VCA: envelope or declicked gate; accent adds 25 % */
        v.gate_slew += ((gate ? 1.0f : 0.0f) - v.gate_slew) * 0.051f;     /* about 2 ms */
        float x = v.low * (values[VCA_MODE] == 0 ? v.env : v.gate_slew);
        if (v.accent) x *= 1.25f;
        out[i] = dv_softclip(x + x);    /* make-up gain into the clipper, like a driven 101 */
    }
}

/* the stages along the pattern: width = length, height = pitch; hollow = rest,
 * split = multi, with a bar on top for accents and a ramp for slides */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    int count = values[STAGES], total = 0, at = 0;
    for (int i = 0; i < count; i++) total += stages[i].length;
    for (int i = 0; i < count; i++) {
        const stage_t *s = &stages[i];
        int x0 = x + at * w / total, x1 = x + (at + s->length) * w / total - 3;
        int now = s16 < 16u && i == stage_now && v.started;
        int top = y + 6 + (19 - s->pitch) * (h - 16) / 31, bh = y + h - top;
        uint16_t c = now ? DV_C_TEXT : (s->flags & F_ACCENT) ? DV_C_LIME : DV_C_DEEP;
        at += s->length;
        if (s->mode == REST) {
            dv_rframe(fb, x0, y + h - 8, x1 - x0, 8, 3, now ? DV_C_TEXT : DV_C_LINE, DV_C_PANEL);
            continue;
        }
        if (s->mode == MULTI) {
            for (int pulse = 0; pulse < s->length; pulse++) {
                int a = x0 + pulse * (x1 - x0 + 3) / s->length;
                dv_rrect(fb, a, top, (x1 - x0 + 3) / s->length - 3, bh, 2, c);
            }
        } else {
            int wide = s->mode == HOLD ? x1 - x0 : (x1 - x0) / s->length;
            dv_rrect(fb, x0, top, wide < 6 ? 6 : wide, bh, 3, c);
            if (s->mode != HOLD && s->length > 1)
                dv_rrect(fb, x0 + wide + 2, y + h - 3, x1 - x0 - wide - 2, 3, 1, DV_C_LINE);
        }
        if (s->flags & F_SLIDE) dv_rrect(fb, x0, top - 5, x1 - x0, 2, 1, DV_C_DIM);
    }
}

const dv_machine_t DV_MINI_ATLANTIS = {
    "MINI ATLANTIS", NPARAM, 9, PARAMS, PAGES, LAYOUT, values, init, update, reset, step, render, action, key, visual,
};
