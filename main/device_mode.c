/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "device_mode.h"

#include "app_state.h"
#include "bluetooth/bt_input.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "joystick/joystick_basic.h"
#include "joystick/joystick_handle.h"
#include "mic/mic_handle.h"
#include "ui/ui.h"

static const char *TAG = "mode_mgr";

static void log_porta_levels(const char *stage)
{
    ESP_LOGI(TAG, "%s: gpio0=%d gpio26=%d", stage, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
}

static void reset_shared_port_a_pins(TickType_t settle_ticks)
{
    ESP_LOGI(TAG, "reset PortA pins GPIO0/GPIO26 settle=%lu ms",
             (unsigned long)(settle_ticks * portTICK_PERIOD_MS));
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_26);
    vTaskDelay(settle_ticks);
    log_porta_levels("reset PortA pins done");
}

const char *device_mode_name(uint8_t mode)
{
    switch (mode) {
    case MODE_SETUP:
        return "Setup";
    case MODE_RUNNING:
        return "Mouse";
    case MODE_IMU:
        return "IMU";
    case MODE_MIC:
        return "Mic";
    case MODE_SWITCHING:
        return "Switching";
    default:
        return "Unknown";
    }
}

bool device_mode_needs_joystick(uint8_t mode)
{
    return mode == MODE_SETUP || mode == MODE_RUNNING;
}

uint8_t device_mode_next_primary(uint8_t current_mode)
{
    switch (current_mode) {
    case MODE_IMU:
        return MODE_RUNNING;
    case MODE_RUNNING:
        return MODE_MIC;
    case MODE_MIC:
        return MODE_IMU;
    default:
        return current_mode;
    }
}

uint8_t device_mode_next_setup(uint8_t current_mode)
{
    switch (current_mode) {
    case MODE_SETUP:
        return MODE_IMU;
    case MODE_SWITCHING:
        return current_mode;
    default:
        return MODE_SETUP;
    }
}

void device_mode_enter(uint8_t next_mode)
{
    uint8_t current_mode = app_state_get_mode();
    ESP_LOGI(TAG, "mode request: %s(%u) -> %s(%u) joy_ready=%d hid=%s hfp=%s audio=%s",
             device_mode_name(current_mode), current_mode, device_mode_name(next_mode), next_mode,
             joystick_is_ready(), bt_input_hid_status_text(), bt_input_hfp_status_text(),
             bt_input_audio_status_text());

    if (current_mode == next_mode) {
        return;
    }

    app_state_set_mode(MODE_SWITCHING);
    vTaskDelay(pdMS_TO_TICKS(20));

    if (current_mode == MODE_MIC && next_mode != MODE_MIC) {
        ESP_LOGI(TAG, "mode action: exit mic before entering %s", device_mode_name(next_mode));
        mic_mode_exit();
        reset_shared_port_a_pins(pdMS_TO_TICKS(120));

        switch_screen(next_mode);
        app_state_set_mode(next_mode);
        ESP_LOGI(TAG, "mode commit: %s(%u)", device_mode_name(app_state_get_mode()), app_state_get_mode());
        if (device_mode_needs_joystick(next_mode)) {
            joystick_recover(true);
            ESP_LOGI(TAG, "mode action: joystick recover done ready=%d", joystick_is_ready());
        }
        return;
    }

    if (next_mode == MODE_MIC && current_mode != MODE_MIC) {
        ESP_LOGI(TAG, "mode action: deinit joystick before mic ready=%d", joystick_is_ready());
        joystick_deinit();
        ESP_LOGI(TAG, "mode action: joystick deinit done ready=%d", joystick_is_ready());
        reset_shared_port_a_pins(pdMS_TO_TICKS(80));
        mic_mode_enter();
        switch_screen(MODE_MIC);
        app_state_set_mode(MODE_MIC);
        ESP_LOGI(TAG, "mode commit: %s(%u)", device_mode_name(app_state_get_mode()), app_state_get_mode());
        return;
    }

    switch_screen(next_mode);
    app_state_set_mode(next_mode);
    ESP_LOGI(TAG, "mode commit: %s(%u)", device_mode_name(app_state_get_mode()), app_state_get_mode());
    if (device_mode_needs_joystick(next_mode)) {
        if (!device_mode_needs_joystick(current_mode)) {
            ESP_LOGI(TAG, "mode action: enter joystick mode from %s, recover ready=%d",
                     device_mode_name(current_mode), joystick_is_ready());
            joystick_recover(true);
            ESP_LOGI(TAG, "mode action: joystick recover done ready=%d", joystick_is_ready());
        } else if (!joystick_is_ready()) {
            ESP_LOGI(TAG, "mode action: target needs joystick, reinit ready=%d", joystick_is_ready());
            joystick_reinit();
            ESP_LOGI(TAG, "mode action: joystick reinit done ready=%d", joystick_is_ready());
        }
    }
}
