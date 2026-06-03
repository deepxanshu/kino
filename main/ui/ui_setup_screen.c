/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_setup_screen.h"
#include "../bluetooth/bt_input.h"
#include "../lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

LV_IMG_DECLARE(updown_img);

lv_obj_t *setup_screen       = NULL;
lv_obj_t *setup_device_label = NULL;
lv_obj_t *setup_mouse_label  = NULL;
lv_obj_t *setup_hfp_label    = NULL;
lv_obj_t *setup_audio_label  = NULL;
lv_obj_t *setup_mode_label   = NULL;
lv_obj_t *setup_hint_label   = NULL;

static lv_obj_t *create_status_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 125);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, y);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

/**
 * @brief Create the setup screen UI with Bluetooth connection state.
 * @note This function creates a standalone screen with multiple UI elements:
 *       - Title label at the top
 *       - Channel selection dropdown with options 1-14
 *       - ID selection dropdown with options 0-50
 *       - Start button at the bottom for transitioning to running mode
 * @details The function sets up dropdown controls with initial selections and
 *          applies specific styling including background colors and transparency
 * @warning This function should only be called once per application run to avoid memory leaks
 */
void create_setup_screen()
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

    lv_obj_clear_flag(setup_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(setup_screen);
    lv_label_set_text(label, "StackChan :)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    setup_device_label = create_status_label(setup_screen, 40, "StickC JoyMic");
    setup_mouse_label  = create_status_label(setup_screen, 68, "Mouse: INIT");
    setup_hfp_label    = create_status_label(setup_screen, 96, "HFP: INIT");
    setup_audio_label  = create_status_label(setup_screen, 124, "Audio: OFF");
    setup_mode_label   = create_status_label(setup_screen, 152, "Bat: 100%");
    setup_hint_label   = create_status_label(setup_screen, 194, "A:Mode B:Pair");

    lvgl_port_unlock();
}

/**
 * @brief Update the setup screen UI based on joystick input
 * @param data Pointer to joystick_data_t structure containing current joystick values and selection mode
 * @note This function handles joystick input to navigate and modify settings:
 *       - Highlights the currently selected dropdown (Channel or ID)
 *       - Increases/decreases values using joystick Y-axis movement
 *       - Updates the internal data structure with selected values
 * @details
 *      1. Changes background color of dropdowns to indicate selection
 *      2. Processes joystick Y-axis input for value modification
 *      3. Updates dropdown selections and corresponding data values
 *      4. Applies debouncing delay to prevent rapid value changes
 */
void update_setup_screen(joystick_data_t *data)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (setup_device_label != NULL) {
        lv_label_set_text_fmt(setup_device_label, "Name: %s", BT_INPUT_DEVICE_NAME);
    }
    if (setup_mouse_label != NULL) {
        lv_label_set_text_fmt(setup_mouse_label, "Mouse: %s", bt_input_hid_status_text());
    }
    if (setup_hfp_label != NULL) {
        lv_label_set_text_fmt(setup_hfp_label, "HFP: %s", bt_input_hfp_status_text());
    }
    if (setup_audio_label != NULL) {
        lv_label_set_text_fmt(setup_audio_label, "Audio: %s", bt_input_audio_status_text());
    }
    if (setup_mode_label != NULL) {
        lv_label_set_text_fmt(setup_mode_label, "Bat: %d%%", data->bat);
    }
    if (setup_hint_label != NULL) {
        lv_label_set_text(setup_hint_label, bt_input_is_discoverable() ? "Pairing..." : "A:Mode B:Pair");
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
void ui_setup_screen_destory()
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
    setup_audio_label  = NULL;
    setup_mode_label   = NULL;
    setup_hint_label   = NULL;
}
