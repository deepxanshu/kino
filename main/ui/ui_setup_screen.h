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

#include <stdbool.h>
#include "../joystick/joystick_basic.h"

void create_setup_screen(void);
bool ui_setup_screen_is_ready(void);
bool ui_setup_screen_load(bool animated);
void update_setup_screen(const joystick_data_t *data);
void ui_setup_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif  // _UI_SETUP_SCREEN_H_
