#include "test_harness.h"
#include "power_mgr.h"
#include "esp_pm.h"
#include "esp_sleep.h"

TEST_CASE(power_mgr, init_and_initial_active_state) {
    mock_pm_reset();
    mock_sleep_reset();
    power_mgr_test_reset();

    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_mgr_get_state());
    TEST_ASSERT(_mock_pm_configured_count >= 1);
    TEST_ASSERT(_mock_pm_lock_held);
    TEST_ASSERT_EQUAL(240, _mock_pm_config.max_freq_mhz);
    TEST_ASSERT_EQUAL(80, _mock_pm_config.min_freq_mhz);
    TEST_ASSERT(_mock_pm_config.light_sleep_enable);
}

TEST_CASE(power_mgr, activity_keeps_active_within_timeout) {
    power_mgr_test_reset();

    /* 2.5 seconds inactive (< 5.0 s idle threshold) */
    power_mgr_test_set_inactive_ms(2500);
    power_mgr_update();

    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_mgr_get_state());
    TEST_ASSERT(_mock_pm_lock_held);
}

TEST_CASE(power_mgr, inactivity_transitions_to_idle) {
    power_mgr_test_reset();

    /* 5.0 seconds inactive -> transition to IDLE */
    power_mgr_test_set_inactive_ms(5000);
    power_mgr_update();

    TEST_ASSERT_EQUAL(POWER_STATE_IDLE, power_mgr_get_state());
    TEST_ASSERT(!_mock_pm_lock_held);
}

TEST_CASE(power_mgr, activity_from_idle_restores_active) {
    power_mgr_test_reset();

    /* Enter IDLE */
    power_mgr_test_set_inactive_ms(5000);
    power_mgr_update();
    TEST_ASSERT_EQUAL(POWER_STATE_IDLE, power_mgr_get_state());

    /* Key press or communication activity */
    power_mgr_test_set_inactive_ms(0);
    power_mgr_activity_notify();

    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_mgr_get_state());
    TEST_ASSERT(_mock_pm_lock_held);
}

TEST_CASE(power_mgr, light_sleep_inhibited_by_usb) {
    power_mgr_test_reset();

    /* Inactive for 30s in IDLE */
    power_mgr_test_set_inactive_ms(5000);
    power_mgr_update();
    power_mgr_test_set_inactive_ms(30000);

    /* When USB is active, light sleep must be forbidden */
    power_mgr_set_usb_active(true);
    TEST_ASSERT(!power_mgr_can_enter_light_sleep());

    /* When USB is disconnected, light sleep is permitted once in IDLE */
    power_mgr_set_usb_active(false);
    power_mgr_update();
    TEST_ASSERT(power_mgr_can_enter_light_sleep());
}

TEST_CASE(power_mgr, light_sleep_execution_and_safe_wakeup) {
    mock_sleep_reset();
    power_mgr_test_reset();
    power_mgr_set_usb_active(false);

    /* Inactive for 30s in IDLE */
    power_mgr_test_set_inactive_ms(5000);
    power_mgr_update();
    power_mgr_test_set_inactive_ms(30000);

    TEST_ASSERT(power_mgr_can_enter_light_sleep());

    esp_err_t err = power_mgr_enter_light_sleep();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_light_sleep_enter_count);

    /* Wakeup automatically restores ACTIVE state and frequency lock */
    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_mgr_get_state());
    TEST_ASSERT(_mock_pm_lock_held);
}

TEST_CASE(power_mgr, deep_sleep_qualification_and_execution) {
    mock_sleep_reset();
    power_mgr_test_reset();
    power_mgr_set_usb_active(false);

    /* Inactive for 15 minutes (900,000 ms) */
    power_mgr_test_set_inactive_ms(900000);

    TEST_ASSERT(power_mgr_can_enter_deep_sleep());

    power_mgr_enter_deep_sleep();

    TEST_ASSERT_EQUAL(1, _mock_deep_sleep_enter_count);
    TEST_ASSERT_EQUAL(POWER_STATE_DEEP_SLEEP, power_mgr_get_state());

    /* Ext1 wakeup mask must cover GPIO 1 through 6: (1<<1) | ... | (1<<6) = 0x7E */
    uint64_t expected_mask = (1ULL << 1) | (1ULL << 2) | (1ULL << 3) |
                             (1ULL << 4) | (1ULL << 5) | (1ULL << 6);
    TEST_ASSERT_EQUAL_HEX(expected_mask, _mock_ext1_wakeup_mask);
    TEST_ASSERT_EQUAL(ESP_EXT1_WAKEUP_ANY_LOW, _mock_ext1_wakeup_mode);
}
