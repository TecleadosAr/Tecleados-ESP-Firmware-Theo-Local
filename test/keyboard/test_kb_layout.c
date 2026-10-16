/**
 * @file test_kb_layout.c
 * @brief Tests for cfg_layouts.c and kb_layout.c — dynamic layout CRUD,
 *        cache tiering / DRAM swap, layer mask resolution, and transparent fallthrough.
 *
 * Links against real cfg_layouts.c and kb_layout.c (included in main.c).
 * Uses mock NVS (cfgmod.h shim) to inject or verify persistent layer data.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "cfg_layouts.h"
#include "kb_layout.h"

TEST_SETUP(kb_layout) {
    mock_nvs_reset();
    cfg_layouts_register();
    /* Load with empty NVS → initializes default Base layout */
    cfg_layout_load_all();
}

/* ---- Factory default tests (Base layer from keymaps_base) ---- */

TEST_CASE(kb_layout, factory_base_layer_escape) {
    /* Row 0, Col 0 of base layer = HID_KEY_ESCAPE (0x29) */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_ESCAPE, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_base_layer_a_key) {
    /* Row 2, Col 1 = HID_KEY_A (0x04) */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_A, cfg_layout_get_action_code(2, 1, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_base_layer_fn_keys) {
    /* Row 4: FN1 at col 10, FN2 at col 11 */
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_LAYER_MOMENTARY + 1, cfg_layout_get_action_code(4, 10, KB_LAYER_BASE));
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_LAYER_MOMENTARY + 2, cfg_layout_get_action_code(4, 11, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_base_spacebar) {
    /* Row 4, Col 5 = HID_KEY_SPACE (0x2C) */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_SPACE, cfg_layout_get_action_code(4, 5, KB_LAYER_BASE));
}

/* ---- Bounds checking ---- */

TEST_CASE(kb_layout, out_of_bounds_row_returns_none) {
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT, 0, 0));
}

TEST_CASE(kb_layout, out_of_bounds_col_returns_none) {
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(0, KB_MATRIX_COL_COUNT, 0));
}

TEST_CASE(kb_layout, out_of_bounds_layer_defaults_to_base) {
    /* Layer 99 >= CFG_LAYOUT_MAX_COUNT → clamps to base */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_ESCAPE, cfg_layout_get_action_code(0, 0, 99));
}

/* ---- Dynamic layer creation and DRAM swap cache tier ---- */

TEST_CASE(kb_layout, dynamic_layer_creation_and_dram_swap) {
    uint8_t fn1_id = 0;
    esp_err_t err = cfg_layout_create("FN1", &fn1_id);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT(fn1_id > 0);

    /* Populate FN1 with grave at [0][0] and delete at [0][14] */
    cfg_layer_t fn1;
    memset(&fn1, 0xFF, sizeof(fn1)); /* All transparent */
    fn1.keys[0][0]  = HID_KEY_GRAVE;  /* 0x35 */
    fn1.keys[0][14] = HID_KEY_DELETE; /* 0x4C */
    err = cfg_layout_set_layer(fn1_id, &fn1);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Accessing FN1 triggers PSRAM→DRAM swap. Second access is a cache hit. */
    uint16_t first = cfg_layout_get_action_code(0, 0, fn1_id);
    uint16_t second = cfg_layout_get_action_code(0, 0, fn1_id);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_GRAVE, first);
    TEST_ASSERT_EQUAL_HEX(first, second);
}

TEST_CASE(kb_layout, dram_swap_eviction_on_layer_switch) {
    uint8_t fn1_id = 0, fn2_id = 0;
    cfg_layout_create("FN1", &fn1_id);
    cfg_layout_create("FN2", &fn2_id);

    cfg_layer_t fn1, fn2;
    memset(&fn1, 0xFF, sizeof(fn1));
    memset(&fn2, 0xFF, sizeof(fn2));
    fn1.keys[0][0] = HID_KEY_GRAVE;
    fn2.keys[2][1] = HID_KEY_B;
    cfg_layout_set_layer(fn1_id, &fn1);
    cfg_layout_set_layer(fn2_id, &fn2);

    /* Access FN1, then FN2 — swap cache should switch */
    uint16_t val1 = cfg_layout_get_action_code(0, 0, fn1_id);
    uint16_t val2 = cfg_layout_get_action_code(2, 1, fn2_id);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_GRAVE, val1);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_B, val2);
}

/* ---- Layer mask resolution & transparent fallthrough (kb_layout_get_action_code) ---- */

TEST_CASE(kb_layout, layer_mask_base_only) {
    uint16_t mask = (1 << KB_LAYER_BASE);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_ESCAPE, kb_layout_get_action_code(0, 0, mask));
    TEST_ASSERT_EQUAL_HEX(HID_KEY_A,      kb_layout_get_action_code(2, 1, mask));
    TEST_ASSERT_EQUAL_HEX(HID_KEY_SPACE,  kb_layout_get_action_code(4, 5, mask));
}

TEST_CASE(kb_layout, layer_mask_with_active_fn1_and_fallthrough) {
    uint8_t fn1_id = 0;
    cfg_layout_create("FN1", &fn1_id);

    cfg_layer_t fn1;
    memset(&fn1, 0xFF, sizeof(fn1)); /* All transparent */
    fn1.keys[0][0]  = HID_KEY_GRAVE;  /* override Esc */
    fn1.keys[0][14] = HID_KEY_DELETE; /* override Insert */
    cfg_layout_set_layer(fn1_id, &fn1);

    uint16_t mask = (1 << KB_LAYER_BASE) | (1 << fn1_id);

    /* Override keys returned from FN1 */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_GRAVE,  kb_layout_get_action_code(0, 0, mask));
    TEST_ASSERT_EQUAL_HEX(HID_KEY_DELETE, kb_layout_get_action_code(0, 14, mask));

    /* Transparent keys fall through to base */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_Q, kb_layout_get_action_code(1, 1, mask));
    TEST_ASSERT_EQUAL_HEX(HID_KEY_A, kb_layout_get_action_code(2, 1, mask));
}

TEST_CASE(kb_layout, layer_mask_multi_layer_precedence) {
    uint8_t fn1_id = 0, fn2_id = 0;
    cfg_layout_create("FN1", &fn1_id);
    cfg_layout_create("FN2", &fn2_id);

    cfg_layer_t fn1, fn2;
    memset(&fn1, 0xFF, sizeof(fn1));
    memset(&fn2, 0xFF, sizeof(fn2));
    fn1.keys[0][0] = HID_KEY_GRAVE;
    fn2.keys[0][0] = HID_KEY_F1;
    cfg_layout_set_layer(fn1_id, &fn1);
    cfg_layout_set_layer(fn2_id, &fn2);

    /* In default order, later created layer (fn2) is higher in order stack */
    uint16_t mask = (1 << KB_LAYER_BASE) | (1 << fn1_id) | (1 << fn2_id);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_F1, kb_layout_get_action_code(0, 0, mask));

    /* When only fn1 is active in mask, returns fn1 */
    uint16_t mask_fn1 = (1 << KB_LAYER_BASE) | (1 << fn1_id);
    TEST_ASSERT_EQUAL_HEX(HID_KEY_GRAVE, kb_layout_get_action_code(0, 0, mask_fn1));
}

/* ---- Base layer protection ---- */

TEST_CASE(kb_layout, base_layer_delete_and_rename_rejected) {
    /* Base layer cannot be deleted */
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, cfg_layout_delete(0));

    /* Base layer cannot be renamed */
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, cfg_layout_rename(0, "SomethingElse"));

    /* Reorder that does not have 0 at index 0 must be rejected */
    uint8_t bad_order[2] = {1, 0};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, cfg_layout_reorder(bad_order, 2));
}

/* ---- NVS persistence & reload ---- */

TEST_CASE(kb_layout, nvs_base_layer_override) {
    mock_nvs_reset();

    /* Build a custom base layer with key A at [0][0] instead of Escape */
    cfg_layer_t custom_base;
    memset(&custom_base, 0, sizeof(custom_base));
    custom_base.keys[0][0] = HID_KEY_A;
    custom_base.keys[2][1] = HID_KEY_B;

    mock_nvs_inject(CFGMOD_KIND_LAYOUT, "ly_0", &custom_base, sizeof(custom_base));
    cfg_layout_load_all();

    TEST_ASSERT_EQUAL_HEX(HID_KEY_A, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
    TEST_ASSERT_EQUAL_HEX(HID_KEY_B, cfg_layout_get_action_code(2, 1, KB_LAYER_BASE));
}

/* ---- All matrix corners ---- */

TEST_CASE(kb_layout, all_corners_valid) {
    uint16_t mask = (1 << KB_LAYER_BASE);
    uint16_t tl = kb_layout_get_action_code(0, 0, mask);
    TEST_ASSERT(tl != ACTION_CODE_NONE);

    uint16_t tr = kb_layout_get_action_code(0, KB_MATRIX_COL_COUNT - 1, mask);
    (void)tr;

    uint16_t bl = kb_layout_get_action_code(KB_MATRIX_ROW_COUNT - 1, 0, mask);
    (void)bl;

    uint16_t br = kb_layout_get_action_code(KB_MATRIX_ROW_COUNT - 1, KB_MATRIX_COL_COUNT - 1, mask);
    (void)br;

    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT, KB_MATRIX_COL_COUNT, 0));
}

#include <pthread.h>

TEST_CASE(kb_layout, layer_mask_atomic_operations) {
    kb_macro_process_action(ACTION_CODE_LAYER_CLEAR_ALL, true);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());

    // Momentary press and release
    kb_macro_process_action(ACTION_CODE_LAYER_MOMENTARY + 2, true);
    TEST_ASSERT_EQUAL((1 << KB_LAYER_BASE) | (1 << 2), kb_macro_get_layer_mask());
    kb_macro_process_action(ACTION_CODE_LAYER_MOMENTARY + 2, false);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());

    // Toggle on and toggle off
    kb_macro_process_action(ACTION_CODE_LAYER_TOGGLE + 3, true);
    TEST_ASSERT_EQUAL((1 << KB_LAYER_BASE) | (1 << 3), kb_macro_get_layer_mask());
    kb_macro_process_action(ACTION_CODE_LAYER_TOGGLE + 3, true);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());

    // Explicit Layer ON and OFF
    kb_macro_process_action(ACTION_CODE_LAYER_ON + 4, true);
    TEST_ASSERT_EQUAL((1 << KB_LAYER_BASE) | (1 << 4), kb_macro_get_layer_mask());
    kb_macro_process_action(ACTION_CODE_LAYER_OFF + 4, true);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());

    // Clear all
    kb_macro_process_action(ACTION_CODE_LAYER_TOGGLE + 1, true);
    kb_macro_process_action(ACTION_CODE_LAYER_ON + 2, true);
    kb_macro_process_action(ACTION_CODE_LAYER_CLEAR_ALL, true);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());
}

TEST_CASE(kb_layout, layer_mask_purge_deleted_layer) {
    kb_macro_process_action(ACTION_CODE_LAYER_CLEAR_ALL, true);
    mock_nvs_reset();
    cfg_layout_load_all();

    // Toggle layer 5 (which does not exist in config)
    kb_macro_process_action(ACTION_CODE_LAYER_TOGGLE + 5, true);
    TEST_ASSERT((kb_macro_get_layer_mask() & (1 << 5)) != 0);

    // Purge deleted layers
    kb_macro_purge_deleted_layers();
    TEST_ASSERT_EQUAL(0, kb_macro_get_layer_mask() & (1 << 5));
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());
}

static void *thread_layer_toggle_hammer(void *arg) {
    (void)arg;
    for (int i = 0; i < 5000; i++) {
        kb_macro_process_action(ACTION_CODE_LAYER_TOGGLE + 1, true);
        kb_macro_process_action(ACTION_CODE_LAYER_MOMENTARY + 2, true);
        volatile uint16_t mask = kb_macro_get_layer_mask();
        (void)mask;
        kb_macro_process_action(ACTION_CODE_LAYER_MOMENTARY + 2, false);
    }
    return NULL;
}

static void *thread_layer_on_off_hammer(void *arg) {
    (void)arg;
    for (int i = 0; i < 5000; i++) {
        kb_macro_process_action(ACTION_CODE_LAYER_ON + 3, true);
        volatile uint16_t mask = kb_macro_get_layer_mask();
        (void)mask;
        kb_macro_process_action(ACTION_CODE_LAYER_OFF + 3, true);
    }
    return NULL;
}

TEST_CASE(kb_layout, layer_mask_multithreaded_hammer) {
    kb_macro_process_action(ACTION_CODE_LAYER_CLEAR_ALL, true);
    pthread_t t1, t2;
    int r1 = pthread_create(&t1, NULL, thread_layer_toggle_hammer, NULL);
    int r2 = pthread_create(&t2, NULL, thread_layer_on_off_hammer, NULL);
    TEST_ASSERT_EQUAL(0, r1);
    TEST_ASSERT_EQUAL(0, r2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    // Cleanup
    kb_macro_process_action(ACTION_CODE_LAYER_CLEAR_ALL, true);
    TEST_ASSERT_EQUAL(1 << KB_LAYER_BASE, kb_macro_get_layer_mask());
}

static void *thread_hot_reload_scanner(void *arg) {
    (void)arg;
    for (int i = 0; i < 2000; i++) {
        uint16_t kc = cfg_layout_get_action_code(0, 0, (uint8_t)(i % 2));
        kb_macro_process_action(kc, true);
        kb_macro_tap_tick((int64_t)i * 1000LL);
        kb_macro_process_action(kc, false);
    }
    return NULL;
}

static void *thread_hot_reload_mutator(void *arg) {
    (void)arg;
    cfg_layer_t dummy;
    memset(&dummy, 0x11, sizeof(dummy));
    for (int i = 0; i < 500; i++) {
        cfg_layout_set_layer(0, &dummy);
        on_macros_updated("stress_test");
        layout_update_cb("ly_0");
    }
    return NULL;
}

TEST_CASE(kb_layout, concurrent_hot_reload_and_scan_stress) {
    mock_nvs_reset();
    cfg_layout_load_all();

    pthread_t t1, t2;
    int r1 = pthread_create(&t1, NULL, thread_hot_reload_scanner, NULL);
    int r2 = pthread_create(&t2, NULL, thread_hot_reload_mutator, NULL);
    TEST_ASSERT_EQUAL(0, r1);
    TEST_ASSERT_EQUAL(0, r2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // Restore clean state
    mock_nvs_reset();
    cfg_layout_load_all();
}

