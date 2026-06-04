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

#include <stdbool.h>
#include <stdint.h>
#include "../joystick/joystick_basic.h"
#include "../mic/mic_spectrum.h"

#define MAGIC_AUDIO_BAR_MAX_HEIGHT (40)

void create_running_screen(void);
bool ui_running_screen_is_ready(void);
bool ui_running_screen_load(bool animated);
void update_running_screen(int16_t joyX, int16_t joyY, uint8_t bat, bool pressed,
                           float accel_x, float accel_y, float accel_z,
                           const mic_spectrum_data_t *spectrum, bool mic_running,
                           bool joystick_enabled, bool scroll_active);
void ui_running_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif  // _UI_RUNNING_SCREEN_H_
