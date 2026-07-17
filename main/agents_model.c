/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_model.h"

// kino: Phase 2 demo data. Mirrors the mockup so the on-device page matches what
// we designed. Replace this with serial-fed sessions from the Mac companion.
static const agent_session_t s_demo[] = {
    {"kino-fw", AGENT_STATUS_ATTENTION},
    {"bleu-api", AGENT_STATUS_RUNNING},
    {"site-brief", AGENT_STATUS_RUNNING},
    {"granola", AGENT_STATUS_IDLE},
    {"deploy", AGENT_STATUS_ERROR},
};

size_t agents_model_get(agent_session_t *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }
    size_t n = sizeof(s_demo) / sizeof(s_demo[0]);
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = s_demo[i];
    }
    return n;
}
