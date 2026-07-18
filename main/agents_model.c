/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static agent_session_t s_live[AGENTS_MAX];
static size_t s_live_count = 0;
static bool s_have_live = false;
static char s_selected_id[AGENT_ID_LEN] = "";
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

    // kino: no demo fallback. If no live frame has arrived yet, n stays 0 so the
    // UI shows a clear "waiting for feed" state instead of misleading fake data.
    (void)have_live;
    return n;
}

void agents_model_set_selected(const char *id)
{
    if (s_lock == NULL) {
        agents_model_init();
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    if (id != NULL) {
        strncpy(s_selected_id, id, AGENT_ID_LEN - 1);
        s_selected_id[AGENT_ID_LEN - 1] = '\0';
    } else {
        s_selected_id[0] = '\0';
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

size_t agents_model_get_selected(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }
    if (s_lock == NULL) {
        agents_model_init();
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    strncpy(out, s_selected_id, max - 1);
    out[max - 1] = '\0';
    size_t n = strlen(out);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
    return n;
}
