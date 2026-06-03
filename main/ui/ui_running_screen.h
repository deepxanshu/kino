/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _UI_RUNNING_SCREEN_H_
#define _UI_RUNNING_SCREEN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../joystick/joystick_basic.h"

extern lv_obj_t* running_screen;
extern lv_obj_t* joystick_dot;
extern lv_obj_t* joystick_area;
extern lv_obj_t* battery_label;
extern lv_obj_t* mouse_info_label;
extern lv_obj_t* click_info_label;

void create_running_screen(void);
void update_running_screen(int16_t joyX, int16_t joyY, uint8_t bat, bool pressed, bool bt_connected);
void ui_running_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif  // _UI_RUNNING_SCREEN_H_
