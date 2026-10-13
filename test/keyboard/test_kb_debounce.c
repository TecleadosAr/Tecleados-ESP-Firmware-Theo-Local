/**
 * @file test_kb_debounce.c
 * @brief Unit tests for keyboard matrix debouncing (kb_debounce.c).
 * Verifies eager press detection (p99 < 1.2ms / scan 1), multi-scan release
 * hysteresis, and bounce filtering.
 */
#include "test_harness.h"
#include "kb_debounce.h"
#include "../../components/keyboard/kb_bitmap.h"

TEST_SETUP(kb_debounce) {
    kb_debounce_reset();
}

TEST_CASE(kb_debounce, eager_press_immediate_on_first_scan) {
    uint8_t raw[KB_MATRIX_BITMAP_BYTES] = {0};
    uint8_t stable[KB_MATRIX_BITMAP_BYTES] = {0};

    /* Key 0 pressed on raw matrix */
    kb_bit_set(raw, 0);

    /* Single scan */
    kb_debounce_update(raw, stable);

    /* Eager debouncing confirms keypress immediately on the first scan */
    TEST_ASSERT_TRUE(kb_bit_get(stable, 0));
    TEST_ASSERT_EQUAL(5, kb_debounce_get_counter(0));
}

TEST_CASE(kb_debounce, eager_release_requires_5_consecutive_low_scans) {
    uint8_t raw[KB_MATRIX_BITMAP_BYTES] = {0};
    uint8_t stable[KB_MATRIX_BITMAP_BYTES] = {0};

    /* Press key 5 */
    kb_bit_set(raw, 5);
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(5, kb_debounce_get_counter(5));

    /* Release raw key 5 */
    kb_bit_clear(raw, 5);

    /* Scan 1 low -> counter 4, still stable */
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(4, kb_debounce_get_counter(5));

    /* Scan 2 low -> counter 3, still stable */
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(3, kb_debounce_get_counter(5));

    /* Scan 3 low -> counter 2, still stable */
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(2, kb_debounce_get_counter(5));

    /* Scan 4 low -> counter 1, still stable */
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(1, kb_debounce_get_counter(5));

    /* Scan 5 low -> counter 0, stable cleared */
    kb_debounce_update(raw, stable);
    TEST_ASSERT_FALSE(kb_bit_get(stable, 5));
    TEST_ASSERT_EQUAL(0, kb_debounce_get_counter(5));
}

TEST_CASE(kb_debounce, bounce_during_release_recovers_counter) {
    uint8_t raw[KB_MATRIX_BITMAP_BYTES] = {0};
    uint8_t stable[KB_MATRIX_BITMAP_BYTES] = {0};

    /* Press key 10 */
    kb_bit_set(raw, 10);
    kb_debounce_update(raw, stable);
    TEST_ASSERT_TRUE(kb_bit_get(stable, 10));

    /* Switch begins release: 2 scans low */
    kb_bit_clear(raw, 10);
    kb_debounce_update(raw, stable); // counter 4
    kb_debounce_update(raw, stable); // counter 3
    TEST_ASSERT_EQUAL(3, kb_debounce_get_counter(10));
    TEST_ASSERT_TRUE(kb_bit_get(stable, 10));

    /* Contact bounces high for 1 scan */
    kb_bit_set(raw, 10);
    kb_debounce_update(raw, stable);
    /* Counter restored to max (5), key was never falsely released */
    TEST_ASSERT_EQUAL(5, kb_debounce_get_counter(10));
    TEST_ASSERT_TRUE(kb_bit_get(stable, 10));
}

TEST_CASE(kb_debounce, multiple_keys_debounced_independently) {
    uint8_t raw[KB_MATRIX_BITMAP_BYTES] = {0};
    uint8_t stable[KB_MATRIX_BITMAP_BYTES] = {0};

    /* Press key 1 and key 50 */
    kb_bit_set(raw, 1);
    kb_bit_set(raw, 50);
    kb_debounce_update(raw, stable);

    TEST_ASSERT_TRUE(kb_bit_get(stable, 1));
    TEST_ASSERT_TRUE(kb_bit_get(stable, 50));
    TEST_ASSERT_FALSE(kb_bit_get(stable, 2));

    /* Release key 1 only */
    kb_bit_clear(raw, 1);
    for (int i = 0; i < 5; i++) {
        kb_debounce_update(raw, stable);
    }

    /* Key 1 now released, key 50 still pressed */
    TEST_ASSERT_FALSE(kb_bit_get(stable, 1));
    TEST_ASSERT_TRUE(kb_bit_get(stable, 50));
    TEST_ASSERT_EQUAL(5, kb_debounce_get_counter(50));
}

TEST_CASE(kb_debounce, reset_clears_all_counters) {
    uint8_t raw[KB_MATRIX_BITMAP_BYTES] = {0};
    uint8_t stable[KB_MATRIX_BITMAP_BYTES] = {0};

    kb_bit_set(raw, 3);
    kb_debounce_update(raw, stable);
    TEST_ASSERT_EQUAL(5, kb_debounce_get_counter(3));

    kb_debounce_reset();
    TEST_ASSERT_EQUAL(0, kb_debounce_get_counter(3));
}
