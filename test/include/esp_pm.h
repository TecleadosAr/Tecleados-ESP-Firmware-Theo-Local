#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int  max_freq_mhz;
    int  min_freq_mhz;
    bool light_sleep_enable;
} esp_pm_config_t;

typedef enum {
    ESP_PM_CPU_FREQ_MAX,
    ESP_PM_APB_FREQ_MAX,
    ESP_PM_NO_LIGHT_SLEEP,
} esp_pm_lock_type_t;

typedef void* esp_pm_lock_handle_t;

static esp_pm_config_t _mock_pm_config = {0};
static int _mock_pm_configured_count = 0;
static int _mock_pm_lock_acquire_count = 0;
static int _mock_pm_lock_release_count = 0;
static bool _mock_pm_lock_held = false;

static inline void mock_pm_reset(void) {
    _mock_pm_config = (esp_pm_config_t){0};
    _mock_pm_configured_count = 0;
    _mock_pm_lock_acquire_count = 0;
    _mock_pm_lock_release_count = 0;
    _mock_pm_lock_held = false;
}

static inline esp_err_t esp_pm_configure(const esp_pm_config_t *config) {
    if (config) {
        _mock_pm_config = *config;
        _mock_pm_configured_count++;
    }
    return ESP_OK;
}

static inline esp_err_t esp_pm_get_configuration(esp_pm_config_t *config) {
    if (config) {
        *config = _mock_pm_config;
    }
    return ESP_OK;
}

static inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type,
                                           int arg,
                                           const char* name,
                                           esp_pm_lock_handle_t* out_handle) {
    (void)lock_type; (void)arg; (void)name;
    if (out_handle) *out_handle = (void*)0x3003;
    return ESP_OK;
}

static inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
    (void)handle;
    _mock_pm_lock_acquire_count++;
    _mock_pm_lock_held = true;
    return ESP_OK;
}

static inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
    (void)handle;
    _mock_pm_lock_release_count++;
    _mock_pm_lock_held = false;
    return ESP_OK;
}

static inline esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
