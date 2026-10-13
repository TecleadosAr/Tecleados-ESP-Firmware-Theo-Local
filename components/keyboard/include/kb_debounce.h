#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kb_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize or reset internal debouncing state for all keys.
 */
void kb_debounce_init(void);

/**
 * @brief Reset internal debouncing counter state for all keys to 0.
 */
void kb_debounce_reset(void);

/**
 * @brief Filter a raw matrix scan into a debounced stable matrix.
 *
 * Implements eager debouncing (immediate confirmation on first scan, or after
 * CONFIG_TEF_PRESS_CONFIRM_SCANS) and integrator release filtering over
 * CONFIG_TEF_RELEASE_DEBOUNCE_SCANS.
 *
 * @param raw    Current hardware scan (KB_MATRIX_BITMAP_BYTES).
 * @param stable Debounced stable state output (KB_MATRIX_BITMAP_BYTES).
 */
void kb_debounce_update(const uint8_t *raw, uint8_t *stable);

/**
 * @brief Read the internal debounce counter value for a given key index.
 * Exposed for diagnostic and unit testing purposes.
 *
 * @param key_index Index of the key [0..KB_MATRIX_KEYS-1].
 * @return Current counter value (0..CONFIG_TEF_RELEASE_DEBOUNCE_SCANS).
 */
uint8_t kb_debounce_get_counter(size_t key_index);

#ifdef __cplusplus
}
#endif
