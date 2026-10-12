/**
 * @file mock_tinyusb.h
 * @brief Mock TinyUSB, usb_module and BLE-HID module APIs for host testing.
 *
 * Tracks calls to USB/BLE send functions and allows tests to control
 * the return values and connection state.
 *
 * The BLE functions are deliberately NON-static: production `blemod.h`
 * (included as-is by kb_report.c / statusmod.c) declares them with external
 * linkage, and a `static` definition would be a redeclaration error. The test
 * runner is a single translation unit, so a plain definition here is fine.
 */
#pragma once

#include "mock_esp.h"
#include <string.h>

/* ---- TinyUSB types ---- */

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} tusb_desc_device_t;

/* ---- Descriptor constants (match firmware) ---- */
#define TUSB_DESC_DEVICE 1
#define TUSB_DESC_CONFIG_ATT_SELF_POWERED 0xC0
#define CFG_TUD_ENDPOINT0_SIZE 64

/* Unused macros needed by descriptor headers */
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_HID_DESCRIPTOR(...) 0
#define TUD_HID_INOUT_DESCRIPTOR(...) 0
#define TUD_HID_REPORT_DESC_KEYBOARD(...) 0
#define TUD_HID_REPORT_DESC_CONSUMER(...) 0
#define HID_REPORT_ID(x) 0

/* ---- Mock state ---- */

static bool _mock_usb_mounted = true;
static bool _mock_usb_hid_ready[4] = {true, true, true, true};
static bool _mock_usb_boot_protocol = false;

/* Capture sent reports */
#define MOCK_MAX_REPORTS 64

typedef struct {
    uint8_t interface;
    uint8_t report_id;
    uint8_t data[64];
    uint16_t len;
} mock_usb_report_t;

static mock_usb_report_t _mock_reports[MOCK_MAX_REPORTS];
static int _mock_report_count = 0;

/* ---- TinyUSB API stubs ---- */

static inline bool tud_mounted(void) { return _mock_usb_mounted; }

static inline bool tud_hid_n_ready(uint8_t itf) {
    return itf < 4 ? _mock_usb_hid_ready[itf] : false;
}

static inline bool tud_hid_n_report(uint8_t itf, uint8_t report_id,
                                     const void *report, uint16_t len) {
    if (_mock_report_count < MOCK_MAX_REPORTS) {
        mock_usb_report_t *r = &_mock_reports[_mock_report_count++];
        r->interface = itf;
        r->report_id = report_id;
        r->len = len < 64 ? len : 64;
        memcpy(r->data, report, r->len);
    }
    return true;
}

/* ---- USB module API stubs ---- */

static inline bool usb_keyboard_use_boot_protocol(void) {
    return _mock_usb_boot_protocol;
}

static inline bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]) {
    if (_mock_report_count < MOCK_MAX_REPORTS) {
        mock_usb_report_t *r = &_mock_reports[_mock_report_count++];
        r->interface = 0;
        r->report_id = 1;
        r->data[0] = modifier;
        memcpy(r->data + 1, keycodes, 6);
        r->len = 7;
    }
    return true;
}

static inline bool usb_send_keyboard_nkro(uint8_t modifier, const uint8_t *bitmap, uint16_t len) {
    if (_mock_report_count < MOCK_MAX_REPORTS) {
        mock_usb_report_t *r = &_mock_reports[_mock_report_count++];
        r->interface = 0;
        r->report_id = 2;
        r->data[0] = modifier;
        uint16_t copy_len = len < 63 ? len : 63;
        memcpy(r->data + 1, bitmap, copy_len);
        r->len = 1 + copy_len;
    }
    return true;
}

static inline bool usb_send_consumer_report(uint16_t keycode) {
    if (_mock_report_count < MOCK_MAX_REPORTS) {
        mock_usb_report_t *r = &_mock_reports[_mock_report_count++];
        r->interface = 0;
        r->report_id = 4;
        memcpy(r->data, &keycode, 2);
        r->len = 2;
    }
    return true;
}

/* ---- BLE module stubs (see note at top: intentionally non-static) ---- */

static bool _mock_ble_routing_active = false;
static bool _mock_ble_connected = false;
static uint16_t _mock_ble_connected_bitmap = 0;
static int _mock_ble_pairing_profile = -1;
static int _mock_ble_send_count = 0;

bool ble_hid_is_routing_active(void) { return _mock_ble_routing_active; }
bool ble_hid_is_connected(void)      { return _mock_ble_connected; }
uint16_t ble_hid_get_connected_profiles_bitmap(void) { return _mock_ble_connected_bitmap; }
int ble_hid_get_pairing_profile(void) { return _mock_ble_pairing_profile; }

esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len) {
    (void)report; (void)len;
    _mock_ble_send_count++;
    return ESP_OK;
}

esp_err_t ble_hid_send_consumer_report(uint16_t keycode) {
    (void)keycode;
    _mock_ble_send_count++;
    return ESP_OK;
}

void ble_hid_set_routing_active(bool active) { _mock_ble_routing_active = active; }
void ble_hid_profile_pair(uint8_t id) { (void)id; }
void ble_hid_profile_connect_and_select(uint8_t id) { (void)id; }
void ble_hid_profile_toggle_connection(uint8_t id) { (void)id; }

#include "splitmod.h"

/* ---- Split module stubs ---- */

static split_role_t _mock_split_role = SPLIT_ROLE_NONE;
static split_state_t _mock_split_state = SPLIT_STATE_DISABLED;

split_role_t splitmod_get_role(void) { return _mock_split_role; }
split_status_t splitmod_get_status(void) {
    split_status_t st = {
        .state = _mock_split_state,
        .role = _mock_split_role,
        .peer_mac = {0},
        .rssi = 0,
        .latency_us = 0,
    };
    return st;
}

void mock_split_set_role(split_role_t role) { _mock_split_role = role; }
void mock_split_set_state(split_state_t state) { _mock_split_state = state; }

/* ---- Reset ---- */

static inline void mock_usb_reset(void) {
    _mock_usb_mounted = true;
    memset(_mock_usb_hid_ready, 1, sizeof(_mock_usb_hid_ready));
    _mock_usb_boot_protocol = false;
    _mock_report_count = 0;
    _mock_ble_routing_active = false;
    _mock_ble_connected = false;
    _mock_ble_connected_bitmap = 0;
    _mock_ble_pairing_profile = -1;
    _mock_ble_send_count = 0;
    _mock_split_role = SPLIT_ROLE_NONE;
    _mock_split_state = SPLIT_STATE_DISABLED;
}
