/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGENTS_MODEL_H
#define AGENTS_MODEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// kino: agent-session model for the Agents page. Phase 2 returns hardcoded demo
// data so the UI + joystick navigation can be built with no Mac dependency; a
// later phase swaps agents_model_get() for data streamed over USB-serial from a
// Mac companion that watches Codex projects/threads.

typedef enum {
    AGENT_STATUS_RUNNING = 0,    // actively working
    AGENT_STATUS_ATTENTION,      // waiting on the user / needs a look
    AGENT_STATUS_IDLE,           // alive but not doing anything
    AGENT_STATUS_ERROR,          // failed / errored
} agent_status_t;

#define AGENTS_MAX 12
#define AGENT_NAME_LEN 20
#define AGENT_ID_LEN 40  // Codex conversationId (UUID) used for the codex:// deep link

typedef struct {
    char name[AGENT_NAME_LEN];
    agent_status_t status;
    char id[AGENT_ID_LEN];  // conversationId; empty for demo entries
} agent_session_t;

// Call once at startup (creates the internal lock) before any get/set.
void agents_model_init(void);

// Replace the live session list (called by the serial reader when the Mac
// companion sends an update). Until the first set() call, get() returns demo data.
void agents_model_set(const agent_session_t *sessions, size_t count);

// Copies the current sessions into `out` (capacity `max`), returns the count.
// Returns live serial data once any has arrived, otherwise the demo list.
size_t agents_model_get(agent_session_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif  // AGENTS_MODEL_H
