// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#include "highscores.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Usamos o ultimo setor da flash (256 KB - 4 KB = offset 0x3F000 numa pico de 2 MB).
// PICO_FLASH_SIZE_BYTES e definido pelo SDK / board file.
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
#define HI_FLASH_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

static hi_table_t table_ram;

static const hi_table_t *flash_table(void) {
    return (const hi_table_t *)(XIP_BASE + HI_FLASH_OFFSET);
}

static uint32_t calc_checksum(const hi_table_t *t) {
    uint32_t s = 0xA5A5A5A5u;
    const uint8_t *p = (const uint8_t *)t;
    size_t n = offsetof(hi_table_t, checksum);
    for (size_t i = 0; i < n; i++) s = (s * 131) + p[i];
    return s;
}

static void reset_table(void) {
    memset(&table_ram, 0, sizeof(table_ram));
    table_ram.magic   = HISCORE_MAGIC;
    table_ram.version = HISCORE_VERSION;
}

void hi_init(void) {
    reset_table();
}

void hi_load(void) {
    const hi_table_t *f = flash_table();
    if (f->magic == HISCORE_MAGIC && f->version == HISCORE_VERSION) {
        hi_table_t tmp = *f;
        if (tmp.checksum == calc_checksum(&tmp)) {
            table_ram = tmp;
            return;
        }
    }
    reset_table();
}

void __not_in_flash_func(hi_save)(void) {
    table_ram.magic    = HISCORE_MAGIC;
    table_ram.version  = HISCORE_VERSION;
    table_ram.checksum = calc_checksum(&table_ram);

    // Buffer alinhado para a flash (precisa de pelo menos FLASH_PAGE_SIZE = 256 B).
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &table_ram, sizeof(table_ram));

    // flash_range_erase + flash_range_program DEVEM ser feitos com IRQs desabilitadas.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(HI_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(HI_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

bool hi_qualifies(uint16_t score) {
    for (int i = 0; i < HISCORE_COUNT; i++) {
        if (score > table_ram.entries[i].score) return true;
    }
    return false;
}

bool hi_consider(uint16_t score, uint8_t player, uint8_t mode,
                 const char initials[INITIALS_LEN]) {
    int pos = -1;
    for (int i = 0; i < HISCORE_COUNT; i++) {
        if (score > table_ram.entries[i].score) { pos = i; break; }
    }
    if (pos < 0) return false;

    for (int i = HISCORE_COUNT - 1; i > pos; i--) {
        table_ram.entries[i] = table_ram.entries[i - 1];
    }
    table_ram.entries[pos].score  = score;
    table_ram.entries[pos].player = player;
    table_ram.entries[pos].mode   = mode;
    for (int k = 0; k < INITIALS_LEN; k++) {
        char c = initials[k];
        if (c < 'A' || c > 'Z') c = ' ';
        table_ram.entries[pos].initials[k] = c;
    }
    table_ram.entries[pos].initials[INITIALS_LEN] = 0;
    return true;
}

const hi_table_t *hi_get(void) {
    return &table_ram;
}
