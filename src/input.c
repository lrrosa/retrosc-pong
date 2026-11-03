// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "input.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

#define FILTER_ALPHA  3                 // suaviza: novo = (3*ant + novo)/4
#define MOVE_THRESH   40                // delta minimo (em unidades ADC) p/ "moveu"
#define MOVE_HISTORY  20                // janela de frames para detectar movimento
// Debounce do SELETOR, em tres camadas: maioria dentro do frame
// (button_read_majority), confirmacao ao longo de varios frames e lockout
// depois de uma borda aceita (as duas ultimas em input_poll).
#define BUTTON_SAMPLES        8         // leituras do pino por frame
#define BUTTON_SAMPLE_GAP_US  71        // ~1,125 linha de video entre leituras
#define BUTTON_MAJORITY       5         // leituras baixas para o frame contar premido
#define BUTTON_STABLE_FRAMES  3         // frames confirmando pressao e soltura
#define BUTTON_LOCKOUT_FRAMES 6         // frames sem reavaliar apos uma borda aceita

static uint16_t raw[2]      = { 2048, 2048 };
static uint16_t filtered[2] = { 2048, 2048 };
static uint32_t filter_q8[2];          // preserva as fracoes do filtro IIR
static uint16_t baseline[2] = { 2048, 2048 };
static int moved_frames = 0;
static int last_moved_player = 0;
static bool last_button = false;
static bool button_edge = false;
static unsigned button_samples;
static unsigned button_lockout;
static bool button_frame_state;        // resultado da ultima rajada de leituras

// Estado do botao, decidido por MAIORIA de varias leituras.
//
// Uma leitura unica por frame nao filtra ruido nenhum: input_poll() roda sempre
// no mesmo ponto do quadro (logo depois de ntsc_wait_vsync(), que e justamente
// quando o PIO e o DMA reiniciam o frame), entao ruido periodico vindo do
// proprio sinal composto ou do PWM do audio cai sempre na MESMA fase -- sai
// igual em todos os frames, e o debounce por frames passa por cima dele sem
// ver. Era o bastante para o jogo se comportar como se o SELETOR estivesse
// sendo premido sozinho, sendo que o botao estava bom.
//
// O remedio e varrer o periodo de linha (63,5 us) em vez de espiar um ponto
// dele: BUTTON_SAMPLE_GAP_US e 1,125 linha, ou seja, cada leitura cai 1/8 de
// linha adiante da anterior e as 8 cobrem a linha inteira em fases diferentes.
// Custa 7*71 us = 497 us, 3% do frame -- gastos no fim dele, nao no comeco.
//
// Isso cobre o ruido de linha e o do audio. Ruido travado na frequencia do
// FRAME (os 60 Hz da rede, por exemplo) cai na mesma fase de qualquer maneira,
// e ai o remedio e hardware: pull-up externo de 4,7 kOhm em paralelo com os
// ~60 kOhm internos, com 100 nF para GND (ver docs/schematic.md).
//
// Quem chama e input_sample_button(), no FIM do frame -- nao aqui, no comeco.
static bool button_read_majority(void) {
    unsigned low = 0;
    for (unsigned i = 0; i < BUTTON_SAMPLES; i++) {
        if (i) busy_wait_us_32(BUTTON_SAMPLE_GAP_US);
        if (!gpio_get(SELETOR_BUTTON_PIN)) low++;   // pull-up: premido = LOW
    }
    return low >= BUTTON_MAJORITY;
}

void input_init(void) {
    // Forca o SMPS da placa Pico em modo PWM (GPIO23 alto) para reduzir o
    // ripple no supply do ADC -> leituras dos pots mais limpas. Inofensivo em
    // clones onde esse pino nao tem essa funcao. (Datasheet do Pico, sec. 4.3.)
    gpio_init(PICO_SMPS_PS_PIN);
    gpio_set_dir(PICO_SMPS_PS_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_PS_PIN, 1);

    adc_init();
    adc_gpio_init(POT_P1_GPIO);
    adc_gpio_init(POT_P2_GPIO);

    gpio_init(SELETOR_BUTTON_PIN);
    gpio_set_dir(SELETOR_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(SELETOR_BUTTON_PIN);

    // primeira leitura para "armar" baseline
    adc_select_input(POT_P1_ADC); raw[0] = filtered[0] = baseline[0] = adc_read();
    adc_select_input(POT_P2_ADC); raw[1] = filtered[1] = baseline[1] = adc_read();
    for (int i = 0; i < 2; i++) filter_q8[i] = (uint32_t)filtered[i] * 256;
    moved_frames = 0;
    last_moved_player = 0;
    button_frame_state = button_read_majority();
    last_button = button_frame_state;
    button_samples = 0;
    button_lockout = 0;
    button_edge = false;
}

void input_poll(void) {
    adc_select_input(POT_P1_ADC); raw[0] = adc_read();
    adc_select_input(POT_P2_ADC); raw[1] = adc_read();

    for (int i = 0; i < 2; i++) {
        filter_q8[i] = (filter_q8[i] * FILTER_ALPHA + (uint32_t)raw[i] * 256) /
                       (FILTER_ALPHA + 1);
        filtered[i] = (filter_q8[i] + 128) / 256;
        int delta = (int)filtered[i] - (int)baseline[i];
        if (delta < 0) delta = -delta;
        if (delta > MOVE_THRESH) {
            moved_frames = MOVE_HISTORY;
            last_moved_player = i;
            baseline[i] = filtered[i];
        }
    }
    if (moved_frames > 0) moved_frames--;

    bool now = button_frame_state;
    button_edge = false;
    if (button_lockout > 0) {
        // Borda aceita agora mesmo: nao reavalia nada por alguns frames. Sem
        // isso um eco de ruido logo depois da pressao (soltura falsa seguida de
        // pressao falsa) virava uma segunda borda em 2*BUTTON_STABLE_FRAMES, e
        // um toque so pulava duas telas. 6 frames de lockout mais os 3 da
        // confirmacao poem o rearme em 150 ms -- metade do intervalo de quem
        // aperta rapido, e a tela de iniciais pede tres toques seguidos.
        button_lockout--;
        button_samples = 0;
    } else if (now == last_button) {
        button_samples = 0;
    } else if (++button_samples >= BUTTON_STABLE_FRAMES) {
        last_button = now;
        button_samples = 0;
        button_edge = now;
        if (now) button_lockout = BUTTON_LOCKOUT_FRAMES;
    }
}

// A rajada de leituras do botao, chamada no FIM do frame (main.c), depois de
// game_frame(). Ela para a CPU por (BUTTON_SAMPLES-1)*BUTTON_SAMPLE_GAP_US = 497 us,
// e no comeco do frame esse tempo e caro: entre o vsync e a primeira linha ativa
// cabem so (LINES_VSYNC + LINES_TOP_BLANK) * 63,5 us = 2,4 ms, que e a margem
// que o desenho tem para ficar a frente do feixe do DMA. No fim do frame sobram
// ~14 ms ociosos. O preco e a borda ser decidida no input_poll() do frame
// seguinte: um frame de latencia, 17 ms, que ninguem sente num push-button.
void input_sample_button(void) {
    button_frame_state = button_read_majority();
}

int input_pot_raw(int player) {
    return filtered[player & 1];
}

int input_paddle_y(int player, int range) {
    uint16_t v = filtered[player & 1];
    if (range < 0) range = 0;
    int y = (v * range) / 4095;
    if (y < 0) y = 0;
    if (y > range) y = range;
    return y;
}

bool input_seletor_pressed(void) {
    return button_edge;
}

bool input_movement_detected(void) {
    return moved_frames > 0;
}

int input_last_moved(void) {
    return last_moved_player;
}

void input_reset_movement(void) {
    moved_frames = 0;
    baseline[0] = filtered[0];
    baseline[1] = filtered[1];
}
