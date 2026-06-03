/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mic_handle.h"

#include "M5Unified.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "../bluetooth/bt_input.h"
#include "../joystick/joystick_basic.h"
#include "../mic/mic_spectrum.h"
#include "../ui/ui_mic_screen.h"
}

#define MIC_SAMPLE_RATE 8000
#define MIC_SAMPLE_COUNT 160
#define MIC_UI_UPDATE_MS 100

static const char *TAG = "mic_screen";

static volatile bool s_mic_active = false;
static volatile bool s_mic_muted  = false;
static volatile bool s_mic_busy   = false;

void mic_mode_enter(void)
{
    if (s_mic_active) {
        return;
    }

    if (!M5.Mic.isEnabled()) {
        ESP_LOGW(TAG, "Internal mic is not enabled");
        s_mic_active = false;
        return;
    }

    auto cfg          = M5.Mic.config();
    cfg.sample_rate   = MIC_SAMPLE_RATE;
    cfg.dma_buf_len   = 128;
    cfg.dma_buf_count = 4;
    cfg.over_sampling = 1;
    M5.Mic.config(cfg);

    s_mic_muted  = false;
    s_mic_active = M5.Mic.begin();
    bt_input_hfp_mic_set_enabled(s_mic_active);
    bt_input_hfp_audio_reset();
    if (s_mic_active) {
        bt_input_hfp_audio_request();
    }
    ESP_LOGI(TAG, "Mic %s", s_mic_active ? "started" : "failed to start");
}

void mic_mode_exit(void)
{
    s_mic_active = false;
    s_mic_muted  = true;
    bt_input_hfp_mic_set_enabled(false);
    bt_input_hfp_audio_disconnect();

    M5.Mic.end();

    for (int i = 0; i < 200 && s_mic_busy; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGI(TAG, "Mic stopped");
}

void mic_mode_toggle_muted(void)
{
    s_mic_muted = !s_mic_muted;
    bt_input_hfp_mic_set_enabled(!s_mic_muted && s_mic_active);
}

void handle_mic_screen(void *pvParam)
{
    joystick_data_t *joystick_data = (joystick_data_t *)pvParam;
    static int16_t samples[MIC_SAMPLE_COUNT];
    static mic_spectrum_data_t spectrum;
    static mic_spectrum_data_t empty_spectrum;
    static TickType_t last_ui_update = 0;
    empty_spectrum.db = -90;

    while (1) {
        if (joystick_data->screen_mode != MODE_MIC) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!s_mic_active || s_mic_muted) {
            update_mic_screen(&empty_spectrum, joystick_data->bat, s_mic_active, s_mic_muted,
                              bt_input_hfp_connected(), bt_input_hfp_audio_connected());
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        s_mic_busy = true;
        bool recorded = M5.Mic.record(samples, MIC_SAMPLE_COUNT, MIC_SAMPLE_RATE, false);
        if (recorded) {
            while (M5.Mic.isRecording() && s_mic_active && joystick_data->screen_mode == MODE_MIC) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        if (!s_mic_active || joystick_data->screen_mode != MODE_MIC) {
            s_mic_busy = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (recorded && s_mic_active && joystick_data->screen_mode == MODE_MIC) {
            bt_input_hfp_feed_pcm(samples, MIC_SAMPLE_COUNT);

            TickType_t now = xTaskGetTickCount();
            if ((now - last_ui_update) >= pdMS_TO_TICKS(MIC_UI_UPDATE_MS)) {
                mic_spectrum_compute(samples, MIC_SAMPLE_COUNT, MIC_SAMPLE_RATE, MIC_BAR_MAX_HEIGHT, &spectrum);
                update_mic_screen(&spectrum, joystick_data->bat, true, false,
                                  bt_input_hfp_connected(), bt_input_hfp_audio_connected());
                last_ui_update = now;
            }
        } else {
            update_mic_screen(&empty_spectrum, joystick_data->bat, false, false,
                              bt_input_hfp_connected(), bt_input_hfp_audio_connected());
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_mic_busy = false;
    }
}
