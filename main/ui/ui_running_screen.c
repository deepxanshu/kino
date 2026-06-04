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
static lv_obj_t *mic_icon_area    = NULL;
static lv_obj_t *mic_icon_body    = NULL;
static lv_obj_t *mic_icon_yoke    = NULL;
static lv_obj_t *mic_icon_stem    = NULL;
static lv_obj_t *mic_icon_base    = NULL;
static lv_obj_t *audio_area       = NULL;
static lv_obj_t *battery_bar      = NULL;

#define MOUSE_PANEL_SIZE 56
#define MOUSE_DOT_SIZE   8
#define MOUSE_DOT_PRESSED_SIZE (MOUSE_DOT_SIZE * 2)
#define MAGIC_MIC_ICON_SIZE     28
#define MAGIC_AUDIO_AREA_WIDTH  86
#define MAGIC_AUDIO_AREA_HEIGHT 28
#define MAGIC_AUDIO_VISIBLE_BARS 12
#define MAGIC_AUDIO_BAR_WIDTH   4
#define MAGIC_AUDIO_BAR_GAP     2
#define MAGIC_AUDIO_BAR_BASE_Y  25
#define MAGIC_AUDIO_BAR_MAX_DRAW_HEIGHT 21
#define MAGIC_BATTERY_BAR_X     11
#define MAGIC_BATTERY_BAR_Y     211
#define MAGIC_BATTERY_BAR_WIDTH 113
#define MAGIC_BATTERY_BAR_HEIGHT 12
#define MAGIC_BATTERY_SEGMENTS   12
#define MAGIC_BATTERY_SEGMENT_GAP 1

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
static lv_obj_t *audio_bars[MAGIC_AUDIO_VISIBLE_BARS];
static lv_obj_t *battery_segments[MAGIC_BATTERY_SEGMENTS];
static lv_point_t edge_points[12][2];
static lv_point_t cross_points[2][2];
static uint8_t s_last_audio_bars[MAGIC_AUDIO_VISIBLE_BARS];
static lv_point_t s_audio_grid_points[2][2] = {
    {{0, 9}, {MAGIC_AUDIO_AREA_WIDTH, 9}},
    {{0, 18}, {MAGIC_AUDIO_AREA_WIDTH, 18}},
};
static lv_point_t s_mic_yoke_points[] = {
    {7, 11}, {7, 13}, {9, 18}, {14, 20}, {19, 18}, {21, 13}, {21, 11},
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

static void update_mic_icon(bool mic_running)
{
    lv_color_t color = mic_running ? ui_theme_accent_color() : ui_theme_grid_color();

    if (mic_icon_body != NULL && lv_obj_is_valid(mic_icon_body)) {
        lv_obj_set_style_border_color(mic_icon_body, color, LV_PART_MAIN);
    }
    if (mic_icon_yoke != NULL && lv_obj_is_valid(mic_icon_yoke)) {
        lv_obj_set_style_line_color(mic_icon_yoke, color, LV_PART_MAIN);
    }
    if (mic_icon_stem != NULL && lv_obj_is_valid(mic_icon_stem)) {
        lv_obj_set_style_bg_color(mic_icon_stem, color, LV_PART_MAIN);
    }
    if (mic_icon_base != NULL && lv_obj_is_valid(mic_icon_base)) {
        lv_obj_set_style_bg_color(mic_icon_base, color, LV_PART_MAIN);
    }
}

static lv_color_t battery_segment_color(uint8_t battery_percent)
{
    if (battery_percent >= 60) {
        return ui_theme_accent_color();
    }
    if (battery_percent >= 20) {
        return lv_color_make(255, 210, 31);
    }
    return ui_theme_bg_color();
}

static void update_battery_bar(uint8_t battery_percent)
{
    if (battery_bar == NULL || !lv_obj_is_valid(battery_bar)) {
        return;
    }

    uint8_t percent = battery_percent > 100 ? 100 : battery_percent;
    uint8_t filled_segments = (uint8_t)((percent * MAGIC_BATTERY_SEGMENTS + 99) / 100);
    lv_color_t fill_color = battery_segment_color(percent);

    for (int i = 0; i < MAGIC_BATTERY_SEGMENTS; ++i) {
        if (battery_segments[i] == NULL || !lv_obj_is_valid(battery_segments[i])) {
            continue;
        }
        lv_obj_set_style_bg_color(battery_segments[i],
                                  i < filled_segments ? fill_color : ui_theme_bg_color(),
                                  LV_PART_MAIN);
    }
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
    int16_t total_width = MAGIC_AUDIO_VISIBLE_BARS * MAGIC_AUDIO_BAR_WIDTH +
                          (MAGIC_AUDIO_VISIBLE_BARS - 1) * MAGIC_AUDIO_BAR_GAP;
    int16_t start_x = (MAGIC_AUDIO_AREA_WIDTH - total_width) / 2;

    for (int i = 0; i < MAGIC_AUDIO_VISIBLE_BARS; i++) {
        uint8_t target = 0;
        if (spectrum != NULL) {
            int source_index = (i * MIC_SPECTRUM_BANDS) / MAGIC_AUDIO_VISIBLE_BARS;
            uint8_t raw = spectrum->bars[source_index] > MAGIC_AUDIO_BAR_MAX_HEIGHT ?
                          MAGIC_AUDIO_BAR_MAX_HEIGHT : spectrum->bars[source_index];
            target = (uint8_t)((raw * MAGIC_AUDIO_BAR_MAX_DRAW_HEIGHT + MAGIC_AUDIO_BAR_MAX_HEIGHT - 1) /
                               MAGIC_AUDIO_BAR_MAX_HEIGHT);
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

    joystick_area = lv_obj_create(running_screen);
    lv_obj_set_size(joystick_area, MOUSE_PANEL_SIZE, MOUSE_PANEL_SIZE);
    lv_obj_align(joystick_area, LV_ALIGN_TOP_LEFT, 7, 9);
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
    lv_obj_set_style_bg_color(joystick_dot, ui_theme_red_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(joystick_dot, 0, LV_PART_MAIN);
    lv_obj_align(joystick_dot, LV_ALIGN_CENTER, 0, 0);

    cube_container = lv_obj_create(running_screen);
    lv_obj_set_size(cube_container, MOUSE_PANEL_SIZE, MOUSE_PANEL_SIZE);
    lv_obj_align(cube_container, LV_ALIGN_TOP_LEFT, 72, 9);
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
        lv_obj_set_style_line_color(cross_lines[i], ui_theme_cyan_color(), 0);
        lv_obj_add_flag(cross_lines[i], LV_OBJ_FLAG_FLOATING);
        static lv_point_t default_points[2] = {{0, 0}, {0, 0}};
        lv_line_set_points(cross_lines[i], default_points, 2);
    }

    audio_area = lv_obj_create(running_screen);
    lv_obj_set_size(audio_area, MAGIC_AUDIO_AREA_WIDTH, MAGIC_AUDIO_AREA_HEIGHT);
    lv_obj_align(audio_area, LV_ALIGN_TOP_LEFT, 43, 72);
    ui_theme_apply_panel(audio_area);
    lv_obj_set_style_border_width(audio_area, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(audio_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(audio_area, LV_OBJ_FLAG_SCROLLABLE);

    mic_icon_area = lv_obj_create(running_screen);
    lv_obj_set_size(mic_icon_area, MAGIC_MIC_ICON_SIZE, MAGIC_MIC_ICON_SIZE);
    lv_obj_align(mic_icon_area, LV_ALIGN_TOP_LEFT, 7, 72);
    ui_theme_apply_panel(mic_icon_area);
    lv_obj_set_style_border_width(mic_icon_area, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mic_icon_area, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mic_icon_area, LV_OBJ_FLAG_SCROLLABLE);

    mic_icon_yoke = lv_line_create(mic_icon_area);
    lv_obj_set_size(mic_icon_yoke, MAGIC_MIC_ICON_SIZE, MAGIC_MIC_ICON_SIZE);
    lv_obj_align(mic_icon_yoke, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_line_set_points(mic_icon_yoke, s_mic_yoke_points,
                       sizeof(s_mic_yoke_points) / sizeof(s_mic_yoke_points[0]));
    lv_obj_set_style_line_width(mic_icon_yoke, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(mic_icon_yoke, true, LV_PART_MAIN);

    mic_icon_body = lv_obj_create(mic_icon_area);
    lv_obj_set_size(mic_icon_body, 8, 12);
    lv_obj_align(mic_icon_body, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_opa(mic_icon_body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(mic_icon_body, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(mic_icon_body, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mic_icon_body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mic_icon_body, LV_OBJ_FLAG_SCROLLABLE);

    mic_icon_stem = lv_obj_create(mic_icon_area);
    lv_obj_set_size(mic_icon_stem, 2, 5);
    lv_obj_align(mic_icon_stem, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_opa(mic_icon_stem, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(mic_icon_stem, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(mic_icon_stem, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mic_icon_stem, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mic_icon_stem, LV_OBJ_FLAG_SCROLLABLE);

    mic_icon_base = lv_obj_create(mic_icon_area);
    lv_obj_set_size(mic_icon_base, 9, 2);
    lv_obj_align(mic_icon_base, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_opa(mic_icon_base, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(mic_icon_base, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(mic_icon_base, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mic_icon_base, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mic_icon_base, LV_OBJ_FLAG_SCROLLABLE);
    update_mic_icon(false);

    create_audio_grid_line(audio_area, 0);
    create_audio_grid_line(audio_area, 1);

    int16_t total_width = MAGIC_AUDIO_VISIBLE_BARS * MAGIC_AUDIO_BAR_WIDTH +
                          (MAGIC_AUDIO_VISIBLE_BARS - 1) * MAGIC_AUDIO_BAR_GAP;
    int16_t start_x = (MAGIC_AUDIO_AREA_WIDTH - total_width) / 2;
    for (int i = 0; i < MAGIC_AUDIO_VISIBLE_BARS; i++) {
        audio_bars[i] = lv_obj_create(audio_area);
        lv_obj_set_size(audio_bars[i], MAGIC_AUDIO_BAR_WIDTH, 1);
        lv_obj_set_style_bg_color(audio_bars[i], ui_theme_accent_color(), LV_PART_MAIN);
        lv_obj_set_style_border_width(audio_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(audio_bars[i], 0, LV_PART_MAIN);
        lv_obj_align(audio_bars[i], LV_ALIGN_TOP_LEFT, start_x + i * (MAGIC_AUDIO_BAR_WIDTH + MAGIC_AUDIO_BAR_GAP),
                     MAGIC_AUDIO_BAR_BASE_Y);
        s_last_audio_bars[i] = 0;
    }

    battery_bar = lv_obj_create(running_screen);
    lv_obj_set_size(battery_bar, MAGIC_BATTERY_BAR_WIDTH, MAGIC_BATTERY_BAR_HEIGHT);
    lv_obj_align(battery_bar, LV_ALIGN_TOP_LEFT, MAGIC_BATTERY_BAR_X, MAGIC_BATTERY_BAR_Y);
    lv_obj_set_style_bg_color(battery_bar, ui_theme_bg_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(battery_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(battery_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(battery_bar, LV_OBJ_FLAG_SCROLLABLE);

    int16_t segment_width = 8;
    int16_t total_segment_width = MAGIC_BATTERY_SEGMENTS * segment_width +
                                  (MAGIC_BATTERY_SEGMENTS - 1) * MAGIC_BATTERY_SEGMENT_GAP;
    int16_t segment_start_x = (MAGIC_BATTERY_BAR_WIDTH - total_segment_width) / 2;
    for (int i = 0; i < MAGIC_BATTERY_SEGMENTS; ++i) {
        battery_segments[i] = lv_obj_create(battery_bar);
        lv_obj_set_size(battery_segments[i], segment_width, MAGIC_BATTERY_BAR_HEIGHT);
        lv_obj_align(battery_segments[i], LV_ALIGN_TOP_LEFT,
                     segment_start_x + i * (segment_width + MAGIC_BATTERY_SEGMENT_GAP), 0);
        lv_obj_set_style_bg_color(battery_segments[i], ui_theme_bg_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(battery_segments[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(battery_segments[i], ui_theme_fg_color(), LV_PART_MAIN);
        lv_obj_set_style_border_width(battery_segments[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(battery_segments[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(battery_segments[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(battery_segments[i], LV_OBJ_FLAG_SCROLLABLE);
    }

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
    int16_t dot_size = pressed ? MOUSE_DOT_PRESSED_SIZE : MOUSE_DOT_SIZE;
    int16_t min_pos = dot_size / 2;
    int16_t max_pos = MOUSE_PANEL_SIZE - (dot_size / 2);
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
        joystick_dot == NULL || battery_bar == NULL ||
        audio_area == NULL || !lv_obj_is_valid(audio_area) ||
        mic_icon_area == NULL || !lv_obj_is_valid(mic_icon_area)) {
        lvgl_port_unlock();
        return;
    }

    lv_obj_set_size(joystick_dot, dot_size, dot_size);
    lv_obj_align(joystick_dot, LV_ALIGN_TOP_LEFT, x_pos - (dot_size / 2), y_pos - (dot_size / 2));
    lv_obj_set_style_bg_color(joystick_dot,
                              joystick_enabled ? ui_theme_red_color() : ui_theme_grid_color(),
                              LV_PART_MAIN);
    update_mouse_imu_cube(accel_x, accel_y, accel_z);
    update_mic_icon(mic_running);
    update_magic_audio_bars(mic_running ? spectrum : NULL);
    update_battery_bar(bat);

    (void)bt_connected;
    (void)hfp_connected;
    (void)audio_connected;
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
    mic_icon_area      = NULL;
    mic_icon_body      = NULL;
    mic_icon_yoke      = NULL;
    mic_icon_stem      = NULL;
    mic_icon_base      = NULL;
    audio_area         = NULL;
    battery_bar        = NULL;
    for (int i = 0; i < 12; ++i) {
        edge_lines[i] = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        cross_lines[i] = NULL;
    }
    for (int i = 0; i < MAGIC_AUDIO_VISIBLE_BARS; i++) {
        audio_bars[i] = NULL;
        s_last_audio_bars[i] = 0;
    }
    for (int i = 0; i < MAGIC_BATTERY_SEGMENTS; i++) {
        battery_segments[i] = NULL;
    }
}
