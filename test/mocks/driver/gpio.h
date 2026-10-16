#pragma once
#include "Arduino.h"
typedef int gpio_num_t;
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN, GPIO_FLOATING } gpio_pull_mode_t;
typedef int esp_err_t;
#define ESP_OK 0
inline int gpio_get_level(gpio_num_t p) { return mock::gpio_level[p]; }
inline esp_err_t gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) { return ESP_OK; }
