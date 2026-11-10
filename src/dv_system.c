/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* SYSTEM: the settings of the instrument itself, selected after MASTER (ARP twice). They are
 * not part of a preset; they are kept in their own storage slot and written when SYSTEM is
 * left. The clock settings act in the core. Bluetooth, the USB audio layout, MIDI clock out
 * and the panel lights are read by the platform through dv_setting(). */
#include "dv.h"
#include "dv_dsp.h"
#include "dv_gfx.h"

static const char *const SOURCES[2] = {"AUTO", "INT"};
static const char *const ON_OFF[2] = {"OFF", "ON"};
static const char *const USB_MODES[2] = {"STEREO", "TRACKS"};
static const char *const BITS[2] = {"16", "24"};
static const dv_param_t PARAMS[DV_SET_COUNT] = {
    {"SOURCE", 0, 1, 0, SOURCES}, {"OFFSET", -40, 40, 0, 0}, {"SEND", 0, 1, 0, ON_OFF},
    {"BT MIDI", 0, 1, 0, ON_OFF}, {"USB OUT", 0, 1, 1, USB_MODES}, {"BITS", 0, 1, 1, BITS},
    {"BRIGHT", 10, 100, 100, 0},  {"KEY LED", 0, 1, 1, ON_OFF},
};
static const int8_t LAYOUT[3][4] = {
    {DV_SET_CLOCK_SOURCE, DV_SET_CLOCK_OFFSET, DV_SET_CLOCK_SEND, -1},
    {DV_SET_BLUETOOTH, DV_SET_USB_AUDIO, DV_SET_USB_BITS, -1},
    {DV_SET_BRIGHT, DV_SET_KEY_LEDS, -1, -1}};
static const char *const PAGES[3] = {"CLOCK", "LINK", "PANEL"};
static int16_t values[DV_SET_COUNT];
static uint8_t changed;

int dv_setting(unsigned which) { return which < DV_SET_COUNT ? values[which] : 0; }

static void update(void) { changed = 1; }

void dv_system_save(void)
{
    uint8_t data[4 + 2 * DV_SET_COUNT] = {'D', 'S', 1, DV_SET_COUNT};
    if (!changed) return;
    for (unsigned i = 0; i < DV_SET_COUNT; i++) {
        data[4 + 2 * i] = (uint8_t)(values[i] & 0xFF);
        data[5 + 2 * i] = (uint8_t)((values[i] >> 8) & 0xFF);
    }
    if (dv_store_write(DV_PRESETS, data, sizeof data)) changed = 0;
}

static void init(void)
{
    uint8_t data[4 + 2 * DV_SET_COUNT];
    for (unsigned i = 0; i < DV_SET_COUNT; i++) values[i] = PARAMS[i].def;
    if (dv_store_read(DV_PRESETS, data, sizeof data) == sizeof data && data[0] == 'D' && data[1] == 'S' && data[2] == 1)
        for (unsigned i = 0; i < DV_SET_COUNT; i++) {
            int value = (int16_t)(data[4 + 2 * i] | data[5 + 2 * i] << 8);
            values[i] = (int16_t)(value < PARAMS[i].min ? PARAMS[i].min : value > PARAMS[i].max ? PARAMS[i].max : value);
        }
    changed = 0;
}

/* what the instrument is doing right now, in three lines */
static void visual(uint16_t *fb, int x, int y, int w, int h, unsigned s16)
{
    (void)s16; (void)h;
    static const char *const names[3] = {"CLOCK", "USB", "BLUETOOTH"};
    const char *state[3];
    state[0] = values[DV_SET_CLOCK_SOURCE] ? "INTERNAL" : dv_clock_is_external() ? "FOLLOWING" : "INTERNAL, LISTENING";
    state[1] = values[DV_SET_USB_AUDIO] ? "6 TRACKS + MIX" : "STEREO MIX";
    state[2] = values[DV_SET_BLUETOOTH] ? "MIDI ON" : "OFF";
    for (int i = 0; i < 3; i++) {
        int lit = i == 0 ? dv_clock_is_external() : i == 1 ? values[DV_SET_USB_AUDIO] : values[DV_SET_BLUETOOTH];
        dv_disc(fb, (float)x + 4.0f, (float)(y + 8 + i * 18), 3.0f, lit ? DV_C_LIME : DV_C_LINE);
        dv_text(fb, DV_FONT_SMALL, x + 14, y + 2 + i * 18, 0, names[i], DV_C_DIM);
        dv_text(fb, DV_FONT_SMALL, x + w, y + 2 + i * 18, 2, state[i], DV_C_TEXT);
    }
}

const dv_machine_t DV_SYSTEM = {
    "SYSTEM", "SYSTEM", 101, DV_SET_COUNT, 3, PARAMS, PAGES, LAYOUT, values, init, update, 0, 0, 0, 0, 0, visual,
};
