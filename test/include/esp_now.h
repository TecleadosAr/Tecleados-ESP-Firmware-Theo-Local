#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ESP_NOW_ETH_ALEN 6

typedef enum {
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL,
} esp_now_send_status_t;
