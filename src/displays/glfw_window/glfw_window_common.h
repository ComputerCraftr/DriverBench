#ifndef DRIVERBENCH_GLFW_WINDOW_COMMON_H
#define DRIVERBENCH_GLFW_WINDOW_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

#include "../display_frame_loop_common.h"
#include "../display_presentation_policy.h"
#include "../display_runtime_config_common.h"
#include "core/db_geometry.h"

typedef enum {
    DB_GLFW_WINDOW_VISIBLE = 0,
    DB_GLFW_WINDOW_HIDDEN = 1,
} db_glfw_window_visibility_t;

typedef db_display_frame_loop_result_t (*db_glfw_frame_fn_t)(
    void *user_data, uint32_t frame_index, double elapsed_ms);

typedef struct {
    const char *backend;
    db_glfw_frame_fn_t frame_fn;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    GLFWwindow *window;
    const db_display_renderer_runtime_t *resolved_runtime;
    db_resize_schedule_t resize_schedule;
    int resize_enabled;
    int resize_applied;
} db_glfw_loop_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    int valid;
} db_glfw_framebuffer_extent_t;

GLFWwindow *
db_glfw_create_no_api_window(const char *backend, const char *title,
                             int width_px, int height_px,
                             db_glfw_window_visibility_t visibility);
GLFWwindow *db_glfw_create_opengl_window(
    const char *backend, const char *title, int width_px, int height_px,
    int context_major, int context_minor, int core_profile, int swap_interval,
    db_glfw_window_visibility_t visibility);
GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles, db_glfw_window_visibility_t visibility);
void db_glfw_destroy_window(GLFWwindow *window);
db_display_frame_loop_run_result_t db_glfw_run_loop(db_glfw_loop_t *loop);
db_glfw_framebuffer_extent_t
db_glfw_get_framebuffer_extent(GLFWwindow *window, const char *backend_name);
db_presentation_buffer_age_t
db_glfw_query_presentation_buffer_age(GLFWwindow *window,
                                      uint32_t history_capacity);
void db_glfw_log_presentation_buffer_age(
    const char *backend_name, const db_presentation_buffer_age_t *age);
int db_glfw_presentation_buffer_age_changed(
    const db_presentation_buffer_age_t *previous,
    const db_presentation_buffer_age_t *current);

#endif
