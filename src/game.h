// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_GAME_H
#define PONG_GAME_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    GS_ATTRACT,
    GS_MENU,            // menu de modo, so aparece depois de apertar o SELETOR
    GS_PHASE_INTRO,
    GS_COUNTDOWN,
    GS_PLAY,
    GS_PAUSE,           // SELETOR no meio da partida: continuar ou sair
    GS_ROUND_END,
    GS_PHASE_END,
    GS_GAME_OVER,
    GS_ENTER_INITIALS,
    GS_HIGH_SCORES,
} game_state_t;

typedef enum {
    MODE_ARCADE = 0,    // 1 jogador (P1) contra a CPU
    MODE_VERSUS = 1,    // 2 jogadores
    MODE_COUNT
} game_mode_t;

void game_init(void);

// Avanca um frame de logica e desenha no framebuffer. Chamar a 60 fps.
void game_frame(void);

game_state_t game_get_state(void);
game_mode_t  game_get_mode(void);

#endif // PONG_GAME_H
