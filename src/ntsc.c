// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "ntsc.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

#include "ntsc.pio.h"

// =================================================================
// Framebuffer e contadores publicos
// =================================================================
uint32_t fb[FB_WORDS] __attribute__((aligned(4)));
volatile uint32_t ntsc_frame_count = 0;

// =================================================================
// Tabela de descritores de linha (uma entrada por scanline do frame).
// Construida em ntsc_init().
//   bit 0 = vsync line
//   bit 1 = active line
// =================================================================
static uint32_t line_descriptors[LINES_PER_FRAME];

// =================================================================
// Ponteiros usados pelos DMAs de "reload" (control channels).
// Em RAM (.data) para nao depender do XIP, que e suspenso durante writes
// na flash (highscores).
// =================================================================
static uint32_t *fb_base_ptr   = fb;
static uint32_t *desc_base_ptr = line_descriptors;

// =================================================================
// IDs de PIO / SM / DMA channels
// =================================================================
#define NTSC_PIO          pio0
#define SM_SYNC           0
#define SM_DATA           1

static int dma_sync_data;   // empurra descritores para sync SM
static int dma_sync_ctrl;   // reinicia dma_sync_data (loop infinito)
static int dma_pix_data;    // empurra pixel words para data SM
static int dma_pix_ctrl;    // reinicia dma_pix_data (loop infinito)

// =================================================================
// IRQ handler do DMA de control de descritores -- fira 1x por frame.
// =================================================================
static void __not_in_flash_func(ntsc_dma_irq_handler)(void) {
    if (dma_irqn_get_channel_status(0, dma_sync_ctrl)) {
        dma_irqn_acknowledge_channel(0, dma_sync_ctrl);
        ntsc_frame_count++;
    }
}

void ntsc_wait_vsync(void) {
    uint32_t f = ntsc_frame_count;
    while (ntsc_frame_count == f) {
        tight_loop_contents();
    }
}

// =================================================================
// Setup
// =================================================================
void ntsc_init(void) {
    // ----- 1. Preenche a tabela de descritores -----
    int i = 0;
    for (int n = 0; n < LINES_VSYNC; n++)      line_descriptors[i++] = 0b01;  // vsync
    for (int n = 0; n < LINES_TOP_BLANK; n++)  line_descriptors[i++] = 0b00;  // blank
    for (int n = 0; n < LINES_ACTIVE; n++)     line_descriptors[i++] = 0b10;  // active
    for (int n = 0; n < LINES_BOT_BLANK; n++)  line_descriptors[i++] = 0b00;  // blank

    // ----- 2. Limpa framebuffer -----
    for (int j = 0; j < FB_WORDS; j++) fb[j] = 0;

    // ----- 3. Carrega programas PIO -----
    uint sync_offset = pio_add_program(NTSC_PIO, &ntsc_sync_program);
    uint data_offset = pio_add_program(NTSC_PIO, &ntsc_data_program);

    // ----- 4. Configura GPIOs -----
    pio_gpio_init(NTSC_PIO, NTSC_SYNC_PIN);
    pio_gpio_init(NTSC_PIO, NTSC_VIDEO_PIN);
    pio_sm_set_consecutive_pindirs(NTSC_PIO, SM_SYNC, NTSC_SYNC_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(NTSC_PIO, SM_DATA, NTSC_VIDEO_PIN, 1, true);

    // Drive strength maximo (12 mA) + slew rate rapido nos pinos do DAC.
    // O GPIO do RP2040 tem ~40-50 ohm de impedancia de saida no default
    // (4 mA), o que abaixa os niveis do DAC Thevenin (branco fica < 1,10 V).
    // Subir para 12 mA reduz essa impedancia e aproxima os niveis do
    // calculado, deixando preto/branco mais consistentes e a imagem mais
    // nitida. (Insight do projeto obstruse/pico-composite8.)
    gpio_set_drive_strength(NTSC_SYNC_PIN,  GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(NTSC_VIDEO_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(NTSC_SYNC_PIN,  GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(NTSC_VIDEO_PIN, GPIO_SLEW_RATE_FAST);

    // ----- 5. Configura sync SM -----
    {
        pio_sm_config c = ntsc_sync_program_get_default_config(sync_offset);
        sm_config_set_sideset_pins(&c, NTSC_SYNC_PIN);
        sm_config_set_out_shift(&c, true /*shift right*/, false /*no autopull*/, 32);
        sm_config_set_in_shift(&c, false, false, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        // clkdiv = 147.118 -> linha de 54 ciclos casa com 63.555 us de NTSC.
        // (54 ciclos em vez de 27 para o H-sync ficar em 4.71 us, padrao.)
        sm_config_set_clkdiv(&c, 147.118f);
        pio_sm_init(NTSC_PIO, SM_SYNC, sync_offset, &c);
    }

    // ----- 6. Configura data SM -----
    {
        pio_sm_config c = ntsc_data_program_get_default_config(data_offset);
        sm_config_set_out_pins(&c, NTSC_VIDEO_PIN, 1);
        sm_config_set_set_pins(&c, NTSC_VIDEO_PIN, 1);
        // MSB primeiro: shift left
        sm_config_set_out_shift(&c, false /*shift left*/, true /*autopull*/, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        sm_config_set_clkdiv(&c, 4.0f);
        pio_sm_init(NTSC_PIO, SM_DATA, data_offset, &c);
    }

    // ----- 7. Aloca DMA channels -----
    dma_sync_data = dma_claim_unused_channel(true);
    dma_sync_ctrl = dma_claim_unused_channel(true);
    dma_pix_data  = dma_claim_unused_channel(true);
    dma_pix_ctrl  = dma_claim_unused_channel(true);

    // ----- 8. DMA: descritores -> sync SM TX FIFO -----
    //   data channel: 262 transfers, paced by sync SM DREQ, depois encadeia o ctrl
    {
        dma_channel_config c = dma_channel_get_default_config(dma_sync_data);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(NTSC_PIO, SM_SYNC, true));
        channel_config_set_chain_to(&c, dma_sync_ctrl);
        dma_channel_configure(dma_sync_data, &c,
                              &NTSC_PIO->txf[SM_SYNC],
                              line_descriptors,
                              LINES_PER_FRAME,
                              false);
    }
    //   ctrl channel: reescreve o read_addr do data channel e o re-aciona
    {
        dma_channel_config c = dma_channel_get_default_config(dma_sync_ctrl);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, false);
        dma_channel_configure(dma_sync_ctrl, &c,
                              &dma_hw->ch[dma_sync_data].al3_read_addr_trig,
                              &desc_base_ptr,
                              1,
                              false);
    }

    // ----- 9. DMA: pixel words -> data SM TX FIFO -----
    {
        dma_channel_config c = dma_channel_get_default_config(dma_pix_data);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(NTSC_PIO, SM_DATA, true));
        channel_config_set_chain_to(&c, dma_pix_ctrl);
        dma_channel_configure(dma_pix_data, &c,
                              &NTSC_PIO->txf[SM_DATA],
                              fb,
                              FB_WORDS,
                              false);
    }
    {
        dma_channel_config c = dma_channel_get_default_config(dma_pix_ctrl);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, false);
        dma_channel_configure(dma_pix_ctrl, &c,
                              &dma_hw->ch[dma_pix_data].al3_read_addr_trig,
                              &fb_base_ptr,
                              1,
                              false);
    }

    // ----- 10. IRQ uma vez por frame (no ctrl do sync) -----
    dma_irqn_set_channel_enabled(0, dma_sync_ctrl, true);
    irq_set_exclusive_handler(DMA_IRQ_0, ntsc_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // ----- 11. Inicia tudo -----
    //  Ordem: starta os DMAs antes dos SMs para que as FIFOs ja tenham dados.
    dma_channel_start(dma_pix_data);
    dma_channel_start(dma_sync_data);

    pio_sm_set_enabled(NTSC_PIO, SM_DATA, true);
    pio_sm_set_enabled(NTSC_PIO, SM_SYNC, true);
}
