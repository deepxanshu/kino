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

// Start the WiFi data layer: UDP socket on KINO_TCP_PORT + a broadcast beacon
// every 3s so the companion always knows the stick's address (no mDNS, no TCP).
// The companion sends "@A|..." datagrams; the stick replies "@SEL <id>" to the
// last sender. Connectionless and self-healing across IP changes/reboots.
void agents_net_start(void);

// Send a line to the last-seen companion (e.g. "@SEL <id>\n"). No-op if none.
void agents_net_send_line(const char *line);

bool agents_net_client_connected(void);

#ifdef __cplusplus
}
#endif

#endif  // AGENTS_NET_H
