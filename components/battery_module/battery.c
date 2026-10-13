#include "battery.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"

#include "event_bus.h"
#include "blemod.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#ifdef TAG
#undef TAG
#endif
#define TAG "BATTERY"

/* Default configuration fallbacks if not defined by Kconfig */
#ifndef CONFIG_TEF_BATTERY_ENABLE
#define CONFIG_TEF_BATTERY_ENABLE 1
#endif

#ifndef CONFIG_TEF_BATTERY_ADC_GPIO
#define CONFIG_TEF_BATTERY_ADC_GPIO -1
#endif

#ifndef CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE
#define CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE 2000
#endif

#ifndef CONFIG_TEF_BATTERY_MIN_MV
#define CONFIG_TEF_BATTERY_MIN_MV 3300
#endif

#ifndef CONFIG_TEF_BATTERY_MAX_MV
#define CONFIG_TEF_BATTERY_MAX_MV 4200
#endif

#ifndef CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC
#define CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC 60
#endif

#ifndef CONFIG_TEF_BATTERY_DELTA_NOTIFY_PCT
#define CONFIG_TEF_BATTERY_DELTA_NOTIFY_PCT 2
#endif

/* Weak stub for ble_hid_notify_battery_all so battery_module can link
 * independently in test harnesses where blemod is omitted. */
__attribute__((weak)) void ble_hid_notify_battery_all(uint8_t level) {
    (void)level;
}

/* Internal driver state */
static bool                      s_batt_initialized       = false;
static adc_oneshot_unit_handle_t s_batt_adc_handle        = NULL;
static adc_cali_handle_t         s_batt_cali_handle       = NULL;
static adc_channel_t             s_batt_adc_chan          = ADC_CHANNEL_6;
static bool                      s_batt_adc_chan_valid    = false;

static uint32_t                  s_batt_last_voltage_mv   = 0;
static uint8_t                   s_batt_filtered_pct      = 0xFF;
static uint8_t                   s_batt_last_notified_pct = 0xFF;
static int64_t                   s_batt_last_notify_us    = 0;
static bool                      s_batt_is_charging       = false;
static uint32_t                  s_batt_mock_voltage_mv   = 0;
static esp_timer_handle_t        s_batt_timer             = NULL;

static void battery_timer_cb(void *arg) {
    (void)arg;
    battery_poll_and_notify();
}

/* LiPo discharge curve LUT */
typedef struct {
    uint32_t mv;
    uint8_t  pct;
} batt_lut_entry_t;

static const batt_lut_entry_t s_batt_lut[] = {
    { 4200, 100 },
    { 4050,  90 },
    { 3950,  75 },
    { 3850,  50 },
    { 3750,  30 },
    { 3650,  15 },
    { 3500,   5 },
    { 3300,   0 }
};
static const size_t s_batt_lut_count = sizeof(s_batt_lut) / sizeof(s_batt_lut[0]);

/* Map ESP32-S3 GPIO to ADC1 Channel */
static bool gpio_to_adc1_channel(int gpio, adc_channel_t *out_chan) {
    if (gpio < 1 || gpio > 10) return false;
    *out_chan = (adc_channel_t)(gpio - 1);
    return true;
}

uint8_t battery_voltage_to_pct(uint32_t mv) {
    if (mv >= s_batt_lut[0].mv) return 100;
    if (mv <= s_batt_lut[s_batt_lut_count - 1].mv) return 0;

    for (size_t i = 0; i < s_batt_lut_count - 1; i++) {
        if (mv <= s_batt_lut[i].mv && mv >= s_batt_lut[i + 1].mv) {
            uint32_t v_high = s_batt_lut[i].mv;
            uint32_t v_low  = s_batt_lut[i + 1].mv;
            uint32_t p_high = s_batt_lut[i].pct;
            uint32_t p_low  = s_batt_lut[i + 1].pct;

            uint32_t pct = p_low + ((mv - v_low) * (p_high - p_low)) / (v_high - v_low);
            return (uint8_t)(pct > 100 ? 100 : pct);
        }
    }
    return 0;
}

void battery_set_mock_voltage_mv(uint32_t mv) {
    s_batt_mock_voltage_mv = mv;
    if (mv > 0) {
        /* Force immediate update for unit testing */
        battery_poll_and_notify();
    }
}

void battery_test_reset(void) {
    s_batt_filtered_pct      = 0xFF;
    s_batt_last_notified_pct = 0xFF;
    s_batt_last_voltage_mv   = 0;
    s_batt_last_notify_us    = 0;
    s_batt_is_charging       = false;
    s_batt_mock_voltage_mv   = 0;
    s_batt_initialized       = false;
    if (s_batt_timer) {
        esp_timer_stop(s_batt_timer);
        esp_timer_delete(s_batt_timer);
        s_batt_timer = NULL;
    }
}

static uint32_t read_battery_voltage_mv_internal(void) {
    if (s_batt_mock_voltage_mv > 0) {
        return s_batt_mock_voltage_mv;
    }

    if (!s_batt_adc_chan_valid || s_batt_adc_handle == NULL) {
        return 0;
    }

    /* 5-sample median filter to eliminate RF transmit current transient dips */
    int samples[5];
    for (int i = 0; i < 5; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_batt_adc_handle, s_batt_adc_chan, &raw);
        if (err != ESP_OK) {
            raw = 0;
        }
        samples[i] = raw;
    }

    /* Simple insertion sort for 5 elements */
    for (int i = 1; i < 5; i++) {
        int key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j = j - 1;
        }
        samples[j + 1] = key;
    }
    int median_raw = samples[2];

    int pin_mv = 0;
    if (s_batt_cali_handle) {
        adc_cali_raw_to_voltage(s_batt_cali_handle, median_raw, &pin_mv);
    } else {
        /* Default approximation if calibration scheme not available */
        pin_mv = (median_raw * 3100) / 4095;
    }

    if (pin_mv < 0) pin_mv = 0;

    uint32_t batt_mv = ((uint32_t)pin_mv * (uint32_t)CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE) / 1000U;
    return batt_mv;
}

esp_err_t battery_init(void) {
    if (s_batt_initialized) return ESP_OK;

#if !CONFIG_TEF_BATTERY_ENABLE
    ESP_LOGI(TAG, "Battery monitoring disabled by configuration");
    s_batt_initialized = true;
    return ESP_OK;
#endif

    int adc_gpio = CONFIG_TEF_BATTERY_ADC_GPIO;
    if (adc_gpio < 0) {
        ESP_LOGI(TAG, "Battery ADC pin disabled (GPIO %d)", adc_gpio);
        s_batt_initialized = true;
        return ESP_OK;
    }

    if (!gpio_to_adc1_channel(adc_gpio, &s_batt_adc_chan)) {
        ESP_LOGW(TAG, "GPIO %d is not a valid ADC1 pin on ESP32-S3", adc_gpio);
        s_batt_initialized = true;
        return ESP_ERR_INVALID_ARG;
    }
    s_batt_adc_chan_valid = true;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = 0,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_batt_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC1 unit: %s", esp_err_to_name(err));
        s_batt_initialized = true;
        return err;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(s_batt_adc_handle, s_batt_adc_chan, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC1 channel %d: %s", (int)s_batt_adc_chan, esp_err_to_name(err));
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT_1,
        .chan     = s_batt_adc_chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_batt_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Curve fitting calibration unavailable: %s (using linear fallback)", esp_err_to_name(err));
        s_batt_cali_handle = NULL;
    }

    s_batt_initialized = true;
    ESP_LOGI(TAG, "Battery driver initialized on GPIO %d (ADC1_CH%d, divider permille %d)",
             adc_gpio, (int)s_batt_adc_chan, CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE);

    /* Initial sample */
    battery_poll_and_notify();

    /* Start periodic polling timer if not already created */
    if (s_batt_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &battery_timer_cb,
            .name     = "batt_poll",
        };
        esp_err_t terr = esp_timer_create(&timer_args, &s_batt_timer);
        if (terr == ESP_OK) {
            uint64_t interval_us = (uint64_t)CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC * 1000000ULL;
            if (interval_us == 0) interval_us = 60ULL * 1000000ULL;
            esp_timer_start_periodic(s_batt_timer, interval_us);
        }
    }

    return ESP_OK;
}

uint8_t battery_get_level_pct(void) {
    if (!s_batt_initialized) {
        battery_init();
    }
    return s_batt_filtered_pct;
}

uint32_t battery_get_voltage_mv(void) {
    if (!s_batt_initialized) {
        battery_init();
    }
    return s_batt_last_voltage_mv;
}

bool battery_is_charging(void) {
    return s_batt_is_charging;
}

void battery_poll_and_notify(void) {
    uint32_t v_mv = read_battery_voltage_mv_internal();
    if (v_mv == 0) {
        /* No valid reading available */
        return;
    }
    s_batt_last_voltage_mv = v_mv;
    uint8_t raw_pct = battery_voltage_to_pct(v_mv);

    /* Apply hysteresis and monotonicity filter */
    if (s_batt_filtered_pct == 0xFF) {
        s_batt_filtered_pct = raw_pct;
    } else {
        if (raw_pct < s_batt_filtered_pct) {
            /* Discharging monotonically */
            s_batt_filtered_pct = raw_pct;
            s_batt_is_charging  = false;
        } else if (raw_pct >= s_batt_filtered_pct + 5) {
            /* Significant upward jump -> charger connected / VBUS powered */
            s_batt_filtered_pct = raw_pct;
            s_batt_is_charging  = true;
        }
        /* Minor upward jitter (< 5%) during discharge is filtered out */
    }

    int64_t now_us = esp_timer_get_time();
    int delta = abs((int)s_batt_filtered_pct - (int)s_batt_last_notified_pct);
    bool interval_elapsed = (now_us - s_batt_last_notify_us) >=
                            ((int64_t)CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC * 1000000LL);

    if (s_batt_last_notified_pct == 0xFF || delta >= CONFIG_TEF_BATTERY_DELTA_NOTIFY_PCT || interval_elapsed) {
        s_batt_last_notified_pct = s_batt_filtered_pct;
        s_batt_last_notify_us    = now_us;

        /* Post event to system event bus */
        esp_event_post(BATTERY_EVENTS, BATTERY_EVENT_LEVEL_CHANGED,
                       &s_batt_filtered_pct, sizeof(s_batt_filtered_pct), 0);

        /* Notify active BLE connections */
        ble_hid_notify_battery_all(s_batt_filtered_pct);

        ESP_LOGD(TAG, "Battery updated: %lu mV, %u %% (charging=%s)",
                 (unsigned long)s_batt_last_voltage_mv, s_batt_filtered_pct, s_batt_is_charging ? "yes" : "no");
    }
}
