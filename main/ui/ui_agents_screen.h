/*
 * SPDX-FileCopyrightText: 2026 Deepanshu (kino fork)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _UI_AGENTS_SCREEN_H_
#define _UI_AGENTS_SCREEN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include "../agents_model.h"

void create_agents_screen(void);
bool ui_agents_screen_is_ready(void);
bool ui_agents_screen_load(bool animated);
void update_agents_screen(const agent_session_t *sessions, size_t count, int selected);
void ui_agents_screen_destory(void);

#ifdef __cplusplus
}
#endif

#endif  // _UI_AGENTS_SCREEN_H_
