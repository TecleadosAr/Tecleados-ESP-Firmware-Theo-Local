#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tinyusb.h"

// real stuff
bool usb_keyboard_use_boot_protocol(void);
bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]);
bool usb_send_keyboard_nkro(uint8_t modifier, const uint8_t *bitmap,
                            uint16_t len);
bool usb_send_consumer_report(uint16_t keycode);

void usb_task(void *arg);
void usb_init();

// callback stuff (to be managed inside usb_callbacks)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen);

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize);