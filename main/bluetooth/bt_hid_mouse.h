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
#define BT_HID_KEY_REPORT_SIZE (2)  // kino: [0]=modifier bitmap, [1]=keycode
// kino: modifier bitmap bits, matches Keyboard/Keypad usages 0xE0-0xE7 in order.
#define BT_HID_MOD_LEFT_CTRL (1u << 0)
#define BT_HID_MOD_LEFT_SHIFT (1u << 1)
#define BT_HID_MOD_LEFT_ALT (1u << 2)
#define BT_HID_MOD_LEFT_GUI (1u << 3)  // Cmd on macOS
#define BT_HID_F15_USAGE (0x6a)
// kino: dictation trigger is Ctrl+F5, matching what Deepanshu already bound
// in Wispr Flow. Note: macOS reserves bare Ctrl+F5 by default for "Move focus
// to the window toolbar" (System Settings > Keyboard > Keyboard Shortcuts >
// Keyboard). If focus jumps unexpectedly on tap, disable that shortcut there
// or pick a different combo. (Fn+Space is NOT an option on any Bluetooth
// device -- Fn is consumed inside Apple keyboards' own controller and has no
// transmittable HID usage code.)
#define BT_HID_F5_USAGE (0x3e)
#define BT_HID_ESC_USAGE (0x29)    // Keyboard Escape usage, used to cancel dictation
#define BT_HID_ENTER_USAGE (0x28)  // Keyboard Return/Enter usage, used to send/submit
#define BT_HID_F15_TAP_MS (80)

esp_hidd_app_param_t *bt_hid_mouse_app_param(void);
esp_hidd_qos_param_t *bt_hid_mouse_qos_param(void);
void bt_hid_key_report_build(uint8_t report[BT_HID_KEY_REPORT_SIZE], uint8_t modifiers, uint8_t usage, bool pressed);

#ifdef __cplusplus
}
#endif

#endif  // BT_HID_MOUSE_H
