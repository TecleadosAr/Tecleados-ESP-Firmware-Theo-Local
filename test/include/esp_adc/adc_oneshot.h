#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef void* adc_oneshot_unit_handle_t;

typedef enum {
    ADC_UNIT_1 = 1,
    ADC_UNIT_2 = 2,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
    ADC_CHANNEL_8,
    ADC_CHANNEL_9,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_12  = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
    ADC_BITWIDTH_9       = 9,
    ADC_BITWIDTH_10      = 10,
    ADC_BITWIDTH_11      = 11,
    ADC_BITWIDTH_12      = 12,
} adc_bitwidth_t;

typedef struct {
    adc_unit_t unit_id;
    uint32_t   clk_src;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t    atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

static inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                                             adc_oneshot_unit_handle_t *ret_unit) {
    (void)init_config;
    if (ret_unit) *ret_unit = (void*)0x1001;
    return ESP_OK;
}

static inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle,
                                                   adc_channel_t channel,
                                                   const adc_oneshot_chan_cfg_t *config) {
    (void)handle; (void)channel; (void)config;
    return ESP_OK;
}

static int _mock_adc_raw_value = 2774; /* ~2100 mV @ 12-bit DB_12 -> 4200 mV battery */

static inline void mock_adc_set_raw(int raw) {
    _mock_adc_raw_value = raw;
}

static inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle,
                                         adc_channel_t channel,
                                         int *out_raw) {
    (void)handle; (void)channel;
    if (out_raw) *out_raw = _mock_adc_raw_value;
    return ESP_OK;
}

static inline esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
