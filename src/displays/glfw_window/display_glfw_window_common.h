#ifndef DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H
#define DRIVERBENCH_DISPLAY_GLFW_WINDOW_COMMON_H

typedef struct GLFWwindow GLFWwindow;

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px);
GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval);
void db_glfw_destroy_window(GLFWwindow *window);
void db_glfw_poll_events(void);
double db_glfw_time_seconds(void);
int db_glfw_resolve_swap_interval(void);

#endif
