#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize battery measurement subsystem.
 *
 * Configures the ADC oneshot unit and curve fitting calibration on the
 * configured battery ADC pin (default GPIO 7 in split configuration per pin budget),
 * or initializes fuel gauge support.
 *
 * @return ESP_OK on success, or error code on failure.
 */
esp_err_t battery_init(void);

/**
 * @brief Get the current filtered battery charge level percentage.
 *
 * @return Battery percentage 0–100, or 0xFF if wired / unknown / disabled.
 */
uint8_t battery_get_level_pct(void);

/**
 * @brief Get the latest calculated battery voltage in millivolts.
 *
 * @return Voltage in mV (e.g. 3850 for 3.85 V), or 0 if uninitialized / unknown.
 */
uint32_t battery_get_voltage_mv(void);

/**
 * @brief Check if battery is currently detected as charging.
 *
 * @return true if charging, false otherwise.
 */
bool battery_is_charging(void);

/**
 * @brief Periodic poll and notification routine.
 *
 * Takes ADC samples, performs median filtering to suppress RF transmit spikes,
 * enforces monotonicity in discharge, and dispatches updates via BLE and event_bus
 * if the report interval has elapsed or delta >= threshold.
 */
void battery_poll_and_notify(void);

/**
 * @brief Convert a raw battery voltage in millivolts to 0–100 percentage
 *        using a calibrated LiPo discharge curve.
 *
 * @param mv Voltage in millivolts (e.g. 3300 to 4200).
 * @return Percentage between 0 and 100.
 */
uint8_t battery_voltage_to_pct(uint32_t mv);

/**
 * @brief Test hook: override raw voltage for deterministic host tests.
 *
 * @param mv Mock voltage in mV (pass 0 to return to normal ADC reading).
 */
void battery_set_mock_voltage_mv(uint32_t mv);

/**
 * @brief Test hook: reset battery filter and state for unit tests.
 */
void battery_test_reset(void);

#ifdef __cplusplus
}
#endif
