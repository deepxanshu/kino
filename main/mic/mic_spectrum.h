/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MIC_SPECTRUM_H
#define MIC_SPECTRUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define MIC_SPECTRUM_BANDS (16)

typedef struct {
    uint8_t bars[MIC_SPECTRUM_BANDS];
    uint16_t rms;
    uint16_t peak;
    int8_t db;
} mic_spectrum_data_t;

void mic_spectrum_compute(const int16_t *samples, size_t sample_count, uint32_t sample_rate, uint8_t max_bar_height,
                          mic_spectrum_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
