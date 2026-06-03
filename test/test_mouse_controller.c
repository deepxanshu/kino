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

int main(void)
{
    test_axis_delta_deadzone_and_caps();
    test_center_reset_and_tracking();
    return 0;
}
