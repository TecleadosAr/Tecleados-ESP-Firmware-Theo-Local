#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    ESP_EXT1_WAKEUP_ANY_LOW  = 0,
    ESP_EXT1_WAKEUP_ANY_HIGH = 1,
    ESP_EXT1_WAKEUP_ALL_LOW  = 2,
} esp_sleep_ext1_wakeup_mode_t;

static bool _mock_gpio_wakeup_enabled = false;
static uint64_t _mock_ext1_wakeup_mask = 0;
static esp_sleep_ext1_wakeup_mode_t _mock_ext1_wakeup_mode = 0;
static int _mock_light_sleep_enter_count = 0;
static int _mock_deep_sleep_enter_count = 0;

static inline void mock_sleep_reset(void) {
    _mock_gpio_wakeup_enabled = false;
    _mock_ext1_wakeup_mask = 0;
    _mock_ext1_wakeup_mode = 0;
    _mock_light_sleep_enter_count = 0;
    _mock_deep_sleep_enter_count = 0;
}

static inline esp_err_t esp_sleep_enable_gpio_wakeup(void) {
    _mock_gpio_wakeup_enabled = true;
    return ESP_OK;
}

static inline esp_err_t esp_sleep_disable_wakeup_source(int source) {
    (void)source;
    return ESP_OK;
}

static inline esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) {
    _mock_ext1_wakeup_mask = mask;
    _mock_ext1_wakeup_mode = mode;
    return ESP_OK;
}

static inline esp_err_t esp_light_sleep_start(void) {
    _mock_light_sleep_enter_count++;
    return ESP_OK;
}

static inline void esp_deep_sleep_start(void) {
    _mock_deep_sleep_enter_count++;
}
