/**
 * @file test_kb_combo.c
 * @brief Unit tests for keyboard combo engine (kb_combo.c).
 *
 * Verifies combo matching, subset subsumption, strict order enforcement,
 * delayed press buffering/discarding, cancel_keys retroactive release,
 * release_on_first_key, and timeout expiration via kb_combo_tick().
 */
#include "test_harness.h"
#include "kb_combo.h"
#include "cfg_combos.h"
#include "kb_macro.h"
#include "kb_layout.h"
#include "../../components/keyboard/kb_bitmap.h"

static cfg_combo_t s_test_combos[CFG_COMBOS_MAX_COUNT];
static combo_rt_t  s_test_combo_rt[CFG_COMBOS_MAX_COUNT];

TEST_SETUP(kb_combo) {
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    memset(s_test_combos, 0, sizeof(s_test_combos));
    memset(s_test_combo_rt, 0, sizeof(s_test_combo_rt));
    memset(s_suppressed, 0, sizeof(s_suppressed));
    s_suppressed_count = 0;

    s_combos = s_test_combos;
    s_combo_rt = s_test_combo_rt;
    s_combo_count = 0;
    mock_timer_set(1000000LL);
}

TEST_CASE(kb_combo, simple_combo_trigger_and_release) {
    // 2-key combo on row 1 col 1 + row 1 col 2 -> action 0x06 (HID_KEY_C)
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 1; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 1; s_combos[0].keys[1].col = 2;
    s_combos[0].action = 0x06; // 'C'
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].delayed_press = false;
    s_combos[0].release_on_first_key = false;
    s_combo_count = 1;

    // Press key 1
    bool consumed = kb_combo_process_key(1, 1, true, 0x01);
    TEST_ASSERT_FALSE(consumed);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x06));

    // Press key 2 -> matches combo!
    consumed = kb_combo_process_key(1, 2, true, 0x01);
    TEST_ASSERT_FALSE(consumed);
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x06));

    // Release key 1: release_on_first_key is false, matched_count is 1 -> combo stays active
    kb_combo_process_key(1, 1, false, 0x01);
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x06));

    // Release key 2: matched_count becomes 0 -> combo releases
    kb_combo_process_key(1, 2, false, 0x01);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x06));
}

TEST_CASE(kb_combo, subsumption_subset_cancelled_by_superset) {
    // Combo 0 (longer, 3 keys): (1,1) + (1,2) + (1,3) -> action 0x1C (Y)
    s_combos[0].id = 0;
    s_combos[0].key_count = 3;
    s_combos[0].keys[0].row = 1; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 1; s_combos[0].keys[1].col = 2;
    s_combos[0].keys[2].row = 1; s_combos[0].keys[2].col = 3;
    s_combos[0].action = 0x1C;
    s_combos[0].active_layers = 0xFFFF;

    // Combo 1 (shorter subset, 2 keys): (1,1) + (1,2) -> action 0x1B (X)
    s_combos[1].id = 1;
    s_combos[1].key_count = 2;
    s_combos[1].keys[0].row = 1; s_combos[1].keys[0].col = 1;
    s_combos[1].keys[1].row = 1; s_combos[1].keys[1].col = 2;
    s_combos[1].action = 0x1B;
    s_combos[1].active_layers = 0xFFFF;
    s_combo_count = 2;

    // Press (1,1) and (1,2) -> subset combo triggers
    kb_combo_process_key(1, 1, true, 0x01);
    kb_combo_process_key(1, 2, true, 0x01);

    TEST_ASSERT_TRUE(s_combo_rt[1].is_active);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x1B)); // 'X' active
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);

    // Now press 3rd key (1,3) -> 3-key combo completes!
    // The subset combo must be cancelled and action 0x1B released, 0x1C pressed.
    kb_combo_process_key(1, 3, true, 0x01);

    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
    TEST_ASSERT_FALSE(s_combo_rt[1].is_active);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x1B)); // 'X' cancelled
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x1C));  // 'Y' active
}

TEST_CASE(kb_combo, subsumed_by_already_active_superset_ignored) {
    // Combo 0 (longer, 3 keys): (1,1) + (1,2) + (1,3) -> action 0x1C (Y)
    s_combos[0].id = 0;
    s_combos[0].key_count = 3;
    s_combos[0].keys[0].row = 1; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 1; s_combos[0].keys[1].col = 2;
    s_combos[0].keys[2].row = 1; s_combos[0].keys[2].col = 3;
    s_combos[0].action = 0x1C;
    s_combos[0].active_layers = 0xFFFF;

    // Combo 1 (shorter subset, 2 keys): (1,1) + (1,2) -> action 0x1B (X)
    s_combos[1].id = 1;
    s_combos[1].key_count = 2;
    s_combos[1].keys[0].row = 1; s_combos[1].keys[0].col = 1;
    s_combos[1].keys[1].row = 1; s_combos[1].keys[1].col = 2;
    s_combos[1].action = 0x1B;
    s_combos[1].active_layers = 0xFFFF;
    s_combo_count = 2;

    // Activate Combo 0 directly
    s_combo_rt[0].is_active = true;
    s_combo_rt[0].matched_count = 3;
    s_combo_rt[0].keys_pressed[0] = true;
    s_combo_rt[0].keys_pressed[1] = true;
    s_combo_rt[0].keys_pressed[2] = true;
    kb_bit_set(s_v_nkro, 0x1C);

    // If subset keys are evaluated, combo 1 should see it is subsumed by combo 0
    s_combo_rt[1].matched_count = 1;
    s_combo_rt[1].keys_pressed[0] = true;
    kb_combo_process_key(1, 2, true, 0x01);

    // Combo 1 does NOT fire because combo 0 is active and superset
    TEST_ASSERT_FALSE(s_combo_rt[1].is_active);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x1B));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x1C));
}

TEST_CASE(kb_combo, strict_order_enforcement) {
    // 2-key combo with strict_order = true
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 2; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 2; s_combos[0].keys[1].col = 2;
    s_combos[0].action = 0x1D; // 'Z'
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].strict_order = true;
    s_combo_count = 1;

    // Press key 1 FIRST (trigger_idx = 1 != matched_count = 0)
    kb_combo_process_key(2, 2, true, 0x01);
    TEST_ASSERT_EQUAL(0, s_combo_rt[0].matched_count);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);

    // Now press key 0 (trigger_idx = 0 == matched_count = 0)
    kb_combo_process_key(2, 1, true, 0x01);
    TEST_ASSERT_EQUAL(1, s_combo_rt[0].matched_count);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active); // Still not complete

    // Release both
    kb_combo_process_key(2, 1, false, 0x01);
    kb_combo_process_key(2, 2, false, 0x01);
    TEST_ASSERT_EQUAL(0, s_combo_rt[0].matched_count);

    // Now press in CORRECT order: key 0 then key 1
    kb_combo_process_key(2, 1, true, 0x01);
    TEST_ASSERT_EQUAL(1, s_combo_rt[0].matched_count);

    kb_combo_process_key(2, 2, true, 0x01);
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x1D));
}

TEST_CASE(kb_combo, delayed_press_suppression_and_discard) {
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 3; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 3; s_combos[0].keys[1].col = 2;
    s_combos[0].action = 0x07; // 'D'
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].delayed_press = true;
    s_combos[0].delay_ms = 50;
    s_combo_count = 1;

    // Press key 0: should be suppressed
    bool consumed = kb_combo_process_key(3, 1, true, 0x01);
    TEST_ASSERT_TRUE(consumed);
    TEST_ASSERT_EQUAL(1, s_suppressed_count);
    TEST_ASSERT_EQUAL(3, s_suppressed[0].row);
    TEST_ASSERT_EQUAL(1, s_suppressed[0].col);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x07));

    // Press key 1: completes combo!
    consumed = kb_combo_process_key(3, 2, true, 0x01);
    TEST_ASSERT_TRUE(consumed);
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x07)); // Combo action fired

    // Suppressed keys for this combo should be discarded
    TEST_ASSERT_EQUAL(0, s_suppressed_count);
}

TEST_CASE(kb_combo, delayed_press_released_before_trigger_fires_single_key) {
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 3; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 3; s_combos[0].keys[1].col = 2;
    s_combos[0].action = 0x07;
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].delayed_press = true;
    s_combos[0].delay_ms = 50;
    s_combo_count = 1;

    // Press key 0 -> suppressed
    kb_combo_process_key(3, 1, true, 0x01);
    TEST_ASSERT_EQUAL(1, s_suppressed_count);

    // Release key 0 before key 1 is pressed -> fires single key and removes from buffer
    kb_combo_process_key(3, 1, false, 0x01);
    TEST_ASSERT_EQUAL(0, s_suppressed_count);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);
}

TEST_CASE(kb_combo, cancel_keys_retroactive_release) {
    // cancel_keys = true with delayed_press = false
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 0; s_combos[0].keys[0].col = 0; // In default layout, Escape (0x29)
    s_combos[0].keys[1].row = 0; s_combos[0].keys[1].col = 1; // In default layout, 1 (0x1E)
    s_combos[0].action = 0x06; // 'C'
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].delayed_press = false;
    s_combos[0].cancel_keys = true;
    s_combo_count = 1;

    // Press (0,0): fires normal action 0x29 (Escape)
    kb_combo_process_key(0, 0, true, 0x01);
    kb_macro_process_action(kb_layout_get_action_code(0, 0, 0x01), true);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x29));

    // Press (0,1): completes combo! cancel_keys releases (0,0) and (0,1) retroactively
    kb_combo_process_key(0, 1, true, 0x01);

    // 0x29 should be cancelled, and combo action 0x06 pressed
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x29));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x06));
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);
}

TEST_CASE(kb_combo, release_on_first_key_behavior) {
    // Test release_on_first_key = true
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 1; s_combos[0].keys[0].col = 1;
    s_combos[0].keys[1].row = 1; s_combos[0].keys[1].col = 2;
    s_combos[0].action = 0x06;
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].release_on_first_key = true;
    s_combo_count = 1;

    kb_combo_process_key(1, 1, true, 0x01);
    kb_combo_process_key(1, 2, true, 0x01);
    TEST_ASSERT_TRUE(s_combo_rt[0].is_active);

    // Release first key: immediately releases combo action
    kb_combo_process_key(1, 1, false, 0x01);
    TEST_ASSERT_FALSE(s_combo_rt[0].is_active);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x06));
}

TEST_CASE(kb_combo, tick_flushes_expired_delayed_keys) {
    s_combos[0].id = 0;
    s_combos[0].key_count = 2;
    s_combos[0].keys[0].row = 0; s_combos[0].keys[0].col = 0; // Escape (0x29)
    s_combos[0].keys[1].row = 0; s_combos[0].keys[1].col = 1;
    s_combos[0].action = 0x06;
    s_combos[0].active_layers = 0xFFFF;
    s_combos[0].delayed_press = true;
    s_combos[0].delay_ms = 40;
    s_combo_count = 1;

    mock_timer_set(1000000LL); // 1000ms
    kb_combo_process_key(0, 0, true, 0x01);
    TEST_ASSERT_EQUAL(1, s_suppressed_count);

    // Tick at 1020ms (< 40ms): not expired
    kb_combo_tick(1020000LL);
    TEST_ASSERT_EQUAL(1, s_suppressed_count);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x29));

    // Tick at 1050ms (> 40ms): expired! Flushes key retroactively
    kb_combo_tick(1050000LL);
    TEST_ASSERT_EQUAL(0, s_suppressed_count);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x29));
}

TEST_CASE(kb_combo, reload_flag_consumed_by_check_reload) {
    atomic_store_explicit(&s_combo_reload_pending, true, memory_order_release);
    kb_combo_check_reload();
    TEST_ASSERT_FALSE(atomic_load_explicit(&s_combo_reload_pending, memory_order_relaxed));
}
