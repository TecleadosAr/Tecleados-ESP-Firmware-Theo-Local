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
