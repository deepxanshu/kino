/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "bt_pairing_status.h"

bt_pairing_state_t bt_pairing_status_resolve(bool discoverable, bool pairing_active,
                                             bool paired_or_connected)
{
    if (pairing_active) {
        return BT_PAIRING_STATE_PAIRING;
    }
    if (paired_or_connected && !discoverable) {
        return BT_PAIRING_STATE_PAIRED;
    }
    return BT_PAIRING_STATE_DISCOVERABLE;
}

const char *bt_pairing_status_text(bt_pairing_state_t state)
{
    switch (state) {
    case BT_PAIRING_STATE_PAIRING:
        return "Pairing";
    case BT_PAIRING_STATE_PAIRED:
        return "Paired";
    case BT_PAIRING_STATE_DISCOVERABLE:
    default:
        return "Discoverable";
    }
}
