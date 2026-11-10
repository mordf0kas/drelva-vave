/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
/* Drawing into a 240 x 240 RGB565 frame: rectangles and Michroma text in the house palette. */
#ifndef DV_GFX_H
#define DV_GFX_H
#include <stdint.h>
#include "dv.h"

#define DV_RGB(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)))
/* house look (DrelvaLookAndFeel.h) */
#define DV_C_BASE  DV_RGB(0x07, 0x09, 0x0B)
#define DV_C_PANEL DV_RGB(0x0B, 0x0E, 0x12)
#define DV_C_LINE  DV_RGB(0x2B, 0x32, 0x3D)
#define DV_C_LIME  DV_RGB(0xB9, 0xDC, 0x87)
#define DV_C_DEEP  DV_RGB(0x97, 0xB7, 0x72)
#define DV_C_TEXT  DV_RGB(0xE5, 0xEA, 0xF2)
#define DV_C_DIM   DV_RGB(0x92, 0x9E, 0xAF)
#define DV_C_FAINT DV_RGB(0x62, 0x6D, 0x7E)

typedef struct { uint16_t offset; uint8_t width, advance; } dv_glyph_t;
typedef struct { const uint8_t *data; const dv_glyph_t *glyphs; uint8_t height; } dv_font_t;
extern const dv_font_t *const DV_FONT_SMALL, *const DV_FONT_MEDIUM, *const DV_FONT_LARGE;

void dv_fill(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void dv_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
int dv_text_width(const dv_font_t *f, const char *s);
/* align: 0 left of x, 1 centred on x, 2 right edge at x; returns the width drawn */
int dv_text(uint16_t *fb, const dv_font_t *f, int x, int y, int align, const char *s, uint16_t c);
#endif
