/**
 * @file mock_comm.h
 * @brief Mock for comm_module transport, session, and dispatch for host testing.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "comm_defs.h"
#include "comm_transport.h"

static comm_data_callback_t _mock_comm_callbacks[COMM_MODULE_COUNT] = {0};
static uint8_t _mock_comm_last_payload[COMM_MAX_PACKET_SIZE];
static uint16_t _mock_comm_last_len = 0;
static comm_module_id_t _mock_comm_last_module = (comm_module_id_t)0;
static comm_transport_t _mock_comm_last_target = COMM_TRANSPORT_NONE;
static int _mock_comm_send_count = 0;

static uint8_t _mock_comm_last_single_packet[COMM_REPORT_SIZE];
static uint16_t _mock_comm_last_single_len = 0;
static int _mock_comm_single_packet_count = 0;

static comm_transport_t _mock_comm_session_active = COMM_TRANSPORT_NONE;

#define MOCK_COMM_MAX_PACKETS 128
typedef struct {
    comm_transport_t target;
    uint8_t flags;
    uint16_t remaining;
    uint8_t payload_len;
    uint8_t payload[COMM_REPORT_SIZE];
} mock_comm_packet_t;

static mock_comm_packet_t _mock_comm_packets[MOCK_COMM_MAX_PACKETS];
static int _mock_comm_packet_count = 0;

bool comm_transport_is_connected(comm_transport_t transport) {
    (void)transport;
    return true;
}

static inline void mock_comm_reset(void) {
    memset(_mock_comm_callbacks, 0, sizeof(_mock_comm_callbacks));
    memset(_mock_comm_last_payload, 0, sizeof(_mock_comm_last_payload));
    _mock_comm_last_len = 0;
    _mock_comm_last_module = (comm_module_id_t)0;
    _mock_comm_last_target = COMM_TRANSPORT_NONE;
    _mock_comm_send_count = 0;
    memset(_mock_comm_last_single_packet, 0, sizeof(_mock_comm_last_single_packet));
    _mock_comm_last_single_len = 0;
    _mock_comm_single_packet_count = 0;
    _mock_comm_session_active = COMM_TRANSPORT_NONE;
    _mock_comm_packet_count = 0;
    memset(_mock_comm_packets, 0, sizeof(_mock_comm_packets));
}

void comm_register_module(comm_module_id_t module_id, comm_data_callback_t callback) {
    if (module_id < COMM_MODULE_COUNT) {
        _mock_comm_callbacks[module_id] = callback;
    }
}

bool comm_send_message(comm_transport_t target, comm_module_id_t module_id, const uint8_t *payload, uint16_t len) {
    _mock_comm_last_target = target;
    _mock_comm_last_module = module_id;
    _mock_comm_send_count++;
    if (payload && len <= sizeof(_mock_comm_last_payload)) {
        memcpy(_mock_comm_last_payload, payload, len);
        _mock_comm_last_len = len;
    } else {
        _mock_comm_last_len = 0;
    }
    return true;
}

bool comm_session_try_lock(comm_transport_t transport) {
    if (_mock_comm_session_active == COMM_TRANSPORT_NONE || _mock_comm_session_active == transport) {
        _mock_comm_session_active = transport;
        return true;
    }
    return false;
}

void comm_session_unlock(void) {
    _mock_comm_session_active = COMM_TRANSPORT_NONE;
}

comm_transport_t comm_session_get_active(void) {
    return _mock_comm_session_active;
}

static uint16_t _mock_transport_max_packet(void) {
    return COMM_REPORT_SIZE;
}

static const comm_transport_ops_t _mock_transport_ops = {
    .get_max_packet_size = _mock_transport_max_packet,
};

const comm_transport_ops_t *comm_transport_get(comm_transport_t transport) {
    (void)transport;
    return &_mock_transport_ops;
}

bool comm_build_send_single_packet(comm_transport_t target, uint8_t flags,
                                   uint16_t remaining, uint8_t payload_len,
                                   const uint8_t *payload) {
    _mock_comm_single_packet_count++;
    comm_packet_header_t hdr;
    hdr.flags = flags;
    hdr.remaining_packets = remaining;
    hdr.payload_len = payload_len;
    memcpy(_mock_comm_last_single_packet, &hdr, sizeof(hdr));
    if (payload && payload_len > 0 && sizeof(hdr) + payload_len <= sizeof(_mock_comm_last_single_packet)) {
        memcpy(_mock_comm_last_single_packet + sizeof(hdr), payload, payload_len);
    }
    _mock_comm_last_single_len = sizeof(hdr) + payload_len;

    if (_mock_comm_packet_count < MOCK_COMM_MAX_PACKETS) {
        mock_comm_packet_t *p = &_mock_comm_packets[_mock_comm_packet_count++];
        p->target = target;
        p->flags = flags;
        p->remaining = remaining;
        p->payload_len = payload_len;
        if (payload && payload_len > 0) {
            size_t copy_len = payload_len > COMM_REPORT_SIZE ? COMM_REPORT_SIZE : payload_len;
            memcpy(p->payload, payload, copy_len);
        }
    }
    return true;
}

bool comm_execute_callback(comm_transport_t source, comm_module_id_t callback_module, uint8_t const *data, uint16_t data_len) {
    if (callback_module < COMM_MODULE_COUNT && _mock_comm_callbacks[callback_module]) {
        return _mock_comm_callbacks[callback_module](source, (uint8_t *)data, data_len);
    }
    return false;
}

bool comm_dispatch_message(comm_transport_t source, const uint8_t *data, uint16_t len) {
    if (!data || len == 0) return false;
    comm_module_id_t mod = (comm_module_id_t)data[0];
    if (mod < COMM_MODULE_COUNT && _mock_comm_callbacks[mod]) {
        return _mock_comm_callbacks[mod](source, (uint8_t *)(data + 1), len - 1);
    }
    return false;
}
