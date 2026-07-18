/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_agents_screen.h"
#include "../lvgl_port.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define AGENTS_VISIBLE_ROWS 7
#define AGENTS_ROW_H        24
#define AGENTS_ROW_TOP      34
#define AGENTS_DOT_SIZE     8

static lv_obj_t *agents_screen  = NULL;
static lv_obj_t *agents_title    = NULL;
static lv_obj_t *agents_count    = NULL;
static lv_obj_t *agents_empty    = NULL;
static lv_obj_t *agents_hint     = NULL;
static lv_obj_t *row_box[AGENTS_VISIBLE_ROWS];
static lv_obj_t *row_dot[AGENTS_VISIBLE_ROWS];
static lv_obj_t *row_name[AGENTS_VISIBLE_ROWS];
static lv_obj_t *row_status[AGENTS_VISIBLE_ROWS];

static lv_point_t s_agents_underline[2] = {{0, 0}, {117, 0}};

static lv_color_t status_color(agent_status_t s)
{
    switch (s) {
    case AGENT_STATUS_RUNNING:
        return ui_theme_accent_color();
    case AGENT_STATUS_ATTENTION:
        return ui_theme_yellow_color();
    case AGENT_STATUS_ERROR:
        return ui_theme_red_color();
    default:
        return ui_theme_grid_color();
    }
}

static const char *status_text(agent_status_t s)
{
    switch (s) {
    case AGENT_STATUS_RUNNING:
        return "run";
    case AGENT_STATUS_ATTENTION:
        return "wait";
    case AGENT_STATUS_ERROR:
        return "err";
    default:
        return "idle";
    }
}

void create_agents_screen(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL) {
        ESP_LOGE("UI", "No default display found!");
        lvgl_port_unlock();
        return;
    }

    if (agents_screen == NULL) {
        agents_screen = lv_obj_create(NULL);
    }
    if (agents_screen == NULL) {
        ESP_LOGE("UI", "Failed to create agents screen!");
        lvgl_port_unlock();
        return;
    }

    lv_obj_clear_flag(agents_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_screen(agents_screen);

    agents_title = lv_label_create(agents_screen);
    lv_label_set_text(agents_title, "AGENTS");
    lv_obj_align(agents_title, LV_ALIGN_TOP_LEFT, 9, 9);
    lv_obj_set_style_text_font(agents_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agents_title, ui_theme_cyan_color(), LV_PART_MAIN);

    agents_count = lv_label_create(agents_screen);
    lv_label_set_text(agents_count, "0");
    lv_obj_align(agents_count, LV_ALIGN_TOP_RIGHT, -10, 9);
    lv_obj_set_style_text_font(agents_count, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agents_count, ui_theme_grid_color(), LV_PART_MAIN);

    lv_obj_t *underline = lv_line_create(agents_screen);
    lv_line_set_points(underline, s_agents_underline, 2);
    lv_obj_align(underline, LV_ALIGN_TOP_LEFT, 9, 30);
    lv_obj_set_style_line_color(underline, ui_theme_border_color(), LV_PART_MAIN);
    lv_obj_set_style_line_width(underline, 1, LV_PART_MAIN);

    for (int i = 0; i < AGENTS_VISIBLE_ROWS; ++i) {
        row_box[i] = lv_obj_create(agents_screen);
        lv_obj_set_size(row_box[i], 121, AGENTS_ROW_H);
        lv_obj_align(row_box[i], LV_ALIGN_TOP_LEFT, 7, AGENTS_ROW_TOP + i * AGENTS_ROW_H);
        lv_obj_set_style_bg_opa(row_box[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row_box[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row_box[i], 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row_box[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(row_box[i], LV_OBJ_FLAG_SCROLLABLE);

        row_dot[i] = lv_obj_create(row_box[i]);
        lv_obj_set_size(row_dot[i], AGENTS_DOT_SIZE, AGENTS_DOT_SIZE);
        lv_obj_align(row_dot[i], LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_set_style_radius(row_dot[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(row_dot[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row_dot[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(row_dot[i], LV_OBJ_FLAG_SCROLLABLE);

        row_name[i] = lv_label_create(row_box[i]);
        lv_label_set_long_mode(row_name[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row_name[i], 72);
        lv_obj_align(row_name[i], LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_set_style_text_font(row_name[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(row_name[i], ui_theme_fg_color(), LV_PART_MAIN);
        lv_label_set_text(row_name[i], "");

        row_status[i] = lv_label_create(row_box[i]);
        lv_label_set_long_mode(row_status[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row_status[i], 34);
        lv_obj_align(row_status[i], LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_set_style_text_align(row_status[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(row_status[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(row_status[i], ui_theme_grid_color(), LV_PART_MAIN);
        lv_label_set_text(row_status[i], "");

        lv_obj_add_flag(row_box[i], LV_OBJ_FLAG_HIDDEN);
    }

    agents_empty = lv_label_create(agents_screen);
    lv_label_set_text(agents_empty, "waiting for feed");
    lv_obj_align(agents_empty, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(agents_empty, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agents_empty, ui_theme_grid_color(), LV_PART_MAIN);
    lv_obj_add_flag(agents_empty, LV_OBJ_FLAG_HIDDEN);

    agents_hint = lv_label_create(agents_screen);
    lv_label_set_text(agents_hint, "move / focus");
    lv_obj_align(agents_hint, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_font(agents_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(agents_hint, ui_theme_grid_color(), LV_PART_MAIN);

    lvgl_port_unlock();
}

bool ui_agents_screen_is_ready(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bool ready = agents_screen != NULL && lv_obj_is_valid(agents_screen);
    lvgl_port_unlock();
    return ready;
}

bool ui_agents_screen_load(bool animated)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (agents_screen == NULL || !lv_obj_is_valid(agents_screen)) {
        lvgl_port_unlock();
        return false;
    }
    if (animated) {
        lv_scr_load_anim(agents_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    } else {
        lv_disp_load_scr(agents_screen);
    }
    lvgl_port_unlock();
    return true;
}

void update_agents_screen(const agent_session_t *sessions, size_t count, int selected)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (agents_screen == NULL || !lv_obj_is_valid(agents_screen)) {
        lvgl_port_unlock();
        return;
    }

    size_t attention = 0;
    for (size_t i = 0; i < count && sessions != NULL; ++i) {
        if (sessions[i].status == AGENT_STATUS_ATTENTION || sessions[i].status == AGENT_STATUS_ERROR) {
            attention++;
        }
    }
    if (agents_count != NULL) {
        lv_label_set_text_fmt(agents_count, "%u", (unsigned)attention);
        lv_obj_set_style_text_color(agents_count,
                                    attention > 0 ? ui_theme_yellow_color() : ui_theme_grid_color(),
                                    LV_PART_MAIN);
    }
    if (agents_empty != NULL) {
        if (count == 0) {
            lv_obj_clear_flag(agents_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(agents_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (int i = 0; i < AGENTS_VISIBLE_ROWS; ++i) {
        if (row_box[i] == NULL) {
            continue;
        }
        if (sessions == NULL || (size_t)i >= count) {
            lv_obj_add_flag(row_box[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(row_box[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(row_dot[i], status_color(sessions[i].status), LV_PART_MAIN);
        lv_label_set_text(row_name[i], sessions[i].name);
        lv_label_set_text(row_status[i], status_text(sessions[i].status));
        lv_obj_set_style_text_color(row_status[i], status_color(sessions[i].status), LV_PART_MAIN);

        bool sel = (i == selected);
        lv_obj_set_style_border_width(row_box[i], sel ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(row_box[i], ui_theme_cyan_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row_box[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row_box[i], ui_theme_border_color(), LV_PART_MAIN);
        lv_obj_set_style_text_color(row_name[i],
                                    sel ? ui_theme_cyan_color() : ui_theme_fg_color(), LV_PART_MAIN);
    }

    lvgl_port_unlock();
}

void ui_agents_screen_destory(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (agents_screen != NULL) {
        lv_obj_del(agents_screen);
        agents_screen = NULL;
    }
    lvgl_port_unlock();

    agents_title = NULL;
    agents_count = NULL;
    agents_empty = NULL;
    agents_hint = NULL;
    for (int i = 0; i < AGENTS_VISIBLE_ROWS; ++i) {
        row_box[i] = NULL;
        row_dot[i] = NULL;
        row_name[i] = NULL;
        row_status[i] = NULL;
    }
}
