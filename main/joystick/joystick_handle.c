/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joystick_handle.h"
#include "../app_state.h"
#include "../bluetooth/bt_input.h"
#include "../device_mode.h"
#include "../ui/ui_setup_screen.h"
#include "../ui/ui_running_screen.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "joyc_button.h"
#include "joyc_i2c_bridge.h"
#include "mouse_controller.h"
#include <inttypes.h>

static SemaphoreHandle_t s_joystick_i2c_lock;

#define MOUSE_POLL_MS 12
#define MOUSE_UI_REFRESH_MS 60
#define JOY_SETUP_LOG_MS 1000
#define JOY_MOUSE_LOG_MS 250
#define JOY_READ_FAIL_LOG_MS 1000
#define JOY_RECOVER_RETRY_MS 2500
#define JOY_STUCK_RECOVER_RETRY_MS 30000
#define JOY_STUCK_FAST_ATTEMPTS 2
#define JOY_READ_FAILS_BEFORE_RELEASE 3

static const char *TAG = "joy_diag";

static TickType_t s_last_read_fail_log = 0;
static TickType_t s_last_recover_attempt = 0;
static uint8_t s_xy_read_fail_count = 0;
static uint8_t s_recover_stuck_count = 0;
static bool s_recover_stuck_latched = false;
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

void joystick_recover(bool power_cycle)
{
    log_joy_pins(power_cycle ? "joystick recover begin power" : "joystick recover begin");
    if (s_joystick_i2c_lock == NULL) {
        s_joystick_i2c_lock = xSemaphoreCreateMutex();
    }
    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreTake(s_joystick_i2c_lock, portMAX_DELAY);
    }

    bool ok = joyc_i2c_recover(power_cycle);
    s_xy_read_fail_count = 0;

    if (s_joystick_i2c_lock != NULL) {
        xSemaphoreGive(s_joystick_i2c_lock);
    }
    ESP_LOGI(TAG, "joystick recover end ok=%d ready=%d", ok, joystick_is_ready());
    log_joy_pins("joystick recover end");
}

static void joystick_recover_if_due(const char *reason)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t retry_ms = s_recover_stuck_latched ? JOY_STUCK_RECOVER_RETRY_MS : JOY_RECOVER_RETRY_MS;
    if (s_last_recover_attempt != 0 &&
        (now - s_last_recover_attempt) < pdMS_TO_TICKS(retry_ms)) {
        return;
    }
    s_last_recover_attempt = now;
    ESP_LOGW(TAG, "recover: %s ready=%d stuck=%d retry_ms=%" PRIu32 " gpio0=%d gpio26=%d",
             reason, joystick_is_ready(), s_recover_stuck_latched, retry_ms,
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    joystick_recover(true);

    if (joystick_is_ready()) {
        s_recover_stuck_count = 0;
        s_recover_stuck_latched = false;
        return;
    }

    bool scl_stuck_low = gpio_get_level(GPIO_NUM_26) == 0;
    if (scl_stuck_low && s_recover_stuck_count < UINT8_MAX) {
        s_recover_stuck_count++;
    } else if (!scl_stuck_low) {
        s_recover_stuck_count = 0;
        s_recover_stuck_latched = false;
    }

    if (s_recover_stuck_count >= JOY_STUCK_FAST_ATTEMPTS && !s_recover_stuck_latched) {
        s_recover_stuck_latched = true;
        ESP_LOGW(TAG,
                 "recover: JoyC SCL stuck low after %u attempts; backing off until power-cycle or next long retry",
                 s_recover_stuck_count);
    }
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
        s_xy_read_fail_count = 0;
        *joyX = (data[1] << 8) | data[0];
        *joyY = (data[3] << 8) | data[2];
    } else {
        if (s_xy_read_fail_count < UINT8_MAX) {
            s_xy_read_fail_count++;
        }
        *joyX = X_CENTER;
        *joyY = Y_CENTER;
        if ((now - s_last_read_fail_log) >= pdMS_TO_TICKS(JOY_READ_FAIL_LOG_MS)) {
            ESP_LOGW(TAG, "read fail: xy via M5.Ex_I2C ready=%d fail_count=%u gpio0=%d gpio26=%d",
                     joyc_i2c_is_ready(), s_xy_read_fail_count, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            s_last_read_fail_log = now;
        }
        if (s_xy_read_fail_count >= JOY_READ_FAILS_BEFORE_RELEASE) {
            ESP_LOGW(TAG, "read fail: release stale JoyC bus fail_count=%u gpio0=%d gpio26=%d",
                     s_xy_read_fail_count, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
            joyc_i2c_release();
            s_xy_read_fail_count = 0;
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
            if (device_mode_peripheral_switching()) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (!joystick_is_ready()) {
                joystick_recover_if_due("setup JoyC not ready");
            }
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
    static const mic_spectrum_data_t empty_spectrum = {
        .db = -90,
    };

    while (1) {
        if (app_state_get_mode() == MODE_RUNNING) {
            if (device_mode_peripheral_switching()) {
                mouse_mode_active = false;
                last_pressed = false;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (!device_mode_magic_joystick_enabled()) {
                mouse_mode_active = false;
                last_pressed = false;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (!joystick_is_ready()) {
                joystick_recover_if_due("mouse JoyC not ready");
            }
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
            joy_x = snapshot.joyX;
            joy_y = snapshot.joyY;
            mouse_controller_report_t report =
                mouse_controller_update(&mouse, joy_x, joy_y, snapshot.joy_pressed, last_pressed,
                                        (uint32_t)(now * portTICK_PERIOD_MS));
            if ((now - last_ui_update) >= pdMS_TO_TICKS(MOUSE_UI_REFRESH_MS)) {
                update_running_screen(snapshot.joyX, snapshot.joyY, snapshot.bat,
                                      snapshot.joy_pressed,
                                      snapshot.accel_x, snapshot.accel_y, snapshot.accel_z,
                                      &empty_spectrum, false, true, report.scroll_active);
                last_ui_update = now;
            }
            if ((now - last_mouse_log) >= pdMS_TO_TICKS(JOY_MOUSE_LOG_MS)) {
                ESP_LOGI(TAG,
                         "mouse raw: ok=%d x=%u y=%u pressed=%d center=(%d,%d)->(%d,%d) raw_delta=(%d,%d) report=(%d,%d,%d,%d,%u) scroll=%d send=%d hid=%d",
                         read_ok, snapshot.joyX, snapshot.joyY, snapshot.joy_pressed,
                         report.center_before_x, report.center_before_y, mouse.center_x, mouse.center_y,
                         (int16_t)(joy_x - mouse.center_x), (int16_t)(joy_y - mouse.center_y),
                         report.dx, report.dy, report.wheel, report.pan, report.buttons,
                         report.scroll_active, report.should_send, bt_input_hid_connected());
                last_mouse_log = now;
            }
            if (report.scroll_entered) {
                ESP_LOGI(TAG, "mouse scroll enter: hid=%d", bt_input_hid_connected());
            }
            if (report.scroll_exited) {
                ESP_LOGI(TAG, "mouse scroll exit: hid=%d", bt_input_hid_connected());
            }
            if (report.pressed_changed) {
                ESP_LOGI(TAG, "mouse button: raw_btn=%d hid=%d", snapshot.joy_pressed, bt_input_hid_connected());
            }
            if (report.should_send) {
                bt_input_mouse_send(report.buttons, report.dx, report.dy, report.wheel, report.pan);
                last_pressed = snapshot.joy_pressed;
            }
            vTaskDelay(pdMS_TO_TICKS(MOUSE_POLL_MS));
        } else {
            mouse_mode_active = false;
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}
