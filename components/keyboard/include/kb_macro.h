#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the Macro Engine and Virtual State
 */
void kb_macro_init(void);

/**
 * @brief Process an action code from the matrix (edge event)
 */
void kb_macro_process_action(uint16_t action_code, bool is_pressed);

/**
 * @brief Send a raw HID key press to the virtual state
 */
void kb_macro_virtual_press(uint8_t hid_keycode);

/**
 * @brief Send a raw HID key release to the virtual state
 */
void kb_macro_virtual_release(uint8_t hid_keycode);

/**
 * @brief Get the bitmask of all currently active layers (Base is always bit 0)
 */
uint16_t kb_macro_get_layer_mask(void);

/**
 * @brief Force a report to be sent from the current virtual state
 */
#include "esp_err.h"
esp_err_t kb_macro_send_report(void);

/**
 * @brief Fire a single-action tap (press + delay + release) via the macro task.
 * Safe to call from any task context. Does NOT block the caller.
 *
 * @param action_code  Action code to fire (HID, System, CKey, etc.)
 * @param duration_ms  How long to hold the virtual key before releasing (ms).
 *                     If 0, a default of 10 ms is used.
 */
void kb_macro_fire_tap(uint16_t action_code, uint32_t duration_ms, uint32_t delay_ms);

/**
 * @brief Blocked report timing and histogram statistics for kb_macro_send_report
 */
typedef struct {
    uint32_t total_reports;
    uint32_t failed_reports;
    int64_t  max_blocked_us;
    int64_t  total_blocked_us;
    /* Histogram buckets (in microseconds):
     * [0]: < 100 us
     * [1]: 100 us .. 1 ms
     * [2]: 1 ms .. 5 ms
     * [3]: 5 ms .. 20 ms
     * [4]: 20 ms .. 50 ms
     * [5]: 50 ms .. 100 ms
     * [6]: >= 100 ms
     */
    uint32_t buckets[7];
} kb_report_blocked_stats_t;

/**
 * @brief Record a send report latency measurement into telemetry stats.
 */
void kb_macro_track_blocked_time(int64_t blocked_us, int retries, esp_err_t err);

/**
 * @brief Retrieve copy of report blocked statistics.
 */
void kb_macro_get_blocked_stats(kb_report_blocked_stats_t *out_stats);

/**
 * @brief Reset report blocked statistics.
 */
void kb_macro_reset_blocked_stats(void);
