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
#include <string.h>

// kino: dead-simple UDP transport. The stick broadcasts a "KINO" beacon every
// 3s so the Mac companion always knows its address (survives DHCP changes, no
// mDNS, no TCP connections to break). The companion sends "@A|..." frames as
// datagrams to KINO_TCP_PORT; the stick remembers the sender and sends
// "@SEL <id>" datagrams back to it. Connectionless = self-healing.

static const char *TAG = "agents_net";
#define BEACON_PORT (KINO_TCP_PORT + 1)   // companion listens here
#define BEACON_MS   3000
#define FEED_FRESH_MS 8000

static int s_sock = -1;
static struct sockaddr_in s_peer;          // last companion that sent a frame
static bool s_have_peer = false;
static TickType_t s_last_frame_tick = 0;
static SemaphoreHandle_t s_lock = NULL;

void agents_net_send_line(const char *line)
{
    if (line == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sock >= 0 && s_have_peer) {
        // send 3x -- datagrams can drop and this is a user action
        for (int i = 0; i < 3; ++i) {
            sendto(s_sock, line, strlen(line), 0, (struct sockaddr *)&s_peer, sizeof(s_peer));
        }
    }
    xSemaphoreGive(s_lock);
}

bool agents_net_client_connected(void)
{
    return s_have_peer &&
           (xTaskGetTickCount() - s_last_frame_tick) < pdMS_TO_TICKS(FEED_FRESH_MS);
}

static void net_task(void *arg)
{
    (void)arg;
    while (!wifi_conn_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }
    int bc = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(KINO_TCP_PORT),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "bind %d failed", KINO_TCP_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = {.tv_sec = 0, .tv_usec = 300000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sock = sock;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "UDP up: data port %d, beacon -> %d every %dms", KINO_TCP_PORT, BEACON_PORT, BEACON_MS);

    struct sockaddr_in bcast = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
        .sin_port = htons(BEACON_PORT),
    };
    TickType_t last_beacon = 0;
    char buf[512];

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_beacon) >= pdMS_TO_TICKS(BEACON_MS)) {
            static const char beacon[] = "KINO v1";
            sendto(sock, beacon, sizeof(beacon) - 1, 0, (struct sockaddr *)&bcast, sizeof(bcast));
            last_beacon = now;
        }

        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int r = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&src, &slen);
        if (r <= 0) {
            continue;
        }
        buf[r] = '\0';
        // strip trailing newline(s)
        while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r')) {
            buf[--r] = '\0';
        }
        if (strncmp(buf, "@P", 2) == 0) {
            // discovery probe from the companion (it initiates because the Mac
            // firewall blocks unsolicited inbound UDP): reply + learn its addr.
            static const char hello[] = "KINO v1";
            sendto(sock, hello, sizeof(hello) - 1, 0, (struct sockaddr *)&src, slen);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_peer = src;
            s_have_peer = true;
            xSemaphoreGive(s_lock);
            continue;
        }
        if (strncmp(buf, "@A", 2) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_peer = src;
            s_have_peer = true;
            s_last_frame_tick = xTaskGetTickCount();
            xSemaphoreGive(s_lock);
            agents_proto_parse_frame(buf);
        }
    }
}

void agents_net_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    xTaskCreate(net_task, "agents_net", 4096, NULL, 5, NULL);
}
