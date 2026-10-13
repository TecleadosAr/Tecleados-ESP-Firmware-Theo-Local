#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef void* adc_cali_handle_t;

static inline esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage) {
    (void)handle;
    if (voltage) {
        /* Approximate calibration: 4095 raw -> ~3100 mV */
        *voltage = (raw * 3100) / 4095;
    }
    return ESP_OK;
}

static inline esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
