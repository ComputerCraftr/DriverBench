#ifndef DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H
#define DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H

#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

#include "../display_frame_loop_common.h"
#include "../display_gl_hash_readback_common.h"

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
} db_glfw_loop_t;

typedef struct {
    int observed_any_match;
    int preserves_immediately;
    int first_reuse_distance;
    int max_reuse_distance;
} db_glfw_default_fb_probe_result_t;

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
void db_glfw_poll_events(void);
db_display_frame_loop_run_result_t db_glfw_run_loop(db_glfw_loop_t *loop);
db_glfw_default_fb_probe_result_t
db_glfw_probe_default_framebuffer_reuse(const char *backend_name,
                                        GLFWwindow *window);
void db_glfw_log_default_framebuffer_probe(
    const char *backend_name, const db_glfw_default_fb_probe_result_t *result);
int db_glfw_default_framebuffer_probe_is_stable(
    const db_glfw_default_fb_probe_result_t *result);

#endif
