#pragma once

#include <stdint.h>
#include "mocks/mock_esp.h"

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

typedef enum {
    ESP_MAC_WIFI_STA,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
} esp_mac_type_t;

static inline esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    (void)type;
    if (mac) {
        mac[0] = 0xAA; mac[1] = 0xBB; mac[2] = 0xCC;
        mac[3] = 0xDD; mac[4] = 0xEE; mac[5] = 0xFF;
    }
    return ESP_OK;
}
