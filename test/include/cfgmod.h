/**
 * @file cfgmod.h
 * @brief Header for cfgmod API used by host tests.
 *
 * Types, declarations and test helpers (mock_nvs_reset, mock_nvs_inject)
 * backed by real cfgmod.c and mock_nvs.h.
 */
#pragma once
#ifndef _CFGMOD_H_
#define _CFGMOD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mocks/mock_esp.h"
#include "mocks/mock_nvs.h"

#define CFGMOD_MAX_KEY_LEN 12

typedef enum cfgmod_kind : uint8_t {
    CFGMOD_KIND_LAYOUT = 0,
    CFGMOD_KIND_MACRO,
    CFGMOD_KIND_CONNECTION,
    CFGMOD_KIND_SYSTEM,
    CFGMOD_KIND_PHYSICAL,
    CFGMOD_KIND_CKEY,
    CFGMOD_KIND_SPLIT,
    CFGMOD_KIND_COMBO,
    CFGMOD_KIND_BLE_BOND,
    CFGMOD_KIND_MAX
} cfgmod_kind_t;

typedef enum cfgmod_key_id : uint8_t {
    CFG_KEY_TEST = 0,
    CFG_KEY_HELLO,
    CFG_KEY_PHYSICAL_LAYOUT,
    CFG_KEY_LAYOUTS = 0x10,
    CFG_KEY_LAYOUT_SINGLE = 0x11,
    CFG_KEY_LAYOUT_LIMITS = 0x12,
    CFG_KEY_MACROS,
    CFG_KEY_MACRO_LIMITS,
    CFG_KEY_MACRO_SINGLE,
    CFG_KEY_CKEYS,
    CFG_KEY_CKEY_SINGLE,
    CFG_KEY_SYSTEM,
    CFG_KEY_COMBOS,
    CFG_KEY_COMBO_SINGLE,
    CFG_KEY_COMBO_LIMITS,
    CFG_KEY_MAX
} cfgmod_key_id_t;

typedef enum cfgmod_cmd : uint8_t {
    CFG_CMD_GET = 0,
    CFG_CMD_SET
} cfgmod_cmd_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd;
    uint8_t key_id;
    uint16_t item_id;     // ID for *_SINGLE commands (e.g. layer ID, macro ID)
    uint8_t reserved[3];
} cfgmod_req_header_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd;
    uint8_t key_id;
    uint8_t reserved;
    uint32_t status;
} cfgmod_rsp_header_t;

typedef void (*cfgmod_default_fn)(void *out_struct);
typedef void (*cfgmod_on_update_fn)(const char *key);
typedef esp_err_t (*cfgmod_get_fn)(void *out_struct);
typedef esp_err_t (*cfgmod_set_fn)(const void *in_struct);
typedef esp_err_t (*cfgmod_delete_fn)(uint16_t item_id);
typedef esp_err_t (*cfgmod_validate_fn)(void *in_struct);

/* Function declarations provided by cfgmod.c */
esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                               cfgmod_on_update_fn update_fn,
                               size_t struct_size);
void cfgmod_register_get_set(cfgmod_kind_t kind, cfgmod_get_fn get_fn, cfgmod_set_fn set_fn);
void cfgmod_register_delete(cfgmod_kind_t kind, cfgmod_delete_fn delete_fn);
void cfgmod_register_validate(cfgmod_kind_t kind, cfgmod_validate_fn validate_fn);
esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key, void *out_struct);
esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key, const void *in_struct);
esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key, void *out_buf, size_t *inout_len);
esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key, const void *data, size_t len);
esp_err_t cfgmod_delete_storage(cfgmod_kind_t kind, const char *key);
esp_err_t cfgmod_handle_usb_comm(const uint8_t *data, size_t len, uint8_t *out, size_t *out_len, size_t out_max);
bool cfg_is_init(void);


static inline void mock_nvs_inject(cfgmod_kind_t kind, const char *key,
                                   const void *data, size_t len) {
    cfgmod_write_storage(kind, key, data, len);
}

#endif // _CFGMOD_H_
