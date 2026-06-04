/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_theme.h"

#include <stddef.h>

lv_color_t ui_theme_bg_color(void)
{
    return lv_color_make(3, 4, 3);
}

lv_color_t ui_theme_fg_color(void)
{
    return lv_color_make(242, 232, 198);
}

lv_color_t ui_theme_grid_color(void)
{
    return lv_color_make(81, 74, 51);
}

lv_color_t ui_theme_border_color(void)
{
    return lv_color_make(217, 164, 65);
}

lv_color_t ui_theme_accent_color(void)
{
    return lv_color_make(35, 212, 107);
}

lv_color_t ui_theme_cyan_color(void)
{
    return lv_color_make(56, 216, 221);
}

lv_color_t ui_theme_red_color(void)
{
    return lv_color_make(255, 74, 63);
}

lv_color_t ui_theme_yellow_color(void)
{
    return lv_color_make(255, 210, 31);
}

void ui_theme_apply_screen(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(obj, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

void ui_theme_apply_panel(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(obj, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, ui_theme_border_color(), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

void ui_theme_apply_label(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(label, ui_theme_fg_color(), LV_PART_MAIN);
}
