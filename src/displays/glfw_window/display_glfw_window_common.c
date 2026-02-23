#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"
#include <GLFW/glfw3.h>

#include <errno.h>
#include <stdint.h>
#ifndef __APPLE__
#include <sys/errno.h>
#endif
#include <stdlib.h>
#include <time.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"

#define MAX_SLEEP_NS_D 100000000.0

static void db_glfw_init_or_fail(const char *backend) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
}

static void db_glfw_apply_default_hints(void) {
    glfwDefaultWindowHints();
    if (db_glfw_offscreen_enabled() != 0) {
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

static void db_glfw_make_current_and_set_swap(GLFWwindow *window,
                                              int swap_interval) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(swap_interval);
}

static GLFWwindow *
db_glfw_try_context_window(const char *title, int width_px, int height_px,
                           int client_api, int context_major, int context_minor,
                           int core_profile, int swap_interval) {
    db_glfw_apply_default_hints();
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
        db_glfw_make_current_and_set_swap(window, swap_interval);
    }
    return window;
}

int db_glfw_offscreen_enabled(void) {
    return db_value_is_truthy(db_runtime_option_get(DB_RUNTIME_OPT_OFFSCREEN));
}

GLFWwindow *db_glfw_create_no_api_window(const char *backend, const char *title,
                                         int width_px, int height_px) {
    db_glfw_init_or_fail(backend);
    db_glfw_apply_default_hints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return db_glfw_create_window_or_fail(backend, title, width_px, height_px,
                                         "glfwCreateWindow failed");
}

GLFWwindow *db_glfw_create_opengl_window(const char *backend, const char *title,
                                         int width_px, int height_px,
                                         int context_major, int context_minor,
                                         int core_profile, int swap_interval) {
    db_glfw_init_or_fail(backend);
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, context_major,
        context_minor, core_profile, swap_interval);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed");
    }
    return window;
}

GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles) {
    if (out_is_gles != NULL) {
        *out_is_gles = 0;
    }
    db_glfw_init_or_fail(backend);

#ifdef DB_HAS_OPENGL_DESKTOP
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, gl_context_major,
        gl_context_minor, 0, swap_interval);
    if (window != NULL) {
        return window;
    }

    window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_ES_API, 1, 1, 0, swap_interval);
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
#else
    (void)gl_context_major;
    (void)gl_context_minor;
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_ES_API, 1, 1, 0, swap_interval);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed for OpenGL ES 1.1");
    }
    if (out_is_gles != NULL) {
        *out_is_gles = 1;
    }
    return window;
#endif
}

void db_glfw_destroy_window(GLFWwindow *window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void db_glfw_poll_events(void) { glfwPollEvents(); }

double db_glfw_time_seconds(void) { return glfwGetTime(); }

int db_glfw_resolve_swap_interval(const char *backend) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_VSYNC);
    if (value == NULL) {
        return BENCH_GLFW_SWAP_INTERVAL;
    }

    int swap_interval = 0;
    if (db_parse_bool_text(value, &swap_interval) != 0) {
        return swap_interval;
    }

    db_infof(backend, "Invalid %s='%s'; using default swap interval %d",
             DB_RUNTIME_OPT_VSYNC, value, BENCH_GLFW_SWAP_INTERVAL);
    return BENCH_GLFW_SWAP_INTERVAL;
}

double db_glfw_resolve_fps_cap(const char *backend) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_FPS_CAP);
    if (value == NULL) {
        return BENCH_FPS_CAP_D;
    }
    double parsed = 0.0;
    if (db_parse_fps_cap_text(value, &parsed) != 0) {
        return parsed;
    }

    db_infof(backend, "Invalid %s='%s'; using default fps cap %.2f",
             DB_RUNTIME_OPT_FPS_CAP, value, BENCH_FPS_CAP_D);
    return BENCH_FPS_CAP_D;
}

uint32_t db_glfw_resolve_frame_limit(const char *backend) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_FRAME_LIMIT);
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
             DB_RUNTIME_OPT_FRAME_LIMIT, value);
    return 0U;
}

void db_glfw_resolve_hash_settings(const char *backend, int *out_hash_enabled,
                                   int *out_hash_every_frame) {
    const int offscreen_enabled = db_glfw_offscreen_enabled();
    const int hash_framebuffer = db_value_is_truthy(
        db_runtime_option_get(DB_RUNTIME_OPT_FRAMEBUFFER_HASH));
    const int hash_every_frame = db_value_is_truthy(
        db_runtime_option_get(DB_RUNTIME_OPT_HASH_EVERY_FRAME));
    const int hash_enabled =
        (offscreen_enabled != 0) &&
        ((hash_framebuffer != 0) || (hash_every_frame != 0));

    if ((offscreen_enabled == 0) &&
        ((hash_framebuffer != 0) || (hash_every_frame != 0))) {
        db_infof(backend, "ignoring hash env in windowed mode "
                          "(set DRIVERBENCH_OFFSCREEN=1)");
    }
    if (db_runtime_option_get(DB_RUNTIME_OPT_OFFSCREEN_FRAMES) != NULL) {
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

void db_glfw_sleep_to_fps_cap(const char *backend, double frame_start_s,
                              double fps_cap) {
    if (fps_cap <= 0.0) {
        return;
    }

    const double frame_budget_s = 1.0 / fps_cap;
    double remaining_s =
        frame_budget_s - (db_glfw_time_seconds() - frame_start_s);
    while (remaining_s > 0.0) {
        const double remaining_ns_d = remaining_s * DB_NS_PER_SECOND_D;
        const double sleep_ns_d =
            (remaining_ns_d > MAX_SLEEP_NS_D) ? MAX_SLEEP_NS_D : remaining_ns_d;
        const long sleep_ns =
            db_checked_double_to_long(backend, "sleep_ns", sleep_ns_d);
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
