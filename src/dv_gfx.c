/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Drelva Audio */
#include "dv_gfx.h"
#include "dv_font.h"
#include "dv_dsp.h"

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

static void plot(uint16_t *fb, int x, int y, uint16_t c, float cover)
{
    if (x < 0 || y < 0 || x >= (int)DV_LCD || y >= (int)DV_LCD || cover <= 0.03f) return;
    unsigned a = cover >= 1.0f ? 15u : (unsigned)(cover * 15.0f + 0.5f);
    uint16_t *p = &fb[y * (int)DV_LCD + x];
    *p = a == 15u ? c : blend(*p, c, a);
}

void dv_disc(uint16_t *fb, float cx, float cy, float r, uint16_t c)
{
    for (int y = (int)(cy - r - 1.0f); y <= (int)(cy + r + 1.0f); y++)
        for (int x = (int)(cx - r - 1.0f); x <= (int)(cx + r + 1.0f); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            plot(fb, x, y, c, r + 0.5f - dv_sqrt(dx * dx + dy * dy));
        }
}

void dv_rrect(uint16_t *fb, int x, int y, int w, int h, int r, uint16_t c)
{
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    dv_fill(fb, x + r, y, w - 2 * r, h, c);
    dv_fill(fb, x, y + r, r, h - 2 * r, c);
    dv_fill(fb, x + w - r, y + r, r, h - 2 * r, c);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            float dx = (float)(r - i) - 0.5f, dy = (float)(r - j) - 0.5f;
            float cover = (float)r + 0.5f - dv_sqrt(dx * dx + dy * dy);
            plot(fb, x + i, y + j, c, cover);
            plot(fb, x + w - 1 - i, y + j, c, cover);
            plot(fb, x + i, y + h - 1 - j, c, cover);
            plot(fb, x + w - 1 - i, y + h - 1 - j, c, cover);
        }
}

void dv_rframe(uint16_t *fb, int x, int y, int w, int h, int r, uint16_t line, uint16_t inside)
{
    dv_rrect(fb, x, y, w, h, r, line);
    dv_rrect(fb, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, inside);
}

void dv_arc(uint16_t *fb, float cx, float cy, float r, float thick, float a0, float a1, uint16_t c)
{
    float half = thick * 0.5f, span = a1 - a0;
    if (span <= 0.0f) return;
    for (int y = (int)(cy - r - half - 1.0f); y <= (int)(cy + r + half + 1.0f); y++)
        for (int x = (int)(cx - r - half - 1.0f); x <= (int)(cx + r + half + 1.0f); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = dv_sqrt(dx * dx + dy * dy) - r;
            float cover = half + 0.5f - (d < 0.0f ? -d : d);
            if (cover <= 0.0f) continue;
            float a = dv_angle(dx, dy) - a0;
            a -= (float)(int)a;
            if (a < 0.0f) a += 1.0f;
            if (a <= span) plot(fb, x, y, c, cover);
        }
    /* round ends */
    float e0x = cx + r * dv_sin(a0), e0y = cy - r * dv_sin(a0 + 0.25f);
    float e1x = cx + r * dv_sin(a1), e1y = cy - r * dv_sin(a1 + 0.25f);
    dv_disc(fb, e0x, e0y, half - 0.5f, c);
    dv_disc(fb, e1x, e1y, half - 0.5f, c);
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
