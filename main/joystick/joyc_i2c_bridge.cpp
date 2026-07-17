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
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/i2c_types.h"

static constexpr uint8_t JOYC_ADDR = 0x54;
static constexpr uint32_t JOYC_I2C_FREQ = 100000;
static constexpr int JOYC_PORT_POWER_MV = 2800;
static const char *TAG = "joyc_i2c";

typedef enum {
    JOYC_BACKEND_NONE = 0,
    JOYC_BACKEND_M5_EX,
    JOYC_BACKEND_SOFT,
} joyc_backend_t;

static bool s_ready = false;
static joyc_backend_t s_backend = JOYC_BACKEND_NONE;

static bool joyc_bus_idle(void)
{
    return gpio_get_level(GPIO_NUM_0) == 1 && gpio_get_level(GPIO_NUM_26) == 1;
}

static void release_backend(void)
{
    if (s_backend == JOYC_BACKEND_SOFT) {
        joyc_soft_i2c_release();
    } else if (s_backend == JOYC_BACKEND_M5_EX) {
        M5.Ex_I2C.release();
    } else {
        joyc_soft_i2c_release();
        M5.Ex_I2C.release();
    }
    s_backend = JOYC_BACKEND_NONE;
    s_ready = false;
}

static void reset_joyc_pins_to_pullups(void)
{
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_26);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_26, GPIO_PULLUP_ONLY);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_26, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void pulse_i2c_bus(void)
{
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_26, GPIO_PULLUP_ONLY);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT_OD);

    for (int i = 0; i < 9; ++i) {
        gpio_set_level(GPIO_NUM_26, 0);
        esp_rom_delay_us(8);
        gpio_set_level(GPIO_NUM_26, 1);
        esp_rom_delay_us(8);
    }

    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(GPIO_NUM_0, 0);
    esp_rom_delay_us(8);
    gpio_set_level(GPIO_NUM_26, 1);
    esp_rom_delay_us(8);
    gpio_set_level(GPIO_NUM_0, 1);
    esp_rom_delay_us(8);

    reset_joyc_pins_to_pullups();
}

static void power_cycle_porta(void)
{
    ESP_LOGW(TAG, "recover: power-cycle PortA power gpio0=%d gpio26=%d",
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    M5.Power.setExtOutput(false);
    M5.Power.Axp192.setLDO2(0);
    vTaskDelay(pdMS_TO_TICKS(250));
    M5.Power.Axp192.setLDO2(JOYC_PORT_POWER_MV);
    M5.Power.setExtOutput(true);
    vTaskDelay(pdMS_TO_TICKS(350));
}

static void recover_bus_lines(bool power_cycle)
{
    reset_joyc_pins_to_pullups();
    if (!joyc_bus_idle()) {
        ESP_LOGW(TAG, "recover: bus not idle, pulse lines gpio0=%d gpio26=%d",
                 gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
        pulse_i2c_bus();
    }
    if (power_cycle && !joyc_bus_idle()) {
        power_cycle_porta();
        pulse_i2c_bus();
    }
    ESP_LOGI(TAG, "recover: done power=%d idle=%d gpio0=%d gpio26=%d",
             power_cycle, joyc_bus_idle(), gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
}

extern "C" bool joyc_i2c_begin(void)
{
    release_backend();
    recover_bus_lines(false);
    if (!joyc_bus_idle()) {
        ESP_LOGW(TAG, "begin: skip probe, bus stuck gpio0=%d gpio26=%d",
                 gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
        return false;
    }

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
    reset_joyc_pins_to_pullups();

    bool soft_ok = joyc_soft_i2c_begin();
    s_backend = soft_ok ? JOYC_BACKEND_SOFT : JOYC_BACKEND_NONE;
    s_ready = soft_ok;
    ESP_LOGI(TAG, "begin: soft_ok=%d backend=%d gpio0=%d gpio26=%d", soft_ok, s_backend,
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    return s_ready;
}

extern "C" bool joyc_i2c_recover(bool power_cycle)
{
    ESP_LOGW(TAG, "recover: start power=%d ready=%d backend=%d gpio0=%d gpio26=%d",
             power_cycle, s_ready, s_backend, gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    release_backend();
    recover_bus_lines(power_cycle);
    if (joyc_i2c_begin()) {
        return true;
    }

    // kino: the JoyC MCU can hang while leaving the bus lines idle (both high).
    // recover_bus_lines() only power-cycles when the bus looks stuck, so in that
    // case it never resets the JoyC and the probe fails forever -- the "joystick
    // frozen until I restart" bug. If a power-cycle was requested but the probe
    // still failed, force a real PortA power-cycle to hard-reset the JoyC MCU and
    // probe once more.
    if (power_cycle) {
        ESP_LOGW(TAG, "recover: probe failed with idle bus, forcing PortA power-cycle");
        power_cycle_porta();
        pulse_i2c_bus();
        return joyc_i2c_begin();
    }
    return false;
}

extern "C" void joyc_i2c_release(void)
{
    ESP_LOGI(TAG, "release: ready=%d backend=%d port=%d sda=%d scl=%d", s_ready, s_backend,
             M5.Ex_I2C.getPort(), M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL());
    release_backend();
    reset_joyc_pins_to_pullups();
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
