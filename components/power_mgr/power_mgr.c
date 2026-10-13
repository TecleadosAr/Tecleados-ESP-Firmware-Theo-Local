#include "power_mgr.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "kb_matrix_hal.h"
#include "battery.h"
#include "event_bus.h"

#ifdef TAG
#undef TAG
#endif
#define TAG "POWER_MGR"

static void battery_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    (void)handler_args; (void)base;
    if (id == BATTERY_EVENT_LEVEL_CHANGED && event_data) {
        uint8_t level = *(const uint8_t *)event_data;
        if (level == 0 && !power_mgr_is_usb_active() && !battery_is_charging()) {
            ESP_LOGW(TAG, "Critical battery cutoff triggered (0%% charge)!");
            power_mgr_enter_deep_sleep();
        }
    }
}

/* Configuration defaults and Kconfig fallbacks */
#ifndef CONFIG_TEF_POWER_MGR
#define CONFIG_TEF_POWER_MGR 1
#endif

#ifndef CONFIG_TEF_PM_IDLE_TIMEOUT_MS
#define CONFIG_TEF_PM_IDLE_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP
#define CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP 1
#endif

#ifndef CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS
#define CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS 30000
#endif

#ifndef CONFIG_TEF_PM_ENABLE_DEEP_SLEEP
#define CONFIG_TEF_PM_ENABLE_DEEP_SLEEP 1
#endif

#ifndef CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS
#define CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS 900000
#endif

/* Internal state */
static bool                 s_pm_initialized       = false;
static power_state_t        s_pm_state             = POWER_STATE_ACTIVE;
static esp_pm_lock_handle_t s_pm_cpu_max_lock      = NULL;
static bool                 s_pm_lock_acquired     = false;
static int64_t              s_pm_last_activity_us  = 0;
static bool                 s_pm_usb_active        = false;
static uint32_t             s_pm_mock_inactive_ms  = 0;

static uint32_t get_inactive_ms(void) {
    if (s_pm_mock_inactive_ms > 0) {
        return s_pm_mock_inactive_ms;
    }
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_pm_last_activity_us;
    if (elapsed_us < 0) return 0;
    return (uint32_t)(elapsed_us / 1000);
}

esp_err_t power_mgr_init(void) {
    if (s_pm_initialized) return ESP_OK;

#if CONFIG_TEF_POWER_MGR
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz       = 240,
        .min_freq_mhz       = 80,
        .light_sleep_enable = (CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP != 0),
    };
    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }

    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "tef_cpu_max", &s_pm_cpu_max_lock);
    if (err == ESP_OK && s_pm_cpu_max_lock != NULL) {
        esp_pm_lock_acquire(s_pm_cpu_max_lock);
        s_pm_lock_acquired = true;
    } else {
        ESP_LOGW(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
        s_pm_cpu_max_lock = NULL;
        s_pm_lock_acquired = false;
    }
#else
    /* Fallback static 240 MHz operation without power management */
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz       = 240,
        .min_freq_mhz       = 240,
        .light_sleep_enable = false,
    };
    (void)esp_pm_configure(&pm_cfg);
#endif

    /* If resuming from deep sleep, release pad holds */
    gpio_deep_sleep_hold_dis();
    kb_matrix_hal_hold_all_cols(false);

    esp_event_handler_register(BATTERY_EVENTS, BATTERY_EVENT_LEVEL_CHANGED, &battery_event_handler, NULL);

    s_pm_state = POWER_STATE_ACTIVE;
    s_pm_last_activity_us = esp_timer_get_time();
    s_pm_initialized = true;

    ESP_LOGI(TAG, "Power Manager initialized (state=ACTIVE, idle_ms=%d, light_ms=%d, deep_ms=%d)",
             CONFIG_TEF_PM_IDLE_TIMEOUT_MS,
             CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS,
             CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS);
    return ESP_OK;
}

void power_mgr_activity_notify(void) {
    if (!s_pm_initialized) {
        power_mgr_init();
    }

    s_pm_last_activity_us = esp_timer_get_time();

    if (s_pm_state != POWER_STATE_ACTIVE) {
        if (s_pm_cpu_max_lock && !s_pm_lock_acquired) {
            esp_pm_lock_acquire(s_pm_cpu_max_lock);
            s_pm_lock_acquired = true;
        }
        s_pm_state = POWER_STATE_ACTIVE;
        ESP_LOGD(TAG, "Activity notify: transitioned to ACTIVE");
    }
}

void power_mgr_set_usb_active(bool is_active) {
    s_pm_usb_active = is_active;
    if (is_active) {
        power_mgr_activity_notify();
    }
}

bool power_mgr_is_usb_active(void) {
    return s_pm_usb_active;
}

power_state_t power_mgr_get_state(void) {
    return s_pm_state;
}

bool power_mgr_can_enter_light_sleep(void) {
#if !CONFIG_TEF_POWER_MGR || !CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP
    return false;
#else
    if (s_pm_usb_active) return false;
    if (s_pm_state != POWER_STATE_IDLE) return false;
    return (get_inactive_ms() >= (uint32_t)CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS);
#endif
}

bool power_mgr_can_enter_deep_sleep(void) {
#if !CONFIG_TEF_POWER_MGR || !CONFIG_TEF_PM_ENABLE_DEEP_SLEEP
    return false;
#else
    if (s_pm_usb_active) return false;
    if (get_inactive_ms() >= (uint32_t)CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS) {
        return true;
    }
    /* Critical battery cutoff: 0% charge while discharging on battery */
    if (!battery_is_charging()) {
        uint8_t bat = battery_get_level_pct();
        if (bat == 0) {
            return true;
        }
    }
    return false;
#endif
}

esp_err_t power_mgr_enter_light_sleep(void) {
#if !CONFIG_TEF_POWER_MGR || !CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_pm_usb_active) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Drive all matrix columns LOW so any pressed switch pulls its row LOW */
    kb_matrix_hal_set_all_cols(0);

    /* Configure row GPIOs 1..6 for low-level wake */
    for (int gpio = 1; gpio <= 6; gpio++) {
        gpio_wakeup_enable((gpio_num_t)gpio, GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();

    s_pm_state = POWER_STATE_LIGHT_SLEEP;
    ESP_LOGD(TAG, "Entering Light Sleep");

    esp_err_t err = esp_light_sleep_start();

    /* R13 Wakeup Safety: Immediately disable level wakeup to prevent interrupt re-trigger storm
     * while the user's finger is still holding the switch closed */
    for (int gpio = 1; gpio <= 6; gpio++) {
        gpio_wakeup_disable((gpio_num_t)gpio);
    }

    /* Restore columns back to HIGH (default scanning state) */
    kb_matrix_hal_set_all_cols(1);

    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (1UL << ESP_SLEEP_WAKEUP_GPIO)) {
        /* Woken by matrix keypress */
        if (s_pm_cpu_max_lock && !s_pm_lock_acquired) {
            esp_pm_lock_acquire(s_pm_cpu_max_lock);
            s_pm_lock_acquired = true;
        }
        s_pm_state = POWER_STATE_ACTIVE;
        s_pm_last_activity_us = esp_timer_get_time();
        ESP_LOGD(TAG, "Wakeup from Light Sleep via GPIO -> state ACTIVE");
    } else {
        /* Woken by timer or radio, not matrix switch */
        s_pm_state = POWER_STATE_IDLE;
        ESP_LOGD(TAG, "Wakeup from Light Sleep via non-GPIO (causes 0x%" PRIx32 ") -> state IDLE", causes);
    }

    return err;
#endif
}

void power_mgr_enter_deep_sleep(void) {
#if !CONFIG_TEF_POWER_MGR || !CONFIG_TEF_PM_ENABLE_DEEP_SLEEP
    return;
#else
    if (s_pm_usb_active) {
        return;
    }

    /* Keep columns driven LOW and held during deep sleep */
    kb_matrix_hal_set_all_cols(0);
    kb_matrix_hal_hold_all_cols(true);
    gpio_deep_sleep_hold_en();

    /* Enable pull-up on RTC row GPIOs 1..6 */
    uint64_t ext1_mask = 0;
    for (int gpio = 1; gpio <= 6; gpio++) {
        gpio_pullup_en((gpio_num_t)gpio);
        ext1_mask |= (1ULL << gpio);
    }

    esp_sleep_enable_ext1_wakeup_io(ext1_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    s_pm_state = POWER_STATE_DEEP_SLEEP;
    ESP_LOGI(TAG, "Entering Deep Sleep now (wake on RTC GPIO 1..6)");

    esp_deep_sleep_start();
#endif
}

void power_mgr_update(void) {
    if (!s_pm_initialized) return;

#if CONFIG_TEF_POWER_MGR
    uint32_t inactive_ms = get_inactive_ms();

    /* Check deep sleep eligibility (15 min inactivity or critical battery cutoff) */
    if (!s_pm_usb_active && power_mgr_can_enter_deep_sleep()) {
        power_mgr_enter_deep_sleep();
        return;
    }

    if (s_pm_state == POWER_STATE_ACTIVE) {
        if (inactive_ms >= (uint32_t)CONFIG_TEF_PM_IDLE_TIMEOUT_MS) {
            if (s_pm_cpu_max_lock && s_pm_lock_acquired) {
                esp_pm_lock_release(s_pm_cpu_max_lock);
                s_pm_lock_acquired = false;
            }
            s_pm_state = POWER_STATE_IDLE;
            ESP_LOGD(TAG, "Inactivity timeout: ACTIVE -> IDLE");
        }
    }
#endif
}

void power_mgr_test_reset(void) {
    s_pm_initialized      = false;
    s_pm_state            = POWER_STATE_ACTIVE;
    s_pm_cpu_max_lock     = NULL;
    s_pm_lock_acquired    = false;
    s_pm_last_activity_us = 0;
    s_pm_usb_active       = false;
    s_pm_mock_inactive_ms = 0;
    power_mgr_init();
}

void power_mgr_test_set_inactive_ms(uint32_t ms) {
    s_pm_mock_inactive_ms = ms;
}
