#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cfg_custom_keys.h"

/**
 * @brief Initialize the Custom Key engine.
 * Loads all custom keys from NVS into a RAM table.
 * Must be called once from kb_manager_start(), after kb_system_action_init().
 */
void kb_custom_key_init(void);

/**
 * @brief Reload the Custom Key table from NVS.
 * Called by the cfgmod on_update callback whenever the user saves/deletes a custom key.
 */
void kb_custom_key_reload(const char *key);

/**
 * @brief Process a key press/release whose action code is in
 * [ACTION_CODE_CKEY_MIN, ACTION_CODE_CKEY_MAX].
 *
 * Called by kb_macro_process_action() for codes in that range.
 *
 * @param action_code  Full 16-bit action code (e.g. 0x3000 + id).
 * @param is_pressed   true = key down, false = key up.
 */
void kb_custom_key_process_action(uint16_t action_code, bool is_pressed);

/**
 * @brief Check if an action code corresponds to a Custom Key in MultiAction mode.
 *
 * @param action_code  16-bit action code.
 * @param[out] out_ma  Optional pointer to receive copy of cfg_ckey_ma_t.
 * @return true if action_code is an active MultiAction Custom Key.
 */
bool kb_custom_key_is_tap_hold(uint16_t action_code, cfg_ckey_ma_t *out_ma);

/**
 * @brief Look up a custom key by ID from RAM cache.
 *
 * @param id Custom key ID [0..CFG_CKEYS_MAX_COUNT-1].
 * @return Pointer to custom key struct or NULL if not found.
 */
const cfg_custom_key_t *kb_custom_key_get(uint16_t id);

