#pragma once
#include <stdint.h>
#include <stddef.h>
#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE 256
#define PICO_FLASH_SIZE_BYTES FLASH_SECTOR_SIZE
extern uint8_t test_flash[FLASH_SECTOR_SIZE];
#define XIP_BASE ((uintptr_t)test_flash)
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
