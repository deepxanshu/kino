/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_color_t ui_theme_bg_color(void);
lv_color_t ui_theme_fg_color(void);
lv_color_t ui_theme_grid_color(void);
lv_color_t ui_theme_border_color(void);
lv_color_t ui_theme_accent_color(void);
lv_color_t ui_theme_cyan_color(void);
lv_color_t ui_theme_red_color(void);
lv_color_t ui_theme_yellow_color(void);
void ui_theme_apply_screen(lv_obj_t *obj);
void ui_theme_apply_panel(lv_obj_t *obj);
void ui_theme_apply_label(lv_obj_t *label);

#ifdef __cplusplus
}
#endif

#endif  // UI_THEME_H
