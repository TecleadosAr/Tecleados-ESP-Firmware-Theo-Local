/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "driver/gpio.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"

#include "kb_manager.h"
#include "kb_macro.h"
#include "kb_layout.h"

#include "blemod.h"
#include "button.h"
#include "cfgmod.h"
#include "rgb.h"
#include "comm_module.h"
#include "usbmod.h"
#include "statusmod.h"

#include "ble_controller.h"
#include "event_bus.h"
#include "splitmod.h"
#include "battery.h"
#include "power_mgr.h"

#define TAG "MAIN"

static void on_system_button_single_press(void) {
    ESP_LOGI(TAG, "System button single press: Connection Test Beep");
    splitmod_send_test_beep();
}

static void on_system_button_double_press(void) {
    ESP_LOGI(TAG, "System button double press: Activate BLE pairing on Profile 1");
    if (!ble_hid_is_routing_active()) {
        ble_hid_set_routing_active(true);
    }
    ble_hid_profile_pair(0);
}

static void on_system_button_hold_press(void) {
    ESP_LOGI(TAG, "System button hold press: Start split pairing");
    splitmod_start_pairing(0);
}

static void init_procedure(void) {
  event_bus_init();
  button_init(on_system_button_single_press, on_system_button_double_press, on_system_button_hold_press);
  cfg_init();

  // Initialize RGB. GPIO 48 is standard on many ESP32-S3 boards.
  rgb_init(GPIO_NUM_48);

  comm_init();
  usb_init();
  ble_hid_init();

  ble_controller_init();
  status_module_init();

  battery_init();
  power_mgr_init();

  splitmod_init();
  kb_manager_start();
}

void app_main(void) {
  init_procedure();
}