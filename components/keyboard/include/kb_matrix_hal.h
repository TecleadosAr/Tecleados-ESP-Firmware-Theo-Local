#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize precomputed register bitmasks for matrix columns and rows.
 */
void kb_matrix_hal_init(void);

/**
 * @brief Assert (drive LOW) a single column pin.
 * Uses direct register write (GPIO_OUT_W1TC_REG / GPIO_OUT1_W1TC_REG) on target.
 *
 * @param col Index of the physical column [0..KB_MATRIX_COL_COUNT-1].
 */
void kb_matrix_hal_select_col(size_t col);

/**
 * @brief Deassert (drive HIGH) a single column pin.
 * Uses direct register write (GPIO_OUT_W1TS_REG / GPIO_OUT1_W1TS_REG) on target.
 *
 * @param col Index of the physical column [0..KB_MATRIX_COL_COUNT-1].
 */
void kb_matrix_hal_deselect_col(size_t col);

/**
 * @brief Drive all column pins to a specified level (0 = LOW, 1 = HIGH).
 * Uses two direct register writes on target instead of 18 separate calls.
 *
 * @param level 0 (LOW) or 1 (HIGH).
 */
void kb_matrix_hal_set_all_cols(uint32_t level);

/**
 * @brief Read all 6 row inputs in a single register read.
 * On target reads GPIO_IN_REG directly and decodes rows via:
 * rows = (~in >> 1) & 0x3F.
 *
 * @return Bitmask of active rows (bit R is 1 if row R is pressed / LOW).
 */
uint8_t kb_matrix_hal_read_rows(void);

/**
 * @brief Enable or disable pad hold for all column pins.
 * Used during deep sleep to keep columns held LOW so any keypress pulls rows LOW.
 *
 * @param hold true to enable gpio_hold_en on all columns, false to disable.
 */
void kb_matrix_hal_hold_all_cols(bool hold);

#ifdef __cplusplus
}
#endif
