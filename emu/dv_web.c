/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio
 * The exported interface follows Melodee's web/emu/melodee_web.c (Kerem Kilic, David Konsumer),
 * so its page and worklet drive this build too. */
/* Drelva Vave in the browser: the core compiled to WebAssembly and run in an AudioWorklet.
 * The worklet asks for audio; producing it advances the instrument. A frame is drawn about
 * every 15 ms. Presets live in a block of memory the page keeps in the browser's storage.
 * Not here: USB audio channels (web_stem exposes them for tests), note MIDI. */
#include <stdint.h>
#include <string.h>
#include "../src/dv.h"

#define EXPORT(name) __attribute__((used, export_name(#name)))
#define RING 4096u

static uint16_t frame[DV_LCD * DV_LCD], panel[DV_LCD * DV_LCD];
static float stems[DV_TRACKS][DV_BLOCK], block_l[DV_BLOCK], block_r[DV_BLOCK];
static float ring[RING * 2u], out_l[1024], out_r[1024], stem_out[DV_TRACKS][1024];
static float stem_ring[DV_TRACKS][RING];
static uint32_t ring_w, ring_r, fb_gen, blocks, master_q12 = 4096;
static uint64_t samples;

/* presets: per slot a 16-bit length, then the bytes; the page saves the block when store_gen moves */
#define SLOT (2u + DV_PRESET_BYTES)
static uint8_t store[DV_STORE_SLOTS * SLOT];
static uint32_t store_gen;
unsigned dv_store_read(unsigned slot, uint8_t *data, unsigned size)
{
    const uint8_t *s = store + (slot % DV_STORE_SLOTS) * SLOT;
    unsigned n = s[0] | (unsigned)s[1] << 8;
    if (n > DV_PRESET_BYTES || n > size) return 0;
    memcpy(data, s + 2, n);
    return n;
}
int dv_store_write(unsigned slot, const uint8_t *data, unsigned size)
{
    uint8_t *s = store + (slot % DV_STORE_SLOTS) * SLOT;
    if (size > DV_PRESET_BYTES) return 0;
    s[0] = (uint8_t)size;
    s[1] = (uint8_t)(size >> 8);
    memcpy(s + 2, data, size);
    store_gen++;
    return 1;
}
EXPORT(web_store) uint8_t *web_store(void) { return store; }
EXPORT(web_store_size) uint32_t web_store_size(void) { return sizeof store; }
EXPORT(web_store_gen) uint32_t web_store_gen(void) { return store_gen; }

static void draw(void)
{
    dv_draw(frame);
    int changed = 0;
    uint32_t bright = (uint32_t)dv_setting(DV_SET_BRIGHT);   /* the backlight, as the device would dim it */
    for (uint32_t i = 0; i < DV_LCD * DV_LCD; i++) {
        uint32_t px = frame[i];
        px = ((px >> 11) * bright / 100u) << 11 | (((px >> 5) & 63u) * bright / 100u) << 5 | ((px & 31u) * bright / 100u);
        uint16_t c = (uint16_t)(px << 8 | px >> 8);               /* the panel's byte order */
        if (panel[i] != c) { panel[i] = c; changed = 1; }
    }
    fb_gen += (uint32_t)changed;
}

EXPORT(web_boot) void web_boot(uint32_t master)
{
    dv_init();
    master_q12 = (master * master) >> 8;
    draw();
    fb_gen++;
}
EXPORT(web_keys) void web_keys(uint32_t m) { dv_keys(m); }
EXPORT(web_buttons) void web_buttons(uint32_t m) { dv_buttons(m); }
EXPORT(web_encoder) void web_encoder(uint32_t e, int32_t n) { dv_encoder(e, n); }
EXPORT(web_master) void web_master(uint32_t v)
{
    if (v > 1023u) v = 1023u;
    master_q12 = (v * v) >> 8;
}
EXPORT(web_midi_in) void web_midi_in(uint32_t st, uint32_t d1, uint32_t d2) 
{
    (void)d1; (void)d2;
    if (st >= 0xF8u) dv_midi_realtime((uint8_t)st);
}
EXPORT(web_led_keys) uint32_t web_led_keys(void) { return dv_led_keys(); }
EXPORT(web_led_buttons) uint32_t web_led_buttons(void) { return dv_led_buttons(); }
EXPORT(web_fb) uint16_t *web_fb(void) { return panel; }
EXPORT(web_fb_gen) uint32_t web_fb_gen(void) { return fb_gen; }
EXPORT(web_now) uint32_t web_now(void) { return (uint32_t)(samples * 1000u / DV_FS); }
EXPORT(web_out_l) float *web_out_l(void) { return out_l; }
EXPORT(web_out_r) float *web_out_r(void) { return out_r; }
EXPORT(web_stem) float *web_stem(uint32_t t) { return stem_out[t % DV_TRACKS]; }

EXPORT(web_render) void web_render(uint32_t n)        /* n frames (<= 1024) into out_l / out_r */
{
    float master = (float)master_q12 / 4096.0f;
    if (n > 1024u) n = 1024u;
    while (ring_w - ring_r < n) {
        dv_render(stems, block_l, block_r);
        for (uint32_t i = 0; i < DV_BLOCK; i++) {
            uint32_t r = ring_w++ % RING;
            ring[2u * r] = block_l[i] * master;
            ring[2u * r + 1u] = block_r[i] * master;
            for (uint32_t t = 0; t < DV_TRACKS; t++) stem_ring[t][r] = stems[t][i];
        }
        samples += DV_BLOCK;
        if (++blocks % 10u == 0u) draw();              /* about every 14.5 ms */
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = ring_r++ % RING;
        out_l[i] = ring[2u * r];
        out_r[i] = ring[2u * r + 1u];
        for (uint32_t t = 0; t < DV_TRACKS; t++) stem_out[t][i] = stem_ring[t][r];
    }
}
