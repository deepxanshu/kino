/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "button_action.h"

#include <stddef.h>

#include "joystick/joystick_basic.h"

static bool time_after_u32(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

void button_action_reset(button_action_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->btna_pending = false;
    state->btna_deadline_ms = 0;
}

button_action_event_t button_action_update(button_action_state_t *state, uint8_t mode, bool btna_clicked,
                                           uint32_t now_ms)
{
    if (state == NULL) {
        return BUTTON_ACTION_NONE;
    }

    if (mode != MODE_RUNNING) {
        button_action_reset(state);
        return BUTTON_ACTION_NONE;
    }

    if (btna_clicked) {
        if (state->btna_pending && !time_after_u32(now_ms, state->btna_deadline_ms)) {
            button_action_reset(state);
            return BUTTON_ACTION_BTNA_DOUBLE_TOGGLE_F15;
        }

        state->btna_pending = true;
        state->btna_deadline_ms = now_ms + BUTTON_ACTION_BTNA_DOUBLE_CLICK_MS;
        return BUTTON_ACTION_NONE;
    }

    if (state->btna_pending && time_after_u32(now_ms, state->btna_deadline_ms)) {
        button_action_reset(state);
        return BUTTON_ACTION_BTNA_SINGLE;
    }

    return BUTTON_ACTION_NONE;
}
