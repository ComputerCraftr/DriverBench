#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"

#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../bench_config.h"

#define DRIVERBENCH_VSYNC_ENV "DRIVERBENCH_VSYNC"

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

int db_glfw_resolve_swap_interval(void) {
    const char *value = getenv(DRIVERBENCH_VSYNC_ENV);
    if (value == NULL) {
        return BENCH_GLFW_SWAP_INTERVAL;
    }
    if ((strcmp(value, "1") == 0) || (strcmp(value, "true") == 0) ||
        (strcmp(value, "TRUE") == 0) || (strcmp(value, "yes") == 0) ||
        (strcmp(value, "YES") == 0) || (strcmp(value, "on") == 0) ||
        (strcmp(value, "ON") == 0)) {
        return 1;
    }
    if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0) ||
        (strcmp(value, "FALSE") == 0) || (strcmp(value, "no") == 0) ||
        (strcmp(value, "NO") == 0) || (strcmp(value, "off") == 0) ||
        (strcmp(value, "OFF") == 0)) {
        return 0;
    }

    db_infof("display_glfw_window_common",
             "Invalid %s='%s'; using default swap interval %d",
             DRIVERBENCH_VSYNC_ENV, value, BENCH_GLFW_SWAP_INTERVAL);
    return BENCH_GLFW_SWAP_INTERVAL;
}
