#pragma once
#include "mocks/mock_esp.h"
#include <stdint.h>

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY  = 0,
    NVS_READWRITE = 1
} nvs_open_mode_t;
