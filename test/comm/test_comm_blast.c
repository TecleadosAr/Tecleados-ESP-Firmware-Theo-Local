/**
 * @file test_comm_blast.c
 * @brief Unit tests for comm_tx.c Blast+Reconcile protocol and session management.
 */

#include "test_harness.h"
#include "comm_tx.h"
#include "comm_defs.h"
#include "comm_session.h"
#include "mocks/mock_comm.h"
#include <string.h>

static void reset_tx_state(void) {
    mock_comm_reset();
    comm_erase_tx_buffer();
    comm_session_unlock();
}

TEST_CASE(comm_blast, session_try_lock_and_unlock) {
    reset_tx_state();

    TEST_ASSERT_EQUAL(COMM_TRANSPORT_NONE, comm_session_get_active());

    // USB locks session
    TEST_ASSERT(comm_session_try_lock(COMM_TRANSPORT_USB));
    TEST_ASSERT_EQUAL(COMM_TRANSPORT_USB, comm_session_get_active());

    // USB re-locking same session succeeds
    TEST_ASSERT(comm_session_try_lock(COMM_TRANSPORT_USB));

    // BLE cannot lock while USB holds it
    TEST_ASSERT(!comm_session_try_lock(COMM_TRANSPORT_BLE));
    TEST_ASSERT_EQUAL(COMM_TRANSPORT_USB, comm_session_get_active());

    // Unlock
    comm_session_unlock();
    TEST_ASSERT_EQUAL(COMM_TRANSPORT_NONE, comm_session_get_active());

    // BLE can now acquire
    TEST_ASSERT(comm_session_try_lock(COMM_TRANSPORT_BLE));
    TEST_ASSERT_EQUAL(COMM_TRANSPORT_BLE, comm_session_get_active());

    // USB cannot acquire
    TEST_ASSERT(!comm_session_try_lock(COMM_TRANSPORT_USB));

    comm_session_unlock();
    TEST_ASSERT_EQUAL(COMM_TRANSPORT_NONE, comm_session_get_active());
}

TEST_CASE(comm_blast, multi_packet_tx_and_mid_send_on_ack) {
    reset_tx_state();

    // Setup manual blast state: 4 packets total
    // packet 0: FIRST (remaining=3)
    // packet 1: MID (remaining=2)
    // packet 2: MID (remaining=1)
    // packet 3: LAST (remaining=0)
    uint8_t dummy_data[128];
    for (int i = 0; i < 128; i++) dummy_data[i] = (uint8_t)(i + 1);

    ensure_tx_buf_allocated();
    memcpy(s_tx_buf, dummy_data, 128);
    s_tx_buf_len = 128;
    s_tx_current_target = COMM_TRANSPORT_USB;
    s_tx_blast_mode_flag = true;
    s_tx_blast_total_packets = 4;
    s_tx_blast_max_payload_len = 32;
    s_tx_blast_reconcile_attempts = 0;
    s_tx_awaiting_response = true;

    _mock_comm_packet_count = 0;

    // Send first packet
    TEST_ASSERT(tx_send_packet_by_index(0));
    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_FIRST, _mock_comm_packets[0].flags);
    TEST_ASSERT_EQUAL(3, _mock_comm_packets[0].remaining);
    TEST_ASSERT_EQUAL(32, _mock_comm_packets[0].payload_len);

    // Receiver ACKs the FIRST packet
    comm_packet_header_t ack_hdr = {
        .flags = PAYLOAD_FLAG_ACK,
        .remaining_packets = 0,
        .payload_len = 0
    };
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&ack_hdr, sizeof(ack_hdr));

    // Transmitter should have sent all MID packets (idx 1 and 2) followed by STATUS_REQ
    // Total packets sent so far: 1 (FIRST) + 2 (MID) + 1 (STATUS_REQ) = 4
    TEST_ASSERT_EQUAL(4, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_MID, _mock_comm_packets[1].flags);
    TEST_ASSERT_EQUAL(2, _mock_comm_packets[1].remaining);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_MID, _mock_comm_packets[2].flags);
    TEST_ASSERT_EQUAL(1, _mock_comm_packets[2].remaining);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_STATUS_REQ, _mock_comm_packets[3].flags);
}

TEST_CASE(comm_blast, bitmap_retransmit_missing_and_complete) {
    reset_tx_state();

    uint8_t dummy_data[128];
    for (int i = 0; i < 128; i++) dummy_data[i] = (uint8_t)(i + 1);

    ensure_tx_buf_allocated();
    memcpy(s_tx_buf, dummy_data, 128);
    s_tx_buf_len = 128;
    s_tx_current_target = COMM_TRANSPORT_USB;
    s_tx_blast_mode_flag = true;
    s_tx_blast_total_packets = 4;
    s_tx_blast_max_payload_len = 32;
    s_tx_blast_reconcile_attempts = 0;
    s_tx_awaiting_response = true;

    _mock_comm_packet_count = 0;

    // Simulate Receiver sending BITMAP where packet 1 was lost, packet 2 was received.
    // Bitmap payload: bit 1 is 0 (missing), bit 2 is 1 (received) -> byte 0 = 0b00000100 (0x04)
    uint8_t bitmap_pkt[sizeof(comm_packet_header_t) + 1];
    comm_packet_header_t *bhdr = (comm_packet_header_t *)bitmap_pkt;
    bhdr->flags = PAYLOAD_FLAG_BITMAP;
    bhdr->remaining_packets = 0;
    bhdr->payload_len = 1;
    bitmap_pkt[sizeof(comm_packet_header_t)] = (1u << 2); // only packet 2 received

    comm_process_tx_response(COMM_TRANSPORT_USB, bitmap_pkt, sizeof(bitmap_pkt));

    // Transmitter should retransmit packet 1 (MID), then send STATUS_REQ
    TEST_ASSERT_EQUAL(2, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_MID, _mock_comm_packets[0].flags);
    TEST_ASSERT_EQUAL(2, _mock_comm_packets[0].remaining);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_STATUS_REQ, _mock_comm_packets[1].flags);

    // Now receiver sends BITMAP indicating both packet 1 and 2 are received -> bits 1 and 2 set (0x06)
    _mock_comm_packet_count = 0;
    bitmap_pkt[sizeof(comm_packet_header_t)] = (1u << 1) | (1u << 2);

    comm_process_tx_response(COMM_TRANSPORT_USB, bitmap_pkt, sizeof(bitmap_pkt));

    // Transmitter sees all MIDs received -> transmits LAST packet (idx 3)
    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_LAST, _mock_comm_packets[0].flags);
    TEST_ASSERT_EQUAL(0, _mock_comm_packets[0].remaining);

    // Finally, receiver acknowledges with ACK | OK
    comm_packet_header_t ok_hdr = {
        .flags = PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK,
        .remaining_packets = 0,
        .payload_len = 0
    };
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&ok_hdr, sizeof(ok_hdr));

    // Transaction complete: blast active should be false, buffer erased
    TEST_ASSERT(!comm_tx_blast_active());
    TEST_ASSERT_EQUAL(0, s_tx_buf_len);
}

TEST_CASE(comm_blast, reconcile_rounds_limit_aborts) {
    reset_tx_state();

    uint8_t dummy_data[128];
    ensure_tx_buf_allocated();
    memcpy(s_tx_buf, dummy_data, sizeof(dummy_data));
    s_tx_buf_len = 128;
    s_tx_current_target = COMM_TRANSPORT_USB;
    s_tx_blast_mode_flag = true;
    s_tx_blast_total_packets = 4;
    s_tx_blast_max_payload_len = 32;
    s_tx_blast_reconcile_attempts = 0;
    s_tx_awaiting_response = true;

    uint8_t bitmap_pkt[sizeof(comm_packet_header_t) + 1];
    comm_packet_header_t *bhdr = (comm_packet_header_t *)bitmap_pkt;
    bhdr->flags = PAYLOAD_FLAG_BITMAP;
    bhdr->remaining_packets = 0;
    bhdr->payload_len = 1;
    bitmap_pkt[sizeof(comm_packet_header_t)] = 0x00; // none received

    // Rounds 1 to 5 (TX_BLAST_MAX_RECONCILE_ROUNDS = 5)
    for (int round = 1; round <= 5; round++) {
        _mock_comm_packet_count = 0;
        comm_process_tx_response(COMM_TRANSPORT_USB, bitmap_pkt, sizeof(bitmap_pkt));
        TEST_ASSERT_EQUAL(round, s_tx_blast_reconcile_attempts);
        TEST_ASSERT(comm_tx_blast_active());
    }

    // Round 6: exceeds max reconcile attempts -> aborts
    _mock_comm_packet_count = 0;
    comm_process_tx_response(COMM_TRANSPORT_USB, bitmap_pkt, sizeof(bitmap_pkt));

    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_ABORT, _mock_comm_packets[0].flags);
    TEST_ASSERT(!comm_tx_blast_active());
    TEST_ASSERT_EQUAL(0, s_tx_buf_len);
}

TEST_CASE(comm_blast, nak_max_resend_attempts_aborts) {
    reset_tx_state();

    uint8_t dummy_data[64];
    ensure_tx_buf_allocated();
    memcpy(s_tx_buf, dummy_data, sizeof(dummy_data));
    s_tx_buf_len = 64;
    s_tx_current_target = COMM_TRANSPORT_USB;
    s_tx_blast_mode_flag = true;
    s_tx_blast_total_packets = 2;
    s_tx_blast_max_payload_len = 32;
    s_tx_awaiting_response = true;

    comm_packet_header_t nak_hdr = {
        .flags = PAYLOAD_FLAG_NAK,
        .remaining_packets = 0,
        .payload_len = 0
    };

    // Attempt 1: resends packet 0
    _mock_comm_packet_count = 0;
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&nak_hdr, sizeof(nak_hdr));
    TEST_ASSERT_EQUAL(1, s_tx_nak_resend_attempts);
    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_FIRST, _mock_comm_packets[0].flags);
    TEST_ASSERT(comm_tx_blast_active());

    // Attempt 2: resends packet 0
    _mock_comm_packet_count = 0;
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&nak_hdr, sizeof(nak_hdr));
    TEST_ASSERT_EQUAL(2, s_tx_nak_resend_attempts);
    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_FIRST, _mock_comm_packets[0].flags);
    TEST_ASSERT(comm_tx_blast_active());

    // Attempt 3: reached TX_NAK_RESEND_MAX_ATTEMPTS (3) -> aborts
    _mock_comm_packet_count = 0;
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&nak_hdr, sizeof(nak_hdr));
    TEST_ASSERT_EQUAL(1, _mock_comm_packet_count);
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_ABORT, _mock_comm_packets[0].flags);
    TEST_ASSERT(!comm_tx_blast_active());
    TEST_ASSERT_EQUAL(0, s_tx_buf_len);
}

TEST_CASE(comm_blast, response_from_unexpected_transport_or_flag_ignored) {
    reset_tx_state();

    ensure_tx_buf_allocated();
    s_tx_buf_len = 32;
    s_tx_current_target = COMM_TRANSPORT_USB;
    s_tx_blast_mode_flag = true;
    s_tx_awaiting_response = true;

    comm_packet_header_t ack_hdr = {
        .flags = PAYLOAD_FLAG_ACK,
        .remaining_packets = 0,
        .payload_len = 0
    };

    _mock_comm_packet_count = 0;
    // Response arriving on BLE instead of USB should be discarded
    comm_process_tx_response(COMM_TRANSPORT_BLE, (const uint8_t *)&ack_hdr, sizeof(ack_hdr));
    TEST_ASSERT_EQUAL(0, _mock_comm_packet_count);
    TEST_ASSERT(comm_tx_blast_active());

    // Abort flag from correct transport clears state
    comm_packet_header_t abort_hdr = {
        .flags = PAYLOAD_FLAG_ABORT,
        .remaining_packets = 0,
        .payload_len = 0
    };
    comm_process_tx_response(COMM_TRANSPORT_USB, (const uint8_t *)&abort_hdr, sizeof(abort_hdr));
    TEST_ASSERT(!comm_tx_blast_active());
}
