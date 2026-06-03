/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "joystick/joystick_basic.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_state_init(const joystick_data_t *initial);
joystick_data_t app_state_snapshot(void);
uint8_t app_state_get_mode(void);
void app_state_set_mode(uint8_t mode);
void app_state_set_battery(int battery);
void app_state_set_imu(float accel_x, float accel_y, float accel_z);
void app_state_set_joystick(uint16_t joy_x, uint16_t joy_y, bool pressed);

#ifdef __cplusplus
}
#endif

#endif  // APP_STATE_H
