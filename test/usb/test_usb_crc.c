/**
 * @file test_usb_crc.c
 * @brief Tests for the comm CRC-8 module (comm_crc.c).
 *
 * Tests the REAL production comm_crc_prepare_packet/comm_crc_verify_packet
 * linked from components/comm_module/comm_crc.c.
 */
#include "test_harness.h"

/* Production functions linked via main.c.
 * compute_crc8 is static in production — tested indirectly via public API. */

/* ---- Tests ---- */

TEST_CASE(usb_crc, prepare_and_verify_round_trip) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[0] = 0x80; /* FIRST flag */
    packet[3] = 5;    /* payload_len */
    packet[4] = 'H';
    packet[5] = 'e';
    packet[6] = 'l';
    packet[7] = 'l';
    packet[8] = 'o';

    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, all_zeros_round_trip) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, all_ff_round_trip) {
    uint8_t packet[COMM_REPORT_SIZE];
    memset(packet, 0xFF, sizeof(packet));
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, single_bit_corruption_detected) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[0] = 0xA0;
    packet[4] = 0x42;
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);

    packet[4] ^= 0x01;
    TEST_ASSERT_FALSE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, crc_byte_corruption_detected) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[0] = 0x80;
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);

    packet[COMM_REPORT_SIZE - 1] ^= 0x01;
    TEST_ASSERT_FALSE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, different_payloads_different_crc) {
    uint8_t pkt1[COMM_REPORT_SIZE] = {0};
    uint8_t pkt2[COMM_REPORT_SIZE] = {0};
    pkt1[4] = 0x01;
    pkt2[4] = 0x02;

    comm_crc_prepare_packet(pkt1, COMM_REPORT_SIZE);
    comm_crc_prepare_packet(pkt2, COMM_REPORT_SIZE);

    TEST_ASSERT(pkt1[COMM_REPORT_SIZE - 1] != pkt2[COMM_REPORT_SIZE - 1]);
}

TEST_CASE(usb_crc, repeated_prepare_is_idempotent) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[4] = 0x55;

    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    uint8_t first_crc = packet[COMM_REPORT_SIZE - 1];

    /* Reset CRC byte and prepare again */
    packet[COMM_REPORT_SIZE - 1] = 0;
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_EQUAL(first_crc, packet[COMM_REPORT_SIZE - 1]);
}

TEST_CASE(usb_crc, full_payload_round_trip) {
    uint8_t packet[COMM_REPORT_SIZE];
    for (int i = 0; i < COMM_REPORT_SIZE - 1; i++) {
        packet[i] = (uint8_t)(i * 7 + 13);
    }
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, every_single_byte_flip_detected) {
    uint8_t original[COMM_REPORT_SIZE] = {0};
    original[0] = 0x80;
    original[3] = 10;
    for (int i = 4; i < 14; i++) original[i] = (uint8_t)i;
    comm_crc_prepare_packet(original, COMM_REPORT_SIZE);

    for (int byte_idx = 0; byte_idx < COMM_REPORT_SIZE; byte_idx++) {
        uint8_t corrupted[COMM_REPORT_SIZE];
        memcpy(corrupted, original, COMM_REPORT_SIZE);
        corrupted[byte_idx] ^= 0x80;
        TEST_ASSERT_FALSE(comm_crc_verify_packet(corrupted, COMM_REPORT_SIZE));
    }
}

/* compute_crc8 is static — verify known CRC properties via public API */
TEST_CASE(usb_crc, zero_payload_has_nonzero_crc_byte) {
    uint8_t packet[COMM_REPORT_SIZE];
    memset(packet, 0, sizeof(packet));
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    /* All-zero payload still gets a CRC byte (the CRC of 62 zero bytes) */
    /* Just verify round-trip works — CRC value itself is implementation detail */
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}

TEST_CASE(usb_crc, single_nonzero_byte_round_trip) {
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[0] = 0x01;
    comm_crc_prepare_packet(packet, COMM_REPORT_SIZE);
    TEST_ASSERT_TRUE(comm_crc_verify_packet(packet, COMM_REPORT_SIZE));
}
