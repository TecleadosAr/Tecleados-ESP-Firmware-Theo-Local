#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "cfg_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

// Phase tracking for seamless multidevice reconnection
typedef enum {
    RECONN_PHASE_IDLE = 0,
    RECONN_PHASE_SELECTED,  // Try selected up to N times
    RECONN_PHASE_FOREVER,   // Cycle infinitely until ANY connection
    RECONN_PHASE_FINITE     // Cycle finite (up to 5x per profile) until queue empty
} reconn_phase_t;

typedef enum {
    BLE_RECONN_ACT_NONE = 0,
    BLE_RECONN_ACT_RETRY_SELECTED,
    BLE_RECONN_ACT_START_ADVERTISE,
    BLE_RECONN_ACT_FALLBACK_BACKGROUND,
} ble_reconn_action_t;

typedef struct {
    reconn_phase_t phase;
    uint16_t       pending_bitmap;
    uint8_t        cycles[CFG_BLE_MAX_PROFILES];
    int            last_profile;
    int            selected_retries_left;
    int            directed_profile;
} ble_reconnect_t;

void ble_reconnect_init(ble_reconnect_t *r);
void ble_reconnect_reset(ble_reconnect_t *r);

void ble_reconnect_start_selected(ble_reconnect_t *r, int profile_id, int retries);
void ble_reconnect_start_finite(ble_reconnect_t *r, uint16_t pending_bitmap);
void ble_reconnect_start_forever(ble_reconnect_t *r, uint16_t pending_bitmap);

int  ble_reconnect_pick_next(ble_reconnect_t *r);

ble_reconn_action_t ble_reconnect_on_adv_timeout(ble_reconnect_t *r, int profile_id, bool is_valid);
ble_reconn_action_t ble_reconnect_on_connect_failed(ble_reconnect_t *r, int profile_id, bool is_valid);
ble_reconn_action_t ble_reconnect_on_connected(ble_reconnect_t *r, int connected_profile);

#ifdef __cplusplus
}
#endif
