/**
 * @file cfgmod.h
 * @brief Mock stubs for cfgmod API used by cfg_layouts.c, cfg_system.c and statusmod.c.
 */
#pragma once
#ifndef _CFGMOD_H_
#define _CFGMOD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mocks/mock_esp.h"

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

typedef void (*cfgmod_default_fn)(void *out_struct);
typedef void (*cfgmod_on_update_fn)(const char *key);
typedef esp_err_t (*cfgmod_get_fn)(void *out_struct);
typedef esp_err_t (*cfgmod_set_fn)(const void *in_struct);
typedef esp_err_t (*cfgmod_delete_fn)(uint16_t item_id);
typedef esp_err_t (*cfgmod_validate_fn)(void *in_struct);

/* ---- Mock NVS storage ---- */

#define MOCK_NVS_MAX_ENTRIES 32
#define MOCK_NVS_MAX_SIZE    8192

typedef struct {
    cfgmod_kind_t kind;
    char          key[16];
    uint8_t       data[MOCK_NVS_MAX_SIZE];
    size_t        len;
    bool          valid;
} mock_nvs_entry_t;

static mock_nvs_entry_t _mock_nvs_store[MOCK_NVS_MAX_ENTRIES];
static int _mock_nvs_count = 0;

static inline void mock_nvs_reset(void) {
    memset(_mock_nvs_store, 0, sizeof(_mock_nvs_store));
    _mock_nvs_count = 0;
}

static inline void mock_nvs_inject(cfgmod_kind_t kind, const char *key,
                                   const void *data, size_t len) {
    /* If key exists, update in-place */
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            if (len <= MOCK_NVS_MAX_SIZE) {
                memcpy(_mock_nvs_store[i].data, data, len);
                _mock_nvs_store[i].len = len;
            }
            return;
        }
    }
    if (_mock_nvs_count >= MOCK_NVS_MAX_ENTRIES) return;
    mock_nvs_entry_t *e = &_mock_nvs_store[_mock_nvs_count++];
    e->kind = kind;
    strncpy(e->key, key, 15);
    e->key[15] = '\0';
    if (len <= MOCK_NVS_MAX_SIZE) {
        memcpy(e->data, data, len);
        e->len = len;
    }
    e->valid = true;
}

/* ---- Stubbed cfgmod API ---- */

static inline esp_err_t cfgmod_register_kind(cfgmod_kind_t kind,
                                             cfgmod_default_fn def_fn,
                                             cfgmod_on_update_fn update_fn,
                                             size_t struct_size) {
    (void)kind; (void)def_fn; (void)update_fn; (void)struct_size;
    return ESP_OK;
}

static inline void cfgmod_register_get_set(cfgmod_kind_t kind, cfgmod_get_fn get_fn, cfgmod_set_fn set_fn) {
    (void)kind; (void)get_fn; (void)set_fn;
}

static inline void cfgmod_register_delete(cfgmod_kind_t kind, cfgmod_delete_fn delete_fn) {
    (void)kind; (void)delete_fn;
}

static inline void cfgmod_register_validate(cfgmod_kind_t kind, cfgmod_validate_fn validate_fn) {
    (void)kind; (void)validate_fn;
}

static inline esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                                          void *out_struct) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            memcpy(out_struct, _mock_nvs_store[i].data, _mock_nvs_store[i].len);
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key,
                                          const void *in_struct) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            memcpy(_mock_nvs_store[i].data, in_struct, _mock_nvs_store[i].len);
            return ESP_OK;
        }
    }
    mock_nvs_inject(kind, key, in_struct, 512);
    return ESP_OK;
}

static inline esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key,
                                            void *out_buf, size_t *inout_len) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            size_t copy_len = _mock_nvs_store[i].len;
            if (copy_len > *inout_len) copy_len = *inout_len;
            memcpy(out_buf, _mock_nvs_store[i].data, copy_len);
            *inout_len = _mock_nvs_store[i].len;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                                             const void *data, size_t len) {
    mock_nvs_inject(kind, key, data, len);
    return ESP_OK;
}

static inline esp_err_t cfgmod_delete_storage(cfgmod_kind_t kind, const char *key) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            _mock_nvs_store[i].valid = false;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

static inline bool cfg_is_init(void) { return true; }

#endif // _CFGMOD_H_
