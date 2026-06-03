/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __JOYC_SOFT_I2C_H__
#define __JOYC_SOFT_I2C_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joyc_soft_i2c_begin(void);
void joyc_soft_i2c_release(void);
bool joyc_soft_i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
