/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Drelva Vave core: six tracks, one machine per track, each with its own voice and its own
 * mono output (one USB channel per track on the device), plus a master section with a delay,
 * a reverb and an output stage that only the stereo mix goes through. The core knows nothing
 * about the hardware: the platform feeds it buttons, keys, encoder turns and MIDI real-time
 * bytes, asks it for audio blocks and for a 240 x 240 RGB565 frame, and stores presets. */
#ifndef DV_H
#define DV_H
#include <stdint.h>

#define DV_FS 44100
#define DV_BLOCK 64u
#define DV_TRACKS 6u
#define DV_LCD 240u
#define DV_PRESETS 8u
#define DV_PRESET_BYTES 512u
#define DV_STORE_SLOTS (DV_PRESETS + 1u)   /* the presets, then the system settings */

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
    const char *name, *short_name;   /* short: six letters at most, for a knob */
    uint8_t id;                      /* stored in presets: never reuse or renumber */
    uint8_t nparams, npages;
    const dv_param_t *params;
    const char *const *page_names;
    const int8_t (*layout)[4];    /* parameter index per page and knob, -1: empty */
    int16_t *values;
    void (*init)(void);
    void (*update)(void);                          /* values changed; UI context */
    void (*reset)(void);                           /* the pattern restarts; may be 0 */
    void (*step)(unsigned step16, float step_len); /* audio context, each 1/16; length in samples; may be 0 */
    void (*render)(float *out, unsigned n);        /* audio context, mono; may be 0 */
    void (*action)(void);                          /* SEQ button; may be 0 */
    void (*key)(int note);                         /* a key went down, 0..26 from the left; -1: all up; may be 0 */
    void (*visual)(uint16_t *fb, int x, int y, int w, int h, unsigned step16);
} dv_machine_t;

void dv_init(void);
void dv_buttons(uint32_t held);            /* current mask; the core finds the edges */
void dv_keys(uint32_t held);
void dv_encoder(unsigned id, int delta);
/* MIDI real-time bytes from any input (USB, DIN, Bluetooth): 0xF8 clock, 0xFA start,
 * 0xFB continue, 0xFC stop. With a clock coming in the tempo follows it. */
void dv_midi_realtime(uint8_t status);
/* one block: DV_TRACKS dry mono stems of DV_BLOCK samples, and the stereo mix with the effects */
void dv_render(float stems[DV_TRACKS][DV_BLOCK], float *left, float *right);
void dv_draw(uint16_t *fb);                /* host-order RGB565 */
uint32_t dv_led_buttons(void);
uint32_t dv_led_keys(void);

/* system settings (dv_system.c), for the core and the platform */
enum { DV_SET_CLOCK_SOURCE,   /* 0 auto: follow MIDI clock when it comes; 1 internal only */
       DV_SET_CLOCK_OFFSET,   /* ms added to the followed clock; negative plays earlier */
       DV_SET_CLOCK_SEND,     /* 1: send MIDI clock (platform) */
       DV_SET_BLUETOOTH,      /* 1: Bluetooth MIDI on (platform) */
       DV_SET_USB_AUDIO,      /* 0 stereo mix; 1 six tracks + mix (platform) */
       DV_SET_USB_BITS,       /* 0: 16 bit; 1: 24 bit (platform) */
       DV_SET_BRIGHT,         /* screen brightness, 10..100 (platform) */
       DV_SET_KEY_LEDS,       /* 1: key lights on (platform) */
       DV_SET_COUNT };
int dv_setting(unsigned which);
void dv_system_save(void);                 /* writes the settings if they changed */

/* for the machines and the master section */
int dv_is_playing(void);
int dv_clock_is_external(void);
int dv_machine_page(const dv_machine_t *m); /* the page shown of the track holding m; -1 if not on a track */
float dv_meter(unsigned track);            /* recent peak of a track, 0..1 */
float dv_meter_out(unsigned channel);      /* recent peak of the mix, left / right */

/* master effects (dv_fx.c): sends in, stereo mix in and out */
float dv_fx_pump(void);                    /* 0..1: how far each beat ducks the tracks without the kick */
void dv_fx_process(const float *to_delay, const float *to_reverb, float *left, float *right, float step_len);

/* preset storage, provided by the platform: bytes read (0: empty slot), 1 on a successful write */
unsigned dv_store_read(unsigned slot, uint8_t *data, unsigned size);
int dv_store_write(unsigned slot, const uint8_t *data, unsigned size);

extern const dv_machine_t DV_ELASTICO, DV_MASTER, DV_MINI_ATLANTIS, DV_PERC_BOX, DV_PHRASE_BOX, DV_SISMO_BOX, DV_STAB_BOX,
    DV_SWARM_ENGINE, DV_SYSTEM, DV_TEXTURA;
#endif
