/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef JOYC_BUTTON_H
#define JOYC_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joyc_button_decode_pressed(uint8_t raw_button);

#ifdef __cplusplus
}
#endif

#endif  // JOYC_BUTTON_H
