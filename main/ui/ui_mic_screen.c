/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_mic_screen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "../lvgl_port.h"

#define MIC_BAR_COUNT MIC_SPECTRUM_BANDS
#define MIC_AREA_SIZE 115
#define MIC_BAR_WIDTH 4
#define MIC_BAR_GAP 2
#define MIC_BAR_BASE_Y 108

lv_obj_t *mic_screen        = NULL;
lv_obj_t *mic_spectrum_area = NULL;
lv_obj_t *mic_battery_label = NULL;
lv_obj_t *mic_status_label  = NULL;
lv_obj_t *mic_level_label   = NULL;
lv_obj_t *mic_bt_label      = NULL;

static lv_obj_t *s_mic_bars[MIC_BAR_COUNT];
static uint8_t s_last_bars[MIC_BAR_COUNT];
static lv_point_t s_grid_points[2][2] = {
    {{0, 36}, {MIC_AREA_SIZE, 36}},
    {{0, 72}, {MIC_AREA_SIZE, 72}},
};

static void create_grid_line(lv_obj_t *parent, int index)
{
    lv_obj_t *line           = lv_line_create(parent);
    lv_line_set_points(line, s_grid_points[index], 2);
    lv_obj_set_style_line_color(line, lv_color_make(210, 210, 210), 0);
    lv_obj_set_style_line_width(line, 1, 0);
}

void create_mic_screen(void)
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

    if (mic_screen == NULL) {
        mic_screen = lv_obj_create(NULL);
    }

    lv_obj_clear_flag(mic_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(mic_screen);
    lv_label_set_text(label, "StackChan :)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    mic_spectrum_area = lv_obj_create(mic_screen);
    lv_obj_set_size(mic_spectrum_area, MIC_AREA_SIZE, MIC_AREA_SIZE);
    lv_obj_align(mic_spectrum_area, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_border_width(mic_spectrum_area, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(mic_spectrum_area, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(mic_spectrum_area, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(mic_spectrum_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mic_spectrum_area, LV_OBJ_FLAG_SCROLLABLE);

    create_grid_line(mic_spectrum_area, 0);
    create_grid_line(mic_spectrum_area, 1);

    int16_t total_width = MIC_BAR_COUNT * MIC_BAR_WIDTH + (MIC_BAR_COUNT - 1) * MIC_BAR_GAP;
    int16_t start_x     = (MIC_AREA_SIZE - total_width) / 2;
    for (int i = 0; i < MIC_BAR_COUNT; i++) {
        s_mic_bars[i] = lv_obj_create(mic_spectrum_area);
        lv_obj_set_size(s_mic_bars[i], MIC_BAR_WIDTH, 1);
        lv_obj_set_style_bg_color(s_mic_bars[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_mic_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_mic_bars[i], 0, LV_PART_MAIN);
        lv_obj_align(s_mic_bars[i], LV_ALIGN_TOP_LEFT, start_x + i * (MIC_BAR_WIDTH + MIC_BAR_GAP), MIC_BAR_BASE_Y);
        s_last_bars[i] = 0;
    }

    mic_battery_label = lv_label_create(mic_screen);
    lv_label_set_text(mic_battery_label, "Bat: 100%");
    lv_obj_align(mic_battery_label, LV_ALIGN_TOP_LEFT, 10, 160);
    lv_obj_set_style_text_font(mic_battery_label, &lv_font_montserrat_14, 0);

    mic_status_label = lv_label_create(mic_screen);
    lv_label_set_text(mic_status_label, "Mic: OFF");
    lv_obj_align(mic_status_label, LV_ALIGN_TOP_LEFT, 10, 180);
    lv_obj_set_style_text_font(mic_status_label, &lv_font_montserrat_14, 0);

    mic_level_label = lv_label_create(mic_screen);
    lv_label_set_text(mic_level_label, "Level: -90 dB");
    lv_obj_align(mic_level_label, LV_ALIGN_TOP_LEFT, 10, 200);
    lv_obj_set_style_text_font(mic_level_label, &lv_font_montserrat_14, 0);

    mic_bt_label = lv_label_create(mic_screen);
    lv_label_set_text(mic_bt_label, "HFP:WAIT A:OFF");
    lv_obj_align(mic_bt_label, LV_ALIGN_TOP_LEFT, 10, 220);
    lv_obj_set_style_text_font(mic_bt_label, &lv_font_montserrat_14, 0);

    lvgl_port_unlock();
}

void update_mic_screen(const mic_spectrum_data_t *spectrum, uint8_t bat, bool running, bool muted,
                       bool hfp_connected, bool audio_connected)
{
    if (spectrum == NULL) {
        return;
    }

    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (int i = 0; i < MIC_BAR_COUNT; i++) {
        uint8_t target = spectrum->bars[i] > MIC_BAR_MAX_HEIGHT ? MIC_BAR_MAX_HEIGHT : spectrum->bars[i];
        uint8_t height = (uint8_t)((s_last_bars[i] * 3 + target) / 4);
        if (target > s_last_bars[i]) {
            height = target;
        }
        s_last_bars[i] = height;

        if (s_mic_bars[i] != NULL) {
            uint8_t draw_height = height == 0 ? 1 : height;
            lv_obj_set_size(s_mic_bars[i], MIC_BAR_WIDTH, draw_height);
            lv_obj_align(s_mic_bars[i], LV_ALIGN_TOP_LEFT,
                         (MIC_AREA_SIZE - (MIC_BAR_COUNT * MIC_BAR_WIDTH + (MIC_BAR_COUNT - 1) * MIC_BAR_GAP)) / 2 +
                             i * (MIC_BAR_WIDTH + MIC_BAR_GAP),
                         MIC_BAR_BASE_Y - draw_height);
        }
    }

    if (mic_battery_label != NULL) {
        lv_label_set_text_fmt(mic_battery_label, "Bat: %d%%", bat);
    }
    if (mic_status_label != NULL) {
        if (muted) {
            lv_label_set_text(mic_status_label, "Mic: PAUSE");
        } else {
            lv_label_set_text(mic_status_label, running ? "Mic: ON 8k" : "Mic: OFF");
        }
    }
    if (mic_level_label != NULL) {
        lv_label_set_text_fmt(mic_level_label, "Level: %d dB", spectrum->db);
    }
    if (mic_bt_label != NULL) {
        lv_label_set_text_fmt(mic_bt_label, "HFP:%s A:%s", hfp_connected ? "SLC" : "WAIT",
                              audio_connected ? "ON" : "OFF");
    }

    lvgl_port_unlock();
}

void ui_mic_screen_destory(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (mic_screen != NULL) {
        lv_obj_del(mic_screen);
        mic_screen = NULL;
    }

    lvgl_port_unlock();

    mic_spectrum_area = NULL;
    mic_battery_label = NULL;
    mic_status_label  = NULL;
    mic_level_label   = NULL;
    mic_bt_label      = NULL;
    for (int i = 0; i < MIC_BAR_COUNT; i++) {
        s_mic_bars[i] = NULL;
        s_last_bars[i] = 0;
    }
}
