// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_GFX_H
#define PONG_GFX_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

// Limpa todo o framebuffer (cor = 0 ou 1).
void gfx_clear(int color);

// Pixel individual.
void gfx_set(int x, int y, int color);

// Linhas alinhadas.
void gfx_hline(int x, int y, int w, int color);
void gfx_vline(int x, int y, int h, int color);

// Retangulo preenchido.
void gfx_fill_rect(int x, int y, int w, int h, int color);

// Desenha um bitmap 1-bit packed MSB-first.
//   data: array de bytes, stride = (bitmap_w + 7) / 8
//   bitmap_w, bitmap_h: dimensoes do bitmap (em pixels)
//   x, y: canto superior esquerdo no framebuffer
//   color: 1 = desenha bits "1" como branco; bits 0 ficam transparentes
//          0 = desenha bits "1" como preto (mascara); bits 0 transparentes
void gfx_blit(const uint8_t *data, int bitmap_w, int bitmap_h, int x, int y, int color);

// Linha de tela ponteada (separador central do Pong).
void gfx_dotted_vline(int x, int y0, int y1, int dot_h, int gap_h);

#endif // PONG_GFX_H
