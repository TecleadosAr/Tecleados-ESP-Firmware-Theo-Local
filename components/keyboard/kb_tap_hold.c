#include "kb_tap_hold.h"
#include <string.h>

static kb_tap_hold_dispatch_fn    s_dispatch_fn    = NULL;
static kb_tap_hold_fire_tap_fn    s_fire_tap_fn    = NULL;
static kb_tap_hold_send_report_fn s_send_report_fn = NULL;
static kb_tap_hold_lookup_fn      s_lookup_fn      = NULL;

/* State machine */
static th_key_state_t  s_state = TH_STATE_IDLE;
static uint16_t        s_pending_action = 0;
static cfg_ckey_ma_t   s_pending_ma;
static int64_t         s_press_time_us = 0;
static int64_t         s_release_time_us = 0;
static bool            s_nested_key_completed = false;
static bool            s_disallow_hold = false;

/* History / anti-misfire tracking */
static int64_t         s_last_any_key_time_us = 0;
static uint16_t        s_last_tap_key = 0;
static int64_t         s_last_tap_release_us = 0;

/* Deferred events buffer */
static th_event_t      s_buffer[TH_MAX_DEFERRED_EVENTS];
static size_t          s_buffer_count = 0;

static void buffer_clear(void) {
    s_buffer_count = 0;
}

static bool buffer_push(uint16_t action_code, bool is_pressed, int64_t timestamp_us) {
    if (s_buffer_count >= TH_MAX_DEFERRED_EVENTS) {
        return false;
    }
    s_buffer[s_buffer_count].action_code   = action_code;
    s_buffer[s_buffer_count].is_pressed    = is_pressed;
    s_buffer[s_buffer_count].timestamp_us  = timestamp_us;
    s_buffer_count++;
    return true;
}

static void buffer_flush(void) {
    for (size_t i = 0; i < s_buffer_count; i++) {
        if (s_dispatch_fn) {
            s_dispatch_fn(s_buffer[i].action_code, s_buffer[i].is_pressed);
        }
    }
    s_buffer_count = 0;
}

static bool is_key_in_buffer_pressed(uint16_t action_code) {
    for (size_t i = 0; i < s_buffer_count; i++) {
        if (s_buffer[i].action_code == action_code && s_buffer[i].is_pressed) {
            return true;
        }
    }
    return false;
}

static void fire_tap(uint16_t action, uint32_t dur_ms) {
    if (action == 0) return;
    if (s_fire_tap_fn) {
        s_fire_tap_fn(action, dur_ms > 0 ? dur_ms : 20, 0);
    } else if (s_dispatch_fn) {
        s_dispatch_fn(action, true);
        s_dispatch_fn(action, false);
    }
}

static void resolve_to_hold(void) {
    uint32_t hold_act = s_pending_ma.hold_action;
    bool sustain = s_pending_ma.hold_sustain;

    if (hold_act != 0) {
        if (sustain) {
            if (s_dispatch_fn) s_dispatch_fn((uint16_t)hold_act, true);
            s_state = TH_STATE_HOLD_SUSTAINED;
        } else {
            fire_tap((uint16_t)hold_act, s_pending_ma.hold_release_delay_ms);
            s_state = TH_STATE_IDLE;
        }
    } else {
        s_state = TH_STATE_IDLE;
    }

    buffer_flush();
    if (s_send_report_fn) s_send_report_fn();
}

static void resolve_to_tap(int64_t now_us) {
    uint32_t tap_act = s_pending_ma.tap_action;

    /* If double-tap action is configured and buffer is empty, wait for double tap */
    if (s_pending_ma.double_tap_action != 0 && s_buffer_count == 0) {
        s_state = TH_STATE_WAIT_DOUBLE_TAP;
        s_release_time_us = now_us;
        return;
    }

    fire_tap((uint16_t)tap_act, s_pending_ma.tap_release_delay_ms);
    s_last_tap_key = s_pending_action;
    s_last_tap_release_us = now_us;
    s_state = TH_STATE_IDLE;

    buffer_flush();
    if (s_send_report_fn) s_send_report_fn();
}

void kb_tap_hold_init(kb_tap_hold_dispatch_fn dispatch_fn,
                      kb_tap_hold_fire_tap_fn fire_tap_fn,
                      kb_tap_hold_send_report_fn send_report_fn,
                      kb_tap_hold_lookup_fn lookup_fn) {
    s_dispatch_fn    = dispatch_fn;
    s_fire_tap_fn    = fire_tap_fn;
    s_send_report_fn = send_report_fn;
    s_lookup_fn      = lookup_fn;
    kb_tap_hold_reset();
}

void kb_tap_hold_reset(void) {
    s_state = TH_STATE_IDLE;
    s_pending_action = 0;
    memset(&s_pending_ma, 0, sizeof(s_pending_ma));
    s_press_time_us = 0;
    s_release_time_us = 0;
    s_nested_key_completed = false;
    s_disallow_hold = false;
    s_last_any_key_time_us = 0;
    s_last_tap_key = 0;
    s_last_tap_release_us = 0;
    buffer_clear();
}

th_key_state_t kb_tap_hold_get_state(void) {
    return s_state;
}

size_t kb_tap_hold_get_buffered_count(void) {
    return s_buffer_count;
}

bool kb_tap_hold_is_active(void) {
    return (s_state != TH_STATE_IDLE) || (s_buffer_count > 0);
}

void kb_tap_hold_process_key(uint16_t action_code, bool is_pressed, int64_t now_us) {
    cfg_ckey_ma_t ma;
    bool is_th = false;
    if (s_lookup_fn) {
        is_th = s_lookup_fn(action_code, &ma);
    }

    /* Track last key activity for prior-idle calculation */
    int64_t idle_since_last_us = (s_last_any_key_time_us > 0) ? (now_us - s_last_any_key_time_us) : 1000000000LL;
    s_last_any_key_time_us = now_us;

    /* ─── CASE: In TH_STATE_HOLD_SUSTAINED ─── */
    if (s_state == TH_STATE_HOLD_SUSTAINED) {
        if (!is_pressed && action_code == s_pending_action) {
            /* Release the sustained hold action */
            if (s_dispatch_fn && s_pending_ma.hold_action != 0) {
                s_dispatch_fn((uint16_t)s_pending_ma.hold_action, false);
            }
            s_state = TH_STATE_IDLE;
            if (s_send_report_fn) s_send_report_fn();
            return;
        }
        /* Any other key while holding: dispatch directly */
        if (s_dispatch_fn) s_dispatch_fn(action_code, is_pressed);
        return;
    }

    /* ─── CASE: In TH_STATE_WAIT_DOUBLE_TAP ─── */
    if (s_state == TH_STATE_WAIT_DOUBLE_TAP) {
        if (is_pressed && action_code == s_pending_action) {
            /* Double-tap confirmed! */
            fire_tap((uint16_t)s_pending_ma.double_tap_action, s_pending_ma.double_tap_release_delay_ms);
            s_state = TH_STATE_IDLE;
            buffer_flush();
            if (s_send_report_fn) s_send_report_fn();
            return;
        }
        /* Another key pressed during double-tap window -> resolve earlier tap and flush */
        fire_tap((uint16_t)s_pending_ma.tap_action, s_pending_ma.tap_release_delay_ms);
        s_last_tap_key = s_pending_action;
        s_last_tap_release_us = now_us;
        s_state = TH_STATE_IDLE;
        buffer_flush();
        if (s_send_report_fn) s_send_report_fn();
        /* Fall through to handle the new key in IDLE state */
    }

    /* ─── CASE: In TH_STATE_PENDING_DECISION ─── */
    if (s_state == TH_STATE_PENDING_DECISION) {
        /* Release of the pending tap-hold key itself */
        if (!is_pressed && action_code == s_pending_action) {
            if (s_pending_ma.flavor == CKEY_FLAVOR_PERMISSIVE_HOLD && s_nested_key_completed && !s_disallow_hold) {
                resolve_to_hold();
            } else {
                resolve_to_tap(now_us);
            }
            return;
        }

        /* An event from another key while tap-hold is pending */
        if (is_pressed) {
            /* Under HOLD_ON_OTHER_KEY_PRESS: immediately resolve to HOLD */
            if (s_pending_ma.flavor == CKEY_FLAVOR_HOLD_ON_OTHER_KEY_PRESS && !s_disallow_hold) {
                resolve_to_hold();
                if (s_dispatch_fn) s_dispatch_fn(action_code, true);
                if (s_send_report_fn) s_send_report_fn();
                return;
            }

            /* For all other flavors: buffer the press */
            if (!buffer_push(action_code, true, now_us)) {
                /* Buffer full: force resolution to avoid losing events */
                resolve_to_hold();
                if (s_dispatch_fn) s_dispatch_fn(action_code, true);
            }
            return;
        } else {
            /* Release of another key */
            if (s_pending_ma.flavor == CKEY_FLAVOR_PERMISSIVE_HOLD && !s_disallow_hold) {
                /* If this key was pressed during this pending window, it is a nested tap! */
                if (is_key_in_buffer_pressed(action_code)) {
                    s_nested_key_completed = true;
                    resolve_to_hold();
                    if (s_dispatch_fn) s_dispatch_fn(action_code, false);
                    if (s_send_report_fn) s_send_report_fn();
                    return;
                }
            }

            if (!buffer_push(action_code, false, now_us)) {
                resolve_to_hold();
                if (s_dispatch_fn) s_dispatch_fn(action_code, false);
            }
            return;
        }
    }

    /* ─── CASE: In TH_STATE_IDLE ─── */
    if (is_pressed && is_th) {
        /* Check quick-tap repeat (auto-repeat) */
        if (action_code == s_last_tap_key && ma.quick_tap_ms > 0 && s_last_tap_release_us > 0) {
            int64_t since_tap_ms = (now_us - s_last_tap_release_us) / 1000LL;
            if (since_tap_ms >= 0 && since_tap_ms <= (int64_t)ma.quick_tap_ms) {
                /* Quick tap triggered: immediate tap */
                fire_tap((uint16_t)ma.tap_action, ma.tap_release_delay_ms);
                s_last_tap_release_us = now_us;
                if (s_send_report_fn) s_send_report_fn();
                return;
            }
        }

        /* Check require-prior-idle anti-misfire */
        bool disallow_hold = false;
        if (ma.require_prior_idle_ms > 0) {
            int64_t idle_ms = idle_since_last_us / 1000LL;
            if (idle_ms < (int64_t)ma.require_prior_idle_ms) {
                /* Fast typing burst: disallow hold */
                disallow_hold = true;
            }
        }

        /* Enter pending decision */
        s_state = TH_STATE_PENDING_DECISION;
        s_pending_action = action_code;
        s_pending_ma = ma;
        s_press_time_us = now_us;
        s_nested_key_completed = false;
        s_disallow_hold = disallow_hold;
        buffer_clear();
        return;
    }

    /* Regular key in IDLE state: pass through immediately */
    if (s_dispatch_fn) {
        s_dispatch_fn(action_code, is_pressed);
    }
}

void kb_tap_hold_tick(int64_t now_us) {
    if (s_state == TH_STATE_PENDING_DECISION) {
        uint32_t term_ms = s_pending_ma.hold_threshold_ms > 0 ? s_pending_ma.hold_threshold_ms : 200;
        int64_t term_us = (int64_t)term_ms * 1000LL;

        if (now_us - s_press_time_us >= term_us) {
            if (s_disallow_hold) {
                resolve_to_tap(now_us);
            } else {
                resolve_to_hold();
            }
        }
    } else if (s_state == TH_STATE_WAIT_DOUBLE_TAP) {
        uint32_t dt_term_ms = s_pending_ma.double_tap_threshold_ms > 0 ? s_pending_ma.double_tap_threshold_ms : 300;
        int64_t dt_term_us = (int64_t)dt_term_ms * 1000LL;

        if (now_us - s_release_time_us >= dt_term_us) {
            fire_tap((uint16_t)s_pending_ma.tap_action, s_pending_ma.tap_release_delay_ms);
            s_last_tap_key = s_pending_action;
            s_last_tap_release_us = now_us;
            s_state = TH_STATE_IDLE;
            buffer_flush();
            if (s_send_report_fn) s_send_report_fn();
        }
    }
}
