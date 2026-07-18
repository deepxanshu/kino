/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_net.h"
#include "agents_proto.h"
#include "wifi_conn.h"
#include "wifi_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include <string.h>

static const char *TAG = "agents_net";
#define NET_LINE_MAX 512

static int s_client_fd = -1;
static SemaphoreHandle_t s_lock = NULL;

void agents_net_send_line(const char *line)
{
    if (line == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_client_fd >= 0) {
        send(s_client_fd, line, strlen(line), 0);
    }
    xSemaphoreGive(s_lock);
}

bool agents_net_client_connected(void)
{
    return s_client_fd >= 0;
}

static void set_client(int fd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_client_fd = fd;
    xSemaphoreGive(s_lock);
}

static void mdns_start(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed");
        return;
    }
    mdns_hostname_set(KINO_MDNS_HOST);          // -> <host>.local
    mdns_instance_name_set("kino agent stick");
    mdns_service_add(NULL, "_kino", "_tcp", KINO_TCP_PORT, NULL, 0);
    ESP_LOGI(TAG, "mdns: %s.local advertised, port %d", KINO_MDNS_HOST, KINO_TCP_PORT);
}

static void net_task(void *arg)
{
    (void)arg;
    // wait for WiFi so mDNS/socket bind on a real interface
    while (!wifi_conn_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    mdns_start();

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(KINO_TCP_PORT),
    };
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen failed on port %d", KINO_TCP_PORT);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TCP server listening on %d", KINO_TCP_PORT);

    char line[NET_LINE_MAX];
    size_t len = 0;
    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int client = accept(listen_sock, (struct sockaddr *)&src, &slen);
        if (client < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        ESP_LOGI(TAG, "companion connected");
        set_client(client);
        len = 0;
        uint8_t buf[128];
        int r;
        while ((r = recv(client, buf, sizeof(buf), 0)) > 0) {
            for (int i = 0; i < r; ++i) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (len > 0) {
                        line[len] = '\0';
                        agents_proto_parse_frame(line);
                        len = 0;
                    }
                } else if (len < NET_LINE_MAX - 1) {
                    line[len++] = c;
                } else {
                    len = 0;
                }
            }
        }
        ESP_LOGW(TAG, "companion disconnected");
        set_client(-1);
        close(client);
    }
}

void agents_net_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    xTaskCreate(net_task, "agents_net", 5120, NULL, 5, NULL);
}
