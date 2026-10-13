/**
 * @file test_kb_matrix.c
 * @brief Unit tests for physical matrix scanning (kb_matrix.c).
 * Verifies active-low column driving, row reading, and bitmap generation.
 */
#include "test_harness.h"
#include "driver/gpio.h"
#include "kb_matrix.h"
#include "../../components/keyboard/kb_bitmap.h"

TEST_SETUP(kb_matrix) {
    mock_gpio_reset_all();
}

TEST_CASE(kb_matrix, no_keys_pressed_bitmap_all_zero) {
    uint8_t bitmap[KB_MATRIX_BITMAP_BYTES] = {0xFF};
    kb_matrix_scan(bitmap, false);

    for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
        TEST_ASSERT_EQUAL(0, bitmap[i]);
    }
}

TEST_CASE(kb_matrix, single_key_pressed_detected) {
    /* Row 0 = GPIO 1, Col 0 = GPIO 7 -> Bit 0 */
    mock_gpio_set_switch(GPIO_NUM_1, GPIO_NUM_7, true);

    uint8_t bitmap[KB_MATRIX_BITMAP_BYTES] = {0};
    kb_matrix_scan(bitmap, false);

    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 0));
    /* Verify bit 1 is not set */
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 1));
}

TEST_CASE(kb_matrix, multiple_keys_pressed_detected) {
    /* Row 0 Col 0: GPIO 1, GPIO 7 -> bit 0 */
    mock_gpio_set_switch(GPIO_NUM_1, GPIO_NUM_7, true);
    /* Row 2 Col 3: GPIO 3, GPIO 10 -> bit (2 * 18 + 3) = 39 */
    mock_gpio_set_switch(GPIO_NUM_3, GPIO_NUM_10, true);

    uint8_t bitmap[KB_MATRIX_BITMAP_BYTES] = {0};
    kb_matrix_scan(bitmap, false);

    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 0));
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 39));
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 1));
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 38));
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 40));
}

TEST_CASE(kb_matrix, mirrored_cols_detected) {
    /* Row 2 Col 3: GPIO 3, GPIO 10 */
    /* With mirror_cols = true, logical_col = 18 - 1 - 3 = 14 */
    /* Expected bit index = 2 * 18 + 14 = 50 */
    mock_gpio_set_switch(GPIO_NUM_3, GPIO_NUM_10, true);

    uint8_t bitmap[KB_MATRIX_BITMAP_BYTES] = {0};
    kb_matrix_scan(bitmap, true);

    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 50));
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 39));
}

TEST_CASE(kb_matrix, hal_select_and_deselect_col) {
    kb_matrix_hal_init();

    /* Select col 0 (GPIO 7) */
    kb_matrix_hal_select_col(0);
    TEST_ASSERT_EQUAL(0, gpio_get_level(GPIO_NUM_7));

    /* Deselect col 0 */
    kb_matrix_hal_deselect_col(0);
    TEST_ASSERT_EQUAL(1, gpio_get_level(GPIO_NUM_7));
}

TEST_CASE(kb_matrix, hal_set_all_cols_low_and_high) {
    kb_matrix_hal_init();

    /* Set all cols LOW */
    kb_matrix_hal_set_all_cols(0);
    TEST_ASSERT_EQUAL(0, gpio_get_level(GPIO_NUM_7));
    TEST_ASSERT_EQUAL(0, gpio_get_level(GPIO_NUM_18));
    TEST_ASSERT_EQUAL(0, gpio_get_level(GPIO_NUM_21));
    TEST_ASSERT_EQUAL(0, gpio_get_level(GPIO_NUM_42));

    /* Set all cols HIGH */
    kb_matrix_hal_set_all_cols(1);
    TEST_ASSERT_EQUAL(1, gpio_get_level(GPIO_NUM_7));
    TEST_ASSERT_EQUAL(1, gpio_get_level(GPIO_NUM_18));
    TEST_ASSERT_EQUAL(1, gpio_get_level(GPIO_NUM_21));
    TEST_ASSERT_EQUAL(1, gpio_get_level(GPIO_NUM_42));
}

TEST_CASE(kb_matrix, hal_direct_row_read_decoding) {
    kb_matrix_hal_init();

    /* No switch pressed */
    kb_matrix_hal_select_col(0);
    TEST_ASSERT_EQUAL(0, kb_matrix_hal_read_rows());
    kb_matrix_hal_deselect_col(0);

    /* Close Row 0 (GPIO 1) and Row 4 (GPIO 5) on Col 0 */
    mock_gpio_set_switch(GPIO_NUM_1, GPIO_NUM_7, true);
    mock_gpio_set_switch(GPIO_NUM_5, GPIO_NUM_7, true);

    kb_matrix_hal_select_col(0);
    uint8_t rows = kb_matrix_hal_read_rows();
    /* Expected bit 0 (Row 0) and bit 4 (Row 4): (1 << 0) | (1 << 4) = 0x11 */
    TEST_ASSERT_EQUAL(0x11, rows);
    kb_matrix_hal_deselect_col(0);
}

TEST_CASE(kb_matrix, hal_register_formula_simulation) {
    /* Verify the hardware decoding formula: rows = (~in >> 1) & 0x3F */
    /* If no rows pressed: GPIO1..6 are HIGH -> bits 1..6 = 1 */
    uint32_t in_none = 0xFFFFFFFF;
    uint8_t rows_none = (uint8_t)((~in_none >> 1) & 0x3F);
    TEST_ASSERT_EQUAL(0x00, rows_none);

    /* If Row 0 (GPIO 1) is active LOW: bit 1 is 0, rest are 1 */
    uint32_t in_row0 = 0xFFFFFFFF & ~(1UL << 1);
    uint8_t rows_row0 = (uint8_t)((~in_row0 >> 1) & 0x3F);
    TEST_ASSERT_EQUAL(0x01, rows_row0);

    /* If all 6 rows (GPIO 1..6) are active LOW: bits 1..6 are 0 */
    uint32_t in_all = 0xFFFFFFFF & ~(0x3FUL << 1);
    uint8_t rows_all = (uint8_t)((~in_all >> 1) & 0x3F);
    TEST_ASSERT_EQUAL(0x3F, rows_all);
}
