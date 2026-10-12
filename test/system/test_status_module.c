/**
 * @file test_status_module.c
 * @brief Tests for statusmod.c — real production event handlers,
 *        BLE event processing, config event handling, split events, and status push.
 *
 * Links against real statusmod.c (included in main.c).
 * Uses mock event system to post events and dispatch to real handlers,
 * then verifies the binary statusmod_msg_t push via mock_comm.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"
#include "mocks/mock_tinyusb.h"
#include "mocks/mock_comm.h"
#include "statusmod.h"

/* Production: status_module_init() from linked statusmod.c.
   Event bases/IDs from linked event_bus.c via event_bus.h.
   cfg_ble_get_state() from cfg_ble.h shim (controllable mock).
   comm_send_message() from mock_comm.h (captures payload). */

TEST_SETUP(status_module) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_reset();
    mock_comm_reset();
    /* Init with default BLE state (profile 0, USB mode) */
    status_module_init();
    /* Clear posted events and comm records after init */
    _mock_event_count = 0;
    _mock_comm_last_len = 0;
    _mock_comm_send_count = 0;
}

/* Helper: clear posted events without wiping handler registrations */
static void clear_posted_events(void) {
    _mock_event_count = 0;
}

/* Helper: post a BLE event and dispatch it to the real handler */
static void post_and_dispatch_ble(int32_t event_id, void *data, size_t size) {
    clear_posted_events();
    esp_event_post(BLE_EVENTS, event_id, data, size, 0);
    mock_event_dispatch_all();
}

/* Helper: post a Split event and dispatch it to the real handler */
static void post_and_dispatch_split(int32_t event_id, void *data, size_t size) {
    clear_posted_events();
    esp_event_post(SPLIT_EVENTS, event_id, data, size, 0);
    mock_event_dispatch_all();
}

/* Helper: check that a status push was sent */
static bool status_was_pushed(void) {
    return _mock_comm_last_len == sizeof(statusmod_msg_t) &&
           _mock_comm_last_module == MODULE_STATUS;
}

/* Helper: extract binary statusmod_msg_t from the push payload */
static const statusmod_msg_t *get_last_status(void) {
    if (!status_was_pushed()) return NULL;
    return (const statusmod_msg_t *)_mock_comm_last_payload;
}

/* ---- Init tests ---- */

TEST_CASE(status_module, init_registers_handlers) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_reset();
    mock_comm_reset();
    status_module_init();

    /* Should have registered at least 3 event handlers + 1 comm callback */
    TEST_ASSERT(_mock_handler_count >= 3);
    TEST_ASSERT_NOT_NULL(_mock_comm_callbacks[MODULE_STATUS]);
}

TEST_CASE(status_module, init_seeds_cache_from_ble_state) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_set(3, true);
    mock_comm_reset();
    status_module_init();

    /* Trigger a push via manual comms callback */
    _mock_comm_last_len = 0;
    _mock_comm_callbacks[MODULE_STATUS](COMM_TRANSPORT_USB, NULL, 0);

    TEST_ASSERT_TRUE(status_was_pushed());
    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(3, msg->selected_profile);
    TEST_ASSERT_EQUAL(1, msg->transport_mode);
}

/* ---- BLE event: profile connected ---- */

TEST_CASE(status_module, profile_connected_updates_bitmap) {
    int idx = 3;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    TEST_ASSERT_TRUE(status_was_pushed());
    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    /* Bitmap should have bit 3 set = 8 */
    TEST_ASSERT_EQUAL(8, msg->connected_bitmap);
}

TEST_CASE(status_module, profile_disconnected_clears_bitmap) {
    /* Connect then disconnect */
    int idx = 3;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(0, msg->connected_bitmap);
}

TEST_CASE(status_module, multiple_profiles_connected) {
    int idx0 = 0, idx2 = 2, idx8 = 8;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx0, sizeof(idx0));
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx2, sizeof(idx2));
    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx8, sizeof(idx8));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    /* bits 0,2,8 = 1+4+256 = 261 */
    TEST_ASSERT_EQUAL(261, msg->connected_bitmap);
}

/* ---- BLE event: pairing ---- */

TEST_CASE(status_module, pairing_started_sets_profile) {
    int profile = 5;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(5, msg->pairing_profile);
}

TEST_CASE(status_module, pairing_complete_clears_pairing) {
    int profile = 2;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    ble_pairing_result_t result = { .profile_idx = 2 };
    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(0xFF, msg->pairing_profile);
    TEST_ASSERT_EQUAL(2, msg->selected_profile);
}

TEST_CASE(status_module, pairing_failed_clears_pairing) {
    int profile = 1;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_FAILED, &profile, sizeof(profile));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(0xFF, msg->pairing_profile);
}

TEST_CASE(status_module, pairing_timeout_clears_pairing) {
    int profile = 4;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_TIMEOUT, &profile, sizeof(profile));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(0xFF, msg->pairing_profile);
}

/* ---- BLE event: routing changed ---- */

TEST_CASE(status_module, routing_changed_to_ble) {
    bool active = true;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(1, msg->transport_mode);
}

TEST_CASE(status_module, routing_changed_to_usb) {
    bool active = true;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    active = false;
    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(0, msg->transport_mode);
}

/* ---- Edge cases ---- */

TEST_CASE(status_module, disconnect_nonexistent_safe) {
    int idx = 7;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    TEST_ASSERT_TRUE(status_was_pushed());
    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_EQUAL(0, msg->connected_bitmap);
}

TEST_CASE(status_module, boundary_profile_index_15) {
    int idx = 15;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    /* bit 15 = 32768 */
    TEST_ASSERT_EQUAL(32768, msg->connected_bitmap);
}

/* ---- Manual comm callback ---- */

TEST_CASE(status_module, comm_callback_triggers_push) {
    _mock_comm_last_len = 0;
    TEST_ASSERT_NOT_NULL(_mock_comm_callbacks[MODULE_STATUS]);
    _mock_comm_callbacks[MODULE_STATUS](COMM_TRANSPORT_USB, NULL, 0);
    TEST_ASSERT_TRUE(status_was_pushed());
}

/* ---- Full event sequence ---- */

TEST_CASE(status_module, full_ble_event_sequence) {
    int idx = 1;
    bool active = true;
    ble_pairing_result_t result = { .profile_idx = 1 };

    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &idx, sizeof(idx));
    post_and_dispatch_ble(BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result));
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    _mock_comm_last_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL(1, msg->transport_mode);
    TEST_ASSERT_EQUAL(1, msg->selected_profile);
    TEST_ASSERT_EQUAL(0xFF, msg->pairing_profile);
    TEST_ASSERT_EQUAL(0, msg->connected_bitmap);
}

/* ---- Split events ---- */

TEST_CASE(status_module, split_connected_and_disconnected) {
    split_peer_info_t peer = { .role = SPLIT_ROLE_MASTER };
    post_and_dispatch_split(SPLIT_EVENT_CONNECTED, &peer, sizeof(peer));

    const statusmod_msg_t *msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_STATE_CONNECTED, msg->split_state);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_ROLE_MASTER, msg->split_role);

    _mock_comm_last_len = 0;
    post_and_dispatch_split(SPLIT_EVENT_DISCONNECTED, NULL, 0);

    msg = get_last_status();
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_STATE_DISCONNECTED, msg->split_state);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_ROLE_NONE, msg->split_role);
}

TEST_CASE(status_module, split_version_mismatch_event_updates_state) {
    /* Initially connected */
    split_peer_info_t peer = { .role = SPLIT_ROLE_SLAVE };
    post_and_dispatch_split(SPLIT_EVENT_CONNECTED, &peer, sizeof(peer));

    const statusmod_msg_t *msg1 = get_last_status();
    TEST_ASSERT_NOT_NULL(msg1);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_STATE_CONNECTED, msg1->split_state);

    /* Post version mismatch event */
    _mock_comm_last_len = 0;
    post_and_dispatch_split(SPLIT_EVENT_VERSION_MISMATCH, NULL, 0);

    const statusmod_msg_t *msg2 = get_last_status();
    TEST_ASSERT_NOT_NULL(msg2);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_STATE_VERSION_MISMATCH, msg2->split_state);
    TEST_ASSERT_EQUAL((uint8_t)SPLIT_ROLE_NONE, msg2->split_role);
}
