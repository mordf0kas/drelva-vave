/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Mini Atlantis: an SH-101 style mono voice (VCO with saw, pulse, sub and noise, 12 dB
 * low-pass, ADSR, VCA, LFO) played by an eight-stage sequencer in the manner of the ML-185.
 *
 * Every stage has its own PITCH, a number of PULSES (1 to 8 clock pulses it lasts), a GATE
 * mode (REST: silent; SINGLE: one note on the first pulse; MULTI: a note on every pulse;
 * HOLD: one note held through all of them), and three switches: SLIDE (the note glides in,
 * legato), ACCENT and SKIP (the stage is passed over). The pattern is as long as the pulses
 * of its stages add up to, so the rhythm comes from pulse counts and gate modes, not from a
 * grid. LAST sets how many stages play, MODE the order (forward, reverse, pendulum, random,
 * drunk), DIV the clock division and GATE the length of the notes.
 *
 * On the STAGE pages the first eight white keys pick the stage to edit; on any other page the
 * keys transpose. SEQ rolls a whole new pattern from the GEN page settings, which can then be
 * edited stage by stage. The stages are parameters, so presets keep them.
 *
 * The voice is ported from Drelvarom (same parameters and ranges, float at 44.1 kHz). */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

#define NSTAGE 8
#define NSCALE 6
enum {
    TUNE, FINE, PW, SUB_MODE, SAW_LVL, PULSE_LVL, SUB_LVL, NOISE_LVL, CUTOFF, RES, ENV_AMT, KEY_TRACK,
    ATTACK, DECAY, SUSTAIN, RELEASE, GLIDE, GLIDE_MODE, VCA_MODE, LFO_RATE, LFO_PW, LFO_CUTOFF, LFO_PITCH,
    LAST, MODE, DIV, GATE_LEN, SCALE, OCTS, CH_SLIDE, CH_ACCENT, SEED, STAGE,
    S_PITCH, S_PULSES = S_PITCH + NSTAGE, S_GATE = S_PULSES + NSTAGE, S_SLIDE = S_GATE + NSTAGE,
    S_ACCENT = S_SLIDE + NSTAGE, S_SKIP = S_ACCENT + NSTAGE, NPARAM = S_SKIP + NSTAGE
};
enum { REST, SINGLE, MULTI, HOLD };
enum { FWD, REV, PEND, RAND, DRUNK };
enum { PAGE_PLAY, PAGE_STAGE, PAGE_STAGE2, PAGE_SEQ, PAGE_GEN, PAGE_VCO, PAGE_MIXER, PAGE_ENV, PAGE_MOD, PAGE_LFO, NPAGE };

static const char *const SUBS[3] = {"-1 OCT", "-2 OCT", "-2 PLS"};
static const char *const TRACKING[3] = {"0%", "50%", "100%"};
static const char *const GLIDES[2] = {"SLIDE", "ALWAYS"};
static const char *const VCAS[2] = {"ENV", "GATE"};
static const char *const SCALES[NSCALE] = {"MINOR", "PHRYG", "DORIAN", "PENTA", "ACID", "5THS"};
static const char *const MODES[5] = {"FWD", "REV", "PEND", "RANDOM", "DRUNK"};
static const char *const DIVS[3] = {"1/16", "1/8", "1/4"};
static const char *const GATES[4] = {"REST", "SINGLE", "MULTI", "HOLD"};
static const char *const ON_OFF[2] = {"OFF", "ON"};
#define EIGHT(...) {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}
static const dv_param_t PARAMS[NPARAM] = {
    {"TUNE", -24, 24, 7, 0},   {"FINE", -50, 50, 0, 0},   {"PW", 5, 95, 50, 0},      {"SUB", 0, 2, 0, SUBS},
    {"SAW", 0, 100, 100, 0},   {"PULSE", 0, 100, 30, 0},  {"SUB", 0, 100, 50, 0},    {"NOISE", 0, 100, 0, 0},
    {"CUTOFF", 0, 127, 48, 0}, {"RESO", 0, 127, 78, 0},   {"ENV", -64, 63, 34, 0},   {"KEYTRK", 0, 2, 1, TRACKING},
    {"ATTACK", 0, 127, 0, 0},  {"DECAY", 0, 127, 62, 0},  {"SUS", 0, 100, 0, 0},     {"REL", 0, 127, 50, 0},
    {"GLIDE", 0, 127, 60, 0},  {"GL ON", 0, 1, 0, GLIDES}, {"VCA", 0, 1, 0, VCAS},   {"LFO", 0, 127, 76, 0},
    {"LFO PW", 0, 100, 0, 0},  {"LFO CUT", 0, 100, 0, 0}, {"LFO PIT", 0, 100, 0, 0},
    {"LAST", 1, 8, 5, 0},      {"MODE", 0, 4, 0, MODES},  {"DIV", 0, 2, 0, DIVS},    {"GATE", 10, 95, 50, 0},
    {"SCALE", 0, NSCALE - 1, 1, SCALES}, {"OCTS", 1, 2, 1, 0}, {"SLIDE %", 0, 100, 12, 0}, {"ACCENT%", 0, 100, 40, 0},
    {"SEED", 0, 127, 5, 0},    {"STAGE", 1, 8, 1, 0},
    EIGHT("PITCH", -12, 24, 0, 0),
    EIGHT("PULSES", 1, 8, 1, 0),
    EIGHT("GATE", 0, 3, 1, GATES),
    EIGHT("SLIDE", 0, 1, 0, ON_OFF),
    EIGHT("ACCENT", 0, 1, 0, ON_OFF),
    EIGHT("SKIP", 0, 1, 0, ON_OFF),
};
/* the two STAGE rows follow the stage being edited (update) */
static int8_t LAYOUT[NPAGE][4] = {
    {CUTOFF, RES, ENV_AMT, DECAY},
    {STAGE, S_PITCH, S_PULSES, S_GATE},
    {STAGE, S_SLIDE, S_ACCENT, S_SKIP},
    {LAST, MODE, DIV, GATE_LEN},
    {SCALE, OCTS, CH_SLIDE, CH_ACCENT},
    {TUNE, FINE, PW, SUB_MODE},
    {SAW_LVL, PULSE_LVL, SUB_LVL, NOISE_LVL},
    {ATTACK, DECAY, SUSTAIN, RELEASE},
    {GLIDE, GLIDE_MODE, VCA_MODE, LFO_RATE},
    {LFO_PW, LFO_CUTOFF, LFO_PITCH, KEY_TRACK},
};
static const char *const PAGES[NPAGE] = {"PLAY", "STAGE", "STAGE 2", "SEQ", "GEN", "VCO", "MIXER", "ENV", "MOD", "LFO"};

static int16_t values[NPARAM];
static volatile uint8_t stage_now, pulse_now, running;
static volatile int8_t transpose;
static int8_t direction = 1;        /* pendulum */
static uint8_t sixteenths;          /* for DIV */
static dv_rng_t order_rng = {0x1F123BB5u};

/* coefficients from the parameters */
static struct {
    float level[4], k_attack, k_decay, k_release, sustain, k_glide, lfo_inc, damping;
} k;
/* voice state, audio only */
static struct {
    float phase, lfo, pitch, env, gate_slew, low, band;
    uint32_t wraps;
    uint8_t env_stage, gate, accent, glide;
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

/* A whole new pattern from the seed in the chosen scale: root weighted, some rests, multis
 * and holds, slides and accents by the chances on the GEN page. It writes the stage
 * parameters, so the result can be edited and is kept by presets. */
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
        uint32_t r = dv_rng_u32(&rng);
        int pitch = scale[(r & 0xFFu) % 8u] + 12 * (int)(((r >> 8) & 0xFFu) % (uint32_t)values[OCTS]);
        int lr = (int)((r >> 16) & 0x7Fu) * 100 / 128;      /* mostly 1-2 pulses, some longer */
        int mr = (int)(dv_rng_u32(&rng) % 100u);
        values[S_PITCH + i] = (int16_t)(pitch < -12 ? -12 : pitch > 24 ? 24 : pitch);
        values[S_PULSES + i] = (int16_t)(lr < long_bias / 3 ? 3 + (int)((r >> 23) & 3u) : lr < long_bias ? 2 : 1);
        values[S_GATE + i] = (int16_t)(mr < rest ? REST : mr < rest + multi ? MULTI : mr < rest + multi + hold ? HOLD : SINGLE);
        values[S_ACCENT + i] = (int)(dv_rng_u32(&rng) % 100u) < values[CH_ACCENT];
        values[S_SLIDE + i] = (int)(dv_rng_u32(&rng) % 100u) < values[CH_SLIDE];
        values[S_SKIP + i] = 0;
    }
    values[S_PITCH] = 0;                                     /* starts on the root and sounds */
    if (values[S_GATE] == REST) values[S_GATE] = SINGLE;
}

static void update(void)
{
    int stage = values[STAGE] - 1;
    LAYOUT[PAGE_STAGE][1] = (int8_t)(S_PITCH + stage);
    LAYOUT[PAGE_STAGE][2] = (int8_t)(S_PULSES + stage);
    LAYOUT[PAGE_STAGE][3] = (int8_t)(S_GATE + stage);
    LAYOUT[PAGE_STAGE2][1] = (int8_t)(S_SLIDE + stage);
    LAYOUT[PAGE_STAGE2][2] = (int8_t)(S_ACCENT + stage);
    LAYOUT[PAGE_STAGE2][3] = (int8_t)(S_SKIP + stage);
    for (int i = 0; i < 4; i++) k.level[i] = (float)values[SAW_LVL + i] * 0.005f;   /* half scale */
    k.k_attack = approach_ms(time_from_index(values[ATTACK], 1.0f, 4000.0f), 1.8f);
    k.k_decay = approach_ms(time_from_index(values[DECAY], 1.0f, 4000.0f), 4.6f);
    k.k_release = approach_ms(time_from_index(values[RELEASE], 1.0f, 4000.0f), 4.6f);
    k.sustain = (float)values[SUSTAIN] * 0.01f;
    k.k_glide = approach_ms(time_from_index(values[GLIDE], 1.0f, 2000.0f), 4.6f);
    k.lfo_inc = time_from_index(values[LFO_RATE], 0.05f, 30.0f) / (float)DV_FS;
    k.damping = 1.2f - (float)values[RES] * (1.17f / 127.0f);
}

static void reset(void)
{
    running = 0;                    /* the next pulse lands on the first stage */
    sixteenths = 0;
    direction = 1;
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0x51ED270Bu;
    transpose = 0;
    generate();
    update();
    reset();
}

static void action(void)
{
    values[SEED] = (int16_t)((values[SEED] + 1) & 127);
    generate();
}

/* on the STAGE pages the first eight white keys pick the stage; elsewhere the keys transpose
 * (the first C plays the pattern as written) */
static void key(int note)
{
    static const int8_t white[NSTAGE] = {0, 2, 4, 6, 7, 9, 11, 12};
    int page = dv_machine_page(&DV_MINI_ATLANTIS);
    if (note < 0) return;
    if (page == PAGE_STAGE || page == PAGE_STAGE2) {
        for (int i = 0; i < NSTAGE; i++)
            if (white[i] == note) {
                values[STAGE] = (int16_t)(i + 1);
                update();
            }
    } else {
        transpose = (int8_t)(note - 7);
    }
}

/* the stage after `from` in the playing order, passing over stages set to SKIP; -1 if all are */
static int next_stage(int from)
{
    int last = values[LAST];
    for (int tries = 0; tries < 4 * NSTAGE; tries++) {
        switch (values[MODE]) {
        case REV: from = from <= 0 ? last - 1 : from - 1; break;
        case PEND:
            if (last > 1 && (from + direction < 0 || from + direction >= last)) direction = (int8_t)-direction;
            from = last > 1 ? from + direction : 0;
            break;
        case RAND: from = (int)(dv_rng_u32(&order_rng) % (uint32_t)last); break;
        case DRUNK: {
            uint32_t r = dv_rng_u32(&order_rng) & 3u;        /* forward twice as often as back or stay */
            from = (from + (r < 2u ? 1 : r == 2u ? 0 : last - 1)) % last;
            break;
        }
        default: from = from + 1 >= last ? 0 : from + 1; break;
        }
        if (from < 0 || from >= last) from = 0;
        if (!values[S_SKIP + from]) return from;
    }
    return -1;
}

/* where the pattern starts: stage 1, or the last one in reverse; the next playable one if skipped */
static int first_stage(void)
{
    int first = values[MODE] == REV ? values[LAST] - 1 : 0;
    return values[S_SKIP + first] ? next_stage(first) : first;
}

static void step(unsigned s16, float step_len)
{
    (void)s16;
    int div = 1 << values[DIV];
    if (sixteenths++ % div) return;
    float pulse_len = step_len * (float)div;
    int stage = stage_now, pulse = pulse_now + 1;
    if (!running) {
        running = 1;
        stage = first_stage();
        pulse = 0;
    } else if (stage >= values[LAST] || pulse >= values[S_PULSES + stage]) {
        stage = next_stage(stage);
        pulse = 0;
    }
    if (stage < 0) {                                         /* every stage skipped */
        running = 0;
        v.gate_left = 0.0f;
        return;
    }
    stage_now = (uint8_t)stage;
    pulse_now = (uint8_t)pulse;

    int mode = values[S_GATE + stage], pulses = values[S_PULSES + stage];
    float gate = (float)values[GATE_LEN] * 0.01f;
    if (mode == REST) {
        v.gate_left = 0.0f;
    } else if (mode == MULTI || pulse == 0) {
        v.note = (float)(values[S_PITCH + stage] + transpose);
        v.accent = values[S_ACCENT + stage] != 0;
        v.glide = values[S_SLIDE + stage] != 0;
        if (!v.glide) v.gate = 0;                            /* a slid note does not retrigger: legato */
        v.gate_left = pulse_len * (mode == HOLD ? (float)(pulses - 1) + gate : gate);
    }
}

static void render(float *out, unsigned n)
{
    float inv_fs = 1.0f / (float)DV_FS;
    if (!dv_is_playing()) v.gate_left = 0.0f;
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
        float saw = dv_saw_blep(v.phase, inc);
        float pulse = (saw - dv_saw_blep(p2, inc)) * 0.5f;
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

/* the eight stages as on the hardware: a pitch slider and a column of eight pulse lamps each.
 * Lamps: lit for the stage's pulses (bright where a note starts, joined for HOLD, hollow for
 * REST). A line under a stage: SLIDE. A white slider cap: ACCENT. Greyed: SKIP or beyond
 * LAST. Framed: the stage being edited. White lamp: playing now. */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    int col = w / NSTAGE, page = dv_machine_page(&DV_MINI_ATLANTIS);
    int editing = page == PAGE_STAGE || page == PAGE_STAGE2, lamp = (h - 8) / 8;
    for (int i = 0; i < NSTAGE; i++) {
        int cx = x + i * col + 1, live = i < values[LAST] && !values[S_SKIP + i];
        int now = s16 < 16u && running && i == stage_now;
        int mode = values[S_GATE + i], pulses = values[S_PULSES + i];
        if (editing && i == values[STAGE] - 1) dv_rframe(fb, cx - 3, y - 4, col - 1, h + 7, 4, DV_C_LIME, DV_C_PANEL);

        /* pitch slider: -12 at the bottom, +24 at the top */
        int travel = h - 14;
        int cap = y + travel - (values[S_PITCH + i] + 12) * travel / 36;
        dv_rrect(fb, cx + 3, y, 3, travel + 6, 1, DV_C_LINE);
        dv_rrect(fb, cx, cap, 9, 6, 2, !live ? DV_C_FAINT : now || values[S_ACCENT + i] ? DV_C_TEXT : DV_C_LIME);

        /* pulse lamps, the first at the bottom */
        for (int p = 0; p < 8; p++) {
            int ly = y + h - 6 - (p + 1) * lamp, used = p < pulses;
            int sounding = mode == MULTI || mode == HOLD || (mode == SINGLE && p == 0);
            int tall = lamp - 2 + (mode == HOLD && p + 1 < pulses ? 2 : 0);    /* HOLD joins its lamps */
            uint16_t c = !live ? DV_C_LINE : now && p == pulse_now ? DV_C_TEXT : !sounding ? DV_C_LINE
                       : (mode == MULTI || p == 0) ? DV_C_LIME : DV_C_DEEP;
            if (!used) dv_fill(fb, cx + 15, ly + (lamp - 2) / 2, 3, 1, DV_C_LINE);
            else if (mode == REST) dv_rframe(fb, cx + 12, ly, 9, lamp - 2, 1, DV_C_LINE, DV_C_PANEL);
            else dv_rrect(fb, cx + 12, ly - (tall - (lamp - 2)), 9, tall, 1, c);
        }
        if (values[S_SLIDE + i]) dv_rrect(fb, cx, y + h - 3, 21, 2, 1, live ? DV_C_TEXT : DV_C_LINE);
    }
}

const dv_machine_t DV_MINI_ATLANTIS = {
    "MINI ATLANTIS", "ATLANT", 1, NPARAM, NPAGE, PARAMS, PAGES, LAYOUT, values, init, update, reset, step, render,
    action, key, visual,
};
