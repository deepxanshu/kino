/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "M5Unified.h"

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "app_state.h"
#include "button_action.h"
#include "bt_input.h"
#include "device_mode.h"
#include "ui.h"
#include "joystick_handle.h"
#include "mic_handle.h"

#include "lvgl_port.h"

using namespace m5;

static const char *TAG = "app_diag";

static void log_porta_levels(const char *stage)
{
    ESP_LOGI(TAG, "%s: gpio0=%d gpio26=%d", stage, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
}

/**
 * @brief Handle Button Press.
 * 1. BtnA single tap = Enter (send). BtnA double tap = toggle voice dictation
 *    (device mic on/off + Ctrl+F5; joystick pauses while mic is on -- shared PortA).
 * 2. Click BtnPWR (side power button) = Escape (cancel dictation + mic off).
 * 3. Click BtnB to cycle screens Setup -> Magic -> Agents.
 * 4. Hold BtnB 3s to reopen pairing; hold BtnB 8s to clear bonds and reboot.
 */
static void handle_button_press(void)
{
    static bool wait_release = false;
    static bool btnb_pair_hold_done = false;
    static bool btnb_clear_hold_done = false;
    static TickType_t cooldown_until = 0;

    TickType_t now = xTaskGetTickCount();
    if (M5.BtnB.isPressed()) {
        if (!btnb_clear_hold_done && M5.BtnB.pressedFor(8000)) {
            btnb_clear_hold_done = true;
            ESP_LOGW(TAG, "BtnB hold 8s: clear BT bonds and restart");
            bt_input_clear_bonds();
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
            return;
        }
        if (!btnb_pair_hold_done && M5.BtnB.pressedFor(3000)) {
            btnb_pair_hold_done = true;
            ESP_LOGI(TAG, "BtnB hold 3s: enter pairing mode");
            bt_input_set_discoverable(true);
            return;
        }
    } else {
        btnb_pair_hold_done = false;
        btnb_clear_hold_done = false;
    }

    if (wait_release) {
        if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
            wait_release = false;
            cooldown_until = now + pdMS_TO_TICKS(300);
            ESP_LOGI(TAG, "button release: cooldown 300ms mode=%s", device_mode_name(app_state_get_mode()));
        }
        return;
    }
    if (now < cooldown_until) {
        return;
    }

    uint8_t current_mode = app_state_get_mode();
    // kino: BtnA single tap = Enter (send/submit -- the frequent post-dictation
    // action). BtnA double tap = toggle voice dictation (device mic + Ctrl+F5).
    // Voice on double-tap so a stray tap can't start dictation; Enter on single.
    if (M5.BtnA.wasSingleClicked()) {
        ESP_LOGI(TAG, "BtnA single: Enter mode=%s", device_mode_name(current_mode));
        if (current_mode == MODE_RUNNING) {
            bt_input_enter_tap();
        }
        return;
    }
    if (M5.BtnA.wasDoubleClicked()) {
        ESP_LOGI(TAG, "BtnA double: toggle voice mode=%s mic=%d hid=%s",
                 device_mode_name(current_mode), device_mode_magic_mic_enabled(),
                 bt_input_hid_status_text());
        if (current_mode == MODE_RUNNING && device_mode_toggle_magic_function()) {
            bt_input_dictation_tap();
        }
        return;
    }
    // kino: BtnPWR (side power button) = Escape only (cancel dictation + drop mic).
    if (M5.BtnPWR.wasClicked()) {
        ESP_LOGI(TAG, "BtnPWR: Escape mode=%s mic=%d",
                 device_mode_name(current_mode), device_mode_magic_mic_enabled());
        if (current_mode == MODE_RUNNING) {
            bt_input_escape_tap();
            device_mode_set_magic_mic_enabled(false);
        }
        return;
    }

    if (M5.BtnB.wasClicked()) {
        current_mode = app_state_get_mode();
        uint8_t screen_mode = device_mode_next_setup(current_mode);
        ESP_LOGI(TAG, "BtnB click: mode=%s target=%s A_pressed=%d B_pressed=%d gpio37=%d gpio39=%d",
                 device_mode_name(current_mode), device_mode_name(screen_mode), M5.BtnA.isPressed(),
                 M5.BtnB.isPressed(), gpio_get_level(GPIO_NUM_37), gpio_get_level(GPIO_NUM_39));
        if (screen_mode != current_mode) {
            device_mode_enter(screen_mode);
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
    joystick_data_t initial_state = joystick_init();  // init joystick
    app_state_init(&initial_state);
    log_porta_levels("after joystick_init");

    lvgl_port_init();  // init LVGL
    ui_init();         // init UI
    log_porta_levels("after ui init");

    bt_input_init();
    log_porta_levels("after bt_input_init");

    xTaskCreate(handle_setup_screen, "handle_setup_screen", 8192, NULL, 5, NULL);      // handle setup mode
    xTaskCreate(handle_running_screen, "handle_running_screen", 8192, NULL, 5, NULL);  // handle running mode
    xTaskCreate(handle_agents_screen, "handle_agents_screen", 8192, NULL, 5, NULL);    // kino: agents page
    xTaskCreate(handle_mic_screen, "handle_mic_screen", 8192, NULL, 5, NULL);

    while (1) {
        M5.update();
        // Handle button press
        handle_button_press();
        app_state_set_battery(M5.Power.Axp192.getBatteryLevel());  // update battery level

        M5.Imu.update();                              // update IMU data
        imu_data              = M5.Imu.getImuData();  // get IMU data
        app_state_set_imu(imu_data.accel.x, imu_data.accel.y, imu_data.accel.z);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}
