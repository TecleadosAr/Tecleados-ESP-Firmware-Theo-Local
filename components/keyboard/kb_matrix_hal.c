#include "kb_matrix_hal.h"
#include "kb_matrix.h"

#include "driver/gpio.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_reg.h"
#include "esp_rom_sys.h"
#endif

typedef struct {
    bool       is_high_bank;
    uint32_t   mask;
    gpio_num_t gpio;
} col_hal_t;

static const gpio_num_t s_row_pins[KB_MATRIX_ROW_COUNT] = {
    GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6
};

#define LOW_MASK_PIN(p)  (1UL << (p))
#define HIGH_MASK_PIN(p) (1UL << ((p) - 32))

#define COLS_LOW_BANK_MASK ( \
    LOW_MASK_PIN(7)  | LOW_MASK_PIN(8)  | LOW_MASK_PIN(9)  | LOW_MASK_PIN(10) | \
    LOW_MASK_PIN(11) | LOW_MASK_PIN(12) | LOW_MASK_PIN(13) | LOW_MASK_PIN(14) | \
    LOW_MASK_PIN(15) | LOW_MASK_PIN(16) | LOW_MASK_PIN(17) | LOW_MASK_PIN(18) | \
    LOW_MASK_PIN(21) )

#define COLS_HIGH_BANK_MASK ( \
    HIGH_MASK_PIN(38) | HIGH_MASK_PIN(39) | HIGH_MASK_PIN(40) | HIGH_MASK_PIN(41) | \
    HIGH_MASK_PIN(42) )

#define COL_ENTRY_LOW(pin)  { .is_high_bank = false, .mask = LOW_MASK_PIN(pin),  .gpio = (gpio_num_t)(pin) }
#define COL_ENTRY_HIGH(pin) { .is_high_bank = true,  .mask = HIGH_MASK_PIN(pin), .gpio = (gpio_num_t)(pin) }

static const col_hal_t s_cols[KB_MATRIX_COL_COUNT] = {
    COL_ENTRY_LOW(7),
    COL_ENTRY_LOW(8),
    COL_ENTRY_LOW(9),
    COL_ENTRY_LOW(10),
    COL_ENTRY_LOW(11),
    COL_ENTRY_LOW(12),
    COL_ENTRY_LOW(13),
    COL_ENTRY_LOW(14),
    COL_ENTRY_LOW(15),
    COL_ENTRY_LOW(16),
    COL_ENTRY_LOW(17),
    COL_ENTRY_LOW(18),
    COL_ENTRY_LOW(21),
    COL_ENTRY_HIGH(38),
    COL_ENTRY_HIGH(39),
    COL_ENTRY_HIGH(40),
    COL_ENTRY_HIGH(41),
    COL_ENTRY_HIGH(42),
};

void kb_matrix_hal_init(void) {
    /* Pin tables are compile-time constants. */
}

void kb_matrix_hal_select_col(size_t col) {
    if (col >= KB_MATRIX_COL_COUNT) return;

#ifdef ESP_PLATFORM
    if (s_cols[col].is_high_bank) {
        REG_WRITE(GPIO_OUT1_W1TC_REG, s_cols[col].mask);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, s_cols[col].mask);
    }
#else
    gpio_set_level(s_cols[col].gpio, 0);
#endif
}

void kb_matrix_hal_deselect_col(size_t col) {
    if (col >= KB_MATRIX_COL_COUNT) return;

#ifdef ESP_PLATFORM
    if (s_cols[col].is_high_bank) {
        REG_WRITE(GPIO_OUT1_W1TS_REG, s_cols[col].mask);
    } else {
        REG_WRITE(GPIO_OUT_W1TS_REG, s_cols[col].mask);
    }
#else
    gpio_set_level(s_cols[col].gpio, 1);
#endif
}

void kb_matrix_hal_set_all_cols(uint32_t level) {
#ifdef ESP_PLATFORM
    if (level == 0) {
        REG_WRITE(GPIO_OUT_W1TC_REG, COLS_LOW_BANK_MASK);
        REG_WRITE(GPIO_OUT1_W1TC_REG, COLS_HIGH_BANK_MASK);
    } else {
        REG_WRITE(GPIO_OUT_W1TS_REG, COLS_LOW_BANK_MASK);
        REG_WRITE(GPIO_OUT1_W1TS_REG, COLS_HIGH_BANK_MASK);
    }
#else
    for (size_t c = 0; c < KB_MATRIX_COL_COUNT; ++c) {
        gpio_set_level(s_cols[c].gpio, level);
    }
#endif
}

uint8_t kb_matrix_hal_read_rows(void) {
#ifdef ESP_PLATFORM
    /* Direct read of low GPIO bank (GPIO0..31). Rows are GPIO1..6.
     * Pulled-up rows are logic 1 when open, pulled down to 0 when closed.
     * Invert bits and shift right by 1 to align Row 0 (GPIO1) to bit 0. */
    uint32_t in = REG_READ(GPIO_IN_REG);
    return (uint8_t)((~in >> 1) & 0x3F);
#else
    uint8_t rows = 0;
    for (size_t r = 0; r < KB_MATRIX_ROW_COUNT; ++r) {
        if (gpio_get_level(s_row_pins[r]) == 0) {
            rows |= (uint8_t)(1U << r);
        }
    }
    return rows;
#endif
}

void kb_matrix_hal_hold_all_cols(bool hold) {
    for (size_t c = 0; c < KB_MATRIX_COL_COUNT; ++c) {
        if (hold) {
            gpio_hold_en(s_cols[c].gpio);
        } else {
            gpio_hold_dis(s_cols[c].gpio);
        }
    }
}
