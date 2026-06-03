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

#include <stdint.h>
#include "lvgl.h"

typedef struct {
    float pitch;
    float roll;
} IMU_Angle_t;

extern lv_obj_t *imu_screen;
extern lv_obj_t *imu_battery_label;
extern lv_obj_t *imu_pitch_label;
extern lv_obj_t *imu_roll_label;

void create_imu_screen(void);
IMU_Angle_t update_imu_screen(float ax, float ay, float az, uint8_t bat);
void ui_imu_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif
