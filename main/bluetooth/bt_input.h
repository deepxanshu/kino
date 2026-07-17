/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BT_INPUT_H
#define BT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_INPUT_DEVICE_NAME "Magic Stick"

void bt_input_init(void);

bool bt_input_hid_connected(void);
bool bt_input_hfp_audio_connected(void);
bool bt_input_is_discoverable(void);

const char *bt_input_hid_status_text(void);
const char *bt_input_hfp_status_text(void);
const char *bt_input_audio_status_text(void);
const char *bt_input_pairing_status_text(void);
const char *bt_input_hfp_codec_text(void);
uint32_t bt_input_hfp_pcm_sample_rate(void);

void bt_input_set_discoverable(bool discoverable);
void bt_input_clear_bonds(void);
void bt_input_mouse_send(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan);
void bt_input_dictation_tap(void);  // kino: send Ctrl+F5 (bind Wispr Flow's shortcut to Ctrl+F5)
void bt_input_escape_tap(void);     // kino: send a Keyboard Escape tap (cancel dictation)
void bt_input_enter_tap(void);      // kino: send a Keyboard Return/Enter tap (send/submit)

void bt_input_hfp_mic_set_enabled(bool enabled);
void bt_input_hfp_audio_reset(void);
void bt_input_hfp_feed_pcm(const int16_t *samples, size_t sample_count);

#ifdef __cplusplus
}
#endif

#endif  // BT_INPUT_H
