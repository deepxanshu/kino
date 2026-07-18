/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#include "agents_proto.h"
#include "agents_model.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "agents_proto";

static agent_status_t status_from_char(char c)
{
    switch (c) {
    case 'R':
    case 'r':
        return AGENT_STATUS_RUNNING;
    case 'W':
    case 'w':
        return AGENT_STATUS_ATTENTION;
    case 'E':
    case 'e':
        return AGENT_STATUS_ERROR;
    default:
        return AGENT_STATUS_IDLE;
    }
}

void agents_proto_parse_frame(char *line)
{
    if (line == NULL || strncmp(line, "@A", 2) != 0) {
        return;
    }
    agent_session_t sessions[AGENTS_MAX];
    size_t count = 0;

    char *save = NULL;
    char *tok = strtok_r(line, "|", &save);  // first token: "@A"
    while ((tok = strtok_r(NULL, "|", &save)) != NULL && count < AGENTS_MAX) {
        char *t1 = strchr(tok, '~');
        if (t1 == NULL || t1 == tok) {
            continue;
        }
        *t1 = '\0';
        const char *name = tok;
        char *rest = t1 + 1;  // "S" or "S~id"
        char sc = rest[0];
        const char *id = "";
        char *t2 = strchr(rest, '~');
        if (t2 != NULL) {
            *t2 = '\0';
            id = t2 + 1;
        }
        strncpy(sessions[count].name, name, AGENT_NAME_LEN - 1);
        sessions[count].name[AGENT_NAME_LEN - 1] = '\0';
        sessions[count].status = status_from_char(sc);
        strncpy(sessions[count].id, id, AGENT_ID_LEN - 1);
        sessions[count].id[AGENT_ID_LEN - 1] = '\0';
        count++;
    }

    agents_model_set(sessions, count);
    ESP_LOGI(TAG, "update: %u sessions", (unsigned)count);
}
