/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
#include "dv_gfx.h"
#include "dv_font.h"

const dv_font_t *const DV_FONT_SMALL = &DV_FONT_S;
const dv_font_t *const DV_FONT_MEDIUM = &DV_FONT_M;
const dv_font_t *const DV_FONT_LARGE = &DV_FONT_L;

void dv_fill(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)DV_LCD) x1 = (int)DV_LCD;
    if (y1 > (int)DV_LCD) y1 = (int)DV_LCD;
    for (int j = y; j < y1; j++)
        for (int i = x; i < x1; i++)
            fb[j * (int)DV_LCD + i] = c;
}

void dv_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    dv_fill(fb, x, y, w, 1, c);
    dv_fill(fb, x, y + h - 1, w, 1, c);
    dv_fill(fb, x, y, 1, h, c);
    dv_fill(fb, x + w - 1, y, 1, h, c);
}

static int glyph_index(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    return ch < 32 || ch > 95 ? 0 : ch - 32;
}

int dv_text_width(const dv_font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) w += f->glyphs[glyph_index(*s)].advance;
    return w;
}

static uint16_t blend(uint16_t bg, uint16_t fg, unsigned a)   /* a: 0..15 */
{
    unsigned r = ((bg >> 11) * (15u - a) + (fg >> 11) * a) / 15u;
    unsigned g = (((bg >> 5) & 63u) * (15u - a) + ((fg >> 5) & 63u) * a) / 15u;
    unsigned b = ((bg & 31u) * (15u - a) + (fg & 31u) * a) / 15u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

int dv_text(uint16_t *fb, const dv_font_t *f, int x, int y, int align, const char *s, uint16_t c)
{
    int width = dv_text_width(f, s);
    if (align == 1) x -= width / 2;
    else if (align == 2) x -= width;
    for (; *s; s++) {
        const dv_glyph_t *g = &f->glyphs[glyph_index(*s)];
        const uint8_t *p = f->data + g->offset;
        unsigned stride = (g->width + 1u) / 2u;
        for (int j = 0; j < f->height; j++, p += stride) {
            int py = y + j;
            if (py < 0 || py >= (int)DV_LCD) continue;
            for (unsigned i = 0; i < g->width; i++) {
                unsigned a = (i & 1u) ? p[i / 2u] & 15u : p[i / 2u] >> 4;
                int px = x + (int)i;
                if (a && px >= 0 && px < (int)DV_LCD)
                    fb[py * (int)DV_LCD + px] = blend(fb[py * (int)DV_LCD + px], c, a);
            }
        }
        x += g->advance;
    }
    return width;
}
