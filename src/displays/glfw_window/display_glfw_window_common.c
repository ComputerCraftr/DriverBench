#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"
#include <GLFW/glfw3.h>

#include <stdint.h>

#include "../../core/db_core.h"
#include "../display_frame_loop_common.h"

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

static int db_glfw_loop_should_continue(void *user_data) {
    const db_glfw_loop_t *loop = (const db_glfw_loop_t *)user_data;
    return ((loop != NULL) && (glfwWindowShouldClose(loop->window) == 0)) ? 1
                                                                          : 0;
}

static void db_glfw_loop_pre_frame(void *user_data, uint32_t frame_index) {
    (void)user_data;
    (void)frame_index;
    db_glfw_poll_events();
}

static db_display_frame_loop_result_t
db_glfw_loop_frame_adapter(void *user_data, uint32_t frame_index,
                           double elapsed_ms) {
    const db_glfw_loop_t *loop = (const db_glfw_loop_t *)user_data;
    if ((loop == NULL) || (loop->frame_fn == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    return loop->frame_fn(loop->user_data, frame_index, elapsed_ms);
}

uint64_t db_glfw_run_loop(const db_glfw_loop_t *loop) {
    if ((loop == NULL) || (loop->frame_fn == NULL) || (loop->window == NULL)) {
        return 0U;
    }
    const db_display_frame_loop_t shared_loop = {
        .backend = loop->backend,
        .fps_cap = loop->fps_cap,
        .frame_limit = loop->frame_limit,
        .user_data = (void *)loop,
        .should_continue_fn = db_glfw_loop_should_continue,
        .pre_frame_fn = db_glfw_loop_pre_frame,
        .frame_fn = db_glfw_loop_frame_adapter,
    };
    return db_display_run_frame_loop(&shared_loop).frames;
}
