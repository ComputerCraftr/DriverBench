#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"
#include <GLFW/glfw3.h>

#include <errno.h>
#include <stdint.h>
#ifndef __APPLE__
#include <sys/errno.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/db_core.h"
#include "../bench_config.h"
#include "../display_env_common.h"

#define MAX_SLEEP_NS_D 100000000.0
#define NS_PER_SECOND_D 1000000000.0

int db_glfw_offscreen_enabled(void) {
    return db_env_is_truthy(DB_ENV_OFFSCREEN);
}

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
    if (db_glfw_offscreen_enabled() != 0) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
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
    glfwDefaultWindowHints();
    if (db_glfw_offscreen_enabled() != 0) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
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

GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles) {
    if (out_is_gles != NULL) {
        *out_is_gles = 0;
    }
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }

    glfwDefaultWindowHints();
    if (db_glfw_offscreen_enabled() != 0) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, gl_context_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gl_context_minor);
    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window != NULL) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(swap_interval);
        return window;
    }

    glfwDefaultWindowHints();
    if (db_glfw_offscreen_enabled() != 0) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    window = glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend,
                 "glfwCreateWindow failed for both OpenGL and OpenGL ES");
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(swap_interval);
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

int db_glfw_resolve_swap_interval(void) {
    const char *value = getenv(DB_ENV_VSYNC);
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
             "Invalid %s='%s'; using default swap interval %d", DB_ENV_VSYNC,
             value, BENCH_GLFW_SWAP_INTERVAL);
    return BENCH_GLFW_SWAP_INTERVAL;
}

double db_glfw_resolve_fps_cap(const char *backend) {
    const char *value = getenv(DB_ENV_FPS_CAP);
    if (value == NULL) {
        return BENCH_FPS_CAP_D;
    }
    if ((strcmp(value, "0") == 0) || (strcmp(value, "off") == 0) ||
        (strcmp(value, "OFF") == 0) || (strcmp(value, "false") == 0) ||
        (strcmp(value, "FALSE") == 0) || (strcmp(value, "uncapped") == 0) ||
        (strcmp(value, "none") == 0)) {
        return 0.0;
    }

    char *endptr = NULL;
    const double parsed = strtod(value, &endptr);
    if ((endptr != value) && (endptr != NULL) && (*endptr == '\0') &&
        (parsed > 0.0)) {
        return parsed;
    }

    db_infof(backend, "Invalid %s='%s'; using default fps cap %.2f",
             DB_ENV_FPS_CAP, value, BENCH_FPS_CAP_D);
    return BENCH_FPS_CAP_D;
}

uint32_t db_glfw_resolve_frame_limit(const char *backend) {
    const char *value = getenv(DB_ENV_FRAME_LIMIT);
    if ((value == NULL) || (value[0] == '\0')) {
        return 0U;
    }

    char *endptr = NULL;
    const unsigned long parsed = strtoul(value, &endptr, 10);
    if ((endptr != value) && (endptr != NULL) && (*endptr == '\0') &&
        (parsed <= UINT32_MAX)) {
        return (uint32_t)parsed;
    }

    db_infof(backend, "Invalid %s='%s'; using unlimited runtime",
             DB_ENV_FRAME_LIMIT, value);
    return 0U;
}

void db_glfw_resolve_hash_settings(const char *backend, int *out_hash_enabled,
                                   int *out_hash_every_frame) {
    const int offscreen_enabled = db_glfw_offscreen_enabled();
    const int hash_framebuffer = db_env_is_truthy(DB_ENV_FRAMEBUFFER_HASH);
    const int hash_every_frame = db_env_is_truthy(DB_ENV_HASH_EVERY_FRAME);
    const int hash_enabled =
        (offscreen_enabled != 0) &&
        ((hash_framebuffer != 0) || (hash_every_frame != 0));

    if ((offscreen_enabled == 0) &&
        ((hash_framebuffer != 0) || (hash_every_frame != 0))) {
        db_infof(backend, "ignoring hash env in windowed mode "
                          "(set DRIVERBENCH_OFFSCREEN=1)");
    }
    if (getenv(DB_ENV_OFFSCREEN_FRAMES) != NULL) {
        db_infof(backend, "ignoring DRIVERBENCH_OFFSCREEN_FRAMES for "
                          "GLFW backend (use DRIVERBENCH_FRAME_LIMIT)");
    }

    if (out_hash_enabled != NULL) {
        *out_hash_enabled = hash_enabled;
    }
    if (out_hash_every_frame != NULL) {
        *out_hash_every_frame = hash_every_frame;
    }
}

void db_glfw_sleep_to_fps_cap(double frame_start_s, double fps_cap) {
    if (fps_cap <= 0.0) {
        return;
    }

    const double frame_budget_s = 1.0 / fps_cap;
    double remaining_s =
        frame_budget_s - (db_glfw_time_seconds() - frame_start_s);
    while (remaining_s > 0.0) {
        const double remaining_ns_d = remaining_s * NS_PER_SECOND_D;
        const double sleep_ns_d =
            (remaining_ns_d > MAX_SLEEP_NS_D) ? MAX_SLEEP_NS_D : remaining_ns_d;
        const long sleep_ns = db_checked_double_to_long(
            "display_glfw_window_common", "sleep_ns", sleep_ns_d);
        if (sleep_ns <= 0L) {
            break;
        }

        struct timespec request = {0};
        request.tv_nsec = sleep_ns;
        // NOLINTNEXTLINE(misc-include-cleaner)
        if ((nanosleep(&request, NULL) != 0) && (errno != EINTR)) {
            break;
        }
        remaining_s = frame_budget_s - (db_glfw_time_seconds() - frame_start_s);
    }
}
