/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BT_HID_MOUSE_H
#define BT_HID_MOUSE_H

#include "esp_hidd_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HID_MOUSE_REPORT_SIZE (5)

esp_hidd_app_param_t *bt_hid_mouse_app_param(void);
esp_hidd_qos_param_t *bt_hid_mouse_qos_param(void);

#ifdef __cplusplus
}
#endif

#endif  // BT_HID_MOUSE_H
