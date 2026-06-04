#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../main/button_action.h"
#include "../main/joystick/joystick_basic.h"

static void test_btna_single_waits_for_double_window(void)
{
    button_action_state_t state = {0};

    assert(button_action_update(&state, MODE_RUNNING, true, 1000) == BUTTON_ACTION_NONE);
    assert(state.btna_pending);
    assert(button_action_update(&state, MODE_RUNNING, false,
                                1000 + BUTTON_ACTION_BTNA_DOUBLE_CLICK_MS) == BUTTON_ACTION_NONE);

    assert(button_action_update(&state, MODE_RUNNING, false,
                                1001 + BUTTON_ACTION_BTNA_DOUBLE_CLICK_MS) ==
           BUTTON_ACTION_BTNA_SINGLE);
    assert(!state.btna_pending);
}

static void test_btna_double_wins_over_single(void)
{
    button_action_state_t state = {0};

    assert(button_action_update(&state, MODE_RUNNING, true, 1000) == BUTTON_ACTION_NONE);
    assert(button_action_update(&state, MODE_RUNNING, true, 1200) ==
           BUTTON_ACTION_BTNA_DOUBLE_TOGGLE_F15);
    assert(!state.btna_pending);

    assert(button_action_update(&state, MODE_RUNNING, false,
                                1001 + BUTTON_ACTION_BTNA_DOUBLE_CLICK_MS) == BUTTON_ACTION_NONE);
}

static void test_btna_setup_does_not_start_pending_action(void)
{
    button_action_state_t state = {0};

    assert(button_action_update(&state, MODE_SETUP, true, 1000) == BUTTON_ACTION_NONE);
    assert(!state.btna_pending);
    assert(button_action_update(&state, MODE_SETUP, true, 1200) == BUTTON_ACTION_NONE);
    assert(!state.btna_pending);
}

static void test_mode_change_clears_pending_btna_click(void)
{
    button_action_state_t state = {0};

    assert(button_action_update(&state, MODE_RUNNING, true, 1000) == BUTTON_ACTION_NONE);
    assert(state.btna_pending);
    assert(button_action_update(&state, MODE_SETUP, false, 1100) == BUTTON_ACTION_NONE);
    assert(!state.btna_pending);
}

int main(void)
{
    test_btna_single_waits_for_double_window();
    test_btna_double_wins_over_single();
    test_btna_setup_does_not_start_pending_action();
    test_mode_change_clears_pending_btna_click();
    return 0;
}
