/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGENTS_SERIAL_H
#define AGENTS_SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

// Task: reads newline-delimited "@A|name~S|name~S|..." frames from the USB-serial
// (UART0) and updates agents_model with the live Codex session list. S = status
// char: R running, W waiting/attention, E error, anything else = idle.
void handle_agents_serial(void *pvParam);

#ifdef __cplusplus
}
#endif

#endif  // AGENTS_SERIAL_H
