/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "M5Unified.h"

extern "C" {
#include <stdio.h>
#include "esp_log.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "lvgl.h"

#include "bt_input.h"
#include "ui.h"
#include "joystick_handle.h"
#include "mic_handle.h"

#include "lvgl_port.h"

using namespace m5;

joystick_data_t joystick_data;
static const char *TAG = "app_diag";

// extern void lvgl_port_init(M5GFX &gfx);

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

static const char *mode_name(uint8_t mode)
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
    default:
        return "Unknown";
    }
}

/**
 * @brief Handle Button Press.
 * 1. Press BtnA to switch setup/mouse/mic UI.
 * 2. Press BtnB to enter pairing state on setup mode.
 * 3. Press BtnB to mute/unmute mic on mic mode.
 */
static void enter_screen_mode(uint8_t next_mode)
{
    uint8_t current_mode = joystick_data.screen_mode;
    ESP_LOGI(TAG, "mode request: %s(%u) -> %s(%u) joy_ready=%d hid=%s hfp=%s audio=%s",
             mode_name(current_mode), current_mode, mode_name(next_mode), next_mode, joystick_is_ready(),
             bt_input_hid_status_text(), bt_input_hfp_status_text(), bt_input_audio_status_text());

    if (current_mode == MODE_MIC && next_mode != MODE_MIC) {
        joystick_data.screen_mode = next_mode;
        ESP_LOGI(TAG, "mode action: exit mic before entering %s", mode_name(next_mode));
        mic_mode_exit();
        reset_shared_port_a_pins(pdMS_TO_TICKS(50));
        joystick_reinit();
        ESP_LOGI(TAG, "mode action: joystick reinit done ready=%d", joystick_is_ready());

        switch_screen(joystick_data.screen_mode);
        return;
    }

    if (next_mode == MODE_MIC && current_mode != MODE_MIC) {
        joystick_data.screen_mode = MODE_MIC;
        ESP_LOGI(TAG, "mode action: deinit joystick before mic ready=%d", joystick_is_ready());
        joystick_deinit();
        ESP_LOGI(TAG, "mode action: joystick deinit done ready=%d", joystick_is_ready());
        reset_shared_port_a_pins(pdMS_TO_TICKS(20));
        mic_mode_enter();
        switch_screen(joystick_data.screen_mode);
        return;
    }

    joystick_data.screen_mode = next_mode;
    switch_screen(joystick_data.screen_mode);
}

void handle_button_press()
{
    static uint8_t screen_mode = MODE_SETUP;
    static bool wait_release = false;
    static TickType_t cooldown_until = 0;

    TickType_t now = xTaskGetTickCount();
    if (wait_release) {
        if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
            wait_release = false;
            cooldown_until = now + pdMS_TO_TICKS(300);
            ESP_LOGI(TAG, "button release: cooldown 300ms mode=%s", mode_name(joystick_data.screen_mode));
        }
        return;
    }
    if (now < cooldown_until) {
        return;
    }

    if (M5.BtnA.wasClicked()) {
        if (joystick_data.screen_mode == MODE_SETUP) {
            screen_mode = MODE_RUNNING;
        } else if (joystick_data.screen_mode == MODE_RUNNING) {
            screen_mode = MODE_MIC;
        } else {
            screen_mode = MODE_RUNNING;
        }
        ESP_LOGI(TAG, "BtnA click: mode=%s target=%s A_pressed=%d B_pressed=%d gpio37=%d gpio39=%d",
                 mode_name(joystick_data.screen_mode), mode_name(screen_mode), M5.BtnA.isPressed(),
                 M5.BtnB.isPressed(), gpio_get_level(GPIO_NUM_37), gpio_get_level(GPIO_NUM_39));
        enter_screen_mode(screen_mode);
        wait_release = true;
        return;
    }
    if (M5.BtnB.wasClicked()) {
        ESP_LOGI(TAG, "BtnB click: mode=%s A_pressed=%d B_pressed=%d gpio37=%d gpio39=%d",
                 mode_name(joystick_data.screen_mode), M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                 gpio_get_level(GPIO_NUM_37), gpio_get_level(GPIO_NUM_39));
        if (joystick_data.screen_mode == MODE_SETUP) {
            bt_input_set_discoverable(true);
        } else if (joystick_data.screen_mode == MODE_MIC) {
            mic_mode_toggle_muted();
        }
        wait_release = true;
    }
}

void app_main(void)
{
    imu_data_t imu_data;

    ESP_LOGI(TAG, "boot reset_reason=%d", esp_reset_reason());
    log_porta_levels("boot before nvs");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "nvs init ret=%s", esp_err_to_name(ret));
    log_porta_levels("after nvs");

    log_porta_levels("before M5.begin");
    M5.begin();
    log_porta_levels("after M5.begin");
    M5.Power.begin();
    log_porta_levels("after M5.Power.begin");
    M5.Power.Axp192.setLDO2(2800);  // set LDO2 to 2.8V for external devices
    log_porta_levels("after setLDO2 2800");
    M5.Imu.init(&M5.In_I2C);        // init IMU with internal I2C port
    log_porta_levels("after M5.Imu.init");
    ESP_LOGI(TAG, "M5 In_I2C port=%d display=%ldx%ld", M5.In_I2C.getPort(), M5.Display.width(),
             M5.Display.height());

    log_porta_levels("before joystick_init");
    joystick_data = joystick_init();  // init joystick
    log_porta_levels("after joystick_init");

    lvgl_port_init();  // init LVGL
    ui_init();         // init UI
    log_porta_levels("after ui init");

    bt_input_init();
    log_porta_levels("after bt_input_init");

    xTaskCreate(handle_setup_screen, "handle_setup_screen", 8192, &joystick_data, 5, NULL);      // handle setup mode
    xTaskCreate(handle_running_screen, "handle_running_screen", 8192, &joystick_data, 5, NULL);  // handle running mode
    xTaskCreate(handle_imu_screen, "handle_imu_screen", 8192, &joystick_data, 5, NULL);
    xTaskCreate(handle_mic_screen, "handle_mic_screen", 8192, &joystick_data, 5, NULL);

    while (1) {
        M5.update();
        // Handle button press
        handle_button_press();
        joystick_data.bat = (M5.Power.Axp192.getBatteryLevel());  // updata battery level

        joystick_data.bat = (joystick_data.bat > 100) ? 100 : joystick_data.bat;
        joystick_data.bat = (joystick_data.bat < 0) ? 0 : joystick_data.bat;

        M5.Imu.update();                              // update IMU data
        imu_data              = M5.Imu.getImuData();  // get IMU data
        joystick_data.accel_x = imu_data.accel.x;
        joystick_data.accel_y = imu_data.accel.y;
        joystick_data.accel_z = imu_data.accel.z;

#if 0
        printf("Accel: (%.2f, %.2f, %.2f), Gyro: (%.2f, %.2f, %.2f)\n",
               joystick_data.accel_x, joystick_data.accel_y, joystick_data.accel_z,
               joystick_data.gyro_x, joystick_data.gyro_y, joystick_data.gyro_z);
#endif
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
}
