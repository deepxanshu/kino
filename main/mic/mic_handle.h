/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MIC_HANDLE_H
#define MIC_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void mic_mode_enter(void);
void mic_mode_exit(void);
void mic_mode_toggle_muted(void);
bool mic_mode_is_muted(void);
bool mic_mode_is_active(void);
void handle_mic_screen(void *pvParam);

#ifdef __cplusplus
}
#endif

#endif
