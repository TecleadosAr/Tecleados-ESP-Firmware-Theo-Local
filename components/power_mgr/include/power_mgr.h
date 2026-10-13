#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_STATE_ACTIVE = 0,     /**< CPU 240 MHz (max freq lock acquired), system active */
    POWER_STATE_IDLE,           /**< CPU 80 MHz (DFS base / tickless idle), lock released */
    POWER_STATE_LIGHT_SLEEP,    /**< Light sleep entered, wake via matrix row GPIOs */
    POWER_STATE_DEEP_SLEEP,     /**< Deep sleep entered, wake via RTC GPIO1-6 */
} power_state_t;

/**
 * @brief Initialize the unified power manager.
 *
 * Configures esp_pm_configure(), creates and acquires the CPU max frequency lock,
 * and sets up power management according to Kconfig.
 *
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t power_mgr_init(void);

/**
 * @brief Notify power manager of key, communication, or user activity.
 *
 * Resets inactivity timers, acquires the CPU frequency lock if released,
 * and transitions state to POWER_STATE_ACTIVE.
 */
void power_mgr_activity_notify(void);

/**
 * @brief Set USB connection state.
 *
 * When USB is active, light and deep sleep transitions are inhibited
 * to maintain TinyUSB PHY integrity.
 *
 * @param is_active true if USB is connected/mounted, false otherwise.
 */
void power_mgr_set_usb_active(bool is_active);

/**
 * @brief Returns true if USB is currently active.
 */
bool power_mgr_is_usb_active(void);

/**
 * @brief Get the current power management state.
 */
power_state_t power_mgr_get_state(void);

/**
 * @brief Check if system is currently eligible for light sleep.
 *
 * Returns true if in IDLE state, USB is inactive, and inactivity duration
 * meets or exceeds light sleep threshold.
 */
bool power_mgr_can_enter_light_sleep(void);

/**
 * @brief Check if system is currently eligible for deep sleep.
 */
bool power_mgr_can_enter_deep_sleep(void);

/**
 * @brief Execute entry into Light Sleep.
 *
 * Prepares matrix rows (GPIO1–6) for low-level wakeup, drives columns LOW,
 * enters esp_light_sleep_start(), and on wakeup immediately disables level wakeup,
 * restores columns, and re-acquires the CPU frequency lock.
 *
 * @return ESP_OK on wake.
 */
esp_err_t power_mgr_enter_light_sleep(void);

/**
 * @brief Execute entry into Deep Sleep.
 *
 * Configures RTC EXT1 wakeup on rows (GPIO1–6), enables pad hold on columns,
 * and enters esp_deep_sleep_start(). Never returns on hardware.
 */
void power_mgr_enter_deep_sleep(void);

/**
 * @brief Periodic power manager update tick.
 *
 * Evaluates elapsed inactivity against configured timeouts:
 * - ACTIVE -> IDLE after CONFIG_TEF_PM_IDLE_TIMEOUT_MS
 * - IDLE -> LIGHT SLEEP after CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS (if wireless & allowed)
 * - IDLE -> DEEP SLEEP after CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS
 */
void power_mgr_update(void);

/**
 * @brief Test hooks for unit testing.
 */
void power_mgr_test_reset(void);
void power_mgr_test_set_inactive_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
