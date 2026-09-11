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
// Um frame de silencio entre dois beeps seguidos, e o tom que espera por ele.
static volatile int gap_frames = 0;
static volatile int pend_freq = 0, pend_frames = 0;

static void silencia(void) {
    pwm_set_chan_level(audio_slice, audio_chan, 0);
}


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
    int frames = (ms * 60) / 1000;              // assumindo 60 fps
    if (frames < 1) frames = 1;

    // Com um tom ainda soando, corta agora e comeca o novo depois de um frame
    // de silencio. Sem essa pausa, dois eventos seguidos -- a bola bate na
    // parede e logo depois na raquete -- saem emendados num tom so, e o
    // segundo simplesmente nao acontece para quem esta ouvindo.
    if (beep_frames_remaining > 0 || gap_frames > 0) {
        silencia();
        beep_frames_remaining = 0;
        gap_frames  = 1;
        pend_freq   = freq_hz;
        pend_frames = frames;
        return;
    }
    set_frequency(freq_hz);
    beep_frames_remaining = frames;
}

void audio_tick_frame(void) {
    if (gap_frames > 0) {
        if (--gap_frames == 0) {
            set_frequency(pend_freq);
            beep_frames_remaining = pend_frames;
        }
        return;
    }
    if (beep_frames_remaining > 0 && --beep_frames_remaining == 0) silencia();
}

// A caixa nao toca nota grave curta. O alto-falante do gabinete rende pouco
// abaixo de uns 400 Hz, e um beep de 50 ms nessa regiao tem uma duzia de ciclos
// para se fazer ouvir -- na maquina real, as rebatidas em 226 Hz (as do Pong
// original) sumiram. Entao os sons CURTOS ficam na faixa que a caixa toca bem,
// separados por altura, e so o do ponto, que e longo, desce ao grave.
void audio_paddle_hit(void) { audio_beep(480, 70); }
void audio_wall_hit(void)   { audio_beep(640, 45); }
void audio_brick_hit(void)  { audio_beep(880, 30); }
// O ponto e o unico som grave, e e o mais longo: 300 ms dao tempo de a caixa
// mover ar de verdade. Nenhum outro som chega perto dessa nota.
void audio_score(void)      { audio_beep(150, 300); }
void audio_confirm(void)    { audio_beep(660, 90); }
void audio_attract_tick(void){ audio_beep(880, 20); }
