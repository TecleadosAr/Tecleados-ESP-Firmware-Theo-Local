/**
 * @file test_kb_macro.c
 * @brief Unit tests for macro execution engine (kb_macro.c).
 *
 * Verifies all 8 execution modes (once_stack_once, once_no_stack, once_stack_n,
 * hold_repeat, hold_repeat_cancel, toggle_repeat, toggle_repeat_cancel, burst_n),
 * recursion depth limiting (depth <= 5), and cancellation cleanup (kb_macro_force_clear).
 */
#include "test_harness.h"
#include "kb_macro.h"
#include "cfg_macros.h"
#include "../../components/keyboard/kb_bitmap.h"

static cfg_macro_list_t s_test_macro_list;

TEST_SETUP(kb_macro) {
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    if (!s_macro_mutex) {
        s_macro_mutex = xSemaphoreCreateMutex();
    }
    if (!s_macro_queue) {
        s_macro_queue = xQueueCreate(32, sizeof(macro_queue_item_t));
    } else {
        xQueueReset(s_macro_queue);
    }

    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    memset(s_rt_state, 0, sizeof(s_rt_state));
    memset(&s_test_macro_list, 0, sizeof(s_test_macro_list));
    s_macros = &s_test_macro_list;
}

TEST_CASE(kb_macro, exec_mode_once_stack_once) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_ONCE_STACK_ONCE;

    // First press when not running: enqueues item
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));

    // Simulate macro running
    s_rt_state[0].is_running = true;
    s_rt_state[0].pending_count = 0;

    // Press while running: sets pending_count = 1
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, s_rt_state[0].pending_count);

    // Another press while running: pending_count stays 1 (capped)
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, s_rt_state[0].pending_count);
}

TEST_CASE(kb_macro, exec_mode_once_no_stack) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_ONCE_NO_STACK;

    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));

    s_rt_state[0].is_running = true;
    s_rt_state[0].pending_count = 0;

    // Press while running: ignored completely
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(0, s_rt_state[0].pending_count);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue)); // No new item
}

TEST_CASE(kb_macro, exec_mode_once_stack_n) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_ONCE_STACK_N;
    s_macros->macros[0].stack_max = 3;

    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));

    s_rt_state[0].is_running = true;
    s_rt_state[0].pending_count = 0;

    // 1st press while running
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(1, s_rt_state[0].pending_count);

    // 2nd press while running
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(2, s_rt_state[0].pending_count);

    // 3rd press while running -> reaches stack_max (3)
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(3, s_rt_state[0].pending_count);

    // 4th press while running: capped at 3
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(3, s_rt_state[0].pending_count);
}

TEST_CASE(kb_macro, exec_mode_hold_repeat) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_HOLD_REPEAT;

    // Press: sets key_held = true, enqueues item
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_TRUE(s_rt_state[0].key_held);
    TEST_ASSERT_FALSE(s_rt_state[0].cancel_requested);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));

    // Release: sets key_held = false
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, false);
    TEST_ASSERT_FALSE(s_rt_state[0].key_held);
    TEST_ASSERT_FALSE(s_rt_state[0].cancel_requested);
}

TEST_CASE(kb_macro, exec_mode_hold_repeat_cancel) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_HOLD_REPEAT_CANCEL;

    // Press: key_held = true, cancel_requested = false
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_TRUE(s_rt_state[0].key_held);
    TEST_ASSERT_FALSE(s_rt_state[0].cancel_requested);

    // Release: key_held = false, cancel_requested = true!
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, false);
    TEST_ASSERT_FALSE(s_rt_state[0].key_held);
    TEST_ASSERT_TRUE(s_rt_state[0].cancel_requested);
}

TEST_CASE(kb_macro, exec_mode_toggle_repeat) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_TOGGLE_REPEAT;

    // Press 1: turns toggle_active ON
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_TRUE(s_rt_state[0].toggle_active);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));

    // Press 2: turns toggle_active OFF
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_FALSE(s_rt_state[0].toggle_active);
    TEST_ASSERT_FALSE(s_rt_state[0].cancel_requested);
}

TEST_CASE(kb_macro, exec_mode_toggle_repeat_cancel) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_TOGGLE_REPEAT_CANCEL;

    // Press 1: toggle ON
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_TRUE(s_rt_state[0].toggle_active);
    TEST_ASSERT_FALSE(s_rt_state[0].cancel_requested);

    // Press 2: toggle OFF and cancel_requested = true
    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_FALSE(s_rt_state[0].toggle_active);
    TEST_ASSERT_TRUE(s_rt_state[0].cancel_requested);
}

TEST_CASE(kb_macro, exec_mode_burst_n) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].exec_mode = MACRO_EXEC_BURST_N;
    s_macros->macros[0].repeat_count = 5;

    kb_macro_process_action(ACTION_CODE_MACRO_MIN + 0, true);
    TEST_ASSERT_EQUAL(5, s_rt_state[0].burst_remaining);
    TEST_ASSERT_EQUAL(1, uxQueueMessagesWaiting(s_macro_queue));
}

TEST_CASE(kb_macro, execute_macro_events_press_and_release) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].event_count = 2;
    s_macros->macros[0].events[0].type = MACRO_EVT_KEY_PRESS;
    s_macros->macros[0].events[0].value = 0x04; // 'A'
    s_macros->macros[0].events[1].type = MACRO_EVT_KEY_RELEASE;
    s_macros->macros[0].events[1].value = 0x04;

    macro_rt_state_t rt = {0};
    bool ok = execute_macro_cancellable(0, 0, false, &rt);
    TEST_ASSERT_TRUE(ok);

    // Both press and release occurred -> key 0x04 is not held
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04));
}

TEST_CASE(kb_macro, execute_macro_events_tap) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].event_count = 1;
    s_macros->macros[0].events[0].type = MACRO_EVT_KEY_TAP;
    s_macros->macros[0].events[0].value = 0x05; // 'B'
    s_macros->macros[0].events[0].press_duration_ms = 5;

    macro_rt_state_t rt = {0};
    bool ok = execute_macro_cancellable(0, 0, false, &rt);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));
}

TEST_CASE(kb_macro, recursion_depth_limiting) {
    // Macro 0 calls Macro 1, Macro 1 calls Macro 0
    s_macros->count = 2;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].event_count = 1;
    s_macros->macros[0].events[0].type = MACRO_EVT_KEY_PRESS;
    s_macros->macros[0].events[0].value = ACTION_CODE_MACRO_MIN + 1; // Calls macro 1

    s_macros->macros[1].id = 1;
    s_macros->macros[1].event_count = 1;
    s_macros->macros[1].events[0].type = MACRO_EVT_KEY_PRESS;
    s_macros->macros[1].events[0].value = ACTION_CODE_MACRO_MIN + 0; // Calls macro 0

    macro_rt_state_t rt = {0};
    // Should terminate safely at depth > 5 without crashing or infinite recursion
    bool ok = execute_macro_cancellable(0, 0, false, &rt);
    TEST_ASSERT_TRUE(ok);
}

TEST_CASE(kb_macro, cancellation_cleans_virtual_keys) {
    s_macros->count = 1;
    s_macros->macros[0].id = 0;
    s_macros->macros[0].event_count = 3;
    s_macros->macros[0].events[0].type = MACRO_EVT_KEY_PRESS;
    s_macros->macros[0].events[0].value = 0x04; // 'A'
    s_macros->macros[0].events[1].type = MACRO_EVT_DELAY_MS;
    s_macros->macros[0].events[1].value = 100; // 100ms delay
    s_macros->macros[0].events[2].type = MACRO_EVT_KEY_RELEASE;
    s_macros->macros[0].events[2].value = 0x04;

    macro_rt_state_t rt = {0};
    rt.cancel_requested = true;

    // With is_cancellable = true and cancel_requested = true, should abort and clear virtual NKRO
    bool ok = execute_macro_cancellable(0, 0, true, &rt);
    TEST_ASSERT_FALSE(ok);

    // Verify s_v_nkro was completely cleared by kb_macro_force_clear()
    for (size_t i = 0; i < sizeof(s_v_nkro); i++) {
        TEST_ASSERT_EQUAL(0, s_v_nkro[i]);
    }
}
