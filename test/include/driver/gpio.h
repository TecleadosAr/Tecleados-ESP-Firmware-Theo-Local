/* GPIO driver shim — stub types for host testing */
#pragma once

#include <stdint.h>

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

static inline int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode) { (void)gpio_num; (void)mode; return 0; }
static inline int gpio_set_level(gpio_num_t gpio_num, uint32_t level) { (void)gpio_num; (void)level; return 0; }
static inline int gpio_reset_pin(gpio_num_t gpio_num) { (void)gpio_num; return 0; }
static inline int gpio_get_level(gpio_num_t gpio_num) { (void)gpio_num; return 0; }
