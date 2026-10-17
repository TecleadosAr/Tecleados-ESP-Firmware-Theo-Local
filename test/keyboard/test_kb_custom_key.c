/**
 * @file test_kb_custom_key.c
 * @brief Unit tests for custom key engine (kb_custom_key.c).
 *
 * Verifies PressRelease mode (press_sustain, wait_for_finish timing),
 * MultiAction mode (single tap, double tap, hold, hold_sustain),
 * and kb_custom_key_is_tap_hold query function.
 */
#include "test_harness.h"
#include "kb_custom_key.h"
#include "cfg_custom_keys.h"
#include "kb_macro.h"
#include "kb_system_action.h"
#include "event_bus.h"
#include "../../components/keyboard/kb_bitmap.h"

static cfg_custom_key_t s_test_ckeys[CFG_CKEYS_MAX_COUNT];
static TickType_t       s_test_press_end_tick[CFG_CKEYS_MAX_COUNT];
static bool             s_test_ma_hold_sustained[CFG_CKEYS_MAX_COUNT];

TEST_SETUP(kb_custom_key) {
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    memset(s_tap_slots, 0, sizeof(s_tap_slots));
    memset(s_test_ckeys, 0, sizeof(s_test_ckeys));
    memset(s_test_press_end_tick, 0, sizeof(s_test_press_end_tick));
    memset(s_test_ma_hold_sustained, 0, sizeof(s_test_ma_hold_sustained));

    s_ckeys = s_test_ckeys;
    s_press_end_tick = s_test_press_end_tick;
    s_ma_hold_sustained = s_test_ma_hold_sustained;
    s_ckey_count = 0;
    mock_timer_set(1000000LL);
}

TEST_CASE(kb_custom_key, pr_mode_press_sustain) {
    // Custom key 0: PressRelease with press_sustain = true
    s_ckeys[0].id = 0;
    s_ckeys[0].mode = CKEY_MODE_PRESS_RELEASE;
    s_ckeys[0].rules.pr.press_sustain = true;
    s_ckeys[0].rules.pr.press_action = 0x04; // 'A'
    s_ckeys[0].rules.pr.release_action = 0x05; // 'B'
    s_ckeys[0].rules.pr.release_tap_release_delay_ms = 20;
    s_ckey_count = 1;

    // Physical key DOWN
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 0, true);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04)); // 'A' held
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));

    // Physical key UP
    mock_timer_set(1050000LL);
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 0, false);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04)); // 'A' released

    // Release action 'B' tap was queued
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());
    kb_macro_tap_tick(1050000LL); // Fires press of 'B'
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x05));

    kb_macro_tap_tick(1070000LL); // +20ms: releases 'B'
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}

TEST_CASE(kb_custom_key, pr_mode_wait_for_finish_delays_release_action) {
    // Custom key 1: PressRelease with wait_for_finish = true
    s_ckeys[1].id = 1;
    s_ckeys[1].mode = CKEY_MODE_PRESS_RELEASE;
    s_ckeys[1].rules.pr.press_sustain = false;
    s_ckeys[1].rules.pr.press_action = 0x06; // 'C'
    s_ckeys[1].rules.pr.press_tap_release_delay_ms = 50;
    s_ckeys[1].rules.pr.wait_for_finish = true;
    s_ckeys[1].rules.pr.release_action = 0x07; // 'D'
    s_ckeys[1].rules.pr.release_tap_release_delay_ms = 30;
    s_ckey_count = 2;

    _mock_tick_count = 1000;
    mock_timer_set(1000000LL); // 1000ms

    // Key press: schedules press tap for 'C' (50ms duration)
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 1, true);
    // s_press_end_tick recorded as 1000 + 50 = 1050
    TEST_ASSERT_EQUAL(1050, s_press_end_tick[1]);

    // Key released 20ms later (at tick 1020, 30ms remaining before press tap finishes)
    _mock_tick_count = 1020;
    mock_timer_set(1020000LL);
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 1, false);

    // Slot 0 has press tap 'C' (ends at 1050ms)
    // Slot 1 has release tap 'D' scheduled with delay = 30ms (press at 1020 + 30 = 1050ms)
    TEST_ASSERT_EQUAL(0x07, s_tap_slots[1].action_code);
    TEST_ASSERT_EQUAL(1050000LL, s_tap_slots[1].press_deadline_us);
    TEST_ASSERT_EQUAL(1080000LL, s_tap_slots[1].release_deadline_us);

    // Tick at 1020ms: 'C' is pressed, 'D' is still waiting
    kb_macro_tap_tick(1020000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x06));
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x07));

    // Tick at 1050ms: 'C' releases, 'D' presses!
    kb_macro_tap_tick(1050000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x06));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x07));

    // Tick at 1080ms: 'D' releases
    kb_macro_tap_tick(1080000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x07));
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}

TEST_CASE(kb_custom_key, pr_mode_without_wait_for_finish_fires_immediately) {
    // Custom key 1: wait_for_finish = false
    s_ckeys[1].id = 1;
    s_ckeys[1].mode = CKEY_MODE_PRESS_RELEASE;
    s_ckeys[1].rules.pr.press_sustain = false;
    s_ckeys[1].rules.pr.press_action = 0x06;
    s_ckeys[1].rules.pr.press_tap_release_delay_ms = 50;
    s_ckeys[1].rules.pr.wait_for_finish = false;
    s_ckeys[1].rules.pr.release_action = 0x07;
    s_ckeys[1].rules.pr.release_tap_release_delay_ms = 30;
    s_ckey_count = 2;

    _mock_tick_count = 1000;
    mock_timer_set(1000000LL);
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 1, true);

    _mock_tick_count = 1020;
    mock_timer_set(1020000LL);
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 1, false);

    // Release action 'D' scheduled with delay 0 (press deadline = 1020ms)
    TEST_ASSERT_EQUAL(1020000LL, s_tap_slots[1].press_deadline_us);
}

TEST_CASE(kb_custom_key, ma_mode_single_tap) {
    s_ckeys[2].id = 2;
    s_ckeys[2].mode = CKEY_MODE_MULTI_ACTION;
    s_ckeys[2].rules.ma.tap_action = 0x14; // 'Q'
    s_ckeys[2].rules.ma.tap_release_delay_ms = 25;
    s_ckeys[2].rules.ma.double_tap_action = 0x15;
    s_ckeys[2].rules.ma.hold_action = 0x16;
    s_ckeys[2].rules.ma.hold_sustain = false;
    s_ckey_count = 3;

    mock_timer_set(2000000LL);

    // Simulate system action event SINGLE_TAP for CKey 2
    kb_sys_action_event_t ev = {
        .action_code = ACTION_CODE_CKEY_MIN + 2,
        .event = KB_EV_SINGLE_TAP,
    };
    ckey_action_event_handler(NULL, KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev);

    // Tap action 'Q' scheduled
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());
    kb_macro_tap_tick(2000000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x14));

    kb_macro_tap_tick(2025000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x14));
}

TEST_CASE(kb_custom_key, ma_mode_double_tap) {
    s_ckeys[2].id = 2;
    s_ckeys[2].mode = CKEY_MODE_MULTI_ACTION;
    s_ckeys[2].rules.ma.tap_action = 0x14;
    s_ckeys[2].rules.ma.double_tap_action = 0x15; // 'R'
    s_ckeys[2].rules.ma.double_tap_release_delay_ms = 35;
    s_ckeys[2].rules.ma.hold_action = 0x16;
    s_ckeys[2].rules.ma.hold_sustain = false;
    s_ckey_count = 3;

    mock_timer_set(3000000LL);

    kb_sys_action_event_t ev = {
        .action_code = ACTION_CODE_CKEY_MIN + 2,
        .event = KB_EV_DOUBLE_TAP,
    };
    ckey_action_event_handler(NULL, KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev);

    TEST_ASSERT_TRUE(kb_macro_has_active_taps());
    kb_macro_tap_tick(3000000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x15));

    kb_macro_tap_tick(3035000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x15));
}

TEST_CASE(kb_custom_key, ma_mode_hold_with_sustain) {
    s_ckeys[3].id = 3;
    s_ckeys[3].mode = CKEY_MODE_MULTI_ACTION;
    s_ckeys[3].rules.ma.hold_action = 0xE1; // Left Shift
    s_ckeys[3].rules.ma.hold_sustain = true;
    s_ckey_count = 4;

    mock_timer_set(4000000LL);

    // Event HOLD arrives
    kb_sys_action_event_t ev_hold = {
        .action_code = ACTION_CODE_CKEY_MIN + 3,
        .event = KB_EV_HOLD,
    };
    ckey_action_event_handler(NULL, KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev_hold);

    // Sustained hold: action 0xE1 pressed immediately in s_v_nkro
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0xE1));
    TEST_ASSERT_TRUE(s_ma_hold_sustained[3]);

    // Event RELEASE arrives
    kb_sys_action_event_t ev_rel = {
        .action_code = ACTION_CODE_CKEY_MIN + 3,
        .event = KB_EV_RELEASE,
    };
    ckey_action_event_handler(NULL, KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev_rel);

    // Released!
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0xE1));
    TEST_ASSERT_FALSE(s_ma_hold_sustained[3]);
}

TEST_CASE(kb_custom_key, is_tap_hold_lookup) {
    s_ckeys[0].id = 0;
    s_ckeys[0].mode = CKEY_MODE_PRESS_RELEASE;

    s_ckeys[2].id = 2;
    s_ckeys[2].mode = CKEY_MODE_MULTI_ACTION;
    s_ckeys[2].rules.ma.flavor = CKEY_FLAVOR_HOLD_ON_OTHER_KEY_PRESS;
    s_ckeys[2].rules.ma.hold_threshold_ms = 220;

    s_ckey_count = 3;

    cfg_ckey_ma_t ma = {0};

    // ID 2 is MultiAction -> true
    TEST_ASSERT_TRUE(kb_custom_key_is_tap_hold(ACTION_CODE_CKEY_MIN + 2, &ma));
    TEST_ASSERT_EQUAL(CKEY_FLAVOR_HOLD_ON_OTHER_KEY_PRESS, ma.flavor);
    TEST_ASSERT_EQUAL(220, ma.hold_threshold_ms);

    // ID 0 is PressRelease -> false
    TEST_ASSERT_FALSE(kb_custom_key_is_tap_hold(ACTION_CODE_CKEY_MIN + 0, &ma));

    // Non-CKey action code -> false
    TEST_ASSERT_FALSE(kb_custom_key_is_tap_hold(0x0004, &ma));
    TEST_ASSERT_FALSE(kb_custom_key_is_tap_hold(ACTION_CODE_CKEY_MAX + 1, &ma));
}

TEST_CASE(kb_custom_key, reload_flag_consumed_by_check_reload) {
    atomic_store_explicit(&s_ckey_reload_pending, true, memory_order_release);
    kb_custom_key_check_reload();
    TEST_ASSERT_FALSE(atomic_load_explicit(&s_ckey_reload_pending, memory_order_relaxed));
}
