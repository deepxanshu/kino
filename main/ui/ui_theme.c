/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_theme.h"

#include <stddef.h>

lv_color_t ui_theme_bg_color(void)
{
    return lv_color_black();
}

lv_color_t ui_theme_fg_color(void)
{
    return lv_color_white();
}

lv_color_t ui_theme_grid_color(void)
{
    return lv_color_make(64, 64, 64);
}

void ui_theme_apply_screen(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(obj, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}

void ui_theme_apply_panel(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(obj, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, ui_theme_fg_color(), LV_PART_MAIN);
}

void ui_theme_apply_label(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(label, ui_theme_fg_color(), LV_PART_MAIN);
}
