// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_INPUT_H
#define PONG_INPUT_H

#include <stdint.h>
#include <stdbool.h>

void input_init(void);

// Le os dois potenciometros uma vez. Resultado em 0..4095, com low-pass simples.
void input_poll(void);

// Posicao do paddle 0..range, filtrada. 'range' e a folga vertical da fase
// (FB_HEIGHT menos a altura total ocupada pela raquete).
int input_paddle_y(int player, int range);

// Valor cru filtrado do pot 0..4095 (para entrada de iniciais).
int input_pot_raw(int player);

// Botao SELETOR pressionado neste frame (rising edge).
bool input_seletor_pressed(void);

// True se algum dos dois pots se mexeu nos ultimos N samples (para sair do attract).
bool input_movement_detected(void);

// Reseta o detector de movimento (chamar ao entrar em attract).
void input_reset_movement(void);

// Qual pot se mexeu por ultimo (0 = P1, 1 = P2). Usado pelo menu do attract,
// que aceita o pot de qualquer um dos dois jogadores.
int input_last_moved(void);

#endif // PONG_INPUT_H
