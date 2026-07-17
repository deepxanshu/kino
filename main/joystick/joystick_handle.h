/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __JOYSTICK_HANDLE_H__
#define __JOYSTICK_HANDLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "joystick_basic.h"

joystick_data_t joystick_init(void);
void joystick_reinit(void);
void joystick_recover(bool power_cycle);
void joystick_deinit(void);
bool joystick_is_ready(void);
bool joystick_read_state(uint16_t *joyX, uint16_t *joyY, bool *pressed);
void handle_setup_screen(void *pvParam);
void handle_running_screen(void *pvParam);
void handle_agents_screen(void *pvParam);
// Focus the currently-selected agent thread (send @SEL over serial) and jump to
// the Magic home page. Callable from the joystick press or a button.
void agents_select_current(void);

#ifdef __cplusplus
}
#endif

#endif
