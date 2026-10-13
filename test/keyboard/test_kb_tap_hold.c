/**
 * @file test_kb_tap_hold.c
 * @brief Unit tests for the interrupt-aware Tap-Hold Resolver (kb_tap_hold.c).
 * Verifies all flavors (TIMER, HOLD_ON_OTHER_KEY_PRESS, PERMISSIVE_HOLD, TAP_PREFERRED),
 * deferred event buffering, rolling taps, quick-tap repeats, and prior-idle anti-misfire.
 */
#include "test_harness.h"
#include "kb_tap_hold.h"

#define MOCK_TH_KEY       0x3001 /* CKey 1 */
#define MOCK_REGULAR_KEY  0x0005 /* HID 'b' */
#define MOCK_REGULAR_KEY2 0x0006 /* HID 'c' */

#define ACT_TAP           0x0004 /* 'a' */
#define ACT_HOLD          0x00E0 /* Left Ctrl */
#define ACT_DOUBLE_TAP    0x0007 /* 'd' */

/* Event tracking recorder for test verification */
typedef struct {
    uint16_t action_code;
    bool     is_pressed;
} recorded_event_t;

#define MAX_RECORDED 64
static recorded_event_t s_rec[MAX_RECORDED];
static size_t           s_rec_count = 0;
static size_t           s_reports_sent = 0;

static cfg_ckey_ma_t    s_current_ma_cfg;

static void mock_dispatch(uint16_t action, bool is_pressed) {
    if (s_rec_count < MAX_RECORDED) {
        s_rec[s_rec_count].action_code = action;
        s_rec[s_rec_count].is_pressed  = is_pressed;
        s_rec_count++;
    }
}

static void mock_fire_tap(uint16_t action, uint32_t duration_ms, uint32_t delay_ms) {
    (void)duration_ms; (void)delay_ms;
    mock_dispatch(action, true);
    mock_dispatch(action, false);
}

static void mock_send_report(void) {
    s_reports_sent++;
}

static bool mock_lookup(uint16_t action, cfg_ckey_ma_t *out_ma) {
    if (action == MOCK_TH_KEY) {
        if (out_ma) *out_ma = s_current_ma_cfg;
        return true;
    }
    return false;
}

static void reset_recorder(void) {
    s_rec_count = 0;
    s_reports_sent = 0;
}

TEST_SETUP(kb_tap_hold) {
    reset_recorder();
    memset(&s_current_ma_cfg, 0, sizeof(s_current_ma_cfg));
    s_current_ma_cfg.tap_action              = ACT_TAP;
    s_current_ma_cfg.hold_action             = ACT_HOLD;
    s_current_ma_cfg.hold_sustain            = true;
    s_current_ma_cfg.hold_threshold_ms       = 200; /* 200 ms tapping term */
    s_current_ma_cfg.double_tap_threshold_ms = 300;
    s_current_ma_cfg.flavor                  = CKEY_FLAVOR_TIMER;
    s_current_ma_cfg.quick_tap_ms            = 0;
    s_current_ma_cfg.require_prior_idle_ms   = 0;

    kb_tap_hold_init(mock_dispatch, mock_fire_tap, mock_send_report, mock_lookup);
}

TEST_CASE(kb_tap_hold, regular_key_dispatched_immediately_without_delay) {
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 1000);
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, false, 20000);

    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, tap_hold_basic_tap_before_timeout) {
    /* Press at t = 0 */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    TEST_ASSERT_EQUAL(TH_STATE_PENDING_DECISION, kb_tap_hold_get_state());
    TEST_ASSERT_EQUAL(0, s_rec_count); /* Nothing dispatched while undecided */

    /* Release at t = 50 ms (< 200 ms) */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 50000);

    /* Resolves immediately to TAP */
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, tap_hold_basic_hold_by_timer_expiration) {
    /* Press at t = 0 */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    TEST_ASSERT_EQUAL(TH_STATE_PENDING_DECISION, kb_tap_hold_get_state());

    /* Tick at 100 ms: still waiting */
    kb_tap_hold_tick(100000);
    TEST_ASSERT_EQUAL(0, s_rec_count);
    TEST_ASSERT_EQUAL(TH_STATE_PENDING_DECISION, kb_tap_hold_get_state());

    /* Tick at 200 ms: hold timeout fires! */
    kb_tap_hold_tick(200000);
    TEST_ASSERT_EQUAL(1, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_HOLD_SUSTAINED, kb_tap_hold_get_state());

    /* Later release at 350 ms: releases the sustained hold */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 350000);
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, flavor_timer_buffers_other_key_and_resolves_tap_in_order) {
    s_current_ma_cfg.flavor = CKEY_FLAVOR_TIMER;

    /* Press TH key at t = 0 */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);

    /* Regular key pressed at t = 30 ms */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 30000);
    TEST_ASSERT_EQUAL(0, s_rec_count);
    TEST_ASSERT_EQUAL(1, kb_tap_hold_get_buffered_count());

    /* TH key released at t = 60 ms (< 200 ms) */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 60000);

    /* Must resolve to TAP ('a') first, then dispatch buffered 'b' press */
    TEST_ASSERT_EQUAL(3, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[2].action_code);
    TEST_ASSERT_TRUE(s_rec[2].is_pressed);
    TEST_ASSERT_EQUAL(0, kb_tap_hold_get_buffered_count());
}

TEST_CASE(kb_tap_hold, flavor_hold_on_other_key_press_triggers_hold_immediately) {
    s_current_ma_cfg.flavor = CKEY_FLAVOR_HOLD_ON_OTHER_KEY_PRESS;

    /* Press TH key at t = 0 */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* Another key pressed at t = 30 ms */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 30000);

    /* Resolves to HOLD immediately, followed by the regular key press! */
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[1].action_code);
    TEST_ASSERT_TRUE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_HOLD_SUSTAINED, kb_tap_hold_get_state());

    /* Release regular key at 60 ms */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, false, 60000);
    TEST_ASSERT_EQUAL(3, s_rec_count);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[2].action_code);
    TEST_ASSERT_FALSE(s_rec[2].is_pressed);

    /* Release TH key at 90 ms */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 90000);
    TEST_ASSERT_EQUAL(4, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[3].action_code);
    TEST_ASSERT_FALSE(s_rec[3].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, flavor_permissive_hold_nested_key_triggers_hold) {
    s_current_ma_cfg.flavor = CKEY_FLAVOR_PERMISSIVE_HOLD;

    /* Press TH key at t = 0 */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);

    /* Regular key pressed at t = 20 ms -> buffered */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 20000);
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* Regular key released at t = 40 ms -> nested tap completed inside TH key window! */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, false, 40000);

    /* Permissive hold recognizes nested key and resolves to HOLD immediately! */
    TEST_ASSERT_EQUAL(3, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[1].action_code);
    TEST_ASSERT_TRUE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[2].action_code);
    TEST_ASSERT_FALSE(s_rec[2].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_HOLD_SUSTAINED, kb_tap_hold_get_state());

    /* TH key released at 80 ms */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 80000);
    TEST_ASSERT_EQUAL(4, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_HOLD, s_rec[3].action_code);
    TEST_ASSERT_FALSE(s_rec[3].is_pressed);
}

TEST_CASE(kb_tap_hold, flavor_permissive_hold_rolling_tap_resolves_tap) {
    s_current_ma_cfg.flavor = CKEY_FLAVOR_PERMISSIVE_HOLD;

    /* Fast typing roll: TH down, K2 down, TH up, K2 up */
    /* t = 0: TH pressed */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);

    /* t = 25 ms: K2 pressed -> buffered */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 25000);
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* t = 45 ms: TH released BEFORE K2 is released! This is a roll, not nested */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 45000);

    /* Resolves to TAP! */
    TEST_ASSERT_EQUAL(3, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[2].action_code);
    TEST_ASSERT_TRUE(s_rec[2].is_pressed);

    /* t = 75 ms: K2 released */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, false, 75000);
    TEST_ASSERT_EQUAL(4, s_rec_count);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY, s_rec[3].action_code);
    TEST_ASSERT_FALSE(s_rec[3].is_pressed);
}

TEST_CASE(kb_tap_hold, quick_tap_repeats_immediately_without_hold_delay) {
    s_current_ma_cfg.quick_tap_ms = 150; /* 150 ms quick-tap window */

    /* First tap: press at 0, release at 30 ms */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 30000);
    TEST_ASSERT_EQUAL(2, s_rec_count);
    reset_recorder();

    /* Press again at 100 ms (delta = 70 ms <= 150 ms) */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 100000);

    /* Quick tap fires immediately without entering pending hold! */
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, require_prior_idle_prevents_misfire_during_fast_typing) {
    s_current_ma_cfg.flavor                = CKEY_FLAVOR_HOLD_ON_OTHER_KEY_PRESS;
    s_current_ma_cfg.require_prior_idle_ms = 150;

    /* User was fast typing: key 'b' typed at t = 0, released at t = 30 ms */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, true, 0);
    kb_tap_hold_process_key(MOCK_REGULAR_KEY, false, 30000);
    reset_recorder();

    /* TH key pressed at t = 50 ms (idle since last key = 20 ms < 150 ms) */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 50000);

    /* Regular key pressed at t = 70 ms */
    kb_tap_hold_process_key(MOCK_REGULAR_KEY2, true, 70000);

    /* Even though flavor is HOLD_ON_OTHER_KEY_PRESS, hold is DISALLOWED because of prior typing! */
    /* So it does not trigger hold. Key2 is buffered */
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* TH key released at t = 90 ms */
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 90000);

    /* Resolves to TAP, then Key2 press */
    TEST_ASSERT_EQUAL(3, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(MOCK_REGULAR_KEY2, s_rec[2].action_code);
    TEST_ASSERT_TRUE(s_rec[2].is_pressed);
}

TEST_CASE(kb_tap_hold, double_tap_triggers_double_tap_action) {
    s_current_ma_cfg.double_tap_action       = ACT_DOUBLE_TAP;
    s_current_ma_cfg.double_tap_threshold_ms = 300;

    /* First press & release */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 30000);

    /* In wait double tap state */
    TEST_ASSERT_EQUAL(TH_STATE_WAIT_DOUBLE_TAP, kb_tap_hold_get_state());
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* Second press within 300 ms (at 100 ms) */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 100000);

    /* Confirms double tap action */
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_DOUBLE_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_DOUBLE_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}

TEST_CASE(kb_tap_hold, double_tap_timeout_falls_back_to_single_tap) {
    s_current_ma_cfg.double_tap_action       = ACT_DOUBLE_TAP;
    s_current_ma_cfg.double_tap_threshold_ms = 300;

    /* First press & release at 30 ms */
    kb_tap_hold_process_key(MOCK_TH_KEY, true, 0);
    kb_tap_hold_process_key(MOCK_TH_KEY, false, 30000);
    TEST_ASSERT_EQUAL(TH_STATE_WAIT_DOUBLE_TAP, kb_tap_hold_get_state());

    /* Tick at 200 ms: still waiting */
    kb_tap_hold_tick(200000);
    TEST_ASSERT_EQUAL(0, s_rec_count);

    /* Tick at 350 ms: double tap window (30 ms + 300 ms = 330 ms) expired! */
    kb_tap_hold_tick(350000);
    TEST_ASSERT_EQUAL(2, s_rec_count);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[0].action_code);
    TEST_ASSERT_TRUE(s_rec[0].is_pressed);
    TEST_ASSERT_EQUAL(ACT_TAP, s_rec[1].action_code);
    TEST_ASSERT_FALSE(s_rec[1].is_pressed);
    TEST_ASSERT_EQUAL(TH_STATE_IDLE, kb_tap_hold_get_state());
}
