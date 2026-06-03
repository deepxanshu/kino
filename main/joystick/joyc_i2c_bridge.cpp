/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joyc_i2c_bridge.h"

#include "joyc_soft_i2c.h"
#include "M5Unified.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "hal/i2c_types.h"

static constexpr uint8_t JOYC_ADDR = 0x54;
static constexpr uint32_t JOYC_I2C_FREQ = 100000;
static const char *TAG = "joyc_i2c";

typedef enum {
    JOYC_BACKEND_NONE = 0,
    JOYC_BACKEND_M5_EX,
    JOYC_BACKEND_SOFT,
} joyc_backend_t;

static bool s_ready = false;
static joyc_backend_t s_backend = JOYC_BACKEND_NONE;

static void reset_joyc_pins(void)
{
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_26);
}

extern "C" bool joyc_i2c_begin(void)
{
    ESP_LOGI(TAG,
             "begin: before ex_port=%d sda=%d scl=%d gpio0=%d gpio26=%d",
             M5.Ex_I2C.getPort(), M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(),
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));

    bool begin_ok = M5.Ex_I2C.begin(I2C_NUM_0, GPIO_NUM_0, GPIO_NUM_26);
    bool probe_ok = begin_ok && M5.Ex_I2C.scanID(JOYC_ADDR, JOYC_I2C_FREQ);

    ESP_LOGI(TAG,
             "begin: begin_ok=%d probe54=%d ex_port=%d sda=%d scl=%d gpio0=%d gpio26=%d",
             begin_ok, probe_ok, M5.Ex_I2C.getPort(), M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(),
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    if (probe_ok) {
        s_backend = JOYC_BACKEND_M5_EX;
        s_ready = true;
        return true;
    }

    ESP_LOGW(TAG, "begin: M5.Ex_I2C probe failed, fallback to software I2C");
    M5.Ex_I2C.release();
    reset_joyc_pins();

    bool soft_ok = joyc_soft_i2c_begin();
    s_backend = soft_ok ? JOYC_BACKEND_SOFT : JOYC_BACKEND_NONE;
    s_ready = soft_ok;
    ESP_LOGI(TAG, "begin: soft_ok=%d backend=%d gpio0=%d gpio26=%d", soft_ok, s_backend,
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    return s_ready;
}

extern "C" void joyc_i2c_release(void)
{
    ESP_LOGI(TAG, "release: ready=%d backend=%d port=%d sda=%d scl=%d", s_ready, s_backend,
             M5.Ex_I2C.getPort(), M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL());
    if (s_backend == JOYC_BACKEND_SOFT) {
        joyc_soft_i2c_release();
    } else if (s_backend == JOYC_BACKEND_M5_EX) {
        M5.Ex_I2C.release();
    }
    s_backend = JOYC_BACKEND_NONE;
    s_ready = false;
    ESP_LOGI(TAG, "release: done gpio0=%d gpio26=%d", gpio_get_level(GPIO_NUM_0),
             gpio_get_level(GPIO_NUM_26));
}

extern "C" bool joyc_i2c_is_ready(void)
{
    return s_ready;
}

extern "C" bool joyc_i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_ready || data == nullptr || len == 0) {
        return false;
    }
    if (s_backend == JOYC_BACKEND_M5_EX) {
        return M5.Ex_I2C.readRegister(JOYC_ADDR, reg, data, len, JOYC_I2C_FREQ);
    }
    if (s_backend == JOYC_BACKEND_SOFT) {
        return joyc_soft_i2c_read_bytes(reg, data, len);
    }
    return false;
}
