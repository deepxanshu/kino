/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DEVICE_MODE_H
#define DEVICE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *device_mode_name(uint8_t mode);
bool device_mode_needs_joystick(uint8_t mode);
bool device_mode_magic_mic_enabled(void);
bool device_mode_magic_joystick_enabled(void);
bool device_mode_peripheral_switching(void);
uint8_t device_mode_next_primary(uint8_t current_mode);
uint8_t device_mode_next_setup(uint8_t current_mode);
void device_mode_toggle_magic_function(void);
void device_mode_enter(uint8_t next_mode);

#ifdef __cplusplus
}
#endif

#endif  // DEVICE_MODE_H
