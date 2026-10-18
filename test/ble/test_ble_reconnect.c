/**
 * @file test_ble_reconnect.c
 * @brief Unit tests for decoupled BLE reconnection state machine.
 */

#include "test_harness.h"
#include "ble_reconnect.h"

TEST_CASE(ble_reconnect, init_and_reset) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    TEST_ASSERT_EQUAL(RECONN_PHASE_IDLE, r.phase);
    TEST_ASSERT_EQUAL(0, r.pending_bitmap);
    TEST_ASSERT_EQUAL(-1, r.directed_profile);
    TEST_ASSERT_EQUAL(-1, r.last_profile);
    TEST_ASSERT_EQUAL(0, r.selected_retries_left);

    ble_reconnect_start_selected(&r, 3, 5);
    TEST_ASSERT_EQUAL(RECONN_PHASE_SELECTED, r.phase);
    TEST_ASSERT_EQUAL(3, r.directed_profile);

    ble_reconnect_reset(&r);
    TEST_ASSERT_EQUAL(RECONN_PHASE_IDLE, r.phase);
    TEST_ASSERT_EQUAL(0, r.pending_bitmap);
    TEST_ASSERT_EQUAL(-1, r.directed_profile);
}

TEST_CASE(ble_reconnect, selected_retries_and_exhaustion_to_forever) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    int profile = 2;
    ble_reconnect_start_selected(&r, profile, 3);
    TEST_ASSERT_EQUAL(RECONN_PHASE_SELECTED, r.phase);
    TEST_ASSERT_EQUAL(3, r.selected_retries_left);

    // Timeout 1: 2 retries left -> retry selected
    ble_reconn_action_t act = ble_reconnect_on_adv_timeout(&r, profile, true);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_RETRY_SELECTED, act);
    TEST_ASSERT_EQUAL(2, r.selected_retries_left);
    TEST_ASSERT_EQUAL(profile, r.directed_profile);
    TEST_ASSERT_EQUAL(1, r.cycles[profile]);

    // Timeout 2: 1 retry left -> retry selected
    act = ble_reconnect_on_adv_timeout(&r, profile, true);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_RETRY_SELECTED, act);
    TEST_ASSERT_EQUAL(1, r.selected_retries_left);

    // Timeout 3: 0 retries left -> exhausted!
    // Profile 2 is valid -> added to pending_bitmap and phase becomes FOREVER
    act = ble_reconnect_on_adv_timeout(&r, profile, true);
    TEST_ASSERT_EQUAL(RECONN_PHASE_FOREVER, r.phase);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_START_ADVERTISE, act);
    TEST_ASSERT((r.pending_bitmap & (1u << profile)) != 0);
    TEST_ASSERT_EQUAL(profile, r.directed_profile);
}

TEST_CASE(ble_reconnect, selected_interleaving_secondary_profiles) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    // Start with 4 retries for profile 1, but with profile 3 waiting in pending_bitmap
    ble_reconnect_start_selected(&r, 1, 4);
    r.pending_bitmap = (1u << 3);

    // Timeout 1: retries_left goes from 4 to 3 (odd) -> retry selected
    ble_reconn_action_t act = ble_reconnect_on_adv_timeout(&r, 1, true);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_RETRY_SELECTED, act);
    TEST_ASSERT_EQUAL(1, r.directed_profile);

    // Timeout 2: retries_left goes from 3 to 2 (even) with pending_bitmap != 0
    // -> Interleaves! Picks secondary profile 3
    act = ble_reconnect_on_adv_timeout(&r, 1, true);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_START_ADVERTISE, act);
    TEST_ASSERT_EQUAL(3, r.directed_profile);
}

TEST_CASE(ble_reconnect, round_robin_forever_cycle) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    // Profiles 0, 2, 5
    uint16_t map = (1u << 0) | (1u << 2) | (1u << 5);
    ble_reconnect_start_forever(&r, map);

    TEST_ASSERT_EQUAL(0, ble_reconnect_pick_next(&r));
    TEST_ASSERT_EQUAL(2, ble_reconnect_pick_next(&r));
    TEST_ASSERT_EQUAL(5, ble_reconnect_pick_next(&r));
    // Cycles back to 0
    TEST_ASSERT_EQUAL(0, ble_reconnect_pick_next(&r));
    TEST_ASSERT_EQUAL(2, ble_reconnect_pick_next(&r));
    TEST_ASSERT_EQUAL(5, ble_reconnect_pick_next(&r));
}

TEST_CASE(ble_reconnect, connection_event_transitions_to_finite) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    // Forever phase with profiles 1 and 4
    uint16_t map = (1u << 1) | (1u << 4);
    ble_reconnect_start_forever(&r, map);

    // Profile 1 connects!
    ble_reconn_action_t act = ble_reconnect_on_connected(&r, 1);
    TEST_ASSERT_EQUAL(RECONN_PHASE_FINITE, r.phase);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_START_ADVERTISE, act);
    TEST_ASSERT_EQUAL((1u << 4), r.pending_bitmap);
    TEST_ASSERT_EQUAL(4, r.directed_profile);

    // Profile 4 connects!
    act = ble_reconnect_on_connected(&r, 4);
    TEST_ASSERT_EQUAL(RECONN_PHASE_IDLE, r.phase);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_NONE, act);
    TEST_ASSERT_EQUAL(0, r.pending_bitmap);
    TEST_ASSERT_EQUAL(-1, r.directed_profile);
}

TEST_CASE(ble_reconnect, finite_exhausts_after_5_cycles) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    uint16_t map = (1u << 2);
    ble_reconnect_start_finite(&r, map);

    // Cycles 1 to 4: keeps retrying profile 2
    for (int i = 1; i <= 4; i++) {
        ble_reconn_action_t act = ble_reconnect_on_adv_timeout(&r, 2, true);
        TEST_ASSERT_EQUAL(BLE_RECONN_ACT_START_ADVERTISE, act);
        TEST_ASSERT_EQUAL(RECONN_PHASE_FINITE, r.phase);
        TEST_ASSERT_EQUAL(i, r.cycles[2]);
        TEST_ASSERT_EQUAL(2, r.directed_profile);
    }

    // Cycle 5: exhausts profile 2 (cycles >= 5) -> cleared from pending bitmap -> idle!
    ble_reconn_action_t act = ble_reconnect_on_adv_timeout(&r, 2, true);
    TEST_ASSERT_EQUAL(RECONN_PHASE_IDLE, r.phase);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_FALLBACK_BACKGROUND, act);
    TEST_ASSERT_EQUAL(0, r.pending_bitmap);
    TEST_ASSERT_EQUAL(-1, r.directed_profile);
}

TEST_CASE(ble_reconnect, connect_failed_exhausts_to_forever) {
    ble_reconnect_t r;
    ble_reconnect_init(&r);

    ble_reconnect_start_selected(&r, 3, 1);

    // Connection failed with 1 retry left -> transitions to FOREVER
    ble_reconn_action_t act = ble_reconnect_on_connect_failed(&r, 3, true);
    TEST_ASSERT_EQUAL(RECONN_PHASE_FOREVER, r.phase);
    TEST_ASSERT_EQUAL(BLE_RECONN_ACT_START_ADVERTISE, act);
    TEST_ASSERT((r.pending_bitmap & (1u << 3)) != 0);
    TEST_ASSERT_EQUAL(3, r.directed_profile);
}
