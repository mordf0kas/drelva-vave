/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Phrase Box: counts phrases and plays a transition FX at every phrase line, the way hypnotic
 * techno marks its 64 and 128 step sections.
 *
 * Before the phrase line (the last LEAD steps):
 *   Rise   a noise + detuned saw riser, filter opening
 *   Roll   a snare-style noise roll: 1/4, 1/8, 1/16, then 1/32 in the last two steps
 *   Swell  a reverse cymbal: bright noise that grows and stops dead on the downbeat
 *   Siren  a rave siren: a sine wobbling faster and higher
 *   Kicks  a kick roll on the same accelerating grid, its pitch climbing
 * On the downbeat:
 *   Hit    an impact: sub boom with a pitch drop, noise crash, click
 *   Zap    a laser: a sine falling from very high
 * After it (the first LEAD steps of the new phrase):
 *   Down   a downlifter: the riser played backwards
 *   Sub    a sub drop: a low sine gliding down an octave and fading
 * FX picks a fixed combination, or VARY picks a new one (and a new length) for every phrase.
 * SEQ fires a hit now.
 *
 * Ported from the Drelvarom program; the tempo comes from the instrument's clock. */
#include "../dv.h"
#include "../dv_dsp.h"
#include "../dv_gfx.h"

enum { PHRASE, FX, LEAD, LEN_RND, RES, TONE, PITCH, TAIL, CRASH, DRIVE, NPARAM };
enum { F_RISE = 1, F_HIT = 2, F_ROLL = 4, F_DOWN = 8, F_SWELL = 16, F_SIREN = 32, F_KICKS = 64, F_ZAP = 128, F_SUB = 256 };
#define LEAD_FX (F_RISE | F_ROLL | F_SWELL | F_SIREN | F_KICKS)   /* before the 1 */
#define FALL_FX (F_DOWN | F_SUB)                                  /* after the 1 */
#define VARY 0xFFFFu
#define NTYPE 12
#define NVARY 13
#define NO_STEP 0xFFFFu

static const uint16_t TYPE_MASK[NTYPE] = {
    F_HIT, F_RISE, F_RISE | F_HIT, F_ROLL | F_HIT, F_HIT | F_DOWN, F_SWELL | F_HIT, F_SIREN | F_HIT,
    F_KICKS | F_HIT, F_RISE | F_ZAP, F_HIT | F_SUB, F_RISE | F_HIT | F_DOWN, VARY};
static const char *const TYPE_NAMES[NTYPE] = {
    "HIT", "RISE", "R+HIT", "ROLL", "DOWN", "SWELL", "SIREN", "KICKS", "ZAP", "SUB", "ALL", "VARY"};
static const uint16_t VARIED[NVARY] = {
    F_RISE | F_HIT, F_ROLL | F_HIT, F_HIT | F_DOWN, F_RISE | F_HIT | F_DOWN, F_HIT, F_ROLL | F_HIT | F_DOWN,
    F_SWELL | F_HIT, F_SIREN | F_HIT, F_KICKS | F_HIT, F_RISE | F_ZAP, F_HIT | F_SUB, F_SWELL | F_ZAP | F_SUB,
    F_KICKS | F_HIT | F_SUB};
static const int16_t PHRASE_LEN[5] = {16, 32, 64, 128, 256}, LEAD_LEN[5] = {4, 8, 16, 32, 64};
static const char *const PHRASE_NAMES[5] = {"16", "32", "64", "128", "256"};
static const char *const LEAD_NAMES[5] = {"4", "8", "16", "32", "64"};
static const char *const ON_OFF[2] = {"OFF", "ON"};

static const dv_param_t PARAMS[NPARAM] = {
    {"PHRASE", 0, 4, 2, PHRASE_NAMES}, {"FX", 0, NTYPE - 1, 2, TYPE_NAMES}, {"LEAD", 0, 4, 2, LEAD_NAMES},
    {"RNDLEN", 0, 1, 1, ON_OFF},       {"RESO", 0, 100, 45, 0},             {"TONE", 0, 100, 35, 0},
    {"PITCH", -12, 12, 0, 0},          {"TAIL", 0, 100, 55, 0},             {"CRASH", 0, 100, 60, 0},
    {"DRIVE", 0, 100, 40, 0},
};
static const int8_t LAYOUT[3][4] = {{PHRASE, FX, LEAD, LEN_RND}, {RES, TONE, PITCH, TAIL}, {CRASH, DRIVE, -1, -1}};
static const char *const PAGES[3] = {"PHRASE", "RISE", "HIT"};

#define HIT_BASE (3.0f * 1536.0f - 5.0f * 128.0f)   /* about G1 (49 Hz) */
#define FLOOR 0.00012f                              /* below -78 dB: stop */

static int16_t values[NPARAM];
static volatile uint16_t step_now = NO_STEP, current = F_RISE | F_HIT, next = F_RISE | F_HIT;
static volatile uint8_t fire_hit;
static int lead_steps = 16;
static struct {
    float res, tone, base, boom_decay, crash_decay, crash_amt, drive, roll_decay, kick_decay, zap_drop, zap_decay;
} k;
static struct {
    float in_step, step_len;
    uint8_t half_fired;
    float boom_phase, saw_a, saw_b, siren_lfo, siren_phase, kick_phase, zap_phase, sub_phase;
    float boom_env, boom_pitch, crash_env, click_env, roll_env, kick_env, kick_pitch, zap_env, zap_pitch, last_noise;
    dv_svf2_t sweep, crash, swell;
    dv_rng_t noise;
} v;

static uint16_t fixed_mask(void)
{
    uint16_t m = TYPE_MASK[values[FX]];
    return m == VARY ? VARIED[0] : m;
}

static int16_t fx_seen = -1, lead_seen = -1;
static void update(void)
{
    float tail = 300.0f * dv_exp2((float)values[TAIL] * 0.01f * 3.737f);   /* 300 ms .. 4 s */
    k.res = (float)values[RES] * 1.2f;
    k.tone = (float)values[TONE] * 0.01f;
    k.base = HIT_BASE + (float)values[PITCH] * 128.0f;
    k.boom_decay = dv_settle_ms(tail);
    k.crash_decay = dv_settle_ms(tail * 0.6f);
    k.crash_amt = (float)values[CRASH] * 0.01f;
    k.drive = (float)values[DRIVE] * 0.02f;
    k.roll_decay = dv_settle_ms(70.0f);
    k.kick_decay = dv_settle_ms(120.0f);
    k.zap_drop = dv_settle_ms(180.0f);
    k.zap_decay = dv_settle_ms(260.0f);
    if (values[FX] != fx_seen || values[LEAD] != lead_seen) {
        fx_seen = values[FX];
        lead_seen = values[LEAD];
        current = next = fixed_mask();
        lead_steps = LEAD_LEN[values[LEAD]];
    }
}

static void init(void)
{
    for (int i = 0; i < NPARAM; i++) values[i] = PARAMS[i].def;
    v.noise.s = 0xC0FFEE11u;
    v.step_len = 4937.0f;
    update();
}

static void reset(void) { step_now = NO_STEP; }
static void action(void) { fire_hit = 1; }

static void hit(void)
{
    v.boom_phase = 0.25f;   /* top of the sine: a hard edge */
    v.boom_env = v.boom_pitch = v.crash_env = v.click_env = 1.0f;
}

/* one hit of the roll grid, for the snare roll and/or the kick roll */
static void roll_hit(void)
{
    if (next & F_ROLL) v.roll_env = 1.0f;
    if (next & F_KICKS) {
        v.kick_phase = 0.25f;
        v.kick_env = v.kick_pitch = 1.0f;
    }
}

static int lead_now(int len) { return lead_steps < len / 2 ? lead_steps : len / 2; }

static void step(unsigned s16, float step_len)
{
    (void)s16;
    int len = PHRASE_LEN[values[PHRASE]];
    unsigned s = step_now == NO_STEP || step_now + 1u >= (unsigned)len ? 0u : step_now + 1u;
    step_now = (uint16_t)s;
    v.in_step = 0.0f;
    v.step_len = step_len;
    if (s == 0u) {
        /* the phrase line: the FX that ended the old phrase hits now, then the next one is chosen */
        if (next & F_HIT) hit();
        if (next & F_ZAP) { v.zap_phase = 0.0f; v.zap_env = v.zap_pitch = 1.0f; }
        if (next & F_SUB) v.sub_phase = 0.0f;
        current = next;
        if (TYPE_MASK[values[FX]] == VARY) {
            next = VARIED[dv_rng_u32(&v.noise) % NVARY];
            if (values[LEN_RND]) lead_steps = LEAD_LEN[1u + dv_rng_u32(&v.noise) % 3u];   /* 8, 16 or 32 */
        } else {
            next = TYPE_MASK[values[FX]];
            lead_steps = LEAD_LEN[values[LEAD]];
        }
        v.half_fired = 0;
    } else {
        /* inside the roll window: 1/4 notes, then 1/8, then every step */
        int lead = lead_now(len), into = (int)s - (len - lead);
        if (into >= 0 && (next & (F_ROLL | F_KICKS))) {
            int quarter = lead / 4 ? lead / 4 : 1;
            int every = into < 2 * quarter ? 4 : into < 3 * quarter ? 2 : 1;
            if (into % every == 0) { roll_hit(); v.half_fired = 0; }
        }
    }
}

static inline float turn(float *phase, float hz)
{
    *phase += hz * (1.0f / (float)DV_FS);
    if (*phase >= 1.0f) *phase -= 1.0f;
    return *phase;
}

static void render(float *out, unsigned n)
{
    int len = PHRASE_LEN[values[PHRASE]], lead = lead_now(len);
    unsigned s = step_now;
    if (fire_hit) { fire_hit = 0; hit(); }
    for (unsigned i = 0; i < n; i++) {
        float mix = 0.0f, noise = dv_rng_bipolar(&v.noise);
        float frac = v.in_step / v.step_len;
        if (frac > 0.9999f) frac = 0.9999f;
        v.in_step += 1.0f;

        /* progress before the phrase line (lead) and after it (fall), 0..1, -1 outside */
        float lead_p = -1.0f, fall_p = -1.0f;
        if (s != NO_STEP && dv_is_playing()) {
            if ((next & LEAD_FX) && (int)s >= len - lead) lead_p = ((float)((int)s - (len - lead)) + frac) / (float)lead;
            if ((current & FALL_FX) && (int)s < lead) fall_p = 1.0f - ((float)s + frac) / (float)lead;
            if (lead_p >= 0.0f && (next & (F_ROLL | F_KICKS)) && (int)s >= len - 2 && frac >= 0.5f && !v.half_fired) {
                v.half_fired = 1;      /* the last two steps roll in 1/32s */
                roll_hit();
            }
        }
        float rise = lead_p >= 0.0f && (next & (F_RISE | F_ROLL)) ? lead_p : -1.0f;
        float fall = fall_p >= 0.0f && (current & F_DOWN) ? fall_p : -1.0f;

        /* riser / roll / downlifter: noise + saws through a moving low-pass */
        if (rise >= 0.0f || fall >= 0.0f) {
            float p = rise >= 0.0f ? rise : fall, src = noise;
            dv_svf2_set(&v.sweep, dv_pitch_hz(6000.0f + p * 8200.0f), k.res);     /* about 120 Hz .. 5 kHz */
            if (rise >= 0.0f && (next & F_ROLL)) {
                v.roll_env *= k.roll_decay;
                src *= v.roll_env;
            }
            /* two detuned saws an octave and more above the hit, climbing two octaves */
            float hz = dv_pitch_hz(dv_clampf(k.base + 2.0f * 1536.0f + p * 2.0f * 1536.0f, 0.0f, 14000.0f));
            float inc = hz / (float)DV_FS;
            float saws = (dv_saw_blep(turn(&v.saw_a, hz), inc) + dv_saw_blep(turn(&v.saw_b, hz * 1.0078f), inc * 1.0078f)) * 0.5f;
            src = src * (1.0f - k.tone) + saws * k.tone;
            mix += dv_svf2_lp(&v.sweep, src) * p * p * 0.5f;
        } else {
            v.roll_env = 0.0f;
        }

        /* swell: high-passed noise, the corner coming down, level p^3, cut on the 1 */
        if (lead_p >= 0.0f && (next & F_SWELL)) {
            float n2 = dv_rng_bipolar(&v.noise);
            dv_svf2_set(&v.swell, dv_pitch_hz(13800.0f - lead_p * 4300.0f), k.res * 0.5f);   /* 4.5 kHz .. 650 Hz */
            mix += dv_clampf(n2 - dv_svf2_lp(&v.swell, n2), -1.0f, 1.0f) * lead_p * lead_p * lead_p * 0.25f;
        }

        /* siren: a sine around a centre that climbs an octave, wobbling +-1 octave, faster and faster */
        if (lead_p >= 0.0f && (next & F_SIREN)) {
            float wobble = dv_sin(turn(&v.siren_lfo, 1.5f + lead_p * 9.5f));
            float hz = dv_pitch_hz(dv_clampf(k.base + 4.0f * 1536.0f + lead_p * 1536.0f + wobble * 1536.0f, 0.0f, 14000.0f));
            mix += dv_sin(turn(&v.siren_phase, hz)) * (0.183f + lead_p * 0.427f);
        }

        /* kicks: short sine kicks with a fast pitch drop, the base climbing an octave */
        if (v.kick_env > 0.0f) {
            v.kick_pitch *= 0.99482f;                                      /* about 20 ms pitch fall */
            float climb = lead_p >= 0.0f ? lead_p * 1536.0f : 1536.0f;
            float hz = dv_pitch_hz(dv_clampf(k.base + 1536.0f + climb + v.kick_pitch * 3.0f * 1536.0f, 0.0f, 12000.0f));
            v.kick_env *= k.kick_decay;
            if (v.kick_env < FLOOR) v.kick_env = 0.0f;
            mix += dv_sin(turn(&v.kick_phase, hz)) * v.kick_env * (lead_p >= 0.0f ? 0.366f + lead_p * 0.61f : 0.977f);
        }

        /* zap: a sine falling from about 6 octaves to 1 octave above the hit, driven */
        if (v.zap_env > 0.0f) {
            v.zap_pitch *= k.zap_drop;
            float hz = dv_pitch_hz(dv_clampf(k.base + 1536.0f + v.zap_pitch * 5.0f * 1536.0f, 0.0f, 14000.0f));
            v.zap_env *= k.zap_decay;
            if (v.zap_env < FLOOR) v.zap_env = 0.0f;
            mix += dv_softclip(dv_sin(turn(&v.zap_phase, hz)) * 1.5f) * v.zap_env * 0.5f;
        }

        /* sub: a low sine that glides down an octave over the fall and fades out */
        if (fall_p >= 0.0f && (current & F_SUB)) {
            float hz = dv_pitch_hz(dv_clampf(k.base - (1.0f - fall_p) * 1536.0f, 0.0f, 12000.0f));
            mix += dv_sin(turn(&v.sub_phase, hz)) * fall_p * fall_p * 0.5f * 0.793f;
        }

        /* impact: sub boom with a pitch drop, high-passed noise crash, click */
        if (v.boom_env > 0.0f || v.crash_env > 0.0f) {
            v.boom_pitch *= 0.9985f;                                       /* about 60 ms pitch fall */
            float hz = dv_pitch_hz(dv_clampf(k.base + v.boom_pitch * 3.0f * 1536.0f, 0.0f, 12000.0f));
            v.boom_env *= k.boom_decay;
            v.crash_env *= k.crash_decay;
            v.click_env *= 0.96597f;                                       /* about 2 ms */
            dv_svf2_set(&v.crash, dv_pitch_hz(11000.0f), 0.0f);            /* about 1.2 kHz high-pass corner */
            float hp = dv_clampf(noise - dv_svf2_lp(&v.crash, noise), -1.0f, 1.0f);
            float x = dv_sin(turn(&v.boom_phase, hz)) * v.boom_env + hp * k.crash_amt * v.crash_env
                    + (noise - v.last_noise) * 0.5f * v.click_env;
            mix += dv_softclip(x + x * k.drive);
            if (v.boom_env < FLOOR) v.boom_env = 0.0f;
            if (v.crash_env < FLOOR) v.crash_env = 0.0f;
        }
        v.last_noise = noise;
        out[i] = dv_softclip(mix);
    }
}

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_number(char *p, int value)
{
    char tmp[6];
    int n = 0;
    do { tmp[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = tmp[--n];
    return p;
}

/* the next transition, the count inside the phrase, and the phrase as a bar with the lead marked */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    char text[40], *p = text;
    int len = PHRASE_LEN[values[PHRASE]], s = step_now == NO_STEP || s16 >= 16u ? 0 : step_now, lead = lead_now(len);
    uint16_t m = next;
    if (m & F_RISE) p = put(p, "UP ");
    if (m & F_ROLL) p = put(p, "ROLL ");
    if (m & F_SWELL) p = put(p, "SWELL ");
    if (m & F_SIREN) p = put(p, "SIREN ");
    if (m & F_KICKS) p = put(p, "KICKS ");
    if (m & F_HIT) p = put(p, "HIT ");
    if (m & F_ZAP) p = put(p, "ZAP ");
    if (m & F_DOWN) p = put(p, "DOWN ");
    if (m & F_SUB) p = put(p, "SUB ");
    *p = 0;
    dv_text(fb, DV_FONT_SMALL, x, y, 0, text, v.boom_env > 0.3f ? DV_C_TEXT : DV_C_LIME);
    p = put_number(text, s + 1); p = put(p, " / "); p = put_number(p, len); *p = 0;
    dv_text(fb, DV_FONT_MEDIUM, x + w, y + 14, 2, text, DV_C_TEXT);
    dv_text(fb, DV_FONT_SMALL, x, y + 18, 0, "STEP", DV_C_FAINT);

    int by = y + h - 16, lx = x + (len - lead) * w / len;
    dv_rrect(fb, x, by, w, 10, 4, DV_C_BASE);
    dv_rrect(fb, lx, by, x + w - lx, 10, 4, DV_C_LINE);                    /* the lead before the line */
    if (s16 < 16u) dv_rrect(fb, x, by, 6 + (s + 1) * (w - 6) / len, 10, 4, s >= len - lead ? DV_C_TEXT : DV_C_LIME);
    dv_fill(fb, lx, by - 3, 2, 16, DV_C_DIM);
}

const dv_machine_t DV_PHRASE_BOX = {
    "PHRASE BOX", "PHRASE", 2, NPARAM, 3, PARAMS, PAGES, LAYOUT, values, init, update, reset, step, render, action, 0, visual,
};
