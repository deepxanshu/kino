/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "bt_hid_mouse.h"

#include "bt_input.h"

static uint8_t s_hid_mouse_descriptor[] = {
    0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,  // USAGE (Mouse)
    0xa1, 0x01,  // COLLECTION (Application)
    0x09, 0x01,  //   USAGE (Pointer)
    0xa1, 0x00,  //   COLLECTION (Physical)
    0x05, 0x09,  //     USAGE_PAGE (Button)
    0x19, 0x01,  //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,  //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,  //     LOGICAL_MINIMUM (0)
    0x25, 0x01,  //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,  //     REPORT_COUNT (3)
    0x75, 0x01,  //     REPORT_SIZE (1)
    0x81, 0x02,  //     INPUT (Data,Var,Abs)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x75, 0x05,  //     REPORT_SIZE (5)
    0x81, 0x03,  //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,  //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,  //     USAGE (X)
    0x09, 0x31,  //     USAGE (Y)
    0x09, 0x38,  //     USAGE (Wheel)
    0x15, 0x81,  //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,  //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x03,  //     REPORT_COUNT (3)
    0x81, 0x06,  //     INPUT (Data,Var,Rel)
    0x05, 0x0c,  //     USAGE_PAGE (Consumer)
    0x0a, 0x38, 0x02,  // USAGE (AC Pan)
    0x15, 0x81,  //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,  //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x81, 0x06,  //     INPUT (Data,Var,Rel)
    0xc0,        //   END_COLLECTION
    0xc0,        // END_COLLECTION
};

static esp_hidd_app_param_t s_hid_app = {
    .name          = BT_INPUT_DEVICE_NAME,
    .description   = "Magic Stick Mouse",
    .provider      = "M5Stack",
    .subclass      = ESP_HID_CLASS_MIC,
    .desc_list     = s_hid_mouse_descriptor,
    .desc_list_len = sizeof(s_hid_mouse_descriptor),
};

static esp_hidd_qos_param_t s_hid_qos = {0};

esp_hidd_app_param_t *bt_hid_mouse_app_param(void)
{
    return &s_hid_app;
}

esp_hidd_qos_param_t *bt_hid_mouse_qos_param(void)
{
    return &s_hid_qos;
}
