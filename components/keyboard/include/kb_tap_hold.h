#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cfg_custom_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TH_MAX_DEFERRED_EVENTS 16

typedef enum {
    TH_STATE_IDLE,
    TH_STATE_PENDING_DECISION,     /**< Tap-hold key held, waiting for timeout or other key */
    TH_STATE_WAIT_DOUBLE_TAP,      /**< Tap-hold key released, waiting for potential double-tap */
    TH_STATE_HOLD_SUSTAINED,       /**< Tap-hold resolved as hold and held physically */
} th_key_state_t;

typedef struct {
    uint16_t action_code;
    bool     is_pressed;
    int64_t  timestamp_us;
} th_event_t;

/** Callback to dispatch raw press/release to macro engine (e.g. kb_macro_process_action). */
typedef void (*kb_tap_hold_dispatch_fn)(uint16_t action_code, bool is_pressed);

/** Callback to fire a timed tap (e.g. kb_macro_fire_tap). */
typedef void (*kb_tap_hold_fire_tap_fn)(uint16_t action_code, uint32_t duration_ms, uint32_t delay_ms);

/** Callback to dispatch non-blocking report send (e.g. kb_macro_send_report_nowait). */
typedef void (*kb_tap_hold_send_report_fn)(void);

/** Callback to query MultiAction configuration for an action code. */
typedef bool (*kb_tap_hold_lookup_fn)(uint16_t action_code, cfg_ckey_ma_t *out_ma);

/**
 * @brief Initialize the Tap-Hold Resolver engine with dependencies.
 */
void kb_tap_hold_init(kb_tap_hold_dispatch_fn dispatch_fn,
                      kb_tap_hold_fire_tap_fn fire_tap_fn,
                      kb_tap_hold_send_report_fn send_report_fn,
                      kb_tap_hold_lookup_fn lookup_fn);

/**
 * @brief Reset internal resolver state, buffer and history timers.
 */
void kb_tap_hold_reset(void);

/**
 * @brief Process an action event from keyboard scanning before macro processing.
 *
 * If the key is a Tap-Hold key or if there is an undecided tap-hold key in flight,
 * this function manages the decision state machine and buffers events.
 * If no tap-hold key is pending and the key is regular, it dispatches immediately.
 *
 * @param action_code  16-bit action code.
 * @param is_pressed   true = press, false = release.
 * @param now_us       Current timestamp in microseconds (esp_timer_get_time()).
 */
void kb_tap_hold_process_key(uint16_t action_code, bool is_pressed, int64_t now_us);

/**
 * @brief Periodic tick called by kb_manager scan loop to evaluate timeouts.
 *
 * @param now_us Current timestamp in microseconds.
 */
void kb_tap_hold_tick(int64_t now_us);

/**
 * @brief Check if there is currently a tap-hold key active or undecided.
 */
bool kb_tap_hold_is_active(void);

/**
 * @brief Query current resolver state (for diagnostics and unit tests).
 */
th_key_state_t kb_tap_hold_get_state(void);

/**
 * @brief Query number of buffered deferred events (for unit tests).
 */
size_t kb_tap_hold_get_buffered_count(void);

#ifdef __cplusplus
}
#endif
