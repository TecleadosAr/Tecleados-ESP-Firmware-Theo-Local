/* GPIO driver shim — stub types for host testing */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

typedef int gpio_num_t;

#define GPIO_NUM_0   0
#define GPIO_NUM_1   1
#define GPIO_NUM_2   2
#define GPIO_NUM_3   3
#define GPIO_NUM_4   4
#define GPIO_NUM_5   5
#define GPIO_NUM_6   6
#define GPIO_NUM_7   7
#define GPIO_NUM_8   8
#define GPIO_NUM_9   9
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_16 16
#define GPIO_NUM_17 17
#define GPIO_NUM_18 18
#define GPIO_NUM_21 21
#define GPIO_NUM_38 38
#define GPIO_NUM_39 39
#define GPIO_NUM_40 40
#define GPIO_NUM_41 41
#define GPIO_NUM_42 42

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT = 1,
    GPIO_MODE_OUTPUT = 2,
    GPIO_MODE_OUTPUT_OD = 3,
    GPIO_MODE_INPUT_OUTPUT_OD = 4,
    GPIO_MODE_INPUT_OUTPUT = 5,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE = 1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE = 1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_POSEDGE = 1,
    GPIO_INTR_NEGEDGE = 2,
    GPIO_INTR_ANYEDGE = 3,
    GPIO_INTR_LOW_LEVEL = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
} gpio_int_type_t;

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

typedef void (*gpio_isr_t)(void *arg);

static uint32_t _mock_gpio_levels[64];
static bool _mock_gpio_switch_closed[64][64]; /* [row][col] */

static uint64_t _mock_gpio_held_mask = 0;
static bool     _mock_gpio_deep_sleep_held = false;

static inline void mock_gpio_reset_all(void) {
    for (int i = 0; i < 64; i++) {
        _mock_gpio_levels[i] = 1; /* default HIGH (pullup) */
    }
    memset(_mock_gpio_switch_closed, 0, sizeof(_mock_gpio_switch_closed));
    _mock_gpio_held_mask = 0;
    _mock_gpio_deep_sleep_held = false;
}

static inline uint64_t mock_gpio_get_held_mask(void) {
    return _mock_gpio_held_mask;
}

static inline bool mock_gpio_get_deep_sleep_held(void) {
    return _mock_gpio_deep_sleep_held;
}

static inline void mock_gpio_set_switch(gpio_num_t row, gpio_num_t col, bool closed) {
    if (row >= 0 && row < 64 && col >= 0 && col < 64) {
        _mock_gpio_switch_closed[row][col] = closed;
    }
}

static inline int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode) { (void)gpio_num; (void)mode; return 0; }
static inline int gpio_reset_pin(gpio_num_t gpio_num) { (void)gpio_num; return 0; }
static inline int gpio_config(const gpio_config_t *p) { (void)p; return 0; }
static inline int gpio_install_isr_service(int flags) { (void)flags; return 0; }
static inline int gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args) {
    (void)gpio_num; (void)isr_handler; (void)args; return 0;
}
static inline int gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type) {
    (void)gpio_num; (void)intr_type; return 0;
}
static inline int gpio_intr_enable(gpio_num_t gpio_num) { (void)gpio_num; return 0; }
static inline int gpio_intr_disable(gpio_num_t gpio_num) { (void)gpio_num; return 0; }

static inline int gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    if (gpio_num >= 0 && gpio_num < 64) {
        _mock_gpio_levels[gpio_num] = level;
    }
    return 0;
}

static inline int gpio_get_level(gpio_num_t gpio_num) {
    if (gpio_num >= 0 && gpio_num < 64) {
        for (int c = 0; c < 64; c++) {
            if (_mock_gpio_switch_closed[gpio_num][c]) {
                if (_mock_gpio_levels[c] == 0) {
                    return 0;
                }
            }
        }
        return _mock_gpio_levels[gpio_num];
    }
    return 1;
}

static inline int gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type) {
    (void)gpio_num; (void)intr_type;
    return 0;
}
static inline int gpio_wakeup_disable(gpio_num_t gpio_num) {
    (void)gpio_num;
    return 0;
}
static inline int gpio_hold_en(gpio_num_t gpio_num) {
    if (gpio_num >= 0 && gpio_num < 64) {
        _mock_gpio_held_mask |= (1ULL << gpio_num);
    }
    return 0;
}
static inline int gpio_hold_dis(gpio_num_t gpio_num) {
    if (gpio_num >= 0 && gpio_num < 64) {
        _mock_gpio_held_mask &= ~(1ULL << gpio_num);
    }
    return 0;
}
static inline void gpio_deep_sleep_hold_en(void) {
    _mock_gpio_deep_sleep_held = true;
}
static inline void gpio_deep_sleep_hold_dis(void) {
    _mock_gpio_deep_sleep_held = false;
}
static inline int rtc_gpio_pullup_en(gpio_num_t gpio_num) {
    (void)gpio_num;
    return 0;
}
static inline int gpio_pullup_en(gpio_num_t gpio_num) {
    (void)gpio_num;
    return 0;
}
