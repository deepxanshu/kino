/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joyc_button.h"

bool joyc_button_decode_pressed(uint8_t raw_button)
{
    return raw_button == 0;
}
