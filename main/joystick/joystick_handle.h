/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __JOYSTICK_HANDLE_H__
#define __JOYSTICK_HANDLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "hal/i2c_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../ui/ui_setup_screen.h"
#include "../ui/ui_running_screen.h"
#include "../ui/ui_imu_screen.h"
#include "joystick_basic.h"

joystick_data_t joystick_init();
void joystick_reinit(void);
void joystick_deinit(void);
bool joystick_is_ready(void);
bool joystick_read_state(uint16_t *joyX, uint16_t *joyY, bool *pressed);
void handle_setup_screen(void *pvParam);
void handle_running_screen(void *pvParam);
void handle_imu_screen(void *pvParam);

#ifdef __cplusplus
}
#endif

#endif
