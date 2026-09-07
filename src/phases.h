// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_PHASES_H
#define PONG_PHASES_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

// As fases valem para os dois modos (arcade e versus): elas mudam so o
// "cenario" (raquetes, obstaculos, bichos), nunca as regras de pontuacao.
// A ordem e a progressao de dificuldade -- a BARREIRA III, que e a mais dura
// e a mais demorada, fecha a sequencia.
typedef enum {
    PHASE_CLASSICO = 0,   // pong normal
    PHASE_TRIPLO,         // 3 raquetes pequenas por jogador
    PHASE_NAVE,           // a nave atira e encolhe a raquete atingida
    PHASE_BARREIRA1,      // 2 muros de tijolos no meio
    PHASE_PINBALL,        // obstaculos fixos no meio da quadra
    PHASE_BARREIRA2,      // 3 muros espacados
    PHASE_COLUNA,         // os mesmos obstaculos, subindo e descendo
    PHASE_MURALHA,        // 1 muro atras de cada raquete
    PHASE_REBOUND,        // volei: raquetes deitadas, bola com gravidade
    PHASE_BARREIRA3,      // 4 muros (2 por jogador): a mais dura, por ultimo
    PHASE_COUNT
} phase_id_t;

// Retangulo em pixels (raquetes, obstaculos, sprites).
typedef struct { int x, y, w, h; } rect_t;

#define PADDLE_SEG_MAX 3

// Como a fase muda as regras de bordas/fisica e o que ela solta na quadra.
#define PF_NO_CENTER_LINE  (1u << 0)  // a fase ja tem divisor proprio
#define PF_GRAVITY         (1u << 1)  // a bola cai (Rebound)
#define PF_FLOOR_SCORES    (1u << 2)  // encostar no chao da ponto ao outro lado
#define PF_SIDE_WALLS      (1u << 3)  // laterais rebatem em vez de valer gol
#define PF_PADDLE_HORIZ    (1u << 4)  // raquetes deitadas, pot anda no eixo X
#define PF_TEM_BONUS       (1u << 5)  // o mascote-bonus pode cruzar esta fase

uint32_t phase_flags(void);

// Nome curto e dica de uma fase (para a tela de intro). Aceita qualquer indice.
const char *phase_name(int idx);
const char *phase_hint(int idx);

// Entra na fase: reconstroi tijolos, bichos e obstaculos.
void phase_begin(int idx);

// Comeco de cada ponto. Fases que rearmam os tijolos a cada round (MURALHA)
// e os efeitos temporarios (raquete encolhida) voltam ao normal aqui.
void phase_round_reset(void);

int  phase_current(void);

// True se o mascote-bonus pode cruzar essa fase. Serve tambem para o jogo
// calcular quantos pontos ainda estao em disputa nas fases que faltam.
bool phase_tem_bonus(int idx);

// Curso do pot nesta fase: 0..phase_paddle_range(). Em fase normal e a folga
// vertical (FB_HEIGHT - altura da raquete); no Rebound e o curso horizontal
// dentro da meia-quadra.
int  phase_paddle_range(void);

// Pedacos da raquete do jogador para a posicao 'pos' lida do pot.
// Devolve quantos pedacos foram escritos em out[] (<= PADDLE_SEG_MAX).
int  phase_paddle_segments(int player, int pos, rect_t *out);

// Posicao de onde a bola sai no saque; 'dir' e -1 (para P1) ou +1.
int  phase_serve_x(int dir);
int  phase_serve_y(void);

// Colisao da bola com o que a fase poe na quadra: tijolos (que somem) e
// solidos (bumpers do pinball, coluna movel, rede do Rebound). Posicoes e
// velocidades em Q8; prev_x/prev_y sao as do frame anterior, para saber o lado
// da entrada. Trata no maximo uma colisao por frame e devolve true se houve.
bool phase_ball_collide(int32_t prev_x, int32_t prev_y,
                        int32_t *bx, int32_t *by,
                        int32_t *vx, int32_t *vy);

// Um frame das partes moveis da fase (mascote-bonus, nave, tiros, coluna).
// Recebe a bola e as raquetes ja atualizadas; devolve em bonus[] os pontos
// extras ganhos neste frame por cada jogador. 'last_hitter' e 0/1 (ou -1 se a
// bola ainda nao foi rebatida) e diz quem leva o bonus do mascote.
void phase_update(int32_t ball_x, int32_t ball_y, const int paddle_pos[2],
                  int last_hitter, int bonus[2]);

// Desenha o que e da fase (tijolos, obstaculos, nave, mascote, tiros).
void phase_draw(void);

#endif // PONG_PHASES_H
