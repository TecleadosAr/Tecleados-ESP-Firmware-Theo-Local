/**
 * @file test_kb_report.c
 * @brief Tests for kb_report.c — HID report routing and NKRO-to-6KRO conversion.
 *
 * Tests the REAL production kb_hid_ready, kb_send_report, and
 * kb_send_consumer_report linked from components/keyboard/kb_report.c.
 * virtual_nkro_to_6kro is static — tested indirectly via kb_send_report.
 */
#include "test_harness.h"

/* All constants (HID_MODIFIER_*, NKRO_*, SIXKRO_*)
   and kb_bit_set provided by test_constants.h.
   Production functions (kb_hid_ready, kb_send_report, kb_send_consumer_report)
   are linked via main.c. Mock USB/BLE state is in mock_tinyusb.h. */

/* ---- Setup ---- */

TEST_SETUP(kb_report) {
    mock_usb_reset();
}

/* ---- NKRO-to-6KRO conversion (tested via real kb_send_report → captured mock reports) ---- */

TEST_CASE(kb_report, send_report_empty_nkro) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true; /* 6KRO mode — easier to inspect */

    esp_err_t err = kb_send_report(nkro);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    /* 6KRO report: [modifier, k1..k6] — all zero */
    TEST_ASSERT_EQUAL(0, _mock_reports[0].data[0]); /* modifier */
    for (int i = 1; i <= 6; i++) TEST_ASSERT_EQUAL(0, _mock_reports[0].data[i]);
}

TEST_CASE(kb_report, send_report_single_key) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04); /* HID_KEY_A */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0, _mock_reports[0].data[0]); /* no modifier */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* first key */
}

TEST_CASE(kb_report, send_report_modifier_only) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, HID_MODIFIER_MIN);     /* Left Control */
    kb_bit_set(nkro, HID_MODIFIER_MIN + 1); /* Left Shift */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0x03, _mock_reports[0].data[0]); /* bits 0+1 */
    for (int i = 1; i <= 6; i++) TEST_ASSERT_EQUAL(0, _mock_reports[0].data[i]);
}

TEST_CASE(kb_report, send_report_six_keys_plus_modifier) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, HID_MODIFIER_MIN); /* Left Control */
    for (int k = 0x04; k <= 0x09; k++) kb_bit_set(nkro, k); /* A-F */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0x01, _mock_reports[0].data[0]); /* modifier */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* A */
    TEST_ASSERT_EQUAL(0x09, _mock_reports[0].data[6]); /* F */
}

TEST_CASE(kb_report, send_report_more_than_six_truncates) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    for (int k = 0x04; k <= 0x0C; k++) kb_bit_set(nkro, k); /* 9 keys */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    /* Only first 6 non-modifier keys fit */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* A */
    TEST_ASSERT_EQUAL(0x09, _mock_reports[0].data[6]); /* F */
}

TEST_CASE(kb_report, send_report_all_modifiers) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    for (int m = HID_MODIFIER_MIN; m <= HID_MODIFIER_MAX; m++) kb_bit_set(nkro, m);
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0xFF, _mock_reports[0].data[0]);
}

TEST_CASE(kb_report, send_report_nkro_protocol) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04);
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false; /* NKRO mode */

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    TEST_ASSERT_EQUAL(2, _mock_reports[0].report_id); /* NKRO report */
}

/* ---- Routing tests (real kb_hid_ready) ---- */

TEST_CASE(kb_report, hid_ready_usb_mounted) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;
    TEST_ASSERT_TRUE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_ready_ble_routing) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;
    TEST_ASSERT_TRUE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_not_ready_ble_routing_disconnected) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = false;
    TEST_ASSERT_FALSE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_not_ready_usb_not_mounted) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = false;
    TEST_ASSERT_FALSE(kb_hid_ready());
}

TEST_CASE(kb_report, routing_priority_ble_over_usb) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;

    /* Send a report — should go to BLE, not USB */
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04);
    kb_send_report(nkro);

    /* BLE got the report */
    TEST_ASSERT_EQUAL(1, _mock_ble_send_count);
    /* USB did NOT get a report */
    TEST_ASSERT_EQUAL(0, _mock_report_count);
}

TEST_CASE(kb_report, send_report_fails_when_ble_disconnected) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = false;

    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    esp_err_t err = kb_send_report(nkro);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

/* ---- Consumer report routing (real kb_send_consumer_report) ---- */

TEST_CASE(kb_report, consumer_report_via_usb) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;

    esp_err_t err = kb_send_consumer_report(0x00CD); /* Play/Pause */
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    TEST_ASSERT_EQUAL(4, _mock_reports[0].report_id); /* Consumer report */
}

TEST_CASE(kb_report, consumer_report_via_ble) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;

    esp_err_t err = kb_send_consumer_report(0x00CD);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_ble_send_count);
}

/* ---- Report blocked telemetry & histogram tests ---- */

TEST_CASE(kb_report, blocked_stats_reset_clears_all) {
    kb_macro_reset_blocked_stats();
    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);

    TEST_ASSERT_EQUAL(0, stats.total_reports);
    TEST_ASSERT_EQUAL(0, stats.failed_reports);
    TEST_ASSERT_EQUAL(0, stats.max_blocked_us);
    TEST_ASSERT_EQUAL(0, stats.total_blocked_us);
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL(0, stats.buckets[i]);
    }
}

TEST_CASE(kb_report, blocked_stats_bucket_distribution) {
    kb_macro_reset_blocked_stats();

    /* Track values into each bucket:
     * [0]: < 100 us       -> 50 us
     * [1]: 100 us .. 1 ms -> 500 us
     * [2]: 1 ms .. 5 ms   -> 2000 us
     * [3]: 5 ms .. 20 ms  -> 10000 us
     * [4]: 20 ms .. 50 ms -> 30000 us
     * [5]: 50 ms .. 100 ms-> 80000 us
     * [6]: >= 100 ms      -> 120000 us
     */
    kb_macro_track_blocked_time(50, 0, ESP_OK);
    kb_macro_track_blocked_time(500, 1, ESP_OK);
    kb_macro_track_blocked_time(2000, 2, ESP_OK);
    kb_macro_track_blocked_time(10000, 10, ESP_OK);
    kb_macro_track_blocked_time(30000, 30, ESP_OK);
    kb_macro_track_blocked_time(80000, 80, ESP_OK);
    kb_macro_track_blocked_time(120000, 100, ESP_FAIL);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);

    TEST_ASSERT_EQUAL(7, stats.total_reports);
    TEST_ASSERT_EQUAL(1, stats.failed_reports);
    TEST_ASSERT_EQUAL(120000, stats.max_blocked_us);
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL(1, stats.buckets[i]);
    }
}

TEST_CASE(kb_report, blocked_stats_tracks_failed_reports) {
    kb_macro_reset_blocked_stats();

    kb_macro_track_blocked_time(1000, 1, ESP_OK);
    kb_macro_track_blocked_time(2000, 2, ESP_FAIL);
    kb_macro_track_blocked_time(3000, 3, ESP_ERR_TIMEOUT);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);

    TEST_ASSERT_EQUAL(3, stats.total_reports);
    TEST_ASSERT_EQUAL(2, stats.failed_reports);
    TEST_ASSERT_EQUAL(6000, stats.total_blocked_us);
    TEST_ASSERT_EQUAL(3000, stats.max_blocked_us);
}

TEST_CASE(kb_report, send_report_nowait_succeeds_when_ready) {
    mock_usb_reset();
    _mock_usb_mounted = true;
    _mock_ble_routing_active = false;
    kb_macro_reset_blocked_stats();

    // Ensure mutex is created
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }

    kb_macro_virtual_press(0x04); // HID_KEY_A
    esp_err_t err = kb_macro_send_report_nowait();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);
    TEST_ASSERT_EQUAL(1, stats.total_reports);
    TEST_ASSERT_EQUAL(0, stats.failed_reports);

    kb_macro_virtual_release(0x04);
}

TEST_CASE(kb_report, send_report_nowait_fails_without_blocking) {
    mock_usb_reset();
    _mock_usb_mounted = false;
    _mock_ble_routing_active = false;
    kb_macro_reset_blocked_stats();

    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }

    kb_macro_virtual_press(0x05); // HID_KEY_B
    esp_err_t err = kb_macro_send_report_nowait();
    TEST_ASSERT(err != ESP_OK);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);
    TEST_ASSERT_EQUAL(1, stats.total_reports);
    TEST_ASSERT_EQUAL(1, stats.failed_reports);

    kb_macro_virtual_release(0x05);
}

TEST_CASE(kb_report, send_report_blocking_retries_and_times_out) {
    mock_usb_reset();
    _mock_usb_mounted = false;
    _mock_ble_routing_active = false;
    kb_macro_reset_blocked_stats();

    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t err = kb_macro_send_report_blocking(5);
    TEST_ASSERT(err != ESP_OK);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);
    TEST_ASSERT_EQUAL(1, stats.total_reports);
    TEST_ASSERT_EQUAL(1, stats.failed_reports);
}

TEST_CASE(kb_report, send_report_nowait_zero_retry_on_failure) {
    mock_usb_reset();
    _mock_usb_mounted = false;
    _mock_ble_routing_active = false;
    kb_macro_reset_blocked_stats();

    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = kb_macro_send_report_nowait();
    int64_t elapsed = esp_timer_get_time() - t0;

    TEST_ASSERT(err != ESP_OK);
    // nowait must fail immediately without spinning or delays
    TEST_ASSERT(elapsed < 1000);

    kb_report_blocked_stats_t stats;
    kb_macro_get_blocked_stats(&stats);
    TEST_ASSERT_EQUAL(1, stats.total_reports);
    TEST_ASSERT_EQUAL(1, stats.failed_reports);
}

TEST_CASE(kb_report, report_pending_backoff_simulation) {
    bool s_report_pending = false;
    uint32_t s_pending_backoff_ms = 1;
    int64_t s_next_retry_us = 0;
    int64_t now_us = 1000000LL;
    int64_t s_last_report_sent_us = 0; // >1s ago, keepalive would be due normally

    // 1. Initial key release failed
    s_report_pending = true;
    s_next_retry_us = now_us + (int64_t)s_pending_backoff_ms * 1000LL;
    if (s_pending_backoff_ms < 16) s_pending_backoff_ms *= 2;

    // While report is pending, keepalive MUST NOT fire
    const uint32_t min_report_rate_hz = 1;
    bool keepalive_due = !s_report_pending && (now_us > s_last_report_sent_us + 1000000LL / min_report_rate_hz);
    TEST_ASSERT_EQUAL(false, keepalive_due);

    // Advance 500 us: retry not due yet
    now_us += 500;
    bool retry_due = s_report_pending && (now_us >= s_next_retry_us);
    TEST_ASSERT_EQUAL(false, retry_due);

    // Advance to 1000 us (1 ms): retry due!
    now_us += 500;
    retry_due = s_report_pending && (now_us >= s_next_retry_us);
    TEST_ASSERT_EQUAL(true, retry_due);

    // Simulate second failure: backoff doubles to 4
    TEST_ASSERT_EQUAL(2, s_pending_backoff_ms);
    s_next_retry_us = now_us + (int64_t)s_pending_backoff_ms * 1000LL;
    s_pending_backoff_ms *= 2;
    TEST_ASSERT_EQUAL(4, s_pending_backoff_ms);

    // Simulate repeated failures up to clamp at 16 ms
    for (int i = 0; i < 5; i++) {
        now_us = s_next_retry_us;
        s_next_retry_us = now_us + (int64_t)s_pending_backoff_ms * 1000LL;
        if (s_pending_backoff_ms < 16) s_pending_backoff_ms *= 2;
    }
    TEST_ASSERT_EQUAL(16, s_pending_backoff_ms);

    // Simulate success on reconnect
    s_report_pending = false;
    s_pending_backoff_ms = 1;
    s_last_report_sent_us = now_us;

    TEST_ASSERT_EQUAL(false, s_report_pending);
    TEST_ASSERT_EQUAL(1, s_pending_backoff_ms);
}

TEST_CASE(kb_report, tap_scheduler_lifecycle) {
    mock_usb_reset();
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false;
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_tap_slots, 0, sizeof(s_tap_slots));
    memset(s_v_nkro, 0, sizeof(s_v_nkro));

    mock_timer_set(1000000LL);
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());

    kb_macro_fire_tap(0x04, 20, 0); // Key A, duration 20ms, delay 0
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04)); // Not pressed yet until tick

    // Tick at 1000ms: fires press
    kb_macro_tap_tick(1000000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());

    // Tick at 1010ms (+10ms): still pressed
    kb_macro_tap_tick(1010000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());

    // Tick at 1020ms (+20ms): duration elapsed, fires release
    kb_macro_tap_tick(1020000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}

TEST_CASE(kb_report, tap_scheduler_delayed_press) {
    mock_usb_reset();
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false;
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_tap_slots, 0, sizeof(s_tap_slots));
    memset(s_v_nkro, 0, sizeof(s_v_nkro));

    mock_timer_set(1000000LL);
    kb_macro_fire_tap(0x05, 30, 50); // Key B, duration 30ms, delay 50ms
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());

    // At t = 1000ms: not due
    kb_macro_tap_tick(1000000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));

    // At t = 1049ms: not due yet
    kb_macro_tap_tick(1049000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));

    // At t = 1050ms: delay reached, press fires
    kb_macro_tap_tick(1050000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x05));

    // At t = 1079ms: duration not elapsed
    kb_macro_tap_tick(1079000LL);
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x05));

    // At t = 1080ms (+30ms from press): release fires
    kb_macro_tap_tick(1080000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x05));
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}

TEST_CASE(kb_report, tap_scheduler_pool_overflow_17_slots) {
    mock_usb_reset();
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false;
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_tap_slots, 0, sizeof(s_tap_slots));
    memset(s_v_nkro, 0, sizeof(s_v_nkro));

    mock_timer_set(2000000LL);

    // Fire 16 concurrent taps (actions 0x04..0x13)
    for (uint16_t k = 0x04; k < 0x04 + 16; k++) {
        kb_macro_fire_tap(k, 50, 0);
    }
    TEST_ASSERT_TRUE(kb_macro_has_active_taps());

    // Attempt 17th tap (action 0x14) - should be dropped gracefully
    kb_macro_fire_tap(0x14, 50, 0);

    // Tick to activate the 16 accepted taps
    kb_macro_tap_tick(2000000LL);

    // Verify all 16 accepted keys are pressed
    for (uint16_t k = 0x04; k < 0x04 + 16; k++) {
        TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, (uint8_t)k));
    }
    // Verify 17th key was NOT pressed
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x14));

    // Advance 50ms to release all 16 taps
    kb_macro_tap_tick(2050000LL);
    for (uint16_t k = 0x04; k < 0x04 + 16; k++) {
        TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, (uint8_t)k));
    }
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}

TEST_CASE(kb_report, tap_scheduler_releases_before_presses) {
    mock_usb_reset();
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false;
    if (!s_v_nkro_mutex) {
        s_v_nkro_mutex = xSemaphoreCreateMutex();
    }
    memset(s_tap_slots, 0, sizeof(s_tap_slots));
    memset(s_v_nkro, 0, sizeof(s_v_nkro));

    mock_timer_set(3000000LL);

    // Manually configure slot 0 as WAIT_RELEASE for key 0x04 expiring at t = 3000ms
    s_tap_slots[0].action_code = 0x04;
    s_tap_slots[0].state = TAP_SLOT_WAIT_RELEASE;
    s_tap_slots[0].release_deadline_us = 3000000LL;
    kb_bit_set(s_v_nkro, 0x04);

    // Manually configure slot 1 as WAIT_PRESS for same key 0x04 firing at t = 3000ms
    s_tap_slots[1].action_code = 0x04;
    s_tap_slots[1].state = TAP_SLOT_WAIT_PRESS;
    s_tap_slots[1].press_deadline_us = 3000000LL;
    s_tap_slots[1].release_deadline_us = 3050000LL;

    // Both fire in the same tick: release clears 0x04, then press sets 0x04
    kb_macro_tap_tick(3000000LL);

    // If release was processed first and then press, key 0x04 remains pressed!
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_EQUAL(TAP_SLOT_FREE, s_tap_slots[0].state);
    TEST_ASSERT_EQUAL(TAP_SLOT_WAIT_RELEASE, s_tap_slots[1].state);

    // Release of slot 1
    kb_macro_tap_tick(3050000LL);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_FALSE(kb_macro_has_active_taps());
}


