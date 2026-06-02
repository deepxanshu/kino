/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mic_spectrum.h"

#include <math.h>
#include <string.h>

#define MIC_SPECTRUM_PI (3.14159265358979323846f)
#define MIC_SPECTRUM_DB_FLOOR (-90)

static const uint16_t s_band_freqs[MIC_SPECTRUM_BANDS] = {
    120,  180,  260,  380,  520,  700,  950,  1250,
    1650, 2150, 2800, 3600, 4600, 5700, 6800, 7600,
};

static uint8_t clamp_bar(float value, uint8_t max_bar_height)
{
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= (float)max_bar_height) {
        return max_bar_height;
    }
    return (uint8_t)(value + 0.5f);
}

static uint8_t magnitude_to_bar(float magnitude, uint8_t max_bar_height)
{
    if (magnitude < 4.0f || max_bar_height == 0) {
        return 0;
    }

    float db = 20.0f * log10f(magnitude / 32768.0f);
    if (db <= (float)MIC_SPECTRUM_DB_FLOOR) {
        return 0;
    }
    if (db > 0.0f) {
        db = 0.0f;
    }

    float normalized = (db - (float)MIC_SPECTRUM_DB_FLOOR) / (0.0f - (float)MIC_SPECTRUM_DB_FLOOR);
    return clamp_bar(normalized * (float)max_bar_height, max_bar_height);
}

static float goertzel_magnitude(const int16_t *samples, size_t sample_count, uint32_t sample_rate, float target_freq,
                                float dc_offset)
{
    float k      = 0.5f + ((float)sample_count * target_freq / (float)sample_rate);
    float omega  = 2.0f * MIC_SPECTRUM_PI * k / (float)sample_count;
    float coeff  = 2.0f * cosf(omega);
    float prev   = 0.0f;
    float prev_2 = 0.0f;

    for (size_t i = 0; i < sample_count; i++) {
        float sample = (float)samples[i] - dc_offset;
        float value  = sample + coeff * prev - prev_2;
        prev_2       = prev;
        prev         = value;
    }

    float power = prev_2 * prev_2 + prev * prev - coeff * prev * prev_2;
    if (power <= 0.0f) {
        return 0.0f;
    }
    return sqrtf(power) / (float)sample_count;
}

void mic_spectrum_compute(const int16_t *samples, size_t sample_count, uint32_t sample_rate, uint8_t max_bar_height,
                          mic_spectrum_data_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->db = MIC_SPECTRUM_DB_FLOOR;

    if (samples == NULL || sample_count == 0 || sample_rate == 0) {
        return;
    }

    int64_t sum       = 0;
    uint64_t sum_sq   = 0;
    uint16_t peak_abs = 0;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = samples[i];
        int32_t abs_v  = sample < 0 ? -sample : sample;
        if (abs_v > peak_abs) {
            peak_abs = (uint16_t)abs_v;
        }
        sum += sample;
        sum_sq += (uint64_t)(sample * sample);
    }

    float dc_offset = (float)sum / (float)sample_count;
    float rms       = sqrtf((float)sum_sq / (float)sample_count);

    out->peak = peak_abs;
    out->rms  = (rms > 65535.0f) ? 65535 : (uint16_t)(rms + 0.5f);
    if (out->rms > 0) {
        float db = 20.0f * log10f(rms / 32768.0f);
        if (db < (float)MIC_SPECTRUM_DB_FLOOR) {
            db = (float)MIC_SPECTRUM_DB_FLOOR;
        }
        if (db > 0.0f) {
            db = 0.0f;
        }
        out->db = (int8_t)(db + (db < 0.0f ? -0.5f : 0.5f));
    }

    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        float freq = (float)s_band_freqs[i];
        if (freq >= ((float)sample_rate * 0.5f)) {
            out->bars[i] = 0;
            continue;
        }
        float magnitude = goertzel_magnitude(samples, sample_count, sample_rate, freq, dc_offset);
        out->bars[i]    = magnitude_to_bar(magnitude, max_bar_height);
    }
}
