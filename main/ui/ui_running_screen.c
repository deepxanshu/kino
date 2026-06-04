/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_running_screen.h"
#include "../lvgl_port.h"
#include "ui_theme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include <math.h>
#include <stdlib.h>

static lv_obj_t *running_screen   = NULL;
static lv_obj_t *joystick_dot     = NULL;
static lv_obj_t *joystick_area    = NULL;
static lv_obj_t *cube_container   = NULL;
static lv_obj_t *audio_area       = NULL;
static lv_obj_t *battery_label    = NULL;
static lv_obj_t *mouse_info_label = NULL;
static lv_obj_t *click_info_label = NULL;
static lv_obj_t *joy_info_label   = NULL;
static lv_obj_t *imu_info_label   = NULL;

#define MOUSE_PANEL_SIZE 58
#define MOUSE_DOT_SIZE   8
#define MAGIC_AUDIO_AREA_WIDTH  123
#define MAGIC_AUDIO_AREA_HEIGHT 44
#define MAGIC_AUDIO_BAR_WIDTH   4
#define MAGIC_AUDIO_BAR_GAP     2
#define MAGIC_AUDIO_BAR_BASE_Y  40

typedef struct {
    float x;
    float y;
    float z;
} point3d_t;

typedef struct {
    float x;
    float y;
} point2d_t;

static lv_obj_t *edge_lines[12];
static lv_obj_t *cross_lines[2];
static lv_obj_t *audio_bars[MIC_SPECTRUM_BANDS];
static lv_point_t edge_points[12][2];
static lv_point_t cross_points[2][2];
static uint8_t s_last_audio_bars[MIC_SPECTRUM_BANDS];
static lv_point_t s_audio_grid_points[2][2] = {
    {{0, 14}, {MAGIC_AUDIO_AREA_WIDTH, 14}},
    {{0, 28}, {MAGIC_AUDIO_AREA_WIDTH, 28}},
};

static const point3d_t vertices[8] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

static const int edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static float s_pitch = 0.0f;
static float s_roll  = 0.0f;

static int16_t map_range(int16_t value, int16_t in_min, int16_t in_max, int16_t out_min, int16_t out_max)
{
    return (int16_t)((value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);
}

static int16_t clamp_i16(int16_t value, int16_t min_value, int16_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static lv_obj_t *create_status_label(lv_obj_t *parent, const char *text, int16_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 120);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, y);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    ui_theme_apply_label(label);
    return label;
}

static void create_audio_grid_line(lv_obj_t *parent, int index)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, s_audio_grid_points[index], 2);
    lv_obj_set_style_line_color(line, ui_theme_grid_color(), 0);
    lv_obj_set_style_line_width(line, 1, 0);
}

static void update_magic_audio_bars(const mic_spectrum_data_t *spectrum)
{
    int16_t total_width = MIC_SPECTRUM_BANDS * MAGIC_AUDIO_BAR_WIDTH +
                          (MIC_SPECTRUM_BANDS - 1) * MAGIC_AUDIO_BAR_GAP;
    int16_t start_x = (MAGIC_AUDIO_AREA_WIDTH - total_width) / 2;

    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        uint8_t target = 0;
        if (spectrum != NULL) {
            target = spectrum->bars[i] > MAGIC_AUDIO_BAR_MAX_HEIGHT ? MAGIC_AUDIO_BAR_MAX_HEIGHT : spectrum->bars[i];
        }

        uint8_t height = (uint8_t)((s_last_audio_bars[i] * 3 + target) / 4);
        if (target > s_last_audio_bars[i]) {
            height = target;
        }
        s_last_audio_bars[i] = height;

        if (audio_bars[i] != NULL) {
            uint8_t draw_height = height == 0 ? 1 : height;
            lv_obj_set_size(audio_bars[i], MAGIC_AUDIO_BAR_WIDTH, draw_height);
            lv_obj_align(audio_bars[i], LV_ALIGN_TOP_LEFT, start_x + i * (MAGIC_AUDIO_BAR_WIDTH + MAGIC_AUDIO_BAR_GAP),
                         MAGIC_AUDIO_BAR_BASE_Y - draw_height);
        }
    }
}

static void update_mouse_imu_cube(float ax, float ay, float az)
{
    if (cube_container == NULL || !lv_obj_is_valid(cube_container)) {
        return;
    }
    if (isnan(ax) || isnan(ay) || isnan(az) || isinf(ax) || isinf(ay) || isinf(az)) {
        return;
    }

    float pitch = atan2f(ay, sqrtf(ax * ax + az * az));
    float roll  = atan2f(ax, sqrtf(ay * ay + az * az));
    if (az < 0.0f) {
        pitch = (float)M_PI - pitch;
    }
    s_pitch = pitch;
    s_roll  = roll;

    point2d_t projected[8];
    int center_x = lv_obj_get_width(cube_container) / 2;
    int center_y = lv_obj_get_height(cube_container) / 2;
    float scale  = 16.0f;

    for (int i = 0; i < 8; ++i) {
        point3d_t p = vertices[i];
        float y1    = p.y * cosf(pitch) - p.z * sinf(pitch);
        float z1    = p.y * sinf(pitch) + p.z * cosf(pitch);
        float x1    = p.x;
        float x2    = x1 * cosf(roll) + z1 * sinf(roll);

        projected[i].x = center_x + x2 * scale;
        projected[i].y = center_y + y1 * scale;
    }

    for (int i = 0; i < 12; ++i) {
        edge_points[i][0].x = (int16_t)projected[edges[i][0]].x;
        edge_points[i][0].y = (int16_t)projected[edges[i][0]].y;
        edge_points[i][1].x = (int16_t)projected[edges[i][1]].x;
        edge_points[i][1].y = (int16_t)projected[edges[i][1]].y;
        lv_line_set_points(edge_lines[i], edge_points[i], 2);
    }

    cross_points[0][0] = (lv_point_t){(int16_t)projected[0].x, (int16_t)projected[0].y};
    cross_points[0][1] = (lv_point_t){(int16_t)projected[2].x, (int16_t)projected[2].y};
    lv_line_set_points(cross_lines[0], cross_points[0], 2);

    cross_points[1][0] = (lv_point_t){(int16_t)projected[1].x, (int16_t)projected[1].y};
    cross_points[1][1] = (lv_point_t){(int16_t)projected[3].x, (int16_t)projected[3].y};
    lv_line_set_points(cross_lines[1], cross_points[1], 2);
}

/**
 * @brief Create the mouse screen with joystick and IMU visualization.
 */
void create_running_screen(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL) {
        ESP_LOGE("UI", "No default display found!");
        lvgl_port_unlock();
        return;
    }

    if (running_screen == NULL) {
        running_screen = lv_obj_create(NULL);
    }

    if (running_screen == NULL) {
        ESP_LOGE("UI", "Failed to create running screen!");
        lvgl_port_unlock();
        return;
    }

    lv_obj_clear_flag(running_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_screen(running_screen);

    lv_obj_t *label = lv_label_create(running_screen);
    lv_label_set_text(label, "Magic");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    ui_theme_apply_label(label);

    joystick_area = lv_obj_create(running_screen);
    lv_obj_set_size(joystick_area, MOUSE_PANEL_SIZE, MOUSE_PANEL_SIZE);
    lv_obj_align(joystick_area, LV_ALIGN_TOP_LEFT, 6, 32);
    lv_obj_set_style_border_width(joystick_area, 1, LV_PART_MAIN);
    ui_theme_apply_panel(joystick_area);
    lv_obj_set_style_pad_all(joystick_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(joystick_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cross_line_h        = lv_line_create(joystick_area);
    static lv_point_t points_h[2] = {{0, MOUSE_PANEL_SIZE / 2}, {MOUSE_PANEL_SIZE, MOUSE_PANEL_SIZE / 2}};
    lv_line_set_points(cross_line_h, points_h, 2);
    lv_obj_set_style_line_color(cross_line_h, ui_theme_grid_color(), 0);
    lv_obj_set_style_line_width(cross_line_h, 1, 0);

    lv_obj_t *cross_line_v        = lv_line_create(joystick_area);
    static lv_point_t points_v[2] = {{MOUSE_PANEL_SIZE / 2, 0}, {MOUSE_PANEL_SIZE / 2, MOUSE_PANEL_SIZE}};
    lv_line_set_points(cross_line_v, points_v, 2);
    lv_obj_set_style_line_color(cross_line_v, ui_theme_grid_color(), 0);
    lv_obj_set_style_line_width(cross_line_v, 1, 0);

    joystick_dot = lv_obj_create(joystick_area);
    lv_obj_set_size(joystick_dot, MOUSE_DOT_SIZE, MOUSE_DOT_SIZE);
    lv_obj_set_style_radius(joystick_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(joystick_dot, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_border_width(joystick_dot, 0, LV_PART_MAIN);
    lv_obj_align(joystick_dot, LV_ALIGN_CENTER, 0, 0);

    cube_container = lv_obj_create(running_screen);
    lv_obj_set_size(cube_container, MOUSE_PANEL_SIZE, MOUSE_PANEL_SIZE);
    lv_obj_align(cube_container, LV_ALIGN_TOP_LEFT, 71, 32);
    ui_theme_apply_panel(cube_container);
    lv_obj_set_style_border_width(cube_container, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cube_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cube_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 12; ++i) {
        edge_lines[i] = lv_line_create(cube_container);
        lv_obj_set_style_line_width(edge_lines[i], 2, 0);
        lv_obj_set_style_line_color(edge_lines[i], ui_theme_fg_color(), 0);
        lv_obj_add_flag(edge_lines[i], LV_OBJ_FLAG_FLOATING);
        static lv_point_t default_points[2] = {{0, 0}, {0, 0}};
        lv_line_set_points(edge_lines[i], default_points, 2);
    }

    for (int i = 0; i < 2; ++i) {
        cross_lines[i] = lv_line_create(cube_container);
        lv_obj_set_style_line_width(cross_lines[i], 1, 0);
        lv_obj_set_style_line_color(cross_lines[i], lv_color_make(0, 255, 255), 0);
        lv_obj_add_flag(cross_lines[i], LV_OBJ_FLAG_FLOATING);
        static lv_point_t default_points[2] = {{0, 0}, {0, 0}};
        lv_line_set_points(cross_lines[i], default_points, 2);
    }

    audio_area = lv_obj_create(running_screen);
    lv_obj_set_size(audio_area, MAGIC_AUDIO_AREA_WIDTH, MAGIC_AUDIO_AREA_HEIGHT);
    lv_obj_align(audio_area, LV_ALIGN_TOP_LEFT, 6, 96);
    ui_theme_apply_panel(audio_area);
    lv_obj_set_style_border_width(audio_area, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(audio_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(audio_area, LV_OBJ_FLAG_SCROLLABLE);

    create_audio_grid_line(audio_area, 0);
    create_audio_grid_line(audio_area, 1);

    int16_t total_width = MIC_SPECTRUM_BANDS * MAGIC_AUDIO_BAR_WIDTH +
                          (MIC_SPECTRUM_BANDS - 1) * MAGIC_AUDIO_BAR_GAP;
    int16_t start_x = (MAGIC_AUDIO_AREA_WIDTH - total_width) / 2;
    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        audio_bars[i] = lv_obj_create(audio_area);
        lv_obj_set_size(audio_bars[i], MAGIC_AUDIO_BAR_WIDTH, 1);
        lv_obj_set_style_bg_color(audio_bars[i], ui_theme_fg_color(), LV_PART_MAIN);
        lv_obj_set_style_border_width(audio_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(audio_bars[i], 0, LV_PART_MAIN);
        lv_obj_align(audio_bars[i], LV_ALIGN_TOP_LEFT, start_x + i * (MAGIC_AUDIO_BAR_WIDTH + MAGIC_AUDIO_BAR_GAP),
                     MAGIC_AUDIO_BAR_BASE_Y);
        s_last_audio_bars[i] = 0;
    }

    battery_label    = create_status_label(running_screen, "Magic B:--", 146);
    mouse_info_label = create_status_label(running_screen, "J:ON M:OFF", 164);
    click_info_label = create_status_label(running_screen, "HID:WAIT HFP:WAIT", 182);
    joy_info_label   = create_status_label(running_screen, "Click:UP A:OFF", 200);
    imu_info_label   = create_status_label(running_screen, "P:0 R:0 L:-90", 218);

    lvgl_port_unlock();
}

bool ui_running_screen_is_ready(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bool ready = running_screen != NULL && lv_obj_is_valid(running_screen);
    lvgl_port_unlock();
    return ready;
}

bool ui_running_screen_load(bool animated)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (running_screen == NULL || !lv_obj_is_valid(running_screen)) {
        lvgl_port_unlock();
        return false;
    }
    if (animated) {
        lv_scr_load_anim(running_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    } else {
        lv_disp_load_scr(running_screen);
    }
    lvgl_port_unlock();
    return true;
}

/**
 * @brief Update joystick, IMU, audio, and status on the Magic page.
 */
void update_running_screen(int16_t joyX, int16_t joyY, uint8_t bat, bool pressed, bool bt_connected,
                           float accel_x, float accel_y, float accel_z,
                           const mic_spectrum_data_t *spectrum, bool mic_running,
                           bool joystick_enabled, bool hfp_connected, bool audio_connected,
                           uint32_t sample_rate)
{
    int16_t min_pos = MOUSE_DOT_SIZE / 2;
    int16_t max_pos = MOUSE_PANEL_SIZE - (MOUSE_DOT_SIZE / 2);
    int16_t x_pos   = map_range(joyX, X_MIN, X_MAX, min_pos, max_pos);
    int16_t y_pos   = map_range(joyY, Y_MIN, Y_MAX, max_pos, min_pos);

    int16_t x_center = map_range(X_CENTER, X_MIN, X_MAX, min_pos, max_pos);
    int16_t y_center = map_range(Y_CENTER, Y_MIN, Y_MAX, max_pos, min_pos);

    if (abs(joyX - X_CENTER) < JOYSTICK_DEAD_ZONE) {
        x_pos = x_center;
    }
    if (abs(joyY - Y_CENTER) < JOYSTICK_DEAD_ZONE) {
        y_pos = y_center;
    }

    x_pos = clamp_i16(x_pos, min_pos, max_pos);
    y_pos = clamp_i16(y_pos, min_pos, max_pos);

    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (running_screen == NULL || !lv_obj_is_valid(running_screen) ||
        joystick_dot == NULL || battery_label == NULL || mouse_info_label == NULL ||
        click_info_label == NULL || joy_info_label == NULL || imu_info_label == NULL ||
        audio_area == NULL || !lv_obj_is_valid(audio_area)) {
        lvgl_port_unlock();
        return;
    }

    lv_obj_align(joystick_dot, LV_ALIGN_TOP_LEFT, x_pos - (MOUSE_DOT_SIZE / 2), y_pos - (MOUSE_DOT_SIZE / 2));
    lv_obj_set_style_bg_color(joystick_dot,
                              joystick_enabled ? lv_palette_main(LV_PALETTE_RED) : ui_theme_grid_color(),
                              LV_PART_MAIN);
    update_mouse_imu_cube(accel_x, accel_y, accel_z);
    update_magic_audio_bars(mic_running ? spectrum : NULL);

    int16_t pitch_deg = (int16_t)lroundf(s_pitch * 180.0f / (float)M_PI);
    int16_t roll_deg  = (int16_t)lroundf(s_roll * 180.0f / (float)M_PI);
    int spectrum_db = spectrum == NULL ? -90 : spectrum->db;

    lv_label_set_text_fmt(battery_label, "Magic B:%u%%", (unsigned)bat);
    lv_label_set_text_fmt(mouse_info_label, "J:%s M:%s", joystick_enabled ? "ON" : "OFF",
                          mic_running ? "ON" : "OFF");
    lv_label_set_text_fmt(click_info_label, "HID:%s HFP:%s", bt_connected ? "OK" : "WAIT",
                          hfp_connected ? "SLC" : "WAIT");
    lv_label_set_text_fmt(joy_info_label, "Click:%s A:%s", pressed ? "DOWN" : "UP",
                          audio_connected ? "ON" : "OFF");
    lv_label_set_text_fmt(imu_info_label, "P:%d R:%d L:%d", pitch_deg, roll_deg,
                          mic_running ? spectrum_db : -90);
    (void)sample_rate;
    lvgl_port_unlock();
}

/**
 * @brief Reset all UI object pointers to NULL to prepare for screen destruction
 * @note This function does not actually destroy the UI objects, but resets the pointers
 *       that reference them, allowing the UI to be recreated or switched
 * @warning The actual UI objects should be destroyed separately using LVGL's object destruction functions
 */
void ui_running_screen_destory(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (running_screen != NULL) {
        lv_obj_del(running_screen);
        running_screen = NULL;
    }
    lvgl_port_unlock();
    joystick_dot       = NULL;
    joystick_area      = NULL;
    cube_container     = NULL;
    audio_area         = NULL;
    battery_label      = NULL;
    mouse_info_label   = NULL;
    click_info_label   = NULL;
    joy_info_label     = NULL;
    imu_info_label     = NULL;
    for (int i = 0; i < 12; ++i) {
        edge_lines[i] = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        cross_lines[i] = NULL;
    }
    for (int i = 0; i < MIC_SPECTRUM_BANDS; i++) {
        audio_bars[i] = NULL;
        s_last_audio_bars[i] = 0;
    }
}
