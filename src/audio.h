// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_AUDIO_H
#define PONG_AUDIO_H

#include <stdint.h>

// Inicializa PWM no AUDIO_PIN e o sample timer.
void audio_init(void);

// Toca um beep de onda quadrada em 'freq_hz' por 'ms' milissegundos.
// Chamadas posteriores cancelam beeps em andamento (sem mixing).
void audio_beep(int freq_hz, int ms);

// Atalhos para os sons do Pong.
void audio_paddle_hit(void);   // grave curto
void audio_wall_hit(void);     // medio curto
void audio_score(void);        // agudo longo
void audio_brick_hit(void);    // tijolo quebrado (fases com barreira)
void audio_confirm(void);      // confirmacao no menu do attract
void audio_attract_tick(void); // beep discreto para attract mode

// Chamar uma vez por frame para gerenciar duracao.
void audio_tick_frame(void);

#endif // PONG_AUDIO_H
