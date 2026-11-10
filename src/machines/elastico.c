/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Elastico: the elastic voice of Parkoid without its click, for hypnotic lines.
 *
 * The voice is a two-operator FM pair (a sine with a little feedback phase-modulating a sine)
 * into a wavefolder and a resonant low-pass. What makes it elastic is a spring: every note
 * plucks it, and the spring's swing bends the pitch (STRETCH), and with FLEX also the FM
 * amount and the filter, so the tone stretches and snaps back with the pitch. SPEED is how
 * fast the spring swings, DAMP how soon it comes to rest.
 *
 * Why it does not click, where Parkoid does: Parkoid starts each note with the pitch already
 * thrown to the top of its sweep, an instant jump. Here the spring starts at rest and is given
 * a push, so the pitch leaves the note smoothly and swings out and back. The amplitude also
 * rises over SOFT milliseconds from wherever it was, the oscillators are never restarted, and
 * a change of note always takes a few milliseconds.
 *
 * DRIFT moves the FM amount and the filter very slowly, over RATE bars, so a short figure
 * keeps changing colour: the hypnotic part.
 *
 * It is played by an eight-stage sequencer in the manner of the ML-185. Every stage has its
 * own PITCH, a number of PULSES (1 to 8), a GATE mode (REST, SINGLE: one note on the first
 * pulse, MULTI: a note on every pulse, HOLD: one note held through them) and three switches:
 * SLIDE (the note glides in without a new pluck of the envelope), ACCENT (a harder pluck) and
 * SKIP. LAST sets how many stages play, MODE the order, DIV the clock division, GATE the note
 * length. On the STAGE pages the first eight white keys pick the stage to edit; elsewhere the
 * keys transpose. SEQ rolls a new pattern from the GEN page; the stages are parameters, so
 * they can be edited afterwards and presets keep them. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

#define NSTAGE 8
#define NSCALE 5
#define NRATIO 8
enum {
    STRETCH, TENSION, DAMP, FLEX, RATIO, INDEX, FOLD, CUTOFF, SOFT, DECAY, RES, GLIDE, DRIFT, RATE, TUNE,
    LAST, MODE, DIV, GATE_LEN, SCALE, OCTS, CH_SLIDE, CH_ACCENT, SEED, STAGE,
    S_PITCH, S_PULSES = S_PITCH + NSTAGE, S_GATE = S_PULSES + NSTAGE, S_SLIDE = S_GATE + NSTAGE,
    S_ACCENT = S_SLIDE + NSTAGE, S_SKIP = S_ACCENT + NSTAGE, NPARAM = S_SKIP + NSTAGE
};
enum { REST, SINGLE, MULTI, HOLD };
enum { FWD, REV, PEND, RAND, DRUNK };
enum { PAGE_PLAY, PAGE_TONE, PAGE_SHAPE, PAGE_MOVE, PAGE_STAGE, PAGE_STAGE2, PAGE_SEQ, PAGE_GEN, NPAGE };

static const char *const RATIOS[NRATIO] = {"0.5", "1", "1.5", "2", "3", "3.5", "5", "7"};
static const float RATIO_OF[NRATIO] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 5.0f, 7.0f};
static const char *const RATES[4] = {"4 BAR", "8 BAR", "16 BAR", "32 BAR"};
static const char *const SCALES[NSCALE] = {"PARKER", "MINOR", "PHRYG", "PENTA", "8VES"};
static const char *const MODES[5] = {"FWD", "REV", "PEND", "RANDOM", "DRUNK"};
static const char *const DIVS[3] = {"1/16", "1/8", "1/4"};
static const char *const GATES[4] = {"REST", "SINGLE", "MULTI", "HOLD"};
static const char *const ON_OFF[2] = {"OFF", "ON"};
#define EIGHT(...) {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}, {__VA_ARGS__}
static const dv_param_t PARAMS[NPARAM] = {
    {"STRETCH", -24, 24, 9, 0}, {"SPEED", 0, 100, 45, 0}, {"DAMP", 0, 100, 40, 0},  {"FLEX", 0, 100, 55, 0},
    {"RATIO", 0, NRATIO - 1, 3, RATIOS}, {"INDEX", 0, 100, 38, 0}, {"FOLD", 0, 100, 0, 0}, {"CUTOFF", 0, 100, 52, 0},
    {"SOFT", 0, 100, 30, 0},    {"DECAY", 0, 100, 42, 0},   {"RESO", 0, 100, 35, 0},  {"GLIDE", 0, 100, 25, 0},
    {"DRIFT", 0, 100, 45, 0},   {"RATE", 0, 3, 1, RATES},   {"TUNE", -24, 24, 7, 0},
    {"LAST", 1, 8, 6, 0},       {"MODE", 0, 4, 0, MODES},   {"DIV", 0, 2, 0, DIVS},   {"GATE", 10, 95, 60, 0},
    {"SCALE", 0, NSCALE - 1, 0, SCALES}, {"OCTS", 1, 2, 1, 0}, {"SLIDE %", 0, 100, 25, 0}, {"ACCENT%", 0, 100, 30, 0},
    {"SEED", 0, 127, 3, 0},     {"STAGE", 1, 8, 1, 0},
    EIGHT("PITCH", -12, 24, 0, 0),
    EIGHT("PULSES", 1, 8, 1, 0),
    EIGHT("GATE", 0, 3, 1, GATES),
    EIGHT("SLIDE", 0, 1, 0, ON_OFF),
    EIGHT("ACCENT", 0, 1, 0, ON_OFF),
    EIGHT("SKIP", 0, 1, 0, ON_OFF),
};
/* the two STAGE rows follow the stage being edited (update) */
static int8_t LAYOUT[NPAGE][4] = {
    {STRETCH, TENSION, DAMP, FLEX},
    {RATIO, INDEX, FOLD, CUTOFF},
    {SOFT, DECAY, RES, GLIDE},
    {DRIFT, RATE, TUNE, -1},
    {STAGE, S_PITCH, S_PULSES, S_GATE},
    {STAGE, S_SLIDE, S_ACCENT, S_SKIP},
    {LAST, MODE, DIV, GATE_LEN},
    {SCALE, OCTS, CH_SLIDE, CH_ACCENT},
};
static const char *const PAGES[NPAGE] = {"ELASTIC", "TONE", "SHAPE", "MOVE", "STAGE", "STAGE 2", "SEQ", "GEN"};

static int16_t values[NPARAM];
static volatile uint8_t stage_now, pulse_now, running;
static volatile int8_t transpose;
static int8_t direction = 1;        /* pendulum */
static uint8_t sixteenths;          /* for DIV */
static dv_rng_t order_rng = {0x1F123BB5u};

/* coefficients from the parameters */
static struct {
    float stretch, omega, friction, flex, ratio, index, fold, fold_gain, cutoff_oct, res;
    float attack, decay, glide, drift, drift_steps;
} k;
/* voice state, audio only */
static struct {
    float spring, speed;            /* the spring: where it is (about -1..1) and its speed in the same units */
    float carrier, modulator, last_mod, pitch, note, env, drift_phase, drift_inc;
    float gate_left;                /* samples the gate stays high */
    uint8_t rising, gate;
    dv_svf2_t filter;
} v;
static volatile float shown_spring;

/* A whole new pattern from the seed in the chosen scale. PARKER is mostly the root with the
 * odd octave, fifth, flat seventh and flat third, which is what makes a figure hypnotic
 * rather than a tune. It writes the stage parameters, so the result can be edited and is
 * kept by presets. */
static void generate(void)
{
    static const int8_t scales[NSCALE][8] = {
        {0, 0, 0, 0, 12, 7, 10, 3},    /* Parker */
        {0, 0, 2, 3, 5, 7, 8, 10},     /* minor */
        {0, 0, 1, 3, 5, 7, 8, 10},     /* phrygian */
        {0, 0, 3, 5, 7, 10, 12, 15},   /* minor pentatonic */
        {0, 0, 0, 12, 0, 12, 0, 24},   /* octaves */
    };
    const int long_bias = 45, rest = 10, multi = 12, hold = 8;
    dv_rng_t rng = {0x9E3779B9u ^ ((uint32_t)values[SEED] * 2654435761u)};
    const int8_t *scale = scales[values[SCALE]];
    for (int i = 0; i < NSTAGE; i++) {
        uint32_t r = dv_rng_u32(&rng);
        int pitch = scale[(r & 0xFFu) % 8u];
        if (pitch) pitch += 12 * (int)(((r >> 8) & 0xFFu) % (uint32_t)values[OCTS]);   /* the root stays put */
        int lr = (int)((r >> 16) & 0x7Fu) * 100 / 128;      /* mostly 1-2 pulses, some longer */
        int mr = (int)(dv_rng_u32(&rng) % 100u);
        values[S_PITCH + i] = (int16_t)(pitch > 24 ? 24 : pitch);
        values[S_PULSES + i] = (int16_t)(lr < long_bias / 3 ? 3 + (int)((r >> 23) & 1u) : lr < long_bias ? 2 : 1);
        values[S_GATE + i] = (int16_t)(mr < rest ? REST : mr < rest + multi ? MULTI : mr < rest + multi + hold ? HOLD : SINGLE);
        values[S_ACCENT + i] = (int)(dv_rng_u32(&rng) % 100u) < values[CH_ACCENT];
        values[S_SLIDE + i] = (int)(dv_rng_u32(&rng) % 100u) < values[CH_SLIDE];
        values[S_SKIP + i] = 0;
    }
    values[S_PITCH] = 0;                                     /* starts on the root, accented, and sounds */
    values[S_ACCENT] = 1;
    values[S_SLIDE] = 0;
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

    float hz = 1.5f * dv_exp2((float)values[TENSION] * 0.04f);        /* the spring swings at 1.5 .. 24 Hz */
    float zeta = 0.04f + (float)values[DAMP] * (float)values[DAMP] * 0.00007f;   /* rings long .. settles at once */
    k.stretch = (float)values[STRETCH];
    k.omega = 6.2831853f * hz / (float)DV_FS;                         /* radians per sample */
    k.friction = 2.0f * zeta * k.omega;
    k.flex = (float)values[FLEX] * 0.01f;
    k.ratio = RATIO_OF[values[RATIO]];
    k.index = (float)values[INDEX] * 0.04f / 6.2831853f;              /* up to 4 radians, in turns */
    k.fold = (float)values[FOLD] * 0.01f;
    k.fold_gain = 0.159f * (1.0f + (float)values[FOLD] * 0.04f);      /* sin(a * gain), gain 1..5, in turns */
    k.cutoff_oct = (float)values[CUTOFF] * 0.05f;                     /* 160 Hz .. 5 kHz */
    k.res = (float)values[RES] * 1.15f;
    k.attack = 1.0f - dv_decay_ms(0.4f + (float)values[SOFT] * (float)values[SOFT] * 0.006f);   /* 0.4 .. 60 ms */
    k.decay = dv_settle_ms(40.0f * dv_exp2((float)values[DECAY] * 0.056f));                     /* 40 ms .. 1.9 s */
    k.glide = 1.0f - dv_decay_ms(1.5f + (float)values[GLIDE] * (float)values[GLIDE] * 0.03f);   /* never instant */
    k.drift = (float)values[DRIFT] * 0.01f;
    k.drift_steps = 64.0f * (float)(1 << values[RATE]);               /* 16 steps to the bar */
}

static void reset(void)
{
    running = 0;                    /* the next pulse lands on the first stage */
    sixteenths = 0;
    direction = 1;
    v.drift_phase = 0.0f;
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
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
    int page = dv_machine_page(&DV_ELASTICO);
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
    v.drift_inc = 1.0f / (k.drift_steps * step_len);
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
        int slide = values[S_SLIDE + stage] != 0;
        v.note = (float)(values[S_PITCH + stage] + transpose);
        /* pluck the spring: a push, not a jump. A slid note gives it only a nudge and keeps
         * the envelope where it is; an accent pushes harder. */
        v.speed += slide ? 0.35f : values[S_ACCENT + stage] ? 1.0f : 0.62f;
        if (!slide) v.rising = 1;
        v.gate_left = pulse_len * (mode == HOLD ? (float)(pulses - 1) + gate : gate);
    }
}

static void render(float *out, unsigned n)
{
    float inv_fs = 1.0f / (float)DV_FS;
    if (!dv_is_playing()) v.gate_left = 0.0f;
    /* DRIFT: a very slow triangle on the FM amount and the filter */
    v.drift_phase += v.drift_inc * (float)n;
    if (v.drift_phase >= 1.0f) v.drift_phase -= 1.0f;
    float drift = (v.drift_phase < 0.5f ? 4.0f * v.drift_phase - 1.0f : 3.0f - 4.0f * v.drift_phase) * k.drift;
    for (unsigned i = 0; i < n; i++) {
        int gate = v.gate_left > 0.0f;
        if (gate) v.gate_left -= 1.0f;

        /* the spring */
        v.speed -= k.omega * v.spring + k.friction * v.speed;
        v.spring += k.omega * v.speed;
        float bend = v.spring, reach = bend < 0.0f ? -bend : bend;

        /* pitch in semitones above C3: the note (always gliding a little) and the spring */
        v.pitch += (v.note + (float)values[TUNE] - v.pitch) * k.glide;
        float hz = 130.81f * dv_exp2((v.pitch + bend * k.stretch) * (1.0f / 12.0f));
        if (hz > 9000.0f) hz = 9000.0f;

        /* amplitude: rises over SOFT from wherever it is, holds while the gate is up, then falls */
        if (v.rising) {
            v.env += (1.05f - v.env) * k.attack;
            if (v.env >= 1.0f) { v.env = 1.0f; v.rising = 0; }
        } else {
            v.env = gate ? 0.7f + (v.env - 0.7f) * k.decay : v.env * k.decay;
        }

        /* FM pair: the modulator, with a little feedback, bends the carrier's phase. The amount
         * follows the envelope a little, the spring (FLEX) and the drift. */
        v.carrier += hz * inv_fs;
        v.modulator += hz * k.ratio * inv_fs;
        if (v.carrier >= 1.0f) v.carrier -= 1.0f;
        if (v.modulator >= 1.0f) v.modulator -= 1.0f;
        float mod = dv_sin(v.modulator + v.last_mod * 0.04f + 1.0f);
        v.last_mod = mod;
        float index = k.index * (0.55f + 0.25f * v.env + reach * k.flex * 1.2f + drift * 0.45f);
        if (index < 0.0f) index = 0.0f;
        float x = dv_sin(v.carrier + mod * index + 2.0f);

        /* wavefolder: sin(x * gain), mixed in by FOLD */
        if (k.fold > 0.0f) x += (dv_sin(x * k.fold_gain + 2.0f) - x) * k.fold;

        /* low-pass: knob, the spring and the drift; it closes a little as the note dies */
        float octaves = k.cutoff_oct + reach * k.flex * 2.2f + drift * 1.2f + (v.env - 0.5f) * 0.8f;
        dv_svf2_set(&v.filter, 160.0f * dv_exp2(dv_clampf(octaves, 0.0f, 6.0f)), k.res);
        out[i] = dv_softclip(dv_svf2_lp(&v.filter, x) * v.env * 1.2f);
    }
    shown_spring = v.spring;
}

/* the eight stages as on the hardware: a pitch slider and a column of eight pulse lamps each.
 * Lamps: lit for the stage's pulses (bright where a note starts, joined for HOLD, hollow for
 * REST). A line under a stage: SLIDE. A white slider cap: ACCENT. Greyed: SKIP or beyond
 * LAST. Framed: the stage being edited. White lamp: playing now. The cap of the playing
 * stage rides the spring, so you see the note stretch. */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    int col = w / NSTAGE, page = dv_machine_page(&DV_ELASTICO);
    int editing = page == PAGE_STAGE || page == PAGE_STAGE2, lamp = (h - 8) / 8;
    for (int i = 0; i < NSTAGE; i++) {
        int cx = x + i * col + 1, live = i < values[LAST] && !values[S_SKIP + i];
        int now = s16 < 16u && running && i == stage_now;
        int mode = values[S_GATE + i], pulses = values[S_PULSES + i];
        if (editing && i == values[STAGE] - 1) dv_rframe(fb, cx - 3, y - 4, col - 1, h + 7, 4, DV_C_LIME, DV_C_PANEL);

        /* pitch slider: -12 at the bottom, +24 at the top */
        int travel = h - 14;
        int cap = y + travel - (values[S_PITCH + i] + 12) * travel / 36;
        if (now) cap -= (int)(dv_clampf(shown_spring * k.stretch, -24.0f, 24.0f) * (float)travel / 36.0f);
        if (cap < y - 2) cap = y - 2;
        if (cap > y + travel + 2) cap = y + travel + 2;
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

const dv_machine_t DV_ELASTICO = {
    "ELASTICO", "ELAST", 8, NPARAM, NPAGE, PARAMS, PAGES, LAYOUT, values, init, update, reset, step, render,
    action, key, visual,
};
