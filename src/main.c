// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "config.h"
#include "ntsc.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "highscores.h"
#include "game.h"

int main(void) {
    stdio_init_all();

    // Forca clock conhecido. Default = 125 MHz, e isto bate com a clkdiv do PIO.
    set_sys_clock_khz(125000, true);

    // Subsistemas
    ntsc_init();
    input_init();
    audio_init();
    hi_init();
    hi_load();
    game_init();

    while (true) {
        ntsc_wait_vsync();
        input_poll();
        audio_tick_frame();
        game_frame();
    }
    return 0;
}
