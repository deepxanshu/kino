/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_setup_screen.h"
#include "../bluetooth/bt_input.h"
#include "../lvgl_port.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

LV_IMG_DECLARE(updown_img);

static lv_obj_t *setup_screen       = NULL;
static lv_obj_t *setup_device_label = NULL;
static lv_obj_t *setup_mouse_label  = NULL;
static lv_obj_t *setup_hfp_label    = NULL;
static lv_obj_t *setup_hfp_chan_label = NULL;
static lv_obj_t *setup_mode_label   = NULL;
static lv_obj_t *setup_pairing_label = NULL;

static lv_point_t s_setup_underline_points[2] = {{0, 0}, {117, 0}};

static lv_obj_t *create_status_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 117);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 9, y);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    ui_theme_apply_label(label);

    lv_obj_t *underline = lv_line_create(parent);
    lv_line_set_points(underline, s_setup_underline_points, 2);
    lv_obj_align(underline, LV_ALIGN_TOP_LEFT, 9, y + 18);
    lv_obj_set_style_line_color(underline, ui_theme_border_color(), LV_PART_MAIN);
    lv_obj_set_style_line_width(underline, 1, LV_PART_MAIN);

    return label;
}

static lv_obj_t *create_bottom_status_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 97, 22);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(box, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, ui_theme_border_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 91);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, ui_theme_accent_color(), LV_PART_MAIN);
    return label;
}

/**
 * @brief Create the setup screen UI with Bluetooth connection state.
 * @note This function creates a standalone screen with multiple UI elements:
 *       - Title label at the top
 *       - Bluetooth HID/HFP status labels
 *       - Battery level
 *       - Pairing status
 * @details The function sets up status labels and keeps all text inside the 135x240 screen.
 * @warning This function should only be called once per application run to avoid memory leaks
 */
void create_setup_screen(void)
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

    if (setup_screen == NULL) {
        setup_screen = lv_obj_create(NULL);
    }
    if (setup_screen == NULL) {
        ESP_LOGE("UI", "Failed to create setup screen!");
        lvgl_port_unlock();
        return;
    }

    lv_obj_clear_flag(setup_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_screen(setup_screen);

    lv_obj_t *label = lv_label_create(setup_screen);
    lv_label_set_text(label, "Status");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 11);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    ui_theme_apply_label(label);

    setup_device_label = create_status_label(setup_screen, 42, "Magic Stick");
    lv_obj_set_style_text_color(setup_device_label, ui_theme_accent_color(), LV_PART_MAIN);
    setup_mouse_label  = create_status_label(setup_screen, 67, "Mouse: INIT");
    setup_hfp_label    = create_status_label(setup_screen, 91, "HFP: INIT");
    setup_hfp_chan_label = create_status_label(setup_screen, 115, "HFP Chan: OFF");
    setup_mode_label   = create_status_label(setup_screen, 139, "Battery: 100%");
    setup_pairing_label = create_bottom_status_label(setup_screen, 196, "Discoverable");

    lvgl_port_unlock();
}

bool ui_setup_screen_is_ready(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bool ready = setup_screen != NULL && lv_obj_is_valid(setup_screen);
    lvgl_port_unlock();
    return ready;
}

bool ui_setup_screen_load(bool animated)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (setup_screen == NULL || !lv_obj_is_valid(setup_screen)) {
        lvgl_port_unlock();
        return false;
    }
    if (animated) {
        lv_scr_load_anim(setup_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    } else {
        lv_disp_load_scr(setup_screen);
    }
    lvgl_port_unlock();
    return true;
}

/**
 * @brief Update Bluetooth and battery status on the setup screen.
 * @param data Pointer to joystick data for the current battery level.
 */
void update_setup_screen(const joystick_data_t *data)
{
    if (data == NULL) {
        return;
    }

    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (setup_screen == NULL || !lv_obj_is_valid(setup_screen)) {
        lvgl_port_unlock();
        return;
    }
    if (setup_device_label != NULL) {
        lv_label_set_text(setup_device_label, BT_INPUT_DEVICE_NAME);
    }
    if (setup_mouse_label != NULL) {
        lv_label_set_text_fmt(setup_mouse_label, "Mouse: %s", bt_input_hid_status_text());
    }
    if (setup_hfp_label != NULL) {
        lv_label_set_text_fmt(setup_hfp_label, "HFP: %s", bt_input_hfp_status_text());
    }
    if (setup_hfp_chan_label != NULL) {
        lv_label_set_text_fmt(setup_hfp_chan_label, "HFP Chan: %s",
                              bt_input_hfp_audio_connected() ? "ON" : "OFF");
    }
    if (setup_mode_label != NULL) {
        lv_label_set_text_fmt(setup_mode_label, "Battery: %d%%", data->bat);
    }
    if (setup_pairing_label != NULL) {
        lv_label_set_text(setup_pairing_label, bt_input_pairing_status_text());
    }
    lvgl_port_unlock();
}

/**
 * @brief Destroy the setup screen and reset all UI object pointers to NULL
 * @note This function properly deletes the LVGL objects and resets internal pointers
 * @details
 *      1. Deletes the setup screen and all child objects using lv_obj_del
 *      2. Sets all UI object pointers to NULL to prevent dangling references
 */
void ui_setup_screen_destory(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (setup_screen != NULL) {
        lv_obj_del(setup_screen);
        setup_screen = NULL;
    }
    lvgl_port_unlock();

    setup_device_label = NULL;
    setup_mouse_label  = NULL;
    setup_hfp_label    = NULL;
    setup_hfp_chan_label = NULL;
    setup_mode_label   = NULL;
    setup_pairing_label = NULL;
}
