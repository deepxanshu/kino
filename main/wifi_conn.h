/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start WiFi STA (non-blocking; auto-reconnects). Coexists with Classic BT.
// Credentials come from wifi_config.h (gitignored).
void wifi_conn_start(void);
bool wifi_conn_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_CONN_H
