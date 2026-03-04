#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"
#include <GLFW/glfw3.h>

#include <errno.h>
#include <stdint.h>
#ifndef __APPLE__
#include <sys/errno.h>
#endif
#include <time.h>

#include "../../core/db_core.h"

#define MAX_SLEEP_NS 100000000.0

static void db_glfw_init_or_fail(const char *backend) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
}

static void db_glfw_apply_default_hints(int offscreen_enabled) {
    glfwDefaultWindowHints();
    if (offscreen_enabled != 0) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
}

static GLFWwindow *db_glfw_create_window_or_fail(const char *backend,
                                                 const char *title,
                                                 int width_px, int height_px,
                                                 const char *error_message) {
    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "%s", error_message);
    }
    return window;
}

static GLFWwindow *
db_glfw_try_context_window(const char *title, int width_px, int height_px,
                           int client_api, int context_major, int context_minor,
                           int core_profile, int swap_interval,
                           int offscreen_enabled) {
    db_glfw_apply_default_hints(offscreen_enabled);
    glfwWindowHint(GLFW_CLIENT_API, client_api);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, context_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, context_minor);
    if ((client_api == GLFW_OPENGL_API) && (core_profile != 0)) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }

    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window != NULL) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(swap_interval);
    }
    return window;
}

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int offscreen_enabled) {
    db_glfw_init_or_fail(backend);
    db_glfw_apply_default_hints(offscreen_enabled);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return db_glfw_create_window_or_fail(backend, title, width_px, height_px,
                                         "glfwCreateWindow failed");
}

GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval,
                                         int offscreen_enabled) {
    db_glfw_init_or_fail(backend);
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, context_major,
        context_minor, core_profile, swap_interval, offscreen_enabled);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed");
    }
    return window;
}

GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles, int offscreen_enabled) {
    if (out_is_gles != NULL) {
        *out_is_gles = 0;
    }
    db_glfw_init_or_fail(backend);
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, gl_context_major,
        gl_context_minor, 0, swap_interval, offscreen_enabled);
    if (window != NULL) {
        return window;
    }

    window = db_glfw_try_context_window(title, width_px, height_px,
                                        GLFW_OPENGL_ES_API, 1, 1, 0,
                                        swap_interval, offscreen_enabled);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend,
                 "glfwCreateWindow failed for both OpenGL and OpenGL ES");
    }
    if (out_is_gles != NULL) {
        *out_is_gles = 1;
    }
    db_infof(backend, "OpenGL context creation failed; fell back to GLES 1.1");
    return window;
}

void db_glfw_destroy_window(GLFWwindow *window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void db_glfw_poll_events(void) { glfwPollEvents(); }

double db_glfw_time_seconds(void) { return glfwGetTime(); }

void db_glfw_sleep_to_fps_cap(const char *backend, double frame_start_s,
                              double fps_cap) {
    if (fps_cap <= 0.0) {
        return;
    }

    const double frame_budget_s = 1.0 / fps_cap;
    double remaining_s =
        frame_budget_s - (db_glfw_time_seconds() - frame_start_s);
    while (remaining_s > 0.0) {
        const double remaining_ns = remaining_s * DB_NS_PER_SECOND;
        const double sleep_ns =
            (remaining_ns > MAX_SLEEP_NS) ? MAX_SLEEP_NS : remaining_ns;
        const long sleep_ns_long =
            db_checked_double_to_long(backend, "sleep_ns", sleep_ns);
        if (sleep_ns_long <= 0L) {
            break;
        }

        struct timespec request = {0};
        request.tv_nsec = sleep_ns_long;
        // NOLINTNEXTLINE(misc-include-cleaner)
        if ((nanosleep(&request, NULL) != 0) && (errno != EINTR)) {
            break;
        }
        remaining_s = frame_budget_s - (db_glfw_time_seconds() - frame_start_s);
    }
}
