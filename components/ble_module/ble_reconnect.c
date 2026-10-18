#include "ble_reconnect.h"
#include <string.h>

void ble_reconnect_init(ble_reconnect_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->last_profile = -1;
    r->directed_profile = -1;
}

void ble_reconnect_reset(ble_reconnect_t *r) {
    if (!r) return;
    r->phase = RECONN_PHASE_IDLE;
    r->pending_bitmap = 0;
    r->selected_retries_left = 0;
    r->directed_profile = -1;
    r->last_profile = -1;
    memset(r->cycles, 0, sizeof(r->cycles));
}

void ble_reconnect_start_selected(ble_reconnect_t *r, int profile_id, int retries) {
    if (!r) return;
    r->pending_bitmap = 0;
    r->phase = RECONN_PHASE_SELECTED;
    r->selected_retries_left = retries;
    memset(r->cycles, 0, sizeof(r->cycles));
    r->last_profile = -1;
    r->directed_profile = profile_id;
}

void ble_reconnect_start_finite(ble_reconnect_t *r, uint16_t pending_bitmap) {
    if (!r) return;
    r->pending_bitmap = pending_bitmap;
    r->phase = RECONN_PHASE_FINITE;
    memset(r->cycles, 0, sizeof(r->cycles));
    r->last_profile = -1;
    r->directed_profile = -1;
}

void ble_reconnect_start_forever(ble_reconnect_t *r, uint16_t pending_bitmap) {
    if (!r) return;
    r->pending_bitmap = pending_bitmap;
    r->phase = RECONN_PHASE_FOREVER;
    memset(r->cycles, 0, sizeof(r->cycles));
    r->last_profile = -1;
    r->directed_profile = -1;
}

int ble_reconnect_pick_next(ble_reconnect_t *r) {
    if (!r) return -1;
    if (r->phase == RECONN_PHASE_IDLE || r->pending_bitmap == 0) {
        r->directed_profile = -1;
        r->phase = RECONN_PHASE_IDLE;
        return -1;
    }

    int picked = -1;
    for (int i = 1; i <= CFG_BLE_MAX_PROFILES; i++) {
        int candidate = (r->last_profile + i) % CFG_BLE_MAX_PROFILES;
        if (r->pending_bitmap & (1u << candidate)) {
            picked = candidate;
            break;
        }
    }

    if (picked != -1) {
        r->directed_profile = picked;
        r->last_profile = picked;
    } else {
        r->directed_profile = -1;
        r->phase = RECONN_PHASE_IDLE;
    }
    return picked;
}

ble_reconn_action_t ble_reconnect_on_adv_timeout(ble_reconnect_t *r, int profile_id, bool is_valid) {
    if (!r) return BLE_RECONN_ACT_NONE;

    if (profile_id == -1) {
        r->directed_profile = -1;
        r->phase = RECONN_PHASE_IDLE;
        return BLE_RECONN_ACT_FALLBACK_BACKGROUND;
    }

    if (r->phase == RECONN_PHASE_SELECTED) {
        if (profile_id >= 0 && profile_id < CFG_BLE_MAX_PROFILES) {
            r->cycles[profile_id]++;
        }
        r->selected_retries_left--;

        if (r->selected_retries_left > 0) {
            if (r->selected_retries_left % 2 == 0 && r->pending_bitmap != 0) {
                r->directed_profile = -1;
                int next = ble_reconnect_pick_next(r);
                return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
            } else {
                r->directed_profile = profile_id;
                return BLE_RECONN_ACT_RETRY_SELECTED;
            }
        } else {
            if (is_valid && profile_id >= 0 && profile_id < CFG_BLE_MAX_PROFILES) {
                r->pending_bitmap |= (1u << profile_id);
            }
            r->phase = RECONN_PHASE_FOREVER;
            r->directed_profile = -1;
            int next = ble_reconnect_pick_next(r);
            return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
        }
    } else if (r->phase == RECONN_PHASE_FOREVER) {
        r->directed_profile = -1;
        int next = ble_reconnect_pick_next(r);
        return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
    } else if (r->phase == RECONN_PHASE_FINITE) {
        if (profile_id >= 0 && profile_id < CFG_BLE_MAX_PROFILES) {
            r->cycles[profile_id]++;
            if (r->cycles[profile_id] >= 5) {
                r->pending_bitmap &= ~(1u << profile_id);
            }
        }
        r->directed_profile = -1;
        int next = ble_reconnect_pick_next(r);
        return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
    }

    r->directed_profile = -1;
    r->phase = RECONN_PHASE_IDLE;
    return BLE_RECONN_ACT_FALLBACK_BACKGROUND;
}

ble_reconn_action_t ble_reconnect_on_connect_failed(ble_reconnect_t *r, int profile_id, bool is_valid) {
    if (!r || r->phase == RECONN_PHASE_IDLE) {
        return BLE_RECONN_ACT_FALLBACK_BACKGROUND;
    }

    if (r->phase == RECONN_PHASE_SELECTED) {
        r->selected_retries_left--;
        if (r->selected_retries_left <= 0) {
            if (is_valid && profile_id >= 0 && profile_id < CFG_BLE_MAX_PROFILES) {
                r->pending_bitmap |= (1u << profile_id);
            }
            r->phase = RECONN_PHASE_FOREVER;
        }
    }

    int next = ble_reconnect_pick_next(r);
    return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
}

ble_reconn_action_t ble_reconnect_on_connected(ble_reconnect_t *r, int connected_profile) {
    if (!r) return BLE_RECONN_ACT_NONE;

    if (r->phase == RECONN_PHASE_SELECTED || r->phase == RECONN_PHASE_FOREVER) {
        r->phase = RECONN_PHASE_FINITE;
        memset(r->cycles, 0, sizeof(r->cycles));
    }

    if (connected_profile >= 0 && connected_profile < CFG_BLE_MAX_PROFILES) {
        r->pending_bitmap &= ~(1u << connected_profile);
    }

    if (r->phase == RECONN_PHASE_FINITE && r->pending_bitmap != 0) {
        int next = ble_reconnect_pick_next(r);
        return (next != -1) ? BLE_RECONN_ACT_START_ADVERTISE : BLE_RECONN_ACT_FALLBACK_BACKGROUND;
    }

    r->phase = RECONN_PHASE_IDLE;
    r->directed_profile = -1;
    return BLE_RECONN_ACT_NONE;
}
