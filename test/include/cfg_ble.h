/**
 * @file cfg_ble.h
 * @brief Shim — BLE config stubs for host testing.
 *
 * Provides controllable cfg_ble_get_state() for statusmod.c tests.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CFG_BLE_MAX_PROFILES 9

typedef struct {
    uint8_t addr_type;
    uint8_t val[6];
    bool is_valid;
    uint8_t addr_nonce;
} cfg_ble_profile_t;

typedef struct {
    cfg_ble_profile_t profiles[CFG_BLE_MAX_PROFILES];
    uint8_t selected_profile;
    bool ble_routing_enabled;
    uint32_t sync_version;
} cfg_ble_state_t;

static cfg_ble_state_t _mock_ble_state = {
    .selected_profile = 0,
    .ble_routing_enabled = false,
    .sync_version = 0,
};

static inline const cfg_ble_state_t *cfg_ble_get_state(void) {
    return &_mock_ble_state;
}

static inline void cfg_ble_save_state(const cfg_ble_state_t *state) {
    (void)state;
}

static inline void mock_ble_state_reset(void) {
    _mock_ble_state.selected_profile = 0;
    _mock_ble_state.ble_routing_enabled = false;
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        _mock_ble_state.profiles[i].is_valid = false;
    }
}

static inline void mock_ble_state_set(uint8_t profile, bool ble_routing) {
    _mock_ble_state.selected_profile = profile;
    _mock_ble_state.ble_routing_enabled = ble_routing;
}

static inline void cfg_ble_clear_unsynced(void) {
}

void cfg_ble_init(void) {
}

esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len) {
    (void)out_buf;
    if (inout_len) *inout_len = 0;
    return ESP_OK;
}

esp_err_t cfg_ble_bond_write_all(const void *data, size_t len) {
    (void)data; (void)len;
    return ESP_OK;
}


