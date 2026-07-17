/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_serial.h"
#include "agents_model.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "agents_serial";

// UART0 is the USB-serial console port. We install the driver for RX only and
// read frames the Mac companion writes; console TX logging keeps working.
#define AGENTS_UART      UART_NUM_0
#define AGENTS_LINE_MAX  512
#define AGENTS_RX_BUF    2048

static agent_status_t status_from_char(char c)
{
    switch (c) {
    case 'R':
    case 'r':
        return AGENT_STATUS_RUNNING;
    case 'W':
    case 'w':
        return AGENT_STATUS_ATTENTION;
    case 'E':
    case 'e':
        return AGENT_STATUS_ERROR;
    default:
        return AGENT_STATUS_IDLE;
    }
}

// Parse "@A|name~S~id|name~S~id|..." (line is mutated by strtok_r).
// id (the Codex conversationId) is optional; older frames without it still parse.
static void parse_frame(char *line)
{
    if (strncmp(line, "@A", 2) != 0) {
        return;
    }
    agent_session_t sessions[AGENTS_MAX];
    size_t count = 0;

    char *save = NULL;
    char *tok = strtok_r(line, "|", &save);  // first token: "@A"
    while ((tok = strtok_r(NULL, "|", &save)) != NULL && count < AGENTS_MAX) {
        char *t1 = strchr(tok, '~');
        if (t1 == NULL || t1 == tok) {
            continue;
        }
        *t1 = '\0';
        const char *name = tok;
        char *rest = t1 + 1;  // "S" or "S~id"
        char sc = rest[0];
        const char *id = "";
        char *t2 = strchr(rest, '~');
        if (t2 != NULL) {
            *t2 = '\0';
            id = t2 + 1;
        }
        strncpy(sessions[count].name, name, AGENT_NAME_LEN - 1);
        sessions[count].name[AGENT_NAME_LEN - 1] = '\0';
        sessions[count].status = status_from_char(sc);
        strncpy(sessions[count].id, id, AGENT_ID_LEN - 1);
        sessions[count].id[AGENT_ID_LEN - 1] = '\0';
        count++;
    }

    agents_model_set(sessions, count);
    ESP_LOGI(TAG, "serial update: %u sessions", (unsigned)count);
}

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
                parse_frame(line);
                len = 0;
            }
        } else if (len < AGENTS_LINE_MAX - 1) {
            line[len++] = (char)c;
        } else {
            len = 0;  // overflow guard
        }
    }
}
