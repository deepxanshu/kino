/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _JOYSTICK_BASIC_H_
#define _JOYSTICK_BASIC_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODE_SETUP     (0)
#define MODE_RUNNING   (1)
#define MODE_IMU       (2)
#define MODE_MIC       (3)
#define MODE_SWITCHING (4)

#define JOYSTICK_DEAD_ZONE (300)
#define X_CENTER           (2180)
#define Y_CENTER           (1960)
#define X_MIN              (630)
#define X_MAX              (3730)
#define Y_MIN              (310)
#define Y_MAX              (3460)

typedef struct {
    int8_t bat;
    uint16_t joyX;
    uint16_t joyY;
    bool joy_pressed;
    uint8_t screen_mode;
    float accel_x;
    float accel_y;
    float accel_z;

} joystick_data_t;

#ifdef __cplusplus
}
#endif

#endif
