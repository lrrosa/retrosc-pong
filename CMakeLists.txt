# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pong-rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pong-rp2040
    src/main.c
    src/ntsc.c
    src/gfx.c
    src/font.c
    src/input.c
    src/audio.c
    src/game.c
    src/phases.c
    src/highscores.c
    src/assets.c
)

pico_generate_pio_header(pong-rp2040 ${CMAKE_CURRENT_LIST_DIR}/src/ntsc.pio)

target_include_directories(pong-rp2040 PRIVATE src)

target_link_libraries(pong-rp2040
    pico_stdlib
    pico_rand
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_adc
    hardware_irq
    hardware_clocks
    hardware_flash
    hardware_sync
)

pico_enable_stdio_uart(pong-rp2040 0)
pico_enable_stdio_usb(pong-rp2040 1)

# Cria .uf2, .bin, .hex, etc.
pico_add_extra_outputs(pong-rp2040)
