/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __JOYC_I2C_BRIDGE_H__
#define __JOYC_I2C_BRIDGE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joyc_i2c_begin(void);
bool joyc_i2c_recover(bool power_cycle);
void joyc_i2c_release(void);
bool joyc_i2c_is_ready(void);
bool joyc_i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
