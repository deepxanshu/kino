/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui.h"
#include <stdbool.h>
#include "esp_log.h"
#include "ui_setup_screen.h"
#include "ui_running_screen.h"
#include "ui_agents_screen.h"

typedef void (*ui_screen_action_t)(void);
typedef bool (*ui_screen_check_t)(void);
typedef bool (*ui_screen_load_t)(bool animated);

static bool load_screen(const char *name, ui_screen_action_t create, ui_screen_check_t is_ready,
                        ui_screen_load_t load, ui_screen_action_t destroy)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!is_ready()) {
            create();
            ESP_LOGI("UI", "%s screen created", name);
        }
        if (load(true)) {
            ESP_LOGI("UI", "%s screen loaded", name);
            return true;
        }

        ESP_LOGE("UI", "%s screen is NULL or invalid!", name);
        destroy();
    }
    return false;
}

/**
 * @brief Switches between different UI screens based on the provided screen ID
 *
 * This function manages the display of different UI screens (setup and Magic)
 * by checking if the requested screen exists, creating it if necessary, validating
 * the screen object, and then loading it with a slide-left animation effect.
 *
 * The function implements error handling by destroying and recreating invalid screens
 * using goto statements for retry logic. Each screen type has its own creation
 * and validation flow.
 *
 * @param screen_id An integer representing the target screen mode:
 *                  - MODE_SETUP: Configuration/setup screen
 *                  - MODE_RUNNING: Magic operational screen
 *                  - Any other value: Logs an error message
 *
 * @note The function uses LVGL's animation API to provide smooth screen transitions
 *       with a 200ms left slide animation. Thread safety should be considered when
 *       calling this function from different tasks.
 *
 * @warning This function relies on external screen objects and creation/destruction
 *          functions that must be implemented in other UI modules.
 */
void switch_screen(int screen_id)
{
    if (screen_id == MODE_SETUP) {
        load_screen("Setup", create_setup_screen, ui_setup_screen_is_ready,
                    ui_setup_screen_load, ui_setup_screen_destory);
    } else if (screen_id == MODE_RUNNING) {
        load_screen("Running", create_running_screen, ui_running_screen_is_ready,
                    ui_running_screen_load, ui_running_screen_destory);
    } else if (screen_id == MODE_AGENTS) {
        load_screen("Agents", create_agents_screen, ui_agents_screen_is_ready,
                    ui_agents_screen_load, ui_agents_screen_destory);
    } else {
        ESP_LOGE("UI", "Invalid screen mode!");
    }
}

/**
 * @brief Initialize the UI system by creating and loading the initial setup screen
 * @note This function serves as the entry point for UI initialization
 * @details
 *      1. Creates the setup screen using create_setup_screen()
 *      2. Immediately loads the setup screen as the current display
 *      3. Sets up the initial UI state for user interaction
 * @warning This function should only be called once during application startup
 */
void ui_init(void)
{
    create_setup_screen();
    if (!ui_setup_screen_load(false)) {
        ESP_LOGE("UI", "Initial setup screen load failed");
    }
}
