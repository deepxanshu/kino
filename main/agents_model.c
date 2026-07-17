/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// kino: demo data used until the Mac companion streams real sessions over serial.
// Mirrors the mockup so the on-device page matches what we designed.
static const agent_session_t s_demo[] = {
    {"kino-fw", AGENT_STATUS_ATTENTION, ""},
    {"bleu-api", AGENT_STATUS_RUNNING, ""},
    {"site-brief", AGENT_STATUS_RUNNING, ""},
    {"granola", AGENT_STATUS_IDLE, ""},
    {"deploy", AGENT_STATUS_ERROR, ""},
};

static agent_session_t s_live[AGENTS_MAX];
static size_t s_live_count = 0;
static bool s_have_live = false;
static SemaphoreHandle_t s_lock = NULL;

void agents_model_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void agents_model_set(const agent_session_t *sessions, size_t count)
{
    if (s_lock == NULL) {
        agents_model_init();
    }
    if (sessions == NULL) {
        count = 0;
    }
    if (count > AGENTS_MAX) {
        count = AGENTS_MAX;
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    for (size_t i = 0; i < count; ++i) {
        s_live[i] = sessions[i];
    }
    s_live_count = count;
    s_have_live = true;
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

size_t agents_model_get(agent_session_t *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }
    if (s_lock == NULL) {
        agents_model_init();
    }

    size_t n = 0;
    bool have_live = false;
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    have_live = s_have_live;
    if (have_live) {
        n = s_live_count < max ? s_live_count : max;
        for (size_t i = 0; i < n; ++i) {
            out[i] = s_live[i];
        }
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }

    if (!have_live) {
        n = sizeof(s_demo) / sizeof(s_demo[0]);
        if (n > max) {
            n = max;
        }
        for (size_t i = 0; i < n; ++i) {
            out[i] = s_demo[i];
        }
    }
    return n;
}
