/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joystick_handle.h"
#include "../app_state.h"
#include "../bluetooth/bt_input.h"
#include "../ui/ui_setup_screen.h"
#include "../ui/ui_running_screen.h"
#include "../ui/ui_imu_screen.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "joyc_button.h"
#include "joyc_i2c_bridge.h"
#include "mouse_controller.h"

static SemaphoreHandle_t s_joystick_i2c_lock;

#define MOUSE_POLL_MS 12
#define MOUSE_UI_REFRESH_MS 60
#define JOY_SETUP_LOG_MS 1000
#define JOY_MOUSE_LOG_MS 250
#define JOY_READ_FAIL_LOG_MS 1000

static const char *TAG = "joy_diag";

static TickType_t s_last_read_fail_log = 0;
static uint8_t s_last_button_raw = 0xff;
static bool s_button_raw_seen = false;

static void log_joy_pins(const char *stage)
{
    ESP_LOGI(TAG, "%s: gpio0=%d gpio26=%d", stage, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
}

/**
 * @brief Initialize joystick via I2C interface
 * @note This is an internal static function that configures I2C_NUM_0 as master with SDA on GPIO0 and SCL on GPIO26
 * @details
 *      1. Configures M5.Ex_I2C on I2C_NUM_0 with SDA GPIO0 and SCL GPIO26
 *      2. Probes the JoyC joystick at I2C address 0x54
 * @warning This function assumes the joystick device is at I2C address 0x54
 */
static void joystick_i2c_init(void)
{
    if (s_joystick_i2c_lock == NULL) {
        s_joystick_i2c_lock = xSemaphoreCreateMutex();
    }

    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreTake(s_joystick_i2c_lock, portMAX_DELAY);
    }

    if (joyc_i2c_is_ready()) {
        if (s_joystick_i2c_lock != NULL) {
            xSemaphoreGive(s_joystick_i2c_lock);
        }
        return;
    }

    ESP_LOGI(TAG, "i2c init: begin via M5.Ex_I2C gpio0=%d gpio26=%d", gpio_get_level(GPIO_NUM_0),
             gpio_get_level(GPIO_NUM_26));
    if (!joyc_i2c_begin()) {
        ESP_LOGE(TAG, "JoyC 0x54 probe failed via M5.Ex_I2C");
        if (s_joystick_i2c_lock != NULL) {
            xSemaphoreGive(s_joystick_i2c_lock);
        }
        return;
    }

    ESP_LOGI(TAG, "JoyC ready via M5.Ex_I2C gpio0=%d gpio26=%d", gpio_get_level(GPIO_NUM_0),
             gpio_get_level(GPIO_NUM_26));

    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreGive(s_joystick_i2c_lock);
    }
}

void joystick_reinit(void)
{
    log_joy_pins("joystick reinit begin");
    joystick_i2c_init();
    log_joy_pins("joystick reinit end");
}

void joystick_deinit(void)
{
    log_joy_pins("joystick deinit begin");
    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreTake(s_joystick_i2c_lock, portMAX_DELAY);
    }

    if (joyc_i2c_is_ready()) {
        joyc_i2c_release();
    }

    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreGive(s_joystick_i2c_lock);
    }
    log_joy_pins("joystick deinit end");
}

bool joystick_is_ready(void)
{
    return joyc_i2c_is_ready();
}

/**
 * @brief Read X and Y axis values from the joystick via I2C
 * @param joyX Pointer to store X-axis value (16-bit unsigned integer)
 * @param joyY Pointer to store Y-axis value (16-bit unsigned integer)
 * @return void
 * @details
 *      1. Reads 2 bytes from register address 0x00 (X-axis low/high bytes)
 *      2. Waits 10ms to ensure data stability
 *      3. Reads 2 bytes from register address 0x02 (Y-axis low/high bytes)
 *      4. Combines high and low bytes for both X and Y axes using bit shifting
 *      5. Stores the combined values in the provided pointers
 * @warning This function assumes the joystick provides 16-bit data in little-endian format
 */
bool joystick_read_state(uint16_t *joyX, uint16_t *joyY, bool *pressed)
{
    TickType_t now = xTaskGetTickCount();
    if (s_joystick_i2c_lock != NULL &&
        xSemaphoreTake(s_joystick_i2c_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        *joyX = X_CENTER;
        *joyY = Y_CENTER;
        if (pressed != NULL) {
            *pressed = false;
        }
        if ((now - s_last_read_fail_log) >= pdMS_TO_TICKS(JOY_READ_FAIL_LOG_MS)) {
            ESP_LOGW(TAG, "read fail: i2c mutex timeout ready=%d gpio0=%d gpio26=%d",
                     joyc_i2c_is_ready(), gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            s_last_read_fail_log = now;
        }
        return false;
    }

    if (!joyc_i2c_is_ready()) {
        *joyX = X_CENTER;
        *joyY = Y_CENTER;
        if (pressed != NULL) {
            *pressed = false;
        }
        if (s_joystick_i2c_lock != NULL) {
            xSemaphoreGive(s_joystick_i2c_lock);
        }
        if ((now - s_last_read_fail_log) >= pdMS_TO_TICKS(JOY_READ_FAIL_LOG_MS)) {
            ESP_LOGW(TAG, "read fail: JoyC not ready gpio0=%d gpio26=%d",
                     gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            s_last_read_fail_log = now;
        }
        return false;
    }

    uint8_t data[4];
    bool xy_ok = joyc_i2c_read_bytes(0x00, data, 2);
    vTaskDelay(pdMS_TO_TICKS(20));
    xy_ok = xy_ok && joyc_i2c_read_bytes(0x02, &data[2], 2);
    if (xy_ok) {
        *joyX = (data[1] << 8) | data[0];
        *joyY = (data[3] << 8) | data[2];
    } else {
        *joyX = X_CENTER;
        *joyY = Y_CENTER;
        if ((now - s_last_read_fail_log) >= pdMS_TO_TICKS(JOY_READ_FAIL_LOG_MS)) {
            ESP_LOGW(TAG, "read fail: xy via M5.Ex_I2C ready=%d gpio0=%d gpio26=%d",
                     joyc_i2c_is_ready(), gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            s_last_read_fail_log = now;
        }
    }

    if (pressed != NULL) {
        uint8_t btn = 0;
        bool btn_ok = joyc_i2c_read_bytes(0x30, &btn, 1);
        *pressed = btn_ok && joyc_button_decode_pressed(btn);
        if (btn_ok && (!s_button_raw_seen || btn != s_last_button_raw)) {
            ESP_LOGI(TAG, "button raw: reg30=0x%02x pressed=%d", btn, *pressed);
            s_last_button_raw = btn;
            s_button_raw_seen = true;
        }
        if (!btn_ok && (now - s_last_read_fail_log) >= pdMS_TO_TICKS(JOY_READ_FAIL_LOG_MS)) {
            ESP_LOGW(TAG, "read warn: button via M5.Ex_I2C xy_ok=%d gpio0=%d gpio26=%d",
                     xy_ok, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            s_last_read_fail_log = now;
        }
    }

    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreGive(s_joystick_i2c_lock);
    }
    return xy_ok;
}

/**
 * @brief Public interface to initialize the joystick and return default configuration
 * @return joystick_data_t Structure containing initialized joystick parameters
 * @note This is the main initialization function exposed to users
 * @details
 *      1. Initializes JoyC I2C access
 *      2. Initializes all fields of 'joystick_data_t'
 *         - bat: 0 (battery level, to be updated later)
 *         - joyX, joyY: center (initial joystick positions)
 *         - screen_mode: MODE_SETUP (start in setup mode)
 * @return joystick_data_t
 */
joystick_data_t joystick_init(void)
{
    joystick_i2c_init();
    joystick_data_t tmp;
    tmp.bat         = 0;
    tmp.joyX        = X_CENTER;
    tmp.joyY        = Y_CENTER;
    tmp.joy_pressed = false;
    tmp.accel_x     = 0.0f;
    tmp.accel_y     = 0.0f;
    tmp.accel_z     = 0.0f;
    tmp.screen_mode = MODE_SETUP;
    return tmp;
}

/**
 * @brief Task to handle joystick setup screen
 * @param pvParam Pointer to joystick data, pointing to joystick_data_t structure
 * @note This function runs an infinite loop that continuously reads joystick XY coordinates
 *       and updates the setup screen when the screen mode is MODE_SETUP
 * @details Reads raw joystick data and then calls update_setup_screen function to update screen display
 *          Each loop iteration has a 50ms delay to ensure interface responsiveness
 */
void handle_setup_screen(void *pvParam)
{
    (void)pvParam;
    TickType_t last_setup_log = 0;

    while (1) {
        if (app_state_get_mode() == MODE_SETUP) {
            uint16_t joy_x = X_CENTER;
            uint16_t joy_y = Y_CENTER;
            bool joy_pressed = false;
            bool read_ok = joystick_read_state(&joy_x, &joy_y, &joy_pressed);
            app_state_set_joystick(joy_x, joy_y, joy_pressed);
            joystick_data_t snapshot = app_state_snapshot();
            update_setup_screen(&snapshot);
            TickType_t now = xTaskGetTickCount();
            if ((now - last_setup_log) >= pdMS_TO_TICKS(JOY_SETUP_LOG_MS)) {
                ESP_LOGI(TAG,
                         "setup raw: ok=%d ready=%d x=%u y=%u pressed=%d hid=%s hfp=%s audio=%s discoverable=%d gpio0=%d gpio26=%d",
                         read_ok, joystick_is_ready(), snapshot.joyX, snapshot.joyY,
                         snapshot.joy_pressed, bt_input_hid_status_text(), bt_input_hfp_status_text(),
                         bt_input_audio_status_text(), bt_input_is_discoverable(), gpio_get_level(GPIO_NUM_0),
                         gpio_get_level(GPIO_NUM_26));
                last_setup_log = now;
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}

/**
 * @brief Task to handle mouse mode by reading joystick data and sending HID mouse reports.
 * @param pvParam Pointer to joystick data, pointing to joystick_data_t structure
 * @note This function runs an infinite loop that reads joystick input and sends mouse reports in running mode.
 * @details
 *      1. Reads raw X/Y values from the joystick via I2C
 *      2. Updates the running screen display with current values
 *      3. Tracks center drift while the joystick is idle
 *      4. Applies deadzone and speed curve conversion for HID X/Y deltas
 *      5. Sends HID mouse movement and left-click button reports
 */
void handle_running_screen(void *pvParam)
{
    (void)pvParam;
    mouse_controller_t mouse = {
        .center_x = X_CENTER,
        .center_y = Y_CENTER,
    };
    bool last_pressed = false;
    bool mouse_mode_active = false;
    TickType_t last_ui_update = 0;
    TickType_t last_mouse_log = 0;

    while (1) {
        if (app_state_get_mode() == MODE_RUNNING) {
            uint16_t joy_x = X_CENTER;
            uint16_t joy_y = Y_CENTER;
            bool joy_pressed = false;
            bool read_ok = joystick_read_state(&joy_x, &joy_y, &joy_pressed);
            app_state_set_joystick(joy_x, joy_y, joy_pressed);
            joystick_data_t snapshot = app_state_snapshot();

            if (!mouse_mode_active) {
                mouse_controller_reset(&mouse, snapshot.joyX, snapshot.joyY);
                last_pressed = snapshot.joy_pressed;
                mouse_mode_active = true;
                last_ui_update = 0;
                last_mouse_log = 0;
                ESP_LOGI(TAG, "mouse enter: read_ok=%d raw=(%u,%u) center=(%d,%d) pressed=%d hid=%s",
                         read_ok, snapshot.joyX, snapshot.joyY, mouse.center_x, mouse.center_y,
                         snapshot.joy_pressed, bt_input_hid_status_text());
            }

            TickType_t now = xTaskGetTickCount();
            if ((now - last_ui_update) >= pdMS_TO_TICKS(MOUSE_UI_REFRESH_MS)) {
                update_running_screen(snapshot.joyX, snapshot.joyY, snapshot.bat,
                                      snapshot.joy_pressed, bt_input_hid_connected());
                last_ui_update = now;
            }

            joy_x = snapshot.joyX;
            joy_y = snapshot.joyY;
            mouse_controller_report_t report =
                mouse_controller_update(&mouse, joy_x, joy_y, snapshot.joy_pressed, last_pressed);
            if ((now - last_mouse_log) >= pdMS_TO_TICKS(JOY_MOUSE_LOG_MS)) {
                ESP_LOGI(TAG,
                         "mouse raw: ok=%d x=%u y=%u pressed=%d center=(%d,%d)->(%d,%d) raw_delta=(%d,%d) report=(%d,%d,%u) send=%d hid=%d",
                         read_ok, snapshot.joyX, snapshot.joyY, snapshot.joy_pressed,
                         report.center_before_x, report.center_before_y, mouse.center_x, mouse.center_y,
                         (int16_t)(joy_x - mouse.center_x), (int16_t)(joy_y - mouse.center_y),
                         report.dx, report.dy, report.buttons, report.should_send, bt_input_hid_connected());
                last_mouse_log = now;
            }
            if (report.pressed_changed) {
                ESP_LOGI(TAG, "mouse button: raw_btn=%d hid=%d", snapshot.joy_pressed, bt_input_hid_connected());
            }
            if (report.should_send) {
                bt_input_mouse_send(report.buttons, report.dx, report.dy, 0);
                last_pressed = snapshot.joy_pressed;
            }
            vTaskDelay(pdMS_TO_TICKS(MOUSE_POLL_MS));
        } else {
            mouse_mode_active = false;
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}

/**
 * @brief Task to keep the IMU visualization updated while the IMU page is active.
 *
 * The main loop owns sensor sampling; this task only pushes the latest accelerometer
 * values and battery level into the LVGL IMU screen.
 */
void handle_imu_screen(void *pvParam)
{
    (void)pvParam;

    while (1) {
        if (app_state_get_mode() == MODE_IMU) {
            joystick_data_t snapshot = app_state_snapshot();
            update_imu_screen(snapshot.accel_x, snapshot.accel_y, snapshot.accel_z, snapshot.bat);

            vTaskDelay(30 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}
