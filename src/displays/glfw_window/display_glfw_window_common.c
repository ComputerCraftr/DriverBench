#define GLFW_INCLUDE_NONE
#include "display_glfw_window_common.h"
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../renderers/renderer_gl_common.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_hash_readback_common.h"

static void db_glfw_init_or_fail(const char *backend) {
    if (!glfwInit()) {
        db_failf(backend, "glfwInit failed");
    }
}

static const char *
db_glfw_window_visibility_name(db_glfw_window_visibility_t visibility) {
    return (visibility == DB_GLFW_WINDOW_HIDDEN) ? "hidden" : "visible";
}

static void
db_glfw_apply_default_hints(db_glfw_window_visibility_t visibility) {
    glfwDefaultWindowHints();
    if (visibility == DB_GLFW_WINDOW_HIDDEN) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
}

static void
db_glfw_finalize_window_visibility(const char *backend, GLFWwindow *window,
                                   db_glfw_window_visibility_t visibility) {
    if ((window != NULL) && (visibility == DB_GLFW_WINDOW_HIDDEN)) {
        glfwHideWindow(window);
    }
    db_infof(backend, "glfw window visibility=%s",
             db_glfw_window_visibility_name(visibility));
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
                           db_glfw_window_visibility_t visibility) {
    db_glfw_apply_default_hints(visibility);
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

GLFWwindow *
db_glfw_create_no_api_window(const char *backend, const char *title,
                             int width_px, int height_px,
                             db_glfw_window_visibility_t visibility) {
    db_glfw_init_or_fail(backend);
    db_glfw_apply_default_hints(visibility);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = db_glfw_create_window_or_fail(
        backend, title, width_px, height_px, "glfwCreateWindow failed");
    db_glfw_finalize_window_visibility(backend, window, visibility);
    return window;
}

GLFWwindow *db_glfw_create_opengl_window(
    const char *backend, const char *title, int width_px, int height_px,
    int context_major, int context_minor, int core_profile, int swap_interval,
    db_glfw_window_visibility_t visibility) {
    db_glfw_init_or_fail(backend);
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, context_major,
        context_minor, core_profile, swap_interval, visibility);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend, "glfwCreateWindow failed");
    }
    db_glfw_finalize_window_visibility(backend, window, visibility);
    return window;
}

GLFWwindow *db_glfw_create_gl1_5_or_gles1_1_window(
    const char *backend, const char *title, int width_px, int height_px,
    int gl_context_major, int gl_context_minor, int swap_interval,
    int *out_is_gles, db_glfw_window_visibility_t visibility) {
    if (out_is_gles != NULL) {
        *out_is_gles = 0;
    }
    db_glfw_init_or_fail(backend);
    GLFWwindow *window = db_glfw_try_context_window(
        title, width_px, height_px, GLFW_OPENGL_API, gl_context_major,
        gl_context_minor, 0, swap_interval, visibility);
    if (window != NULL) {
        db_glfw_finalize_window_visibility(backend, window, visibility);
        return window;
    }

    window = db_glfw_try_context_window(title, width_px, height_px,
                                        GLFW_OPENGL_ES_API, 1, 1, 0,
                                        swap_interval, visibility);
    if (window == NULL) {
        glfwTerminate();
        db_failf(backend,
                 "glfwCreateWindow failed for both OpenGL and OpenGL ES");
    }
    if (out_is_gles != NULL) {
        *out_is_gles = 1;
    }
    db_infof(backend, "OpenGL context creation failed; fell back to GLES 1.1");
    db_glfw_finalize_window_visibility(backend, window, visibility);
    return window;
}

void db_glfw_destroy_window(GLFWwindow *window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void db_glfw_poll_events(void) { glfwPollEvents(); }

static uint64_t db_glfw_probe_read_default_fb_hash_or_fail(
    const char *backend_name, int framebuffer_width_px,
    int framebuffer_height_px, db_gl_framebuffer_hash_scratch_t *scratch) {
    const uint8_t *pixels = db_gl_read_framebuffer_rgba8_or_fail(
        backend_name, framebuffer_width_px, framebuffer_height_px, scratch);
    return db_hash_rgba8_pixels_canonical(
        pixels,
        db_checked_int_to_u32(backend_name, "probe_fb_w", framebuffer_width_px),
        db_checked_int_to_u32(backend_name, "probe_fb_h",
                              framebuffer_height_px),
        db_checked_int_to_size(backend_name, "probe_fb_row_pixels",
                               framebuffer_width_px) *
            4U,
        1);
}

static void db_glfw_log_default_framebuffer_probe(
    const char *backend_name, const db_glfw_default_fb_probe_result_t *result) {
    if ((backend_name == NULL) || (result == NULL)) {
        return;
    }
    if (result->observed_any_match == 0) {
        db_infof(backend_name,
                 "glfw default-fb probe: preserve=no first_reuse_distance=0 "
                 "max_reuse_distance=0");
        return;
    }
    const int stable_preserve = (result->preserves_immediately != 0) &&
                                (result->first_reuse_distance == 1) &&
                                (result->max_reuse_distance == 1);
    db_infof(backend_name,
             "glfw default-fb probe: preserve=%s first_reuse_distance=%d "
             "max_reuse_distance=%d",
             (stable_preserve != 0) ? "yes" : "unstable",
             result->first_reuse_distance, result->max_reuse_distance);
}

db_glfw_default_fb_probe_result_t
db_glfw_probe_and_log_default_framebuffer_reuse(const char *backend_name,
                                                GLFWwindow *window) {
    static const float probe_colors[8][4] = {
        {1.0F, 0.0F, 0.0F, 1.0F},   {0.0F, 1.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F, 1.0F},   {1.0F, 1.0F, 0.0F, 1.0F},
        {1.0F, 0.0F, 1.0F, 1.0F},   {0.0F, 1.0F, 1.0F, 1.0F},
        {0.5F, 0.25F, 0.75F, 1.0F}, {0.75F, 0.5F, 0.25F, 1.0F},
    };
    db_glfw_default_fb_probe_result_t result = {0};
    if ((backend_name == NULL) || (window == NULL)) {
        return result;
    }

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    if ((framebuffer_width_px <= 0) || (framebuffer_height_px <= 0)) {
        return result;
    }
    db_gl_set_viewport_px(framebuffer_width_px, framebuffer_height_px);

    db_gl_framebuffer_hash_scratch_t scratch = {0};
    uint64_t pre_swap_hashes[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    for (size_t frame_index = 0U; frame_index < 8U; frame_index++) {
        const float *const rgba = probe_colors[frame_index];
        db_gl_clear_color_rgba(rgba[0], rgba[1], rgba[2], rgba[3]);
        db_gl_clear_color_buffer();
        pre_swap_hashes[frame_index] =
            db_glfw_probe_read_default_fb_hash_or_fail(
                backend_name, framebuffer_width_px, framebuffer_height_px,
                &scratch);
        glfwSwapBuffers(window);
        glfwPollEvents();
        const uint64_t post_swap_hash =
            db_glfw_probe_read_default_fb_hash_or_fail(
                backend_name, framebuffer_width_px, framebuffer_height_px,
                &scratch);
        for (size_t prior_index = 0U; prior_index <= frame_index;
             prior_index++) {
            if (post_swap_hash != pre_swap_hashes[prior_index]) {
                continue;
            }
            result.observed_any_match = 1;
            const int reuse_distance = db_checked_size_to_i32(
                backend_name, "probe_reuse_distance",
                (frame_index - prior_index) + 1U);
            if (prior_index == frame_index) {
                result.preserves_immediately = 1;
            }
            if ((result.first_reuse_distance == 0) ||
                (reuse_distance < result.first_reuse_distance)) {
                result.first_reuse_distance = reuse_distance;
            }
            if (reuse_distance > result.max_reuse_distance) {
                result.max_reuse_distance = reuse_distance;
            }
            break;
        }
    }
    db_gl_hash_scratch_release(&scratch);
    db_glfw_log_default_framebuffer_probe(backend_name, &result);
    return result;
}

int db_glfw_default_framebuffer_probe_is_stable(
    const db_glfw_default_fb_probe_result_t *result) {
    return (result != NULL) && (result->observed_any_match != 0) &&
           (result->preserves_immediately != 0) &&
           (result->first_reuse_distance == 1) &&
           (result->max_reuse_distance == 1);
}

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

db_display_frame_loop_run_result_t db_glfw_run_loop(db_glfw_loop_t *loop) {
    if ((loop == NULL) || (loop->frame_fn == NULL) || (loop->window == NULL)) {
        return (db_display_frame_loop_run_result_t){0};
    }
    const db_display_frame_loop_t shared_loop = {
        .backend = loop->backend,
        .fps_cap = loop->fps_cap,
        .frame_limit = loop->frame_limit,
        .user_data = loop,
        .should_continue_fn = db_glfw_loop_should_continue,
        .pre_frame_fn = db_glfw_loop_pre_frame,
        .frame_fn = db_glfw_loop_frame_adapter,
    };
    return db_display_run_frame_loop(&shared_loop);
}
