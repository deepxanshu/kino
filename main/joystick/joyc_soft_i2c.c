/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joyc_soft_i2c.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2c_bus.h"

static const uint8_t JOYC_ADDR = 0x54;
static const uint32_t JOYC_I2C_FREQ = 100000;
static const char *TAG = "joyc_bus_i2c";

typedef enum {
    JOYC_BUS_BACKEND_NONE = 0,
    JOYC_BUS_BACKEND_HW,
    JOYC_BUS_BACKEND_SW,
} joyc_bus_backend_t;

static i2c_bus_handle_t s_bus = NULL;
static i2c_bus_device_handle_t s_dev = NULL;
static joyc_bus_backend_t s_backend = JOYC_BUS_BACKEND_NONE;

static bool begin_on_port(i2c_port_t port, joyc_bus_backend_t backend)
{
    i2c_config_t conf = {0};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = GPIO_NUM_0;
    conf.scl_io_num = GPIO_NUM_26;
    conf.sda_pullup_en = true;
    conf.scl_pullup_en = true;
    conf.master.clk_speed = JOYC_I2C_FREQ;
    conf.clk_flags = 0;

    s_bus = i2c_bus_create(port, &conf);
    if (s_bus == NULL) {
        ESP_LOGW(TAG, "begin: create %s bus failed port=%d gpio0=%d gpio26=%d",
                 backend == JOYC_BUS_BACKEND_HW ? "hw" : "soft", port,
                 gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
        return false;
    }

    uint8_t scan_buf[128] = {0};
    uint8_t found = i2c_bus_scan(s_bus, scan_buf, sizeof(scan_buf));
    bool probe_ok = false;
    for (uint8_t i = 0; i < found && i < sizeof(scan_buf); ++i) {
        ESP_LOGI(TAG, "%s scan: found 0x%02x",
                 backend == JOYC_BUS_BACKEND_HW ? "hw" : "soft", scan_buf[i]);
        if (scan_buf[i] == JOYC_ADDR) {
            probe_ok = true;
        }
    }

    s_dev = i2c_bus_device_create(s_bus, JOYC_ADDR, 0);
    ESP_LOGI(TAG, "begin: backend=%s found=%u probe54=%d dev=%p gpio0=%d gpio26=%d",
             backend == JOYC_BUS_BACKEND_HW ? "hw" : "soft", found, probe_ok, s_dev,
             gpio_get_level(GPIO_NUM_0), gpio_get_level(GPIO_NUM_26));
    if (probe_ok && s_dev != NULL) {
        s_backend = backend;
        return true;
    }

    joyc_soft_i2c_release();
    return false;
}

bool joyc_soft_i2c_begin(void)
{
    if (begin_on_port(I2C_NUM_0, JOYC_BUS_BACKEND_HW)) {
        return true;
    }

    ESP_LOGW(TAG, "begin: hardware I2C failed, fallback to software I2C");
    return begin_on_port((i2c_port_t)I2C_NUM_SW_0, JOYC_BUS_BACKEND_SW);
}

void joyc_soft_i2c_release(void)
{
    if (s_dev != NULL) {
        i2c_bus_device_delete(&s_dev);
        s_dev = NULL;
    }
    if (s_bus != NULL) {
        i2c_bus_delete(&s_bus);
        s_bus = NULL;
    }
    s_backend = JOYC_BUS_BACKEND_NONE;
}

bool joyc_soft_i2c_scan_id(uint8_t addr)
{
    if (s_bus == NULL) {
        return false;
    }

    uint8_t scan_buf[16] = {0};
    uint8_t found = i2c_bus_scan(s_bus, scan_buf, sizeof(scan_buf));
    for (uint8_t i = 0; i < found && i < sizeof(scan_buf); ++i) {
        if (scan_buf[i] == addr) {
            return true;
        }
    }
    return false;
}

bool joyc_soft_i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    if (s_dev == NULL || data == NULL || len == 0) {
        return false;
    }

    esp_err_t ret = i2c_bus_read_bytes(s_dev, reg, len, data);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "read: backend=%d reg=0x%02x len=%u ret=%s", s_backend, reg,
                 (unsigned)len, esp_err_to_name(ret));
    }
    return ret == ESP_OK;
}
