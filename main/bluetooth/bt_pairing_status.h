/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BT_PAIRING_STATUS_H
#define BT_PAIRING_STATUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_PAIRING_STATE_DISCOVERABLE,
    BT_PAIRING_STATE_PAIRING,
    BT_PAIRING_STATE_PAIRED,
} bt_pairing_state_t;

bt_pairing_state_t bt_pairing_status_resolve(bool discoverable, bool pairing_active,
                                             bool paired_or_connected);
const char *bt_pairing_status_text(bt_pairing_state_t state);

#ifdef __cplusplus
}
#endif

#endif  // BT_PAIRING_STATUS_H
