#ifndef DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H
#define DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H

#include <stdint.h>

typedef struct GLFWwindow GLFWwindow;

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px);
GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval);
GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles);
void db_glfw_destroy_window(GLFWwindow *window);
void db_glfw_poll_events(void);
double db_glfw_time_seconds(void);
int db_glfw_resolve_swap_interval(const char *backend);
double db_glfw_resolve_fps_cap(const char *backend);
uint32_t db_glfw_resolve_frame_limit(const char *backend);
void db_glfw_resolve_hash_settings(const char *backend, int *out_hash_enabled,
                                   int *out_hash_every_frame);
int db_glfw_offscreen_enabled(void);
void db_glfw_sleep_to_fps_cap(const char *backend, double frame_start_s,
                              double fps_cap);

#endif
