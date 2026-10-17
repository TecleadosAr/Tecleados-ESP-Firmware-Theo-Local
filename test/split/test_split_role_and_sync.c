/**
 * @file test_split_role_and_sync.c
 * @brief Unit tests for split_role.c and split_sync.c (decision rules, symmetry, swap, delta).
 */

#include "test_harness.h"
#include "split_role.h"
#include "split_sync.h"
#include "split_protocol.h"
#include <string.h>

static const uint8_t MAC_LOW[6]  = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x01 };
static const uint8_t MAC_HIGH[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x02 };

TEST_CASE(split_role, priority_1_unsynced_ble) {
    // Priority 1: own has unsynced BLE, peer does not -> MASTER
    // Even if peer has USB connected!
    split_role_t r1 = split_role_decide(
        MAC_LOW, MAC_HIGH,
        0, 0, 1, SPLIT_ROLE_NONE,
        1, 1, 0, SPLIT_ROLE_MASTER
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r1);

    // Mirror: peer has unsynced BLE, own does not -> SLAVE
    split_role_t r2 = split_role_decide(
        MAC_HIGH, MAC_LOW,
        1, 1, 0, SPLIT_ROLE_MASTER,
        0, 0, 1, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r2);
}

TEST_CASE(split_role, priority_2_usb_connection) {
    // Both unsynced_ble equal (0).
    // Own has USB, peer does not -> MASTER (even if peer has BLE and higher MAC)
    split_role_t r1 = split_role_decide(
        MAC_LOW, MAC_HIGH,
        1, 0, 0, SPLIT_ROLE_NONE,
        0, 1, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r1);

    // Mirror
    split_role_t r2 = split_role_decide(
        MAC_HIGH, MAC_LOW,
        0, 1, 0, SPLIT_ROLE_NONE,
        1, 0, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r2);
}

TEST_CASE(split_role, priority_3_ble_connection) {
    // Both unsynced_ble and usb equal (0).
    // Own has BLE connected (bitmap > 0), peer has none -> MASTER
    split_role_t r1 = split_role_decide(
        MAC_LOW, MAC_HIGH,
        0, 0x01, 0, SPLIT_ROLE_NONE,
        0, 0x00, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r1);

    // Mirror
    split_role_t r2 = split_role_decide(
        MAC_HIGH, MAC_LOW,
        0, 0x00, 0, SPLIT_ROLE_NONE,
        0, 0x01, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r2);
}

TEST_CASE(split_role, priority_4_last_role) {
    // Both unsynced, usb, ble equal (0).
    // Case 1: own was MASTER, peer was NONE -> MASTER
    split_role_t r1 = split_role_decide(
        MAC_LOW, MAC_HIGH,
        0, 0, 0, SPLIT_ROLE_MASTER,
        0, 0, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r1);

    // Mirror of Case 1: own was NONE, peer was MASTER -> SLAVE
    split_role_t r2 = split_role_decide(
        MAC_HIGH, MAC_LOW,
        0, 0, 0, SPLIT_ROLE_NONE,
        0, 0, 0, SPLIT_ROLE_MASTER
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r2);

    // Case 2: own was SLAVE, peer was NONE -> SLAVE
    split_role_t r3 = split_role_decide(
        MAC_HIGH, MAC_LOW,
        0, 0, 0, SPLIT_ROLE_SLAVE,
        0, 0, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r3);

    // Mirror of Case 2: own was NONE, peer was SLAVE -> MASTER
    split_role_t r4 = split_role_decide(
        MAC_LOW, MAC_HIGH,
        0, 0, 0, SPLIT_ROLE_NONE,
        0, 0, 0, SPLIT_ROLE_SLAVE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r4);
}

TEST_CASE(split_role, priority_5_mac_address_tiebreaker) {
    // All previous priorities tied.
    // Higher MAC wins MASTER.
    split_role_t r_high = split_role_decide(
        MAC_HIGH, MAC_LOW,
        0, 0, 0, SPLIT_ROLE_NONE,
        0, 0, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, r_high);

    split_role_t r_low = split_role_decide(
        MAC_LOW, MAC_HIGH,
        0, 0, 0, SPLIT_ROLE_NONE,
        0, 0, 0, SPLIT_ROLE_NONE
    );
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, r_low);
}

TEST_CASE(split_role, symmetry_property_exhaustive) {
    // For any combination of inputs (A, B), decide(A, B) must be complementary to decide(B, A).
    uint8_t usbs[] = {0, 1};
    uint16_t bles[] = {0, 0x02};
    uint8_t unsyncs[] = {0, 1};
    split_role_t lasts[] = {SPLIT_ROLE_NONE, SPLIT_ROLE_MASTER, SPLIT_ROLE_SLAVE};

    for (int uA = 0; uA < 2; uA++) {
        for (int uB = 0; uB < 2; uB++) {
            for (int bA = 0; bA < 2; bA++) {
                for (int bB = 0; bB < 2; bB++) {
                    for (int sA = 0; sA < 2; sA++) {
                        for (int sB = 0; sB < 2; sB++) {
                            for (int lA = 0; lA < 3; lA++) {
                                for (int lB = 0; lB < 3; lB++) {
                                    split_role_t decA = split_role_decide(
                                        MAC_LOW, MAC_HIGH,
                                        usbs[uA], bles[bA], unsyncs[sA], lasts[lA],
                                        usbs[uB], bles[bB], unsyncs[sB], lasts[lB]
                                    );
                                    split_role_t decB = split_role_decide(
                                        MAC_HIGH, MAC_LOW,
                                        usbs[uB], bles[bB], unsyncs[sB], lasts[lB],
                                        usbs[uA], bles[bA], unsyncs[sA], lasts[lA]
                                    );
                                    TEST_ASSERT(decA != decB);
                                    TEST_ASSERT((decA == SPLIT_ROLE_MASTER && decB == SPLIT_ROLE_SLAVE) ||
                                                (decA == SPLIT_ROLE_SLAVE && decB == SPLIT_ROLE_MASTER));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE(split_role, on_negotiate_valid_and_errors) {
    split_role_negotiate_payload_t p = {
        .proto_version = SPLIT_PROTO_VERSION,
        .usb_connected = 0,
        .ble_connected_bitmap = 0,
        .has_unsynced_ble = 0,
        .last_role = (uint8_t)SPLIT_ROLE_NONE
    };

    split_role_t decided = SPLIT_ROLE_NONE;

    // NULL out_role returns INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      split_role_on_negotiate(MAC_LOW, (const uint8_t *)&p, sizeof(p),
                                              MAC_HIGH, 0, 0, 0, SPLIT_ROLE_NONE, NULL));

    // Truncated payload returns INVALID_SIZE
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      split_role_on_negotiate(MAC_LOW, (const uint8_t *)&p, sizeof(p) - 1,
                                              MAC_HIGH, 0, 0, 0, SPLIT_ROLE_NONE, &decided));

    // Version mismatch returns INVALID_VERSION
    p.proto_version = SPLIT_PROTO_VERSION + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_VERSION,
                      split_role_on_negotiate(MAC_LOW, (const uint8_t *)&p, sizeof(p),
                                              MAC_HIGH, 0, 0, 0, SPLIT_ROLE_NONE, &decided));

    // Valid version succeeds and decides role
    p.proto_version = SPLIT_PROTO_VERSION;
    TEST_ASSERT_EQUAL(ESP_OK,
                      split_role_on_negotiate(MAC_LOW, (const uint8_t *)&p, sizeof(p),
                                              MAC_HIGH, 0, 0, 0, SPLIT_ROLE_NONE, &decided));
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, decided);
}

TEST_CASE(split_role, swap_req_and_ack) {
    split_role_t new_role = SPLIT_ROLE_NONE;

    split_role_on_swap_req(SPLIT_ROLE_MASTER, &new_role);
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, new_role);

    split_role_on_swap_req(SPLIT_ROLE_SLAVE, &new_role);
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, new_role);

    split_role_on_swap_ack(SPLIT_ROLE_MASTER, &new_role);
    TEST_ASSERT_EQUAL(SPLIT_ROLE_SLAVE, new_role);

    split_role_on_swap_ack(SPLIT_ROLE_SLAVE, &new_role);
    TEST_ASSERT_EQUAL(SPLIT_ROLE_MASTER, new_role);
}

TEST_CASE(split_sync, full_and_delta_decompression) {
    split_sync_clear_remote_matrix();
    TEST_ASSERT(split_sync_remote_matrix_changed());
    TEST_ASSERT(!split_sync_remote_matrix_changed()); // reading consumes flag

    uint8_t current_matrix[SPLIT_MATRIX_BYTES];
    split_sync_get_remote_matrix(current_matrix);
    for (int i = 0; i < SPLIT_MATRIX_BYTES; i++) {
        TEST_ASSERT_EQUAL(0, current_matrix[i]);
    }

    // Full state update
    split_key_state_full_payload_t full = { .active_layer = 1 };
    memset(full.matrix, 0xAA, SPLIT_MATRIX_BYTES);
    TEST_ASSERT_EQUAL(ESP_OK, split_sync_on_key_state_full((const uint8_t *)&full, sizeof(full)));
    TEST_ASSERT(split_sync_remote_matrix_changed());

    split_sync_get_remote_matrix(current_matrix);
    for (int i = 0; i < SPLIT_MATRIX_BYTES; i++) {
        TEST_ASSERT_EQUAL(0xAA, current_matrix[i]);
    }

    // Delta payload size validation:
    // changed_mask has 2 bits set (e.g. bit 1 and bit 4 -> 0x0012)
    // Needs 2 value bytes.
    uint8_t delta_buf[sizeof(split_key_state_delta_payload_t) + 4];
    split_key_state_delta_payload_t *d = (split_key_state_delta_payload_t *)delta_buf;
    d->changed_mask = (1u << 1) | (1u << 4);

    // If len < header, returns INVALID_SIZE
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      split_sync_on_key_state_delta(delta_buf, sizeof(split_key_state_delta_payload_t) - 1));

    // If len < header + 2 values, returns INVALID_SIZE
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      split_sync_on_key_state_delta(delta_buf, sizeof(split_key_state_delta_payload_t) + 1));

    // Valid delta: value for byte 1 is 0x11, value for byte 4 is 0x44
    delta_buf[sizeof(split_key_state_delta_payload_t) + 0] = 0x11;
    delta_buf[sizeof(split_key_state_delta_payload_t) + 1] = 0x44;

    TEST_ASSERT_EQUAL(ESP_OK,
                      split_sync_on_key_state_delta(delta_buf, sizeof(split_key_state_delta_payload_t) + 2));
    TEST_ASSERT(split_sync_remote_matrix_changed());

    split_sync_get_remote_matrix(current_matrix);
    // Byte 0 should still be 0xAA
    TEST_ASSERT_EQUAL(0xAA, current_matrix[0]);
    // Byte 1 updated to 0x11
    TEST_ASSERT_EQUAL(0x11, current_matrix[1]);
    // Byte 2, 3 still 0xAA
    TEST_ASSERT_EQUAL(0xAA, current_matrix[2]);
    TEST_ASSERT_EQUAL(0xAA, current_matrix[3]);
    // Byte 4 updated to 0x44
    TEST_ASSERT_EQUAL(0x44, current_matrix[4]);
    // Byte 5 still 0xAA
    TEST_ASSERT_EQUAL(0xAA, current_matrix[5]);
}
