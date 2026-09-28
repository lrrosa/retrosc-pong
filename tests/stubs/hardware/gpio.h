#pragma once
#include <stdbool.h>
#define GPIO_OUT 1
#define GPIO_IN 0
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool out);
void gpio_put(unsigned pin, bool value);
void gpio_pull_up(unsigned pin);
bool gpio_get(unsigned pin);
