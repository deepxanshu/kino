/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joystick_handle.h"
#include "../bluetooth/bt_input.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "joyc_i2c_bridge.h"

static SemaphoreHandle_t s_joystick_i2c_lock;

#define MOUSE_POLL_MS 12
#define MOUSE_UI_REFRESH_MS 60
#define MOUSE_DEAD_ZONE 150
#define MOUSE_CENTER_TRACK_ZONE 260
#define MOUSE_CENTER_TRACK_SHIFT 4
#define MOUSE_MAX_DELTA 48
#define JOY_SETUP_LOG_MS 1000
#define JOY_MOUSE_LOG_MS 250
#define JOY_READ_FAIL_LOG_MS 1000

static const char *TAG = "joy_diag";

static int16_t s_mouse_center_x = X_CENTER;
static int16_t s_mouse_center_y = Y_CENTER;
static TickType_t s_last_read_fail_log = 0;

static void log_joy_pins(const char *stage)
{
    ESP_LOGI(TAG, "%s: gpio0=%d gpio26=%d", stage, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
}

static int16_t abs16(int16_t value)
{
    return value < 0 ? -value : value;
}

static int8_t mouse_axis_delta(int16_t raw, int16_t center, int16_t min_value, int16_t max_value, bool invert)
{
    int16_t delta = raw - center;
    int16_t abs_delta = abs16(delta);
    if (abs_delta <= MOUSE_DEAD_ZONE) {
        return 0;
    }

    int16_t span = delta > 0 ? (max_value - center) : (center - min_value);
    if (span <= MOUSE_DEAD_ZONE) {
        span = MOUSE_DEAD_ZONE + 1;
    }

    int32_t effective = abs_delta - MOUSE_DEAD_ZONE;
    int32_t usable = span - MOUSE_DEAD_ZONE;
    int32_t scaled = 1 + (effective * 8 / usable) + (effective * effective * 40 / (usable * usable));
    if (scaled > MOUSE_MAX_DELTA) {
        scaled = MOUSE_MAX_DELTA;
    }

    int8_t out = delta > 0 ? (int8_t)scaled : (int8_t)-scaled;
    return invert ? -out : out;
}

static void mouse_center_reset(uint16_t joyX, uint16_t joyY)
{
    if (joyX >= X_MIN && joyX <= X_MAX) {
        s_mouse_center_x = joyX;
    } else {
        s_mouse_center_x = X_CENTER;
    }
    if (joyY >= Y_MIN && joyY <= Y_MAX) {
        s_mouse_center_y = joyY;
    } else {
        s_mouse_center_y = Y_CENTER;
    }
}

static void mouse_center_track(uint16_t joyX, uint16_t joyY, bool pressed)
{
    if (pressed) {
        return;
    }

    int16_t dx = (int16_t)joyX - s_mouse_center_x;
    int16_t dy = (int16_t)joyY - s_mouse_center_y;
    if (abs16(dx) <= MOUSE_CENTER_TRACK_ZONE && abs16(dy) <= MOUSE_CENTER_TRACK_ZONE) {
        s_mouse_center_x += dx / (1 << MOUSE_CENTER_TRACK_SHIFT);
        s_mouse_center_y += dy / (1 << MOUSE_CENTER_TRACK_SHIFT);
    }
}

/**
 * @brief Initialize joystick via I2C interface
 * @note This is an internal static function that configures I2C_NUM_0 as master with SDA on GPIO0 and SCL on GPIO26
 * @details
 *      1. Configures M5.Ex_I2C on I2C_NUM_0 with SDA GPIO0 and SCL GPIO26
 *      2. Probes the JoyC joystick at I2C address 0x54
 * @warning This function assumes the joystick device is at I2C address 0x54
 */
static void joystick_i2c_init()
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
        // ESP_LOGE("I2C Joystick", "Failed to read joystick data");
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
        *pressed = btn_ok && (btn != 0);
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
 *      1. Calls internal： device_joystick_init()
 *      2. Initializes all fields of 'joystick_data_t'
 *         - channel: 1 (default communication channel)
 *         - id: 0 (default target ID)
 *         - bat: 0 (battery level, to be updated later)
 *         - joyX, joyY: 0 (initial joystick positions)
 *         - screen_mode: MODE_SETUP (start in setup mode)
 *         - select_mode: CHANNEL_SELECT (default selection mode)
 * @return joystick_data_t
 */
joystick_data_t joystick_init()
{
    joystick_i2c_init();
    joystick_data_t tmp;
    tmp.channel     = 1;
    tmp.id          = 0;
    tmp.bat         = 0;
    tmp.joyX        = 0;
    tmp.joyY        = 0;
    tmp.joy_pressed = false;
    tmp.accel_x     = 0.0f;
    tmp.accel_y     = 0.0f;
    tmp.accel_z     = 0.0f;
    tmp.screen_mode = MODE_SETUP;
    tmp.select_mode = CHANNEL_SELECT;
    tmp.btnB_status = false;
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
    joystick_data_t *joystick_data = (joystick_data_t *)pvParam;
    TickType_t last_setup_log = 0;

    while (1) {
        if (joystick_data->screen_mode == MODE_SETUP) {
            bool read_ok = joystick_read_state(&joystick_data->joyX, &joystick_data->joyY, &joystick_data->joy_pressed);
            update_setup_screen(joystick_data);
            TickType_t now = xTaskGetTickCount();
            if ((now - last_setup_log) >= pdMS_TO_TICKS(JOY_SETUP_LOG_MS)) {
                ESP_LOGI(TAG,
                         "setup raw: ok=%d ready=%d x=%u y=%u btn=%d hid=%s hfp=%s audio=%s discoverable=%d gpio0=%d gpio26=%d",
                         read_ok, joystick_is_ready(), joystick_data->joyX, joystick_data->joyY,
                         joystick_data->joy_pressed, bt_input_hid_status_text(), bt_input_hfp_status_text(),
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
 * @brief Task to handle joystick running screen, responsible for reading joystick data and sending ESP-NOW control
 * packets
 * @param pvParam Pointer to joystick data, pointing to joystick_data_t structure
 * @note This function runs an infinite loop that reads joystick input, processes data, and sends control packets in
 * running mode
 * @details
 *      1. Reads raw X/Y values from the joystick via I2C
 *      2. Updates the running screen display with current values
 *      3. Applies deadzone correction to center the joystick values
 *      4. Maps raw values to yaw/pitch angle ranges (-1280 to 1280 for yaw, 0 to 900 for pitch)
 *      5. Only sends data when changes exceed threshold (5 units) to reduce network traffic
 *      6. Constructs and sends ESP-NOW packet containing target ID, yaw, pitch, speed and button status
 *      7. Each loop iteration has a 30ms delay when in running mode
 */
void handle_running_screen(void *pvParam)
{
    joystick_data_t *joystick_data = (joystick_data_t *)pvParam;
    bool last_pressed = false;
    bool mouse_mode_active = false;
    TickType_t last_ui_update = 0;
    TickType_t last_mouse_log = 0;

    while (1) {
        if (joystick_data->screen_mode == MODE_RUNNING) {
            bool read_ok = joystick_read_state(&joystick_data->joyX, &joystick_data->joyY, &joystick_data->joy_pressed);

            if (!mouse_mode_active) {
                mouse_center_reset(joystick_data->joyX, joystick_data->joyY);
                last_pressed = joystick_data->joy_pressed;
                mouse_mode_active = true;
                last_ui_update = 0;
                last_mouse_log = 0;
                ESP_LOGI(TAG, "mouse enter: read_ok=%d raw=(%u,%u) center=(%d,%d) btn=%d hid=%s",
                         read_ok, joystick_data->joyX, joystick_data->joyY, s_mouse_center_x, s_mouse_center_y,
                         joystick_data->joy_pressed, bt_input_hid_status_text());
            }

            TickType_t now = xTaskGetTickCount();
            if ((now - last_ui_update) >= pdMS_TO_TICKS(MOUSE_UI_REFRESH_MS) &&
                running_screen != NULL && lv_obj_is_valid(running_screen)) {
                update_running_screen(joystick_data->joyX, joystick_data->joyY, joystick_data->bat,
                                      joystick_data->joy_pressed, bt_input_hid_connected());
                last_ui_update = now;
            }

            int16_t joy_x = joystick_data->joyX;
            int16_t joy_y = joystick_data->joyY;
            int16_t center_before_x = s_mouse_center_x;
            int16_t center_before_y = s_mouse_center_y;
            mouse_center_track(joy_x, joy_y, joystick_data->joy_pressed);
            int8_t dx = mouse_axis_delta(joy_x, s_mouse_center_x, X_MIN, X_MAX, false);
            int8_t dy = mouse_axis_delta(joy_y, s_mouse_center_y, Y_MIN, Y_MAX, true);

            uint8_t buttons = joystick_data->joy_pressed ? 0x01 : 0x00;
            bool pressed_changed = joystick_data->joy_pressed != last_pressed;
            bool should_send = dx != 0 || dy != 0 || pressed_changed;
            if ((now - last_mouse_log) >= pdMS_TO_TICKS(JOY_MOUSE_LOG_MS)) {
                ESP_LOGI(TAG,
                         "mouse raw: ok=%d x=%u y=%u btn=%d center=(%d,%d)->(%d,%d) raw_delta=(%d,%d) report=(%d,%d,%u) send=%d hid=%d",
                         read_ok, joystick_data->joyX, joystick_data->joyY, joystick_data->joy_pressed,
                         center_before_x, center_before_y, s_mouse_center_x, s_mouse_center_y,
                         (int16_t)(joy_x - s_mouse_center_x), (int16_t)(joy_y - s_mouse_center_y),
                         dx, dy, buttons, should_send, bt_input_hid_connected());
                last_mouse_log = now;
            }
            if (pressed_changed) {
                ESP_LOGI(TAG, "mouse button: raw_btn=%d hid=%d", joystick_data->joy_pressed, bt_input_hid_connected());
            }
            if (should_send) {
                bt_input_mouse_send(buttons, dx, dy, 0);
                last_pressed = joystick_data->joy_pressed;
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
    joystick_data_t *joystick_data = (joystick_data_t *)pvParam;

    while (1) {
        if (joystick_data->screen_mode == MODE_IMU) {
            update_imu_screen(joystick_data->accel_x, joystick_data->accel_y, joystick_data->accel_z,
                              joystick_data->bat);

            vTaskDelay(30 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}
