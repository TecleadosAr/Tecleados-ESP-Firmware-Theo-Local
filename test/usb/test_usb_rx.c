/**
 * @file test_usb_rx.c
 * @brief Tests for comm_rx.c — real production blast mode RX,
 *        bitmap generation, packet ordering, duplicate rejection, and commit.
 *
 * Links against real comm_rx.c (included in main.c).
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_comm.h"
#include "comm_rx.h"
#include "comm_defs.h"
#include "comm_crc.h"

static void send_rx_packet(uint8_t flags, uint16_t remaining_packets,
                           const uint8_t *payload, uint8_t payload_len) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    comm_packet_header_t *hdr = (comm_packet_header_t *)packet;
    hdr->flags = flags;
    hdr->remaining_packets = remaining_packets;
    hdr->payload_len = payload_len;
    if (payload && payload_len > 0) {
        memcpy(packet + sizeof(comm_packet_header_t), payload, payload_len);
    }
    comm_process_rx_request(COMM_TRANSPORT_USB, packet, sizeof(comm_packet_header_t) + payload_len);
}

TEST_SETUP(usb_rx) {
    mock_comm_reset();
    comm_erase_rx_buffer();
    mock_timer_set(1000); /* nonzero start time */
}

/* ---- Header sizing ---- */

TEST_CASE(usb_rx, packet_header_size) {
    TEST_ASSERT_EQUAL(4, sizeof(comm_packet_header_t));
}

/* ---- Blast mode entry via comm_process_rx_request ---- */

TEST_CASE(usb_rx, blast_entry_on_first_with_remaining) {
    uint8_t payload[10];
    memset(payload, 0xAA, sizeof(payload));

    send_rx_packet(PAYLOAD_FLAG_FIRST, 4 /* remaining */, payload, sizeof(payload));

    TEST_ASSERT_TRUE(comm_rx_blast_active());
}

TEST_CASE(usb_rx, single_packet_does_not_enter_blast) {
    /* FIRST with remaining=0 is the single-packet path */
    uint8_t payload[5] = { (uint8_t)MODULE_CONFIG, 1, 2, 3, 4 };

    send_rx_packet(PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST, 0, payload, sizeof(payload));

    TEST_ASSERT_FALSE(comm_rx_blast_active());
}

/* ---- Blast mode receive + bitmap ---- */

TEST_CASE(usb_rx, blast_receive_and_bitmap) {
    /* Enter blast mode: 3 packets total (rem=2) */
    uint8_t p0[32];
    memset(p0, 0x11, sizeof(p0));
    send_rx_packet(PAYLOAD_FLAG_FIRST, 2, p0, sizeof(p0));

    /* Send MID packet (index 1, rem=1) */
    uint8_t p1[32];
    memset(p1, 0x22, sizeof(p1));
    send_rx_packet(PAYLOAD_FLAG_MID, 1, p1, sizeof(p1));

    /* Build bitmap — should show packets 0 and 1 received */
    uint8_t bitmap[COMM_REPORT_SIZE] = {0};
    uint16_t bitmap_len = 0;
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);

    comm_packet_header_t *hdr = (comm_packet_header_t *)bitmap;
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_BITMAP, hdr->flags);
    TEST_ASSERT_EQUAL(1, hdr->payload_len);
    /* Bits 0 and 1 set = 0x03 in bitmap payload */
    TEST_ASSERT_EQUAL_HEX(0x03, bitmap[sizeof(comm_packet_header_t)]);
    TEST_ASSERT_EQUAL(sizeof(comm_packet_header_t) + 1 + 1, bitmap_len);
}

TEST_CASE(usb_rx, blast_receive_out_of_order) {
    /* Enter blast mode: 4 packets total (rem=3 -> index 0) */
    uint8_t p0[10] = {0};
    send_rx_packet(PAYLOAD_FLAG_FIRST, 3, p0, sizeof(p0));

    /* Send packet index 2 (rem=1) before packet index 1 (rem=2) */
    uint8_t p2[10] = {0x22};
    send_rx_packet(PAYLOAD_FLAG_MID, 1, p2, sizeof(p2));

    /* Bitmap should show bits 0 and 2 set = 0x05 */
    uint8_t bitmap[COMM_REPORT_SIZE] = {0};
    uint16_t bitmap_len = 0;
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);
    TEST_ASSERT_EQUAL_HEX(0x05, bitmap[sizeof(comm_packet_header_t)]);

    /* Now send missing packet index 1 (rem=2) */
    uint8_t p1[10] = {0x11};
    send_rx_packet(PAYLOAD_FLAG_MID, 2, p1, sizeof(p1));

    /* Bitmap should now show bits 0, 1, and 2 set = 0x07 */
    memset(bitmap, 0, sizeof(bitmap));
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);
    TEST_ASSERT_EQUAL_HEX(0x07, bitmap[sizeof(comm_packet_header_t)]);
}

TEST_CASE(usb_rx, blast_duplicate_ignored) {
    /* Enter blast mode: 2 packets total (rem=1) */
    uint8_t p0[5] = {0xAA};
    send_rx_packet(PAYLOAD_FLAG_FIRST, 1, p0, sizeof(p0));

    /* Resend same packet (index 0, rem=1) */
    send_rx_packet(PAYLOAD_FLAG_MID, 1, p0, sizeof(p0));

    /* Bitmap should still show only bit 0 set */
    uint8_t bitmap[COMM_REPORT_SIZE] = {0};
    uint16_t bitmap_len = 0;
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);
    TEST_ASSERT_EQUAL_HEX(0x01, bitmap[sizeof(comm_packet_header_t)]);
}

/* ---- Blast commit ---- */

static bool s_mock_rx_callback_called = false;
static uint16_t s_mock_rx_received_len = 0;

static bool mock_config_callback(comm_transport_t source, uint8_t *data, uint16_t data_len) {
    (void)source;
    (void)data;
    s_mock_rx_callback_called = true;
    s_mock_rx_received_len = data_len;
    return true;
}

TEST_CASE(usb_rx, blast_commit_assembles_full_payload) {
    s_mock_rx_callback_called = false;
    s_mock_rx_received_len = 0;
    comm_register_module(MODULE_CONFIG, mock_config_callback);

    /* Packet 0: starts with MODULE_CONFIG */
    uint8_t p0[20];
    p0[0] = (uint8_t)MODULE_CONFIG;
    memset(p0 + 1, 0xAA, 19);
    send_rx_packet(PAYLOAD_FLAG_FIRST, 1, p0, sizeof(p0));

    /* Packet 1: LAST commits the blast */
    uint8_t p1[COMM_REPORT_SIZE] = {0};
    comm_packet_header_t *hdr1 = (comm_packet_header_t *)p1;
    hdr1->flags = PAYLOAD_FLAG_LAST;
    hdr1->remaining_packets = 0;
    hdr1->payload_len = 10;
    memset(p1 + sizeof(comm_packet_header_t), 0xBB, 10);
    comm_rx_blast_commit(COMM_TRANSPORT_USB, p1, sizeof(comm_packet_header_t) + 10);

    /* After commit, blast mode should be cleared */
    TEST_ASSERT_FALSE(comm_rx_blast_active());
    TEST_ASSERT_TRUE(s_mock_rx_callback_called);
}

TEST_CASE(usb_rx, large_blast_multiple_mid_packets) {
    s_mock_rx_callback_called = false;
    comm_register_module(MODULE_CONFIG, mock_config_callback);

    /* Enter blast mode: 5 packets total (rem=4) */
    uint8_t p0[20];
    p0[0] = (uint8_t)MODULE_CONFIG;
    memset(p0 + 1, 0x10, 19);
    send_rx_packet(PAYLOAD_FLAG_FIRST, 4, p0, sizeof(p0));

    /* Send 3 MID packets (indices 1, 2, 3 with rem = 3, 2, 1) */
    for (int i = 1; i <= 3; i++) {
        uint8_t p_mid[20];
        memset(p_mid, (uint8_t)(0x10 * (i + 1)), sizeof(p_mid));
        send_rx_packet(PAYLOAD_FLAG_MID, 4 - i, p_mid, sizeof(p_mid));
    }

    /* Bitmap should show bits 0-3 set = 0x0F */
    uint8_t bitmap[COMM_REPORT_SIZE] = {0};
    uint16_t bitmap_len = 0;
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);
    TEST_ASSERT_EQUAL_HEX(0x0F, bitmap[sizeof(comm_packet_header_t)]);

    /* Commit with LAST packet (index 4, rem=0) */
    uint8_t p_last[COMM_REPORT_SIZE] = {0};
    comm_packet_header_t *hdr_last = (comm_packet_header_t *)p_last;
    hdr_last->flags = PAYLOAD_FLAG_LAST;
    hdr_last->remaining_packets = 0;
    hdr_last->payload_len = 10;
    memset(p_last + sizeof(comm_packet_header_t), 0x55, 10);
    comm_rx_blast_commit(COMM_TRANSPORT_USB, p_last, sizeof(comm_packet_header_t) + 10);

    TEST_ASSERT_FALSE(comm_rx_blast_active());
    TEST_ASSERT_TRUE(s_mock_rx_callback_called);
}

/* ---- Erase ---- */

TEST_CASE(usb_rx, erase_clears_all_state) {
    uint8_t p0[5] = {1, 2, 3, 4, 5};
    send_rx_packet(PAYLOAD_FLAG_FIRST, 3, p0, sizeof(p0));
    TEST_ASSERT_TRUE(comm_rx_blast_active());

    comm_erase_rx_buffer();
    TEST_ASSERT_FALSE(comm_rx_blast_active());
}

/* ---- Timestamp tracking ---- */

TEST_CASE(usb_rx, timestamp_updated_on_receive) {
    mock_timer_set(5000);

    uint8_t p0[5] = {1};
    send_rx_packet(PAYLOAD_FLAG_FIRST, 2, p0, sizeof(p0));

    mock_timer_set(8000);
    uint8_t p1[5] = {2};
    send_rx_packet(PAYLOAD_FLAG_MID, 1, p1, sizeof(p1));

    /* Timestamp should be updated to latest */
    TEST_ASSERT_EQUAL(8000, comm_rx_get_last_packet_timestamp_us());
}

/* ---- Bitmap response byte count ---- */

TEST_CASE(usb_rx, bitmap_response_correct_byte_count) {
    /* Enter blast mode with 10 packets total (rem=9) */
    uint8_t p0[10] = {0};
    send_rx_packet(PAYLOAD_FLAG_FIRST, 9, p0, sizeof(p0));

    uint8_t bitmap[COMM_REPORT_SIZE] = {0};
    uint16_t bitmap_len = 0;
    comm_rx_blast_build_bitmap_response(bitmap, &bitmap_len);

    comm_packet_header_t *hdr = (comm_packet_header_t *)bitmap;
    /* 10 packets = ceil(10/8) = 2 bytes */
    TEST_ASSERT_EQUAL(2, hdr->payload_len);
    TEST_ASSERT_EQUAL(0, hdr->remaining_packets);
}

/* ---- Constants ---- */

TEST_CASE(usb_rx, max_blast_packets) {
    TEST_ASSERT_EQUAL(384, RX_BLAST_MAX_PACKETS);
}
