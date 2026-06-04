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
#define MOUSE_SCROLL_AXIS_NONE       0
#define MOUSE_SCROLL_AXIS_VERTICAL   1
#define MOUSE_SCROLL_AXIS_HORIZONTAL 2
#define MOUSE_SCROLL_Q8_SCALE        256

static int16_t abs16(int16_t value)
{
    return value < 0 ? -value : value;
}

static int32_t abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static bool time_after_u32(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
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
    mouse->waiting_second_press = false;
    mouse->scroll_active = false;
    mouse->second_press_deadline_ms = 0;
    mouse->scroll_axis = MOUSE_SCROLL_AXIS_NONE;
    mouse->scroll_wheel_accum_q8 = 0;
    mouse->scroll_pan_accum_q8 = 0;
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

static int32_t mouse_controller_scroll_delta_q8(int16_t raw, int16_t center, int16_t min_value, int16_t max_value,
                                                bool invert)
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
    int32_t scaled_q8 = (int32_t)((int64_t)effective * effective * MOUSE_CONTROLLER_MAX_SCROLL_DELTA *
                                  MOUSE_SCROLL_Q8_SCALE / ((int64_t)usable * usable));
    int32_t max_q8 = MOUSE_CONTROLLER_MAX_SCROLL_DELTA * MOUSE_SCROLL_Q8_SCALE;
    if (scaled_q8 > max_q8) {
        scaled_q8 = max_q8;
    }

    int32_t out = delta > 0 ? scaled_q8 : -scaled_q8;
    return invert ? -out : out;
}

static int8_t mouse_controller_accumulate_scroll_q8(int32_t *accum_q8, int32_t delta_q8)
{
    if (delta_q8 == 0) {
        *accum_q8 = 0;
        return 0;
    }

    if ((*accum_q8 > 0 && delta_q8 < 0) || (*accum_q8 < 0 && delta_q8 > 0)) {
        *accum_q8 = 0;
    }

    *accum_q8 += delta_q8;
    int32_t whole = *accum_q8 / MOUSE_SCROLL_Q8_SCALE;
    if (whole > MOUSE_CONTROLLER_MAX_SCROLL_DELTA) {
        whole = MOUSE_CONTROLLER_MAX_SCROLL_DELTA;
    } else if (whole < -MOUSE_CONTROLLER_MAX_SCROLL_DELTA) {
        whole = -MOUSE_CONTROLLER_MAX_SCROLL_DELTA;
    }

    if (whole == 0) {
        return 0;
    }

    *accum_q8 -= whole * MOUSE_SCROLL_Q8_SCALE;
    return (int8_t)whole;
}

static void mouse_controller_update_scroll(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y,
                                           mouse_controller_report_t *report)
{
    int32_t wheel_q8 = mouse_controller_scroll_delta_q8(joy_y, mouse->center_y, Y_MIN, Y_MAX, false);
    int32_t pan_q8 = mouse_controller_scroll_delta_q8(joy_x, mouse->center_x, X_MIN, X_MAX, false);

    if (mouse->scroll_axis == MOUSE_SCROLL_AXIS_NONE) {
        if (wheel_q8 == 0 && pan_q8 == 0) {
            mouse->scroll_wheel_accum_q8 = 0;
            mouse->scroll_pan_accum_q8 = 0;
            return;
        }
        mouse->scroll_axis = abs32(wheel_q8) >= abs32(pan_q8) ?
                             MOUSE_SCROLL_AXIS_VERTICAL : MOUSE_SCROLL_AXIS_HORIZONTAL;
    }

    if (mouse->scroll_axis == MOUSE_SCROLL_AXIS_VERTICAL) {
        report->wheel = mouse_controller_accumulate_scroll_q8(&mouse->scroll_wheel_accum_q8, wheel_q8);
        report->pan = 0;
        mouse->scroll_pan_accum_q8 = 0;
        if (wheel_q8 == 0 && pan_q8 == 0) {
            mouse->scroll_axis = MOUSE_SCROLL_AXIS_NONE;
            mouse->scroll_wheel_accum_q8 = 0;
        }
        return;
    }

    report->wheel = 0;
    report->pan = mouse_controller_accumulate_scroll_q8(&mouse->scroll_pan_accum_q8, pan_q8);
    mouse->scroll_wheel_accum_q8 = 0;
    if (wheel_q8 == 0 && pan_q8 == 0) {
        mouse->scroll_axis = MOUSE_SCROLL_AXIS_NONE;
        mouse->scroll_pan_accum_q8 = 0;
    }
}

mouse_controller_report_t mouse_controller_update(mouse_controller_t *mouse, uint16_t joy_x, uint16_t joy_y,
                                                  bool pressed, bool last_pressed, uint32_t now_ms)
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

    bool rising = pressed && !last_pressed;
    bool falling = !pressed && last_pressed;
    if (mouse->waiting_second_press && time_after_u32(now_ms, mouse->second_press_deadline_ms)) {
        mouse->waiting_second_press = false;
    }

    if (mouse->scroll_active) {
        report.scroll_active = true;
        if (falling) {
            mouse->scroll_active = false;
            mouse->scroll_axis = MOUSE_SCROLL_AXIS_NONE;
            mouse->scroll_wheel_accum_q8 = 0;
            mouse->scroll_pan_accum_q8 = 0;
            report.scroll_active = false;
            report.scroll_exited = true;
            report.should_send = true;
            return report;
        }

        mouse_controller_update_scroll(mouse, joy_x, joy_y, &report);
        report.should_send = report.wheel != 0 || report.pan != 0;
        return report;
    }

    if (rising && mouse->waiting_second_press) {
        mouse->waiting_second_press = false;
        mouse->scroll_active = true;
        mouse->scroll_axis = MOUSE_SCROLL_AXIS_NONE;
        mouse->scroll_wheel_accum_q8 = 0;
        mouse->scroll_pan_accum_q8 = 0;
        report.scroll_active = true;
        report.scroll_entered = true;
        mouse_controller_update_scroll(mouse, joy_x, joy_y, &report);
        report.should_send = true;
        return report;
    }

    mouse_controller_track_center(mouse, joy_x, joy_y, pressed);

    report.dx = mouse_controller_axis_delta(joy_x, mouse->center_x, X_MIN, X_MAX, false);
    report.dy = mouse_controller_axis_delta(joy_y, mouse->center_y, Y_MIN, Y_MAX, true);
    report.buttons = pressed ? 0x01 : 0x00;
    report.pressed_changed = pressed != last_pressed;
    if (falling) {
        mouse->waiting_second_press = true;
        mouse->second_press_deadline_ms = now_ms + MOUSE_CONTROLLER_SCROLL_WINDOW_MS;
    }
    report.should_send = report.dx != 0 || report.dy != 0 || report.pressed_changed;
    return report;
}
