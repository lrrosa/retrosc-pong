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
#define BUTTON_STABLE_FRAMES 2         // confirma pressao e soltura sem repiques

static uint16_t raw[2]      = { 2048, 2048 };
static uint16_t filtered[2] = { 2048, 2048 };
static uint32_t filter_q8[2];          // preserva as fracoes do filtro IIR
static uint16_t baseline[2] = { 2048, 2048 };
static int moved_frames = 0;
static int last_moved_player = 0;
static bool last_button = false;
static bool button_edge = false;
static unsigned button_samples;

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
    last_button = !gpio_get(SELETOR_BUTTON_PIN);
    button_samples = 0;
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

    bool now = !gpio_get(SELETOR_BUTTON_PIN);   // pull-up: pressed = LOW
    button_edge = false;
    if (now == last_button) {
        button_samples = 0;
    } else if (++button_samples >= BUTTON_STABLE_FRAMES) {
        last_button = now;
        button_samples = 0;
        button_edge = now;
    }
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
