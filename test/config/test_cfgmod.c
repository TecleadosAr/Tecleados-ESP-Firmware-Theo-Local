/**
 * @file test_cfgmod.c
 * @brief Unit tests for cfgmod.c (cfgmod_handle_usb_comm dispatch, limits, CRUD, mask updates).
 */

#include "test_harness.h"
#include "cfgmod.h"
#include "cfg_macros.h"
#include "cfg_combos.h"
#include "cfg_custom_keys.h"
#include "cfg_layouts.h"
#include "cfg_storage_keys.h"
#include "mocks/mock_nvs.h"
#include <string.h>

static void setup_cfgmod_test(void) {
    mock_nvs_reset();
    cfg_init();
}

TEST_CASE(cfgmod, get_limits) {
    setup_cfgmod_test();

    uint8_t req_buf[sizeof(cfgmod_req_header_t)];
    uint8_t out_buf[128];
    size_t out_len = 0;

    // 1. LAYOUT_LIMITS
    cfgmod_req_header_t *hdr = (cfgmod_req_header_t *)req_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->cmd = CFG_CMD_GET;
    hdr->key_id = CFG_KEY_LAYOUT_LIMITS;

    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), out_buf, &out_len, sizeof(out_buf)));
    cfgmod_rsp_header_t *rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);
    TEST_ASSERT_EQUAL(sizeof(cfgmod_rsp_header_t) + 1, out_len);
    TEST_ASSERT_EQUAL(CFG_LAYOUT_MAX_COUNT, out_buf[sizeof(cfgmod_rsp_header_t)]);

    // 2. MACRO_LIMITS
    hdr->key_id = CFG_KEY_MACRO_LIMITS;
    out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), out_buf, &out_len, sizeof(out_buf)));
    rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);
    TEST_ASSERT_EQUAL(sizeof(cfgmod_rsp_header_t) + 4, out_len);
    uint16_t m_limits[2];
    memcpy(m_limits, out_buf + sizeof(cfgmod_rsp_header_t), sizeof(m_limits));
    TEST_ASSERT_EQUAL(CFG_MACROS_MAX_COUNT, m_limits[0]);
    TEST_ASSERT_EQUAL(CFG_MACRO_MAX_EVENTS, m_limits[1]);

    // 3. COMBO_LIMITS
    hdr->key_id = CFG_KEY_COMBO_LIMITS;
    out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), out_buf, &out_len, sizeof(out_buf)));
    rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);
    TEST_ASSERT_EQUAL(sizeof(cfgmod_rsp_header_t) + 4, out_len);
    uint16_t c_limits[2];
    memcpy(c_limits, out_buf + sizeof(cfgmod_rsp_header_t), sizeof(c_limits));
    TEST_ASSERT_EQUAL(CFG_COMBOS_MAX_COUNT, c_limits[0]);
    TEST_ASSERT_EQUAL(CFG_COMBO_MAX_KEYS, c_limits[1]);
}

TEST_CASE(cfgmod, macro_single_set_and_delete_mask_update) {
    setup_cfgmod_test();

    uint16_t macro_id = 5;
    size_t req_size = sizeof(cfgmod_req_header_t) + sizeof(cfg_macro_t);
    uint8_t *req_buf = malloc(req_size);
    TEST_ASSERT(req_buf != NULL);

    cfgmod_req_header_t *hdr = (cfgmod_req_header_t *)req_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->cmd = CFG_CMD_SET;
    hdr->key_id = CFG_KEY_MACRO_SINGLE;
    hdr->item_id = macro_id;

    cfg_macro_t *mac = calloc(1, sizeof(cfg_macro_t));
    TEST_ASSERT(mac != NULL);
    mac->id = macro_id;
    mac->event_count = 2;
    strncpy(mac->name, "TestMacro", sizeof(mac->name) - 1);
    memcpy(req_buf + sizeof(cfgmod_req_header_t), mac, sizeof(cfg_macro_t));
    free(mac);

    uint8_t out_buf[128];
    size_t out_len = 0;

    // SET macro 5
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, req_size, out_buf, &out_len, sizeof(out_buf)));
    cfgmod_rsp_header_t *rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify mac_idx mask has bit 5 set
    uint64_t mac_mask = 0;
    size_t len = sizeof(mac_mask);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &mac_mask, &len));
    TEST_ASSERT((mac_mask & (1ULL << macro_id)) != 0);

    // Now DELETE macro 5 by sending SET with 0-length payload
    out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(cfgmod_req_header_t), out_buf, &out_len, sizeof(out_buf)));
    rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify bit 5 in mac_idx is cleared
    len = sizeof(mac_mask);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &mac_mask, &len));
    TEST_ASSERT_EQUAL(0, mac_mask & (1ULL << macro_id));

    free(req_buf);
}

TEST_CASE(cfgmod, combo_single_set_and_delete_mask_update) {
    setup_cfgmod_test();

    uint16_t combo_id = 12;
    size_t req_size = sizeof(cfgmod_req_header_t) + sizeof(cfg_combo_t);
    uint8_t *req_buf = malloc(req_size);
    TEST_ASSERT(req_buf != NULL);

    cfgmod_req_header_t *hdr = (cfgmod_req_header_t *)req_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->cmd = CFG_CMD_SET;
    hdr->key_id = CFG_KEY_COMBO_SINGLE;
    hdr->item_id = combo_id;

    cfg_combo_t cmb = {0};
    cmb.id = combo_id;
    cmb.key_count = 2;
    strncpy(cmb.name, "TestCombo", sizeof(cmb.name) - 1);
    memcpy(req_buf + sizeof(cfgmod_req_header_t), &cmb, sizeof(cmb));

    uint8_t out_buf[128];
    size_t out_len = 0;

    // SET combo 12
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, req_size, out_buf, &out_len, sizeof(out_buf)));
    cfgmod_rsp_header_t *rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify cmb_idx mask has bit 12 set
    uint32_t cmb_mask = 0;
    size_t len = sizeof(cmb_mask);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_COMBO, "cmb_idx", &cmb_mask, &len));
    TEST_ASSERT((cmb_mask & (1u << combo_id)) != 0);

    // DELETE combo 12 (payload len 0)
    out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(cfgmod_req_header_t), out_buf, &out_len, sizeof(out_buf)));
    rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify bit 12 in cmb_idx is cleared
    len = sizeof(cmb_mask);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_COMBO, "cmb_idx", &cmb_mask, &len));
    TEST_ASSERT_EQUAL(0, cmb_mask & (1u << combo_id));

    free(req_buf);
}

TEST_CASE(cfgmod, ckey_single_set_and_delete_mask_update) {
    setup_cfgmod_test();

    uint16_t ckey_id = 70; // > 64 boundary check
    size_t req_size = sizeof(cfgmod_req_header_t) + sizeof(cfg_custom_key_t);
    uint8_t *req_buf = malloc(req_size);
    TEST_ASSERT(req_buf != NULL);

    cfgmod_req_header_t *hdr = (cfgmod_req_header_t *)req_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->cmd = CFG_CMD_SET;
    hdr->key_id = CFG_KEY_CKEY_SINGLE;
    hdr->item_id = ckey_id;

    cfg_custom_key_t ck = {0};
    ck.id = ckey_id;
    ck.mode = CKEY_MODE_PRESS_RELEASE;
    strncpy(ck.name, "TestCKey70", sizeof(ck.name) - 1);
    memcpy(req_buf + sizeof(cfgmod_req_header_t), &ck, sizeof(ck));

    uint8_t out_buf[128];
    size_t out_len = 0;

    // SET ckey 70
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, req_size, out_buf, &out_len, sizeof(out_buf)));
    cfgmod_rsp_header_t *rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify ck_idx mask has bit 70 set (byte 70/8 = 8, bit 70%8 = 6)
    cfg_ckey_index_t cidx = {0};
    size_t len = sizeof(cidx);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &cidx, &len));
    TEST_ASSERT((cidx.mask[ckey_id / 8] & (1u << (ckey_id % 8))) != 0);

    // DELETE ckey 70 (payload len 0)
    out_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(cfgmod_req_header_t), out_buf, &out_len, sizeof(out_buf)));
    rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_OK, rsp->status);

    // Verify bit 70 in ck_idx is cleared
    len = sizeof(cidx);
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &cidx, &len));
    TEST_ASSERT_EQUAL(0, cidx.mask[ckey_id / 8] & (1u << (ckey_id % 8)));

    free(req_buf);
}

TEST_CASE(cfgmod, size_mismatch_and_error_handling) {
    setup_cfgmod_test();

    uint8_t req_buf[sizeof(cfgmod_req_header_t) + 10]; // only 10 bytes payload for combo (expects 64)
    cfgmod_req_header_t *hdr = (cfgmod_req_header_t *)req_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->cmd = CFG_CMD_SET;
    hdr->key_id = CFG_KEY_COMBO_SINGLE;
    hdr->item_id = 1;

    uint8_t out_buf[128];
    size_t out_len = 0;

    // Size mismatch returns rsp.status = ESP_ERR_INVALID_SIZE
    TEST_ASSERT_EQUAL(ESP_OK, cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), out_buf, &out_len, sizeof(out_buf)));
    cfgmod_rsp_header_t *rsp = (cfgmod_rsp_header_t *)out_buf;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, rsp->status);

    // Truncated request (< sizeof header)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      cfgmod_handle_usb_comm(req_buf, sizeof(cfgmod_req_header_t) - 1, out_buf, &out_len, sizeof(out_buf)));

    // Invalid key_id >= CFG_KEY_MAX
    hdr->key_id = CFG_KEY_MAX;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_handle_usb_comm(req_buf, sizeof(cfgmod_req_header_t), out_buf, &out_len, sizeof(out_buf)));

    // Null pointers
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_handle_usb_comm(NULL, sizeof(req_buf), out_buf, &out_len, sizeof(out_buf)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), NULL, &out_len, sizeof(out_buf)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_handle_usb_comm(req_buf, sizeof(req_buf), out_buf, NULL, sizeof(out_buf)));
}
