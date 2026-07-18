/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_serial.h"
#include "agents_proto.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "agents_serial";

// UART0 is the USB-serial console port. Install the driver for RX and read the
// "@A|name~S~id|..." frames the Mac companion writes; console TX keeps working.
#define AGENTS_UART      UART_NUM_0
#define AGENTS_LINE_MAX  512
#define AGENTS_RX_BUF    2048

void handle_agents_serial(void *pvParam)
{
    (void)pvParam;

    if (!uart_is_driver_installed(AGENTS_UART)) {
        esp_err_t err = uart_driver_install(AGENTS_UART, AGENTS_RX_BUF, 0, 0, NULL, 0);
        ESP_LOGI(TAG, "uart_driver_install ret=%d", err);
    }

    char line[AGENTS_LINE_MAX];
    size_t len = 0;
    uint8_t c;

    while (1) {
        int n = uart_read_bytes(AGENTS_UART, &c, 1, pdMS_TO_TICKS(500));
        if (n != 1) {
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                line[len] = '\0';
                agents_proto_parse_frame(line);
                len = 0;
            }
        } else if (len < AGENTS_LINE_MAX - 1) {
            line[len++] = (char)c;
        } else {
            len = 0;  // overflow guard
        }
    }
}
