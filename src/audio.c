// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "audio.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"

// Geramos onda quadrada alternando o duty entre 0 e ~50% a cada meio-periodo.
// Atualizado via IRQ do timer hardware do PWM (wrap IRQ).
// Para isso usamos o PWM em modo "tom": configuramos clkdiv e wrap para gerar
// a frequencia desejada, e fixamos duty cycle em 50% (level = TOP/2).
// Para beeps com volume audivel via filtro RC + amp, isto funciona simples.
//
// O 50% duty cycle no PWM produz uma onda quadrada na frequencia f = sysclk/(div*TOP).
// Filtramos pelo RC do hardware externo.

static int audio_slice;
static int audio_chan;
static volatile int beep_frames_remaining = 0;

void audio_init(void) {
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    audio_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    audio_chan  = pwm_gpio_to_channel(AUDIO_PIN);

    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, 1.0f);
    pwm_config_set_wrap(&c, PWM_TOP);
    pwm_init(audio_slice, &c, false);
    pwm_set_chan_level(audio_slice, audio_chan, 0);
    pwm_set_enabled(audio_slice, true);
}

static void set_frequency(int freq_hz) {
    if (freq_hz <= 0) {
        pwm_set_chan_level(audio_slice, audio_chan, 0);
        return;
    }
    uint32_t sysclk = clock_get_hz(clk_sys);
    // queremos sysclk / (div * TOP) = freq -> div = sysclk / (freq * TOP)
    float div = (float)sysclk / ((float)freq_hz * (float)(PWM_TOP + 1));
    if (div < 1.0f) div = 1.0f;
    if (div > 255.0f) div = 255.0f;
    pwm_set_clkdiv(audio_slice, div);
    pwm_set_wrap(audio_slice, PWM_TOP);
    pwm_set_chan_level(audio_slice, audio_chan, (PWM_TOP + 1) / 2);  // 50% duty
}

void audio_beep(int freq_hz, int ms) {
    set_frequency(freq_hz);
    // assumindo 60 fps
    beep_frames_remaining = (ms * 60) / 1000;
    if (beep_frames_remaining < 1) beep_frames_remaining = 1;
}

void audio_tick_frame(void) {
    if (beep_frames_remaining > 0) {
        if (--beep_frames_remaining == 0) {
            pwm_set_chan_level(audio_slice, audio_chan, 0);
        }
    }
}

// Sons do Pong (frequencias do Pong original eram 246 Hz / 226 Hz / 246 Hz aprox).
void audio_paddle_hit(void) { audio_beep(226, 50); }
void audio_wall_hit(void)   { audio_beep(246, 40); }
void audio_score(void)      { audio_beep(490, 250); }
void audio_brick_hit(void)  { audio_beep(660, 30); }
void audio_confirm(void)    { audio_beep(660, 90); }
void audio_attract_tick(void){ audio_beep(880, 20); }
