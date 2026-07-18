/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGENTS_NET_H
#define AGENTS_NET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the WiFi data layer: advertise <KINO_MDNS_HOST>.local via mDNS and run a
// TCP server on KINO_TCP_PORT. The Mac companion connects and streams the agent
// list ("@A|..." frames); the device sends "@SEL <id>" back over the same socket.
void agents_net_start(void);

// Send a line to the connected companion (e.g. "@SEL <id>\n"). No-op if none.
void agents_net_send_line(const char *line);

bool agents_net_client_connected(void);

#ifdef __cplusplus
}
#endif

#endif  // AGENTS_NET_H
