// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_FONT_H
#define PONG_FONT_H

#include <stdint.h>

// Fonte 5x7 (cell 6x8 com espacamento). Cobre:
//   ' ' '!' '"' '#'... '0'..'9' 'A'..'Z' '.' ':' '-' '?' etc. Apenas maiusculas.
// Cada glifo = 7 bytes; cada byte = uma linha, MSB = pixel esquerdo (5 bits usados).

#define FONT_W 5
#define FONT_H 7
#define FONT_CELL_W 6
#define FONT_CELL_H 8

// Desenha uma string. Caracteres nao mapeados viram espaco. Maiusculas apenas.
// scale = 1, 2, 3, ...
void gfx_text(int x, int y, const char *s, int scale, int color);

// Largura em pixels de uma string ao scale informado.
int gfx_text_width(const char *s, int scale);

#endif // PONG_FONT_H
