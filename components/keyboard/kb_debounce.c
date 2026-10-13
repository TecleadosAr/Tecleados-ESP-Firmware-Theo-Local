#include "kb_debounce.h"
#include <string.h>
#include "kb_bitmap.h"

#ifndef CONFIG_TEF_EAGER_DEBOUNCE
#define CONFIG_TEF_EAGER_DEBOUNCE 1
#endif

#ifndef CONFIG_TEF_PRESS_CONFIRM_SCANS
#define CONFIG_TEF_PRESS_CONFIRM_SCANS 1
#endif

#ifndef CONFIG_TEF_RELEASE_DEBOUNCE_SCANS
#define CONFIG_TEF_RELEASE_DEBOUNCE_SCANS 5
#endif

static uint8_t s_debounce[KB_MATRIX_KEYS];

void kb_debounce_init(void) {
    memset(s_debounce, 0, sizeof(s_debounce));
}

void kb_debounce_reset(void) {
    memset(s_debounce, 0, sizeof(s_debounce));
}

uint8_t kb_debounce_get_counter(size_t key_index) {
    if (key_index < KB_MATRIX_KEYS) {
        return s_debounce[key_index];
    }
    return 0;
}

void kb_debounce_update(const uint8_t *raw, uint8_t *stable) {
    for (size_t i = 0; i < KB_MATRIX_KEYS; ++i) {
        bool raw_pressed = kb_bit_get(raw, i);

#if CONFIG_TEF_EAGER_DEBOUNCE
        if (raw_pressed) {
            if (s_debounce[i] < CONFIG_TEF_PRESS_CONFIRM_SCANS) {
                s_debounce[i]++;
            }
            if (s_debounce[i] >= CONFIG_TEF_PRESS_CONFIRM_SCANS) {
                /* Jump directly to release debounce scans for lowest latency press */
                s_debounce[i] = CONFIG_TEF_RELEASE_DEBOUNCE_SCANS;
                kb_bit_set(stable, i);
            }
        } else {
            if (s_debounce[i] > 0) {
                s_debounce[i]--;
            }
            if (s_debounce[i] == 0) {
                kb_bit_clear(stable, i);
            }
        }
#else
        if (raw_pressed) {
            if (s_debounce[i] < CONFIG_TEF_RELEASE_DEBOUNCE_SCANS) {
                s_debounce[i]++;
            }
        } else {
            if (s_debounce[i] > 0) {
                s_debounce[i]--;
            }
        }

        if (s_debounce[i] == 0) {
            kb_bit_clear(stable, i);
        } else if (s_debounce[i] >= CONFIG_TEF_RELEASE_DEBOUNCE_SCANS) {
            kb_bit_set(stable, i);
        }
#endif
    }
}
