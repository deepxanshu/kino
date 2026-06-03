/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_IMU_SCREEN_H
#define UI_IMU_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float pitch;
    float roll;
} IMU_Angle_t;

void create_imu_screen(void);
bool ui_imu_screen_is_ready(void);
bool ui_imu_screen_load(bool animated);
IMU_Angle_t update_imu_screen(float ax, float ay, float az, uint8_t bat);
void ui_imu_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif
