/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_MIC_SCREEN_H
#define UI_MIC_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "../mic/mic_spectrum.h"
#include "../joystick/joystick_basic.h"

#define MIC_BAR_MAX_HEIGHT (100)

void create_mic_screen(void);
bool ui_mic_screen_is_ready(void);
bool ui_mic_screen_load(bool animated);
void update_mic_screen(const mic_spectrum_data_t *spectrum, uint8_t bat, bool running, bool muted,
                       uint32_t sample_rate, bool hfp_connected, bool audio_connected);
void ui_mic_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif
