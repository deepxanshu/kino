/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGENTS_PROTO_H
#define AGENTS_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

// Parse one "@A|name~S~id|name~S~id|..." frame into agents_model (mutates line).
// Shared by the USB-serial reader and the WiFi/TCP reader.
void agents_proto_parse_frame(char *line);

#ifdef __cplusplus
}
#endif

#endif  // AGENTS_PROTO_H
