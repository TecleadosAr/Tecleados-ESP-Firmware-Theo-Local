#include "test_harness.h"
#include "battery.h"
#include "mocks/mock_esp_event.h"
#include "event_bus.h"

TEST_CASE(battery, voltage_to_pct_curve_boundaries) {
    /* Upper saturation */
    TEST_ASSERT_EQUAL(100, battery_voltage_to_pct(4300));
    TEST_ASSERT_EQUAL(100, battery_voltage_to_pct(4200));

    /* Defined LUT inflection points */
    TEST_ASSERT_EQUAL(90, battery_voltage_to_pct(4050));
    TEST_ASSERT_EQUAL(75, battery_voltage_to_pct(3950));
    TEST_ASSERT_EQUAL(50, battery_voltage_to_pct(3850));
    TEST_ASSERT_EQUAL(30, battery_voltage_to_pct(3750));
    TEST_ASSERT_EQUAL(15, battery_voltage_to_pct(3650));
    TEST_ASSERT_EQUAL(5,  battery_voltage_to_pct(3500));
    TEST_ASSERT_EQUAL(0,  battery_voltage_to_pct(3300));

    /* Lower cutoff saturation */
    TEST_ASSERT_EQUAL(0, battery_voltage_to_pct(3200));
    TEST_ASSERT_EQUAL(0, battery_voltage_to_pct(2500));
}

TEST_CASE(battery, voltage_to_pct_linear_interpolation) {
    /* Midpoint between 4200 (100%) and 4050 (90%) is 4125 mV -> 95% */
    TEST_ASSERT_EQUAL(95, battery_voltage_to_pct(4125));

    /* Midpoint between 3850 (50%) and 3750 (30%) is 3800 mV -> 40% */
    TEST_ASSERT_EQUAL(40, battery_voltage_to_pct(3800));

    /* Midpoint between 3500 (5%) and 3300 (0%) is 3400 mV -> 2% */
    TEST_ASSERT_EQUAL(2, battery_voltage_to_pct(3400));
}

TEST_CASE(battery, mock_voltage_and_getters) {
    battery_test_reset();
    battery_set_mock_voltage_mv(3850);

    TEST_ASSERT_EQUAL(3850, battery_get_voltage_mv());
    TEST_ASSERT_EQUAL(50, battery_get_level_pct());
}

TEST_CASE(battery, monotonicity_and_hysteresis) {
    battery_test_reset();
    /* Start at 75% (3950 mV) */
    battery_set_mock_voltage_mv(3950);
    TEST_ASSERT_EQUAL(75, battery_get_level_pct());
    TEST_ASSERT(!battery_is_charging());

    /* Normal discharge drop to 50% (3850 mV) */
    battery_set_mock_voltage_mv(3850);
    TEST_ASSERT_EQUAL(50, battery_get_level_pct());
    TEST_ASSERT(!battery_is_charging());

    /* Small upward rebound (+2% to 3860 mV) must be suppressed by filter */
    battery_set_mock_voltage_mv(3860);
    TEST_ASSERT_EQUAL(50, battery_get_level_pct());
    TEST_ASSERT(!battery_is_charging());

    /* Another small rebound (+3% to 3865 mV) still suppressed */
    battery_set_mock_voltage_mv(3865);
    TEST_ASSERT_EQUAL(50, battery_get_level_pct());

    /* Large upward jump (charger connected -> 4200 mV / 100%) */
    battery_set_mock_voltage_mv(4200);
    TEST_ASSERT_EQUAL(100, battery_get_level_pct());
    TEST_ASSERT(battery_is_charging());
}

TEST_CASE(battery, event_bus_notification_on_change) {
    battery_test_reset();
    mock_events_reset();

    /* Set new mock voltage that changes level by > 2% */
    battery_set_mock_voltage_mv(3650); /* 15% */

    int matching = mock_event_count_matching(BATTERY_EVENTS, BATTERY_EVENT_LEVEL_CHANGED);
    TEST_ASSERT(matching >= 1);

    const mock_posted_event_t *ev = mock_event_find_last(BATTERY_EVENTS, BATTERY_EVENT_LEVEL_CHANGED);
    TEST_ASSERT_NOT_NULL(ev);

    uint8_t reported_pct = 0;
    memcpy(&reported_pct, ev->data, sizeof(reported_pct));
    TEST_ASSERT_EQUAL(15, reported_pct);
}
