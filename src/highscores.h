// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_HIGHSCORES_H
#define PONG_HIGHSCORES_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

typedef struct {
    uint16_t score;
    uint8_t  player;                       // 1 ou 2 (lado da raquete)
    uint8_t  mode;                         // game_mode_t: 0 = arcade, 1 = versus
    char     initials[INITIALS_LEN + 1];   // 3 letras + terminador NUL
} hi_entry_t;

typedef struct {
    uint32_t   magic;
    uint32_t   version;
    hi_entry_t entries[HISCORE_COUNT];
    uint32_t   checksum;
} hi_table_t;

void hi_init(void);

// Carrega da flash (ou inicializa zerado se invalida).
void hi_load(void);

// Salva a tabela atual na flash.
void hi_save(void);

// Considera um novo placar (total somado de todas as fases) e insere se
// entrar no top.
//   initials: 3 chars (sem precisar de NUL).
//   Retorna true se entrou no top, false caso contrario.
bool hi_consider(uint16_t score, uint8_t player, uint8_t mode,
                 const char initials[INITIALS_LEN]);

// True se este score entraria no top (chamado antes da entrada de iniciais).
bool hi_qualifies(uint16_t score);

// Acesso a tabela em RAM.
const hi_table_t *hi_get(void);

#endif // PONG_HIGHSCORES_H
