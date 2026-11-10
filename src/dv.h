/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Drelva Vave core: six tracks, one machine per track, each with its own voice and its own
 * mono output (one USB channel per track on the device). The core knows nothing about the
 * hardware: the platform feeds it buttons, keys and encoder turns, asks it for audio blocks
 * and for a 240 x 240 RGB565 frame. */
#ifndef DV_H
#define DV_H
#include <stdint.h>

#define DV_FS 44100
#define DV_BLOCK 64u
#define DV_TRACKS 6u
#define DV_LCD 240u

/* panel ids, as the FM-1 key matrix reports them */
enum { DV_BTN_OCT_DN, DV_BTN_OCT_UP, DV_BTN_FX, DV_BTN_SCL, DV_BTN_ENV, DV_BTN_LFO, DV_BTN_EDIT,
       DV_BTN_GLO, DV_BTN_HOME, DV_BTN_SAVE, DV_BTN_ARP, DV_BTN_SEQ, DV_BTN_PLAY, DV_BTN_REC };
enum { DV_ENC_SELECT, DV_ENC_ALGORITHM, DV_ENC_KNOB1, DV_ENC_KNOB2, DV_ENC_KNOB3, DV_ENC_KNOB4,
       DV_ENC_PRESETS };

typedef struct {
    const char *name;
    int16_t min, max, def;
    const char *const *choices;   /* names for min..max, or 0 for a number */
} dv_param_t;

typedef struct {
    const char *name;
    uint8_t nparams, npages;
    const dv_param_t *params;
    const char *const *page_names;
    const int8_t (*layout)[4];    /* parameter index per page and knob, -1: empty */
    int16_t *values;
    void (*init)(void);
    void (*update)(void);                          /* values changed; UI context */
    void (*step)(unsigned step16, int accent);     /* audio context, on each 1/16 */
    void (*render)(float *out, unsigned n);        /* audio context, mono */
    void (*action)(void);                          /* SEQ button: a new pattern */
    void (*visual)(uint16_t *fb, int x, int y, int w, int h, unsigned step16);
} dv_machine_t;

void dv_init(void);
void dv_buttons(uint32_t held);            /* current mask; the core finds the edges */
void dv_keys(uint32_t held);
void dv_encoder(unsigned id, int delta);
/* MIDI real-time bytes from any input (USB, DIN, Bluetooth): 0xF8 clock, 0xFA start,
 * 0xFB continue, 0xFC stop. With a clock coming in the tempo follows it. */
void dv_midi_realtime(uint8_t status);
/* one block: DV_TRACKS mono stems of DV_BLOCK samples, and the stereo mix */
void dv_render(float stems[DV_TRACKS][DV_BLOCK], float *left, float *right);
void dv_draw(uint16_t *fb);                /* host-order RGB565 */
uint32_t dv_led_buttons(void);
uint32_t dv_led_keys(void);

extern const dv_machine_t DV_SISMO_BOX;
#endif
