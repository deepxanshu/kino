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
#include "lvgl.h"
#include "../mic/mic_spectrum.h"

#define MODE_MIC (3)
#define MIC_BAR_MAX_HEIGHT (100)

extern lv_obj_t *mic_screen;
extern lv_obj_t *mic_spectrum_area;
extern lv_obj_t *mic_battery_label;
extern lv_obj_t *mic_status_label;
extern lv_obj_t *mic_level_label;
extern lv_obj_t *mic_bt_label;

void create_mic_screen(void);
void update_mic_screen(const mic_spectrum_data_t *spectrum, uint8_t bat, bool running, bool muted,
                       bool hfp_connected, bool audio_connected);
void ui_mic_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif
