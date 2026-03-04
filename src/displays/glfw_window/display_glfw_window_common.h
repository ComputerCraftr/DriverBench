#ifndef DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H
#define DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H

#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

#include "../display_frame_loop_common.h"

typedef db_display_frame_loop_result_t (*db_glfw_frame_fn_t)(
    void *user_data, uint32_t frame_index, double elapsed_ms);

typedef struct {
    const char *backend;
    db_glfw_frame_fn_t frame_fn;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    GLFWwindow *window;
} db_glfw_loop_t;

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int offscreen_enabled);
GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval,
                                         int offscreen_enabled);
GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles, int offscreen_enabled);
void db_glfw_destroy_window(GLFWwindow *window);
void db_glfw_poll_events(void);
uint64_t db_glfw_run_loop(const db_glfw_loop_t *loop);

#endif
