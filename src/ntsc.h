// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_NTSC_H
#define PONG_NTSC_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

// Framebuffer 1-bit, 256x192. Cada linha = 8 uint32_t. MSB do primeiro word = pixel x=0.
// 1 = branco, 0 = preto.
extern uint32_t fb[FB_WORDS];

// Inicializa PIO + DMA + state machines. Apos isto, alterar 'fb' aparece na tela.
void ntsc_init(void);

// Numero de frames desenhados desde o boot (incrementado a partir do vsync).
extern volatile uint32_t ntsc_frame_count;

// Aguarda o proximo vsync (busy-wait). Usar para sincronizar a logica do jogo em 60 fps.
void ntsc_wait_vsync(void);

#endif // PONG_NTSC_H
