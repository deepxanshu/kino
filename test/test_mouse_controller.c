#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../main/joystick/mouse_controller.h"

static void test_axis_delta_deadzone_and_caps(void)
{
    assert(mouse_controller_axis_delta(X_CENTER, X_CENTER, X_MIN, X_MAX, false) == 0);
    assert(mouse_controller_axis_delta(X_CENTER + 100, X_CENTER, X_MIN, X_MAX, false) == 0);
    assert(mouse_controller_axis_delta(X_MAX, X_CENTER, X_MIN, X_MAX, false) == MOUSE_CONTROLLER_MAX_DELTA);
    assert(mouse_controller_axis_delta(X_MIN, X_CENTER, X_MIN, X_MAX, false) == -MOUSE_CONTROLLER_MAX_DELTA);
    assert(mouse_controller_axis_delta(Y_MAX, Y_CENTER, Y_MIN, Y_MAX, true) < 0);
}

static void test_center_reset_and_tracking(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER + 160, Y_CENTER - 160);
    assert(mouse.center_x == X_CENTER + 160);
    assert(mouse.center_y == Y_CENTER - 160);

    mouse_controller_track_center(&mouse, mouse.center_x + 160, mouse.center_y - 160, false);
    assert(mouse.center_x == X_CENTER + 170);
    assert(mouse.center_y == Y_CENTER - 170);

    mouse_controller_track_center(&mouse, mouse.center_x + 160, mouse.center_y - 160, true);
    assert(mouse.center_x == X_CENTER + 170);
    assert(mouse.center_y == Y_CENTER - 170);

    mouse_controller_reset(&mouse, 0, 0);
    assert(mouse.center_x == X_CENTER);
    assert(mouse.center_y == Y_CENTER);
}

static void test_single_click_still_sends_left_button(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    mouse_controller_report_t down =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    assert(down.buttons == 0x01);
    assert(down.wheel == 0);
    assert(down.pan == 0);
    assert(!down.scroll_active);
    assert(down.should_send);

    mouse_controller_report_t up =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1100);
    assert(up.buttons == 0x00);
    assert(up.wheel == 0);
    assert(up.pan == 0);
    assert(!up.scroll_active);
    assert(up.should_send);
}

static void test_second_hold_enters_scroll_mode_without_left_button(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1080);

    mouse_controller_report_t scroll_start =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1200);
    assert(scroll_start.buttons == 0x00);
    assert(scroll_start.dx == 0);
    assert(scroll_start.dy == 0);
    assert(scroll_start.wheel == 0);
    assert(scroll_start.pan == 0);
    assert(scroll_start.scroll_active);
    assert(scroll_start.scroll_entered);
    assert(scroll_start.should_send);

    mouse_controller_report_t scroll_up =
        mouse_controller_update(&mouse, X_CENTER, Y_MAX, true, true, 1220);
    assert(scroll_up.buttons == 0x00);
    assert(scroll_up.dx == 0);
    assert(scroll_up.dy == 0);
    assert(scroll_up.wheel > 0);
    assert(scroll_up.pan == 0);
    assert(scroll_up.scroll_active);
    assert(scroll_up.should_send);

    mouse_controller_report_t scroll_end =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1300);
    assert(scroll_end.buttons == 0x00);
    assert(scroll_end.wheel == 0);
    assert(scroll_end.pan == 0);
    assert(!scroll_end.scroll_active);
    assert(scroll_end.scroll_exited);
    assert(scroll_end.should_send);
}

static void test_scroll_mode_timeout_keeps_second_press_as_click(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1080);

    mouse_controller_report_t second_down =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false,
                                1080 + MOUSE_CONTROLLER_SCROLL_WINDOW_MS + 1);
    assert(second_down.buttons == 0x01);
    assert(!second_down.scroll_active);
    assert(!second_down.scroll_entered);
    assert(second_down.should_send);
}

static void test_scroll_mode_uses_dominant_axis_for_pan(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1080);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1200);

    mouse_controller_report_t scroll_right =
        mouse_controller_update(&mouse, X_MAX, Y_CENTER, true, true, 1220);
    assert(scroll_right.buttons == 0x00);
    assert(scroll_right.dx == 0);
    assert(scroll_right.dy == 0);
    assert(scroll_right.wheel == 0);
    assert(scroll_right.pan > 0);
    assert(scroll_right.scroll_active);
    assert(scroll_right.should_send);
}

static void test_scroll_mode_uses_quadratic_curve_near_deadzone(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1080);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1200);

    mouse_controller_report_t moderate_scroll =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER + 1200, true, true, 1220);
    assert(moderate_scroll.wheel > 0);
    assert(moderate_scroll.wheel < MOUSE_CONTROLLER_MAX_SCROLL_DELTA);
    assert(moderate_scroll.pan == 0);

    mouse_controller_report_t max_scroll =
        mouse_controller_update(&mouse, X_CENTER, Y_MAX, true, true, 1240);
    assert(max_scroll.wheel == MOUSE_CONTROLLER_MAX_SCROLL_DELTA);
    assert(max_scroll.pan == 0);
}

static void test_scroll_mode_accumulates_sub_unit_scroll_near_deadzone(void)
{
    mouse_controller_t mouse;
    mouse_controller_reset(&mouse, X_CENTER, Y_CENTER);

    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1000);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, false, true, 1080);
    (void)mouse_controller_update(&mouse, X_CENTER, Y_CENTER, true, false, 1200);

    mouse_controller_report_t first_gentle_scroll =
        mouse_controller_update(&mouse, X_CENTER, Y_CENTER + 250, true, true, 1220);
    assert(first_gentle_scroll.wheel == 0);
    assert(first_gentle_scroll.pan == 0);
    assert(first_gentle_scroll.scroll_active);
    assert(!first_gentle_scroll.should_send);

    bool emitted_scroll = false;
    for (uint32_t i = 1; i <= 40; ++i) {
        mouse_controller_report_t gentle_scroll =
            mouse_controller_update(&mouse, X_CENTER, Y_CENTER + 250, true, true, 1220 + i * 20);
        assert(gentle_scroll.wheel >= 0);
        assert(gentle_scroll.wheel <= 1);
        assert(gentle_scroll.pan == 0);
        if (gentle_scroll.wheel == 1) {
            emitted_scroll = true;
            break;
        }
    }
    assert(emitted_scroll);
}

int main(void)
{
    test_axis_delta_deadzone_and_caps();
    test_center_reset_and_tracking();
    test_single_click_still_sends_left_button();
    test_second_hold_enters_scroll_mode_without_left_button();
    test_scroll_mode_timeout_keeps_second_press_as_click();
    test_scroll_mode_uses_dominant_axis_for_pan();
    test_scroll_mode_uses_quadratic_curve_near_deadzone();
    test_scroll_mode_accumulates_sub_unit_scroll_near_deadzone();
    return 0;
}
