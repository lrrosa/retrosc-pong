// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "gfx.h"
#include "ntsc.h"

static inline uint32_t *fb_word(int x, int y) {
    return &fb[y * FB_STRIDE_WORDS + (x >> 5)];
}

void gfx_clear(int color) {
    uint32_t fill = color ? 0xFFFFFFFFu : 0u;
    for (int i = 0; i < FB_WORDS; i++) fb[i] = fill;
}

void gfx_set(int x, int y, int color) {
    if ((unsigned)x >= FB_WIDTH || (unsigned)y >= FB_HEIGHT) return;
    uint32_t *w = fb_word(x, y);
    uint32_t bit = 1u << (31 - (x & 31));
    if (color) *w |= bit;
    else       *w &= ~bit;
}

void gfx_hline(int x, int y, int w, int color) {
    if ((unsigned)y >= FB_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB_WIDTH) w = FB_WIDTH - x;
    if (w <= 0) return;
    for (int i = 0; i < w; i++) gfx_set(x + i, y, color);
}

void gfx_vline(int x, int y, int h, int color) {
    if ((unsigned)x >= FB_WIDTH) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > FB_HEIGHT) h = FB_HEIGHT - y;
    if (h <= 0) return;
    for (int i = 0; i < h; i++) gfx_set(x, y + i, color);
}

void gfx_fill_rect(int x, int y, int w, int h, int color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_WIDTH)  w = FB_WIDTH  - x;
    if (y + h > FB_HEIGHT) h = FB_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    // Caminho rapido: linhas inteiras de 32 bits
    if ((x & 31) == 0 && (w & 31) == 0) {
        uint32_t fill = color ? 0xFFFFFFFFu : 0u;
        for (int row = 0; row < h; row++) {
            uint32_t *w0 = fb_word(x, y + row);
            for (int wi = 0; wi < w / 32; wi++) w0[wi] = fill;
        }
        return;
    }
    // Caminho lento (pixel a pixel)
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            gfx_set(x + col, y + row, color);
}

void gfx_blit(const uint8_t *data, int bw, int bh, int x, int y, int color) {
    int stride = (bw + 7) / 8;
    for (int row = 0; row < bh; row++) {
        int dy = y + row;
        if ((unsigned)dy >= FB_HEIGHT) continue;
        const uint8_t *src_row = data + row * stride;
        for (int col = 0; col < bw; col++) {
            int dx = x + col;
            if ((unsigned)dx >= FB_WIDTH) continue;
            uint8_t b = src_row[col >> 3];
            if (b & (0x80 >> (col & 7))) {
                gfx_set(dx, dy, color);
            }
        }
    }
}

void gfx_dotted_vline(int x, int y0, int y1, int dot_h, int gap_h) {
    int phase = 0;
    int draw = 1;
    for (int y = y0; y < y1; y++) {
        gfx_set(x, y, draw ? 1 : 0);
        phase++;
        if (draw && phase >= dot_h) { phase = 0; draw = 0; }
        else if (!draw && phase >= gap_h) { phase = 0; draw = 1; }
    }
}
