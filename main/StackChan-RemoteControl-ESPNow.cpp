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
#include "lvgl.h"

#include "ui.h"
#include "esp_now_init.h"
#include "joystick_handle.h"
#include "mic_handle.h"

#include "lvgl_port.h"

using namespace m5;

joystick_data_t joystick_data;

// extern void lvgl_port_init(M5GFX &gfx);

static void reset_shared_port_a_pins(TickType_t settle_ticks)
{
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_26);
    vTaskDelay(settle_ticks);
}

/**
 * @brief Handle Button Press.
 * 1. Press BtnA to switch setup/running/IMU/mic UI.
 * 2. Press BtnB to switch espnow-channel or id on setup_mode;
 * 3. Press BtnB to send btnB_status to remote on running_mode.
 */
static void enter_screen_mode(uint8_t next_mode)
{
    uint8_t current_mode = joystick_data.screen_mode;

    if (current_mode == MODE_MIC && next_mode != MODE_MIC) {
        joystick_data.screen_mode = next_mode;
        mic_mode_exit();
        reset_shared_port_a_pins(pdMS_TO_TICKS(50));
        joystick_reinit();

        if (next_mode == MODE_RUNNING) {
            wifi_espnow_reinit(joystick_data.channel);
        }
        switch_screen(joystick_data.screen_mode);
        return;
    }

    if (next_mode == MODE_MIC && current_mode != MODE_MIC) {
        joystick_data.screen_mode = MODE_MIC;
        joystick_deinit();
        reset_shared_port_a_pins(pdMS_TO_TICKS(20));
        mic_mode_enter();
        switch_screen(joystick_data.screen_mode);
        return;
    }

    if (next_mode == MODE_RUNNING) {
        wifi_espnow_reinit(joystick_data.channel);
    }
    joystick_data.screen_mode = next_mode;
    switch_screen(joystick_data.screen_mode);
}

void handle_button_press()
{
    static uint8_t screen_mode = MODE_SETUP;
    // check if BtnA is pressed
    if (M5.BtnA.wasPressed()) {
        // use BtnA to switch mode
        screen_mode = (screen_mode + 1) % 4;
        enter_screen_mode(screen_mode);
    }
    if (M5.BtnB.wasPressed()) {
        if (joystick_data.screen_mode == MODE_SETUP) {
            joystick_data.select_mode = !joystick_data.select_mode;
        } else if ((joystick_data.screen_mode == MODE_RUNNING) || (joystick_data.screen_mode == MODE_IMU)) {
            joystick_data.btnB_status = !joystick_data.btnB_status;
        } else if (joystick_data.screen_mode == MODE_MIC) {
            mic_mode_toggle_muted();
        }
    }
}

void app_main(void)
{
    imu_data_t imu_data;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    M5.begin();
    M5.Power.begin();
    M5.Power.Axp192.setLDO2(2800);  // set LDO2 to 2.8V for external devices
    M5.Imu.init(&M5.In_I2C);        // init IMU with internal I2C port
    printf("IN_I2C port: %d\n", M5.In_I2C.getPort());
    printf("M5 Display width: %ld, height: %ld\n", M5.Display.width(), M5.Display.height());

    joystick_data = joystick_init();  // init joystick

    lvgl_port_init();  // init LVGL
    ui_init();         // init UI

    // init WiFi and ESP-NOW
    wifi_espnow_init(joystick_data.channel);

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
