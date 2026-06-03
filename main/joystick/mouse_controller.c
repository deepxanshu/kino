/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mouse_controller.h"

#include <stddef.h>

#define MOUSE_DEAD_ZONE          150
#define MOUSE_CENTER_TRACK_ZONE  260
#define MOUSE_CENTER_TRACK_SHIFT 4

static int16_t abs16(int16_t value)
{
    return value < 0 ? -value : value;
}

void mouse_controller_reset(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y)
{
    if (mouse == NULL) {
        return;
    }

    if (joy_x >= X_MIN && joy_x <= X_MAX) {
        mouse->center_x = joy_x;
    } else {
        mouse->center_x = X_CENTER;
    }
    if (joy_y >= Y_MIN && joy_y <= Y_MAX) {
        mouse->center_y = joy_y;
    } else {
        mouse->center_y = Y_CENTER;
    }
}

void mouse_controller_track_center(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y, bool pressed)
{
    if (mouse == NULL || pressed) {
        return;
    }

    int16_t dx = (int16_t)joy_x - mouse->center_x;
    int16_t dy = (int16_t)joy_y - mouse->center_y;
    if (abs16(dx) <= MOUSE_CENTER_TRACK_ZONE && abs16(dy) <= MOUSE_CENTER_TRACK_ZONE) {
        mouse->center_x += dx / (1 << MOUSE_CENTER_TRACK_SHIFT);
        mouse->center_y += dy / (1 << MOUSE_CENTER_TRACK_SHIFT);
    }
}

int8_t mouse_controller_axis_delta(int16_t raw, int16_t center, int16_t min_value, int16_t max_value, bool invert)
{
    int16_t delta = raw - center;
    int16_t abs_delta = abs16(delta);
    if (abs_delta <= MOUSE_DEAD_ZONE) {
        return 0;
    }

    int16_t span = delta > 0 ? (max_value - center) : (center - min_value);
    if (span <= MOUSE_DEAD_ZONE) {
        span = MOUSE_DEAD_ZONE + 1;
    }

    int32_t effective = abs_delta - MOUSE_DEAD_ZONE;
    int32_t usable = span - MOUSE_DEAD_ZONE;
    int32_t scaled = 1 + (effective * 8 / usable) + (effective * effective * 40 / (usable * usable));
    if (scaled > MOUSE_CONTROLLER_MAX_DELTA) {
        scaled = MOUSE_CONTROLLER_MAX_DELTA;
    }

    int8_t out = delta > 0 ? (int8_t)scaled : (int8_t)-scaled;
    return invert ? -out : out;
}

mouse_controller_report_t mouse_controller_update(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y,
                                                  bool pressed, bool last_pressed)
{
    mouse_controller_report_t report = {0};
    if (mouse == NULL) {
        report.pressed_changed = pressed != last_pressed;
        report.buttons = pressed ? 0x01 : 0x00;
        report.should_send = report.pressed_changed;
        return report;
    }

    report.center_before_x = mouse->center_x;
    report.center_before_y = mouse->center_y;
    mouse_controller_track_center(mouse, joy_x, joy_y, pressed);

    report.dx = mouse_controller_axis_delta(joy_x, mouse->center_x, X_MIN, X_MAX, false);
    report.dy = mouse_controller_axis_delta(joy_y, mouse->center_y, Y_MIN, Y_MAX, true);
    report.buttons = pressed ? 0x01 : 0x00;
    report.pressed_changed = pressed != last_pressed;
    report.should_send = report.dx != 0 || report.dy != 0 || report.pressed_changed;
    return report;
}
