/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mic_handle.h"

#include "M5Unified.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>

extern "C" {
#include "../app_state.h"
#include "../bluetooth/bt_input.h"
#include "../mic/mic_spectrum.h"
#include "../ui/ui_mic_screen.h"
}

#define MIC_CVSD_SAMPLE_RATE 8000
#define MIC_MSBC_SAMPLE_RATE 16000
#define MIC_FRAME_MS 20
#define MIC_SAMPLE_RATE_MAX MIC_MSBC_SAMPLE_RATE
#define MIC_SAMPLE_COUNT_MAX ((MIC_SAMPLE_RATE_MAX * MIC_FRAME_MS) / 1000)
#define MIC_UI_UPDATE_MS 100
#define MIC_STATS_LOG_MS 1000
#define MIC_HFP_GAIN_Q8 1024
#define MIC_HFP_LIMIT_START 24576
#define MIC_HFP_LIMIT_MAX (INT16_MAX - 16)

static const char *TAG = "mic_screen";

static volatile bool s_mic_active = false;
static volatile bool s_mic_muted  = false;
static volatile bool s_mic_busy   = false;
static uint32_t s_mic_frame_count;
static TickType_t s_mic_last_stats_log;
static int64_t s_mic_last_frame_end_us;
static uint32_t s_mic_hfp_limited_count;
static int32_t s_mic_hpf_prev_in;
static int32_t s_mic_hpf_prev_out;
static uint32_t s_mic_last_sample_rate;

static size_t mic_sample_count_for_rate(uint32_t sample_rate)
{
    size_t sample_count = (sample_rate * MIC_FRAME_MS) / 1000;
    if (sample_count == 0 || sample_count > MIC_SAMPLE_COUNT_MAX) {
        return MIC_SAMPLE_COUNT_MAX;
    }
    return sample_count;
}

static void log_mic_config(const char *stage, const m5::mic_config_t &cfg)
{
    ESP_LOGI(TAG,
             "%s mic cfg: pin_data=%d pin_ws=%d pin_bck=%d port=%d rate=%" PRIu32
             " dma_len=%u dma_count=%u oversampling=%u mag=%u noise_filter=%u task_prio=%u core=%u",
             stage, cfg.pin_data_in, cfg.pin_ws, cfg.pin_bck, (int)cfg.i2s_port, cfg.sample_rate,
             (unsigned)cfg.dma_buf_len, (unsigned)cfg.dma_buf_count, cfg.over_sampling, cfg.magnification,
             cfg.noise_filter_level, cfg.task_priority, cfg.task_pinned_core);
}

static void log_mic_frame_stats(const int16_t *samples, size_t sample_count, uint32_t sample_rate,
                                int64_t record_us, int64_t frame_gap_us)
{
    s_mic_frame_count++;

    TickType_t now = xTaskGetTickCount();
    if (s_mic_last_stats_log != 0 && (now - s_mic_last_stats_log) < pdMS_TO_TICKS(MIC_STATS_LOG_MS)) {
        return;
    }
    s_mic_last_stats_log = now;

    if (samples == NULL || sample_count == 0) {
        ESP_LOGW(TAG, "mic pcm stats skipped: samples=%p count=%u", samples, (unsigned)sample_count);
        return;
    }

    int64_t sum       = 0;
    uint64_t sum_sq   = 0;
    int32_t min_v     = INT16_MAX;
    int32_t max_v     = INT16_MIN;
    uint16_t peak_abs = 0;
    uint32_t clip_count = 0;
    uint32_t zero_count = 0;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = samples[i];
        int32_t abs_v  = sample < 0 ? -sample : sample;
        if (sample < min_v) {
            min_v = sample;
        }
        if (sample > max_v) {
            max_v = sample;
        }
        if (abs_v > peak_abs) {
            peak_abs = (uint16_t)abs_v;
        }
        if (abs_v >= 32000) {
            clip_count++;
        }
        if (sample == 0) {
            zero_count++;
        }
        sum += sample;
        sum_sq += (uint64_t)(sample * sample);
    }

    double dc_offset = (double)sum / (double)sample_count;
    double rms       = sqrt((double)sum_sq / (double)sample_count);
    double db        = -90.0;
    if (rms > 0.0) {
        db = 20.0 * log10(rms / 32768.0);
        if (db < -90.0) {
            db = -90.0;
        } else if (db > 0.0) {
            db = 0.0;
        }
    }

    ESP_LOGI(TAG,
             "mic pcm stats: frames=%" PRIu32 " samples=%u rate=%" PRIu32 " codec=%s rec_us=%" PRId64
             " gap_us=%" PRId64
             " rms=%.1f db=%.1f peak=%u min=%" PRId32 " max=%" PRId32
             " dc=%.1f clip=%" PRIu32 " zero=%" PRIu32 " hfp_gain=12dB limited=%" PRIu32
             " hfp=%s audio=%s muted=%d",
             s_mic_frame_count, (unsigned)sample_count, sample_rate, bt_input_hfp_codec_text(), record_us,
             frame_gap_us, rms, db, peak_abs, min_v, max_v, dc_offset, clip_count, zero_count,
             s_mic_hfp_limited_count, bt_input_hfp_status_text(), bt_input_audio_status_text(), s_mic_muted);
    s_mic_hfp_limited_count = 0;
}

static void log_mic_record_issue(const char *reason, int64_t elapsed_us)
{
    TickType_t now = xTaskGetTickCount();
    if (s_mic_last_stats_log != 0 && (now - s_mic_last_stats_log) < pdMS_TO_TICKS(MIC_STATS_LOG_MS)) {
        return;
    }
    s_mic_last_stats_log = now;
    ESP_LOGW(TAG, "mic record issue: %s active=%d muted=%d busy=%d mode=%u elapsed_us=%" PRId64,
             reason, s_mic_active, s_mic_muted, s_mic_busy, app_state_get_mode(), elapsed_us);
}

static int16_t mic_limit_sample(int32_t sample, uint32_t *limited_count)
{
    if (sample > MIC_HFP_LIMIT_START) {
        sample = MIC_HFP_LIMIT_START + ((sample - MIC_HFP_LIMIT_START) >> 3);
        if (limited_count != NULL) {
            (*limited_count)++;
        }
    } else if (sample < -MIC_HFP_LIMIT_START) {
        sample = -MIC_HFP_LIMIT_START + ((sample + MIC_HFP_LIMIT_START) >> 3);
        if (limited_count != NULL) {
            (*limited_count)++;
        }
    }

    if (sample > MIC_HFP_LIMIT_MAX) {
        return MIC_HFP_LIMIT_MAX;
    }
    if (sample < -MIC_HFP_LIMIT_MAX) {
        return -MIC_HFP_LIMIT_MAX;
    }
    return (int16_t)sample;
}

static void mic_prepare_hfp_samples(const int16_t *in, int16_t *out, size_t sample_count)
{
    uint32_t limited_count = 0;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = in[i];
        int32_t high_passed = sample - s_mic_hpf_prev_in + ((s_mic_hpf_prev_out * 255) >> 8);
        s_mic_hpf_prev_in = sample;
        s_mic_hpf_prev_out = high_passed;

        int32_t gained = (high_passed * MIC_HFP_GAIN_Q8) >> 8;
        out[i] = mic_limit_sample(gained, &limited_count);
    }

    s_mic_hfp_limited_count += limited_count;
}

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
    cfg.sample_rate   = bt_input_hfp_pcm_sample_rate();
    cfg.dma_buf_len   = 128;
    cfg.dma_buf_count = 8;
    cfg.over_sampling = 1;
    M5.Mic.config(cfg);
    log_mic_config("enter", cfg);

    s_mic_muted  = false;
    s_mic_active = M5.Mic.begin();
    s_mic_frame_count = 0;
    s_mic_last_stats_log = 0;
    s_mic_last_frame_end_us = 0;
    s_mic_hfp_limited_count = 0;
    s_mic_hpf_prev_in = 0;
    s_mic_hpf_prev_out = 0;
    s_mic_last_sample_rate = cfg.sample_rate;
    bt_input_hfp_audio_reset();
    bt_input_hfp_mic_set_enabled(s_mic_active);
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
    (void)pvParam;
    static int16_t samples[MIC_SAMPLE_COUNT_MAX];
    static int16_t hfp_samples[MIC_SAMPLE_COUNT_MAX];
    static mic_spectrum_data_t spectrum;
    static mic_spectrum_data_t empty_spectrum;
    static TickType_t last_ui_update = 0;
    empty_spectrum.db = -90;

    while (1) {
        if (app_state_get_mode() != MODE_MIC) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        joystick_data_t snapshot = app_state_snapshot();
        if (!s_mic_active || s_mic_muted) {
            uint32_t sample_rate = bt_input_hfp_pcm_sample_rate();
            update_mic_screen(&empty_spectrum, snapshot.bat, s_mic_active, s_mic_muted,
                              sample_rate,
                              bt_input_hfp_connected(), bt_input_hfp_audio_connected());
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint32_t sample_rate = bt_input_hfp_pcm_sample_rate();
        size_t sample_count = mic_sample_count_for_rate(sample_rate);
        if (sample_rate != s_mic_last_sample_rate) {
            ESP_LOGI(TAG, "mic sample rate switch: %" PRIu32 " -> %" PRIu32 " codec=%s",
                     s_mic_last_sample_rate, sample_rate, bt_input_hfp_codec_text());
            s_mic_last_sample_rate = sample_rate;
            s_mic_last_frame_end_us = 0;
            s_mic_hpf_prev_in = 0;
            s_mic_hpf_prev_out = 0;
        }

        s_mic_busy = true;
        int64_t record_start_us = esp_timer_get_time();
        bool recorded = M5.Mic.record(samples, sample_count, sample_rate, false);
        if (recorded) {
            while (M5.Mic.isRecording() && s_mic_active && app_state_get_mode() == MODE_MIC) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        int64_t record_end_us = esp_timer_get_time();
        int64_t record_us = record_end_us - record_start_us;
        int64_t frame_gap_us = s_mic_last_frame_end_us == 0 ? 0 : record_end_us - s_mic_last_frame_end_us;
        s_mic_last_frame_end_us = record_end_us;

        if (!s_mic_active || app_state_get_mode() != MODE_MIC) {
            s_mic_busy = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (recorded && s_mic_active && app_state_get_mode() == MODE_MIC) {
            mic_prepare_hfp_samples(samples, hfp_samples, sample_count);
            log_mic_frame_stats(samples, sample_count, sample_rate, record_us, frame_gap_us);
            bt_input_hfp_feed_pcm(hfp_samples, sample_count);

            TickType_t now = xTaskGetTickCount();
            if ((now - last_ui_update) >= pdMS_TO_TICKS(MIC_UI_UPDATE_MS)) {
                snapshot = app_state_snapshot();
                mic_spectrum_compute(samples, sample_count, sample_rate, MIC_BAR_MAX_HEIGHT, &spectrum);
                update_mic_screen(&spectrum, snapshot.bat, true, false, sample_rate,
                                  bt_input_hfp_connected(), bt_input_hfp_audio_connected());
                last_ui_update = now;
            }
        } else {
            log_mic_record_issue("record returned false", record_us);
            snapshot = app_state_snapshot();
            sample_rate = bt_input_hfp_pcm_sample_rate();
            update_mic_screen(&empty_spectrum, snapshot.bat, false, false,
                              sample_rate,
                              bt_input_hfp_connected(), bt_input_hfp_audio_connected());
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_mic_busy = false;
    }
}
