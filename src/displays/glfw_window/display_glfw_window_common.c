#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"

#include "../../core/db_core.h"

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed");
    }
    return window;
}

GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, context_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, context_minor);
    if (core_profile) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }

    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed");
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(swap_interval);
    return window;
}

void db_glfw_destroy_window(GLFWwindow *window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void db_glfw_poll_events(void) { glfwPollEvents(); }

double db_glfw_time_seconds(void) { return glfwGetTime(); }
