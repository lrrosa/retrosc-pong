#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define __not_in_flash_func(name) name
void busy_wait_us_32(uint32_t us);
