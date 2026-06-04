/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BT_HID_MOUSE_H
#define BT_HID_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_hidd_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HID_MOUSE_REPORT_ID (1)
#define BT_HID_MOUSE_REPORT_SIZE (5)
#define BT_HID_KEY_REPORT_ID (2)
#define BT_HID_KEY_REPORT_SIZE (1)
#define BT_HID_F15_USAGE (0x6a)
#define BT_HID_F15_TAP_MS (80)

esp_hidd_app_param_t *bt_hid_mouse_app_param(void);
esp_hidd_qos_param_t *bt_hid_mouse_qos_param(void);
void bt_hid_f15_report_build(uint8_t report[BT_HID_KEY_REPORT_SIZE], bool pressed);

#ifdef __cplusplus
}
#endif

#endif  // BT_HID_MOUSE_H
