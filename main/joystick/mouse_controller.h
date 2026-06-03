/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MOUSE_CONTROLLER_H
#define MOUSE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "joystick_basic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOUSE_CONTROLLER_MAX_DELTA (48)

typedef struct {
    int16_t center_x;
    int16_t center_y;
} mouse_controller_t;

typedef struct {
    int16_t center_before_x;
    int16_t center_before_y;
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
    bool pressed_changed;
    bool should_send;
} mouse_controller_report_t;

void mouse_controller_reset(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y);
void mouse_controller_track_center(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y, bool pressed);
int8_t mouse_controller_axis_delta(int16_t raw, int16_t center, int16_t min_value, int16_t max_value, bool invert);
mouse_controller_report_t mouse_controller_update(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y,
                                                  bool pressed, bool last_pressed);

#ifdef __cplusplus
}
#endif

#endif  // MOUSE_CONTROLLER_H
