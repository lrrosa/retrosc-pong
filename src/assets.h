// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_ASSETS_H
#define PONG_ASSETS_H

#include <stdint.h>

// Bitmaps 1-bit, MSB-first, gerados a partir de docs/images/ por tools/png_to_c.py.
// Para regenerar:
//   python tools/png_to_c.py docs/images/logo_retrosc.png       retrosc_logo     0 0 threshold > tools/_logo.inc
//   python tools/png_to_c.py docs/images/mascote_retrosc_16.png retrosc_mascote  0 0 threshold > tools/_mascote16.inc
// E depois copiar para dentro de assets.c.

extern const int RETROSC_LOGO_W;
extern const int RETROSC_LOGO_H;
extern const int RETROSC_LOGO_STRIDE;
extern const uint8_t retrosc_logo_data[];

// Mascote da RetroSC em 16x16 -- o bonus que cruza a quadra (PF_TEM_BONUS).
extern const int RETROSC_MASCOTE_W;
extern const int RETROSC_MASCOTE_H;
extern const int RETROSC_MASCOTE_STRIDE;
extern const uint8_t retrosc_mascote_data[];

#endif // PONG_ASSETS_H
