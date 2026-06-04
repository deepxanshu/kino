/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BUTTON_ACTION_H
#define BUTTON_ACTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_ACTION_BTNA_DOUBLE_CLICK_MS (450)

typedef enum {
    BUTTON_ACTION_NONE = 0,
    BUTTON_ACTION_BTNA_SINGLE,
    BUTTON_ACTION_BTNA_DOUBLE_TOGGLE_F15,
} button_action_event_t;

typedef struct {
    bool btna_pending;
    uint32_t btna_deadline_ms;
} button_action_state_t;

void button_action_reset(button_action_state_t *state);
button_action_event_t button_action_update(button_action_state_t *state, uint8_t mode, bool btna_clicked,
                                           uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif  // BUTTON_ACTION_H
