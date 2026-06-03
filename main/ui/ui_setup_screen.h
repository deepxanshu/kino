/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _UI_SETUP_SCREEN_H_
#define _UI_SETUP_SCREEN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../joystick/joystick_basic.h"

extern lv_obj_t *setup_screen;
extern lv_obj_t *setup_device_label;
extern lv_obj_t *setup_mouse_label;
extern lv_obj_t *setup_hfp_label;
extern lv_obj_t *setup_audio_label;
extern lv_obj_t *setup_mode_label;
extern lv_obj_t *setup_hint_label;

void create_setup_screen(void);
void update_setup_screen(joystick_data_t *data);
void ui_setup_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif  // _UI_SETUP_SCREEN_H_
