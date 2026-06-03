/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_state.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static joystick_data_t s_app_state = {
    .bat = 0,
    .joyX = X_CENTER,
    .joyY = Y_CENTER,
    .joy_pressed = false,
    .screen_mode = MODE_SETUP,
    .accel_x = 0.0f,
    .accel_y = 0.0f,
    .accel_z = 0.0f,
};

static portMUX_TYPE s_app_state_mux = portMUX_INITIALIZER_UNLOCKED;

void app_state_init(const joystick_data_t *initial)
{
    taskENTER_CRITICAL(&s_app_state_mux);
    if (initial != NULL) {
        s_app_state = *initial;
    }
    taskEXIT_CRITICAL(&s_app_state_mux);
}

joystick_data_t app_state_snapshot(void)
{
    joystick_data_t snapshot;
    taskENTER_CRITICAL(&s_app_state_mux);
    snapshot = s_app_state;
    taskEXIT_CRITICAL(&s_app_state_mux);
    return snapshot;
}

uint8_t app_state_get_mode(void)
{
    uint8_t mode;
    taskENTER_CRITICAL(&s_app_state_mux);
    mode = s_app_state.screen_mode;
    taskEXIT_CRITICAL(&s_app_state_mux);
    return mode;
}

void app_state_set_mode(uint8_t mode)
{
    taskENTER_CRITICAL(&s_app_state_mux);
    s_app_state.screen_mode = mode;
    taskEXIT_CRITICAL(&s_app_state_mux);
}

void app_state_set_battery(int battery)
{
    if (battery > 100) {
        battery = 100;
    } else if (battery < 0) {
        battery = 0;
    }

    taskENTER_CRITICAL(&s_app_state_mux);
    s_app_state.bat = (int8_t)battery;
    taskEXIT_CRITICAL(&s_app_state_mux);
}

void app_state_set_imu(float accel_x, float accel_y, float accel_z)
{
    taskENTER_CRITICAL(&s_app_state_mux);
    s_app_state.accel_x = accel_x;
    s_app_state.accel_y = accel_y;
    s_app_state.accel_z = accel_z;
    taskEXIT_CRITICAL(&s_app_state_mux);
}

void app_state_set_joystick(uint16_t joy_x, uint16_t joy_y, bool pressed)
{
    taskENTER_CRITICAL(&s_app_state_mux);
    s_app_state.joyX = joy_x;
    s_app_state.joyY = joy_y;
    s_app_state.joy_pressed = pressed;
    taskEXIT_CRITICAL(&s_app_state_mux);
}
