#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mic_spectrum.h"

#define SAMPLE_RATE 16000
#define SAMPLE_COUNT 256
#define MAX_BAR_HEIGHT 100

static int failures;

static void expect_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("FAIL %s: got %d expected %d\n", name, actual, expected);
        failures++;
    }
}

static void expect_true(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static void fill_sine(int16_t *samples, float freq_hz, float amplitude)
{
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        float phase = 2.0f * (float)M_PI * freq_hz * (float)i / (float)SAMPLE_RATE;
        samples[i] = (int16_t)(sinf(phase) * amplitude);
    }
}

static void test_silence_outputs_zero_bars(void)
{
    int16_t samples[SAMPLE_COUNT] = {0};
    mic_spectrum_data_t data;
    memset(&data, 0x7f, sizeof(data));

    mic_spectrum_compute(samples, SAMPLE_COUNT, SAMPLE_RATE, MAX_BAR_HEIGHT, &data);

    expect_int("silence rms", data.rms, 0);
    expect_int("silence peak", data.peak, 0);
    expect_true("silence db floor", data.db <= -90);
    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        expect_int("silence bar", data.bars[i], 0);
    }
}

static void test_sine_produces_visible_bars_with_height_limit(void)
{
    int16_t samples[SAMPLE_COUNT];
    fill_sine(samples, 1000.0f, 12000.0f);

    mic_spectrum_data_t data;
    memset(&data, 0, sizeof(data));
    mic_spectrum_compute(samples, SAMPLE_COUNT, SAMPLE_RATE, MAX_BAR_HEIGHT, &data);

    uint8_t max_bar = 0;
    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        if (data.bars[i] > max_bar) {
            max_bar = data.bars[i];
        }
        expect_true("bar height limit", data.bars[i] <= MAX_BAR_HEIGHT);
    }

    expect_true("sine rms nonzero", data.rms > 1000);
    expect_true("sine peak nonzero", data.peak > 10000);
    expect_true("sine visible bar", max_bar > 10);
}

static void test_louder_signal_has_higher_rms(void)
{
    int16_t quiet[SAMPLE_COUNT];
    int16_t loud[SAMPLE_COUNT];
    mic_spectrum_data_t quiet_data;
    mic_spectrum_data_t loud_data;

    fill_sine(quiet, 1000.0f, 2000.0f);
    fill_sine(loud, 1000.0f, 14000.0f);

    mic_spectrum_compute(quiet, SAMPLE_COUNT, SAMPLE_RATE, MAX_BAR_HEIGHT, &quiet_data);
    mic_spectrum_compute(loud, SAMPLE_COUNT, SAMPLE_RATE, MAX_BAR_HEIGHT, &loud_data);

    expect_true("louder rms", loud_data.rms > quiet_data.rms);
    expect_true("louder db", loud_data.db > quiet_data.db);
}

int main(void)
{
    test_silence_outputs_zero_bars();
    test_sine_produces_visible_bars_with_height_limit();
    test_louder_signal_has_higher_rms();

    if (failures) {
        printf("%d test failure(s)\n", failures);
        return 1;
    }

    printf("mic_spectrum tests passed\n");
    return 0;
}
