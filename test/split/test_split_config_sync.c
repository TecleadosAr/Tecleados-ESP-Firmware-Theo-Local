/**
 * @file test_split_config_sync.c
 * @brief Unit tests for split_config_sync.c — custom keys, macros, and combos synchronization.
 */
#include "test_harness.h"
#include "split_config_sync.h"
#include "cfg_custom_keys.h"
#include "cfg_macros.h"
#include "cfg_combos.h"
#include "cfg_storage_keys.h"
#include "split_protocol.h"
#include <string.h>

#define MAX_RECORDED_PUSHES 256

static char s_recorded_keys[MAX_RECORDED_PUSHES][SPLIT_CONFIG_SYNC_KEY_LEN + 1];
static uint8_t s_recorded_kinds[MAX_RECORDED_PUSHES];
static size_t s_recorded_count = 0;

static uint64_t mock_seq_alloc(void) {
    static uint64_t s_seq = 100;
    return s_seq++;
}

uint64_t split_session_next_seq(void) {
    return mock_seq_alloc();
}

esp_err_t split_transport_send(const uint8_t *dst_mac, uint8_t proto,
                              uint8_t type, uint64_t seq,
                              const uint8_t *payload, size_t len)
{
    (void)dst_mac; (void)proto; (void)seq;
    if (type == SPLIT_MSG_CONFIG_SYNC && len >= SPLIT_CONFIG_SYNC_HDR_SIZE) {
        const split_config_sync_payload_t *hdr = (const split_config_sync_payload_t *)payload;
        if (s_recorded_count < MAX_RECORDED_PUSHES) {
            s_recorded_kinds[s_recorded_count] = hdr->kind;
            memcpy(s_recorded_keys[s_recorded_count], hdr->key, SPLIT_CONFIG_SYNC_KEY_LEN);
            s_recorded_keys[s_recorded_count][SPLIT_CONFIG_SYNC_KEY_LEN] = '\0';
            s_recorded_count++;
        }
    }
    return ESP_OK;
}

static bool was_key_pushed(uint8_t kind, const char *key) {
    for (size_t i = 0; i < s_recorded_count; i++) {
        if (s_recorded_kinds[i] == kind && strcmp(s_recorded_keys[i], key) == 0) {
            return true;
        }
    }
    return false;
}

TEST_CASE(split_config_sync, custom_keys_sync_beyond_64) {
    s_recorded_count = 0;
    mock_nvs_reset();

    // Setup custom keys index with bits > 64 set: 0, 63, 64, 65, 119
    cfg_ckey_index_t cidx = {0};
    cidx.mask[0] |= (1u << 0);    // ck_0
    cidx.mask[7] |= (1u << 7);    // ck_63 (63/8 = 7, 63%8 = 7)
    cidx.mask[8] |= (1u << 0);    // ck_64 (64/8 = 8, 64%8 = 0)
    cidx.mask[8] |= (1u << 1);    // ck_65 (65/8 = 8, 65%8 = 1)
    cidx.mask[14] |= (1u << 7);   // ck_119 (119/8 = 14, 119%8 = 7)

    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_idx", &cidx, sizeof(cidx));

    // Store dummy payloads for each active key
    cfg_custom_key_t dummy_key = {0};
    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_0", &dummy_key, sizeof(dummy_key));
    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_63", &dummy_key, sizeof(dummy_key));
    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_64", &dummy_key, sizeof(dummy_key));
    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_65", &dummy_key, sizeof(dummy_key));
    cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_119", &dummy_key, sizeof(dummy_key));

    uint8_t peer_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    esp_err_t err = split_config_sync_push_kind(peer_mac, mock_seq_alloc, CFGMOD_KIND_CKEY);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify ck_idx itself was pushed
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_idx"));

    // Verify keys <= 63 were pushed
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_0"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_63"));

    // Verify keys >= 64 were pushed (previously bugged!)
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_64"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_65"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_CKEY, "ck_119"));

    // Verify inactive keys were NOT pushed
    TEST_ASSERT(!was_key_pushed(CFGMOD_KIND_CKEY, "ck_1"));
    TEST_ASSERT(!was_key_pushed(CFGMOD_KIND_CKEY, "ck_66"));
}

TEST_CASE(split_config_sync, macros_sync_all_64) {
    s_recorded_count = 0;
    mock_nvs_reset();

    cfg_macro_index_t midx = {0};
    midx.active_mask = (1ULL << 0) | (1ULL << 31) | (1ULL << 63);
    cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_idx", &midx, sizeof(midx));

    cfg_macro_t dummy_macro = {0};
    cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_0", &dummy_macro, sizeof(dummy_macro));
    cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_31", &dummy_macro, sizeof(dummy_macro));
    cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_63", &dummy_macro, sizeof(dummy_macro));

    uint8_t peer_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    esp_err_t err = split_config_sync_push_kind(peer_mac, mock_seq_alloc, CFGMOD_KIND_MACRO);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_MACRO, "mac_idx"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_MACRO, "mac_0"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_MACRO, "mac_31"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_MACRO, "mac_63"));
    TEST_ASSERT(!was_key_pushed(CFGMOD_KIND_MACRO, "mac_1"));
}

TEST_CASE(split_config_sync, combos_sync_all_32) {
    s_recorded_count = 0;
    mock_nvs_reset();

    cfg_combo_index_t cmbidx = {0};
    cmbidx.active_mask = (1u << 0) | (1u << 15) | (1u << 31);
    cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_idx", &cmbidx, sizeof(cmbidx));

    cfg_combo_t dummy_combo = {0};
    cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_0", &dummy_combo, sizeof(dummy_combo));
    cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_15", &dummy_combo, sizeof(dummy_combo));
    cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_31", &dummy_combo, sizeof(dummy_combo));

    uint8_t peer_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    esp_err_t err = split_config_sync_push_kind(peer_mac, mock_seq_alloc, CFGMOD_KIND_COMBO);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_COMBO, "cmb_idx"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_COMBO, "cmb_0"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_COMBO, "cmb_15"));
    TEST_ASSERT(was_key_pushed(CFGMOD_KIND_COMBO, "cmb_31"));
    TEST_ASSERT(!was_key_pushed(CFGMOD_KIND_COMBO, "cmb_1"));
}
