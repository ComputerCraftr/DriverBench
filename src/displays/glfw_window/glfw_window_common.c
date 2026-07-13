#define GLFW_INCLUDE_NONE
#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_EGL
#define GLFW_EXPOSE_NATIVE_GLX
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include "glfw_window_common.h"
#include <GLFW/glfw3.h>
#ifdef __linux__
#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <GL/glx.h>
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#endif

#include <stddef.h>
#include <stdint.h>
#ifdef __linux__
#include <string.h>
#endif

#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_poll_policy.h"
#include "../display_frame_loop_common.h"
#include "../display_presentation_policy.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"

#ifdef __linux__
#ifndef GLX_BACK_BUFFER_AGE_EXT
#define GLX_BACK_BUFFER_AGE_EXT 0x20F4
#endif
#ifndef EGL_BUFFER_AGE_EXT
#define EGL_BUFFER_AGE_EXT 0x313D
#endif
typedef void (*db_glx_query_drawable_fn_t)(Display *, GLXDrawable, int,
                                           unsigned int *);
typedef const char *(*db_glx_query_extensions_string_fn_t)(Display *, int);
#endif

static void db_glfw_init_or_fail(const char *backend) {
    if (!glfwInit()) {
        DB_RUNTIME_FAIL(backend, "glfwInit failed");
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
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("visibility", db_glfw_window_visibility_name(visibility)),
    };
    db_log_info(backend, "window_visibility", fields,
                DB_LOG_FIELD_COUNT(fields));
}

static GLFWwindow *db_glfw_create_window_or_fail(const char *backend,
                                                 const char *title,
                                                 int width_px, int height_px,
                                                 const char *error_message) {
    GLFWwindow *window =
        glfwCreateWindow(width_px, height_px, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        DB_RUNTIME_FAIL(backend, "%s", error_message);
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
        DB_RUNTIME_FAIL(backend, "glfwCreateWindow failed");
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
        DB_RUNTIME_FAIL(
            backend, "glfwCreateWindow failed for both OpenGL and OpenGL ES");
    }
    if (out_is_gles != NULL) {
        *out_is_gles = 1;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("requested_api", "opengl"),
        DB_LOG_TOKEN("selected_api", "gles1_1"),
        DB_LOG_TOKEN("reason", "opengl_context_creation_failed"),
    };
    db_log_info(backend, "display_policy", fields, DB_LOG_FIELD_COUNT(fields));
    db_glfw_finalize_window_visibility(backend, window, visibility);
    return window;
}

void db_glfw_destroy_window(GLFWwindow *window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

db_glfw_framebuffer_extent_t
db_glfw_get_framebuffer_extent(GLFWwindow *window, const char *backend_name) {
    db_glfw_framebuffer_extent_t extent = {0};
    if (window == NULL) {
        return extent;
    }
    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    if ((framebuffer_width_px <= 0) || (framebuffer_height_px <= 0)) {
        return extent;
    }
    extent.width =
        db_checked_int_to_u32((backend_name != NULL) ? backend_name : "glfw",
                              "framebuffer_width_px", framebuffer_width_px);
    extent.height =
        db_checked_int_to_u32((backend_name != NULL) ? backend_name : "glfw",
                              "framebuffer_height_px", framebuffer_height_px);
    extent.valid = 1;
    return extent;
}

#ifdef __linux__
static int db_extension_list_has_token(const char *extensions,
                                       const char *token) {
    if ((extensions == NULL) || (token == NULL) || (token[0] == '\0')) {
        return 0;
    }
    const size_t token_length = strlen(token);
    const char *match = extensions;
    while ((match = strstr(match, token)) != NULL) {
        const int starts_token =
            DB_BOOL((match == extensions) || (match[-1] == ' '));
        const int ends_token = DB_BOOL((match[token_length] == '\0') ||
                                       (match[token_length] == ' '));
        if ((starts_token != 0) && (ends_token != 0)) {
            return 1;
        }
        match += token_length;
    }
    return 0;
}
#endif

db_presentation_buffer_age_t
db_glfw_query_presentation_buffer_age(GLFWwindow *window,
                                      uint32_t history_capacity) {
    if (window == NULL) {
        return db_presentation_buffer_age_resolve(
            DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U, history_capacity);
    }
#ifdef __linux__
    const EGLDisplay egl_display = glfwGetEGLDisplay();
    const EGLSurface egl_surface = glfwGetEGLSurface(window);
    const char *const egl_extensions =
        (egl_display != EGL_NO_DISPLAY)
            ? eglQueryString(egl_display, EGL_EXTENSIONS)
            : NULL;
    if ((egl_display != EGL_NO_DISPLAY) && (egl_surface != EGL_NO_SURFACE) &&
        (db_extension_list_has_token(egl_extensions, "EGL_EXT_buffer_age") !=
         0)) {
        EGLint age = 0;
        if (eglQuerySurface(egl_display, egl_surface, EGL_BUFFER_AGE_EXT,
                            &age) == EGL_TRUE) {
            return db_presentation_buffer_age_resolve(
                DB_PRESENTATION_BUFFER_AGE_EGL,
                (age > 0) ? db_checked_int_to_u32(
                                DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_GL,
                                "egl_presentation_buffer_age", age)
                          : 0U,
                history_capacity);
        }
    }
    Display *const display = glfwGetX11Display();
    const GLXWindow drawable = glfwGetGLXWindow(window);
    union {
        GLFWglproc generic;
        db_glx_query_drawable_fn_t typed;
    } query_drawable = {.generic = glfwGetProcAddress("glXQueryDrawable")};
    union {
        GLFWglproc generic;
        db_glx_query_extensions_string_fn_t typed;
    } query_extensions = {.generic =
                              glfwGetProcAddress("glXQueryExtensionsString")};
    const char *const glx_extensions =
        ((display != NULL) && (query_extensions.typed != NULL))
            ? query_extensions.typed(display, DefaultScreen(display))
            : NULL;
    if ((display != NULL) && (drawable != 0U) &&
        (query_drawable.typed != NULL) &&
        (db_extension_list_has_token(glx_extensions, "GLX_EXT_buffer_age") !=
         0)) {
        unsigned int age = 0U;
        query_drawable.typed(display, drawable, GLX_BACK_BUFFER_AGE_EXT, &age);
        return db_presentation_buffer_age_resolve(
            DB_PRESENTATION_BUFFER_AGE_GLX, age, history_capacity);
    }
#endif
    return db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U, history_capacity);
}

void db_glfw_log_presentation_buffer_age(
    const char *backend_name, const db_presentation_buffer_age_t *age) {
    db_presentation_log_buffer_age(backend_name, age);
}

static int db_glfw_loop_should_continue(void *user_data) {
    const db_glfw_loop_t *loop = (const db_glfw_loop_t *)user_data;
    return DB_BOOL((loop != NULL) &&
                   (glfwWindowShouldClose(loop->window) == 0));
}

typedef struct {
    GLFWwindow *window;
    const char *backend;
    db_glfw_framebuffer_extent_t old_framebuffer;
    db_glfw_framebuffer_extent_t observed_framebuffer;
} db_glfw_resize_wait_context_t;

static db_sync_wait_result_t db_glfw_resize_wait_attempt(void *user_data,
                                                         uint64_t timeout_ns) {
    db_glfw_resize_wait_context_t *const context =
        (db_glfw_resize_wait_context_t *)user_data;
    const double timeout_seconds = (double)timeout_ns / 1000000000.0;
    glfwWaitEventsTimeout(timeout_seconds);
    context->observed_framebuffer =
        db_glfw_get_framebuffer_extent(context->window, context->backend);
    const int changed = DB_BOOL((context->observed_framebuffer.valid != 0) &&
                                ((context->observed_framebuffer.width !=
                                  context->old_framebuffer.width) ||
                                 (context->observed_framebuffer.height !=
                                  context->old_framebuffer.height)));
    return db_sync_wait_result_make(
        changed ? DB_SYNC_WAIT_COMPLETED : DB_SYNC_WAIT_TIMEOUT, 0U, 0U, 0U,
        changed ? "resize_observed" : "resize_pending");
}

static void db_glfw_loop_apply_scheduled_resize(void *user_data,
                                                uint32_t frame_index) {
    db_glfw_loop_t *const loop = (db_glfw_loop_t *)user_data;
    if ((loop == NULL) || (loop->resize_enabled == 0) ||
        (loop->resize_applied != 0) ||
        (frame_index != loop->resize_schedule.frame)) {
        return;
    }
    int old_window_width = 0;
    int old_window_height = 0;
    glfwGetWindowSize(loop->window, &old_window_width, &old_window_height);
    const db_glfw_framebuffer_extent_t old_framebuffer =
        db_glfw_get_framebuffer_extent(loop->window, loop->backend);
    const db_log_field_t request_fields[] = {
        DB_LOG_U64("frame", frame_index),
        DB_LOG_I64("old_window_width", old_window_width),
        DB_LOG_I64("old_window_height", old_window_height),
        DB_LOG_U64("requested_window_width", loop->resize_schedule.width),
        DB_LOG_U64("requested_window_height", loop->resize_schedule.height),
    };
    db_log_info(loop->backend, "window_resize_request", request_fields,
                DB_LOG_FIELD_COUNT(request_fields));
    glfwSetWindowSize(
        loop->window,
        db_checked_u32_to_int(loop->backend, "requested_window_width",
                              loop->resize_schedule.width),
        db_checked_u32_to_int(loop->backend, "requested_window_height",
                              loop->resize_schedule.height));

    db_glfw_resize_wait_context_t resize_wait = {
        .window = loop->window,
        .backend = loop->backend,
        .old_framebuffer = old_framebuffer,
        .observed_framebuffer = old_framebuffer,
    };
    const db_sync_wait_result_t wait_result = db_progress_execute(
        DB_PROGRESS_GLFW_RESIZE, db_glfw_resize_wait_attempt, &resize_wait);
    db_progress_log_outcome(loop->backend, "observe_resize",
                            DB_PROGRESS_GLFW_RESIZE, &wait_result);
    const db_glfw_framebuffer_extent_t new_framebuffer =
        resize_wait.observed_framebuffer;
    int new_window_width = 0;
    int new_window_height = 0;
    float content_scale_x = 0.0F;
    float content_scale_y = 0.0F;
    glfwGetWindowSize(loop->window, &new_window_width, &new_window_height);
    glfwGetWindowContentScale(loop->window, &content_scale_x, &content_scale_y);
    const int observed =
        DB_BOOL((new_framebuffer.valid != 0) &&
                ((new_framebuffer.width != old_framebuffer.width) ||
                 (new_framebuffer.height != old_framebuffer.height)));
    const db_log_field_t resize_fields[] = {
        DB_LOG_TOKEN("code", observed ? "resize_observed" : "resize_timeout"),
        DB_LOG_U64("frame", frame_index),
        DB_LOG_I64("old_window_width", old_window_width),
        DB_LOG_I64("old_window_height", old_window_height),
        DB_LOG_U64("old_framebuffer_width", old_framebuffer.width),
        DB_LOG_U64("old_framebuffer_height", old_framebuffer.height),
        DB_LOG_I64("new_window_width", new_window_width),
        DB_LOG_I64("new_window_height", new_window_height),
        DB_LOG_U64("new_framebuffer_width", new_framebuffer.width),
        DB_LOG_U64("new_framebuffer_height", new_framebuffer.height),
        DB_LOG_DOUBLE("content_scale_x", content_scale_x),
        DB_LOG_DOUBLE("content_scale_y", content_scale_y),
        DB_LOG_BOOL("observed", observed),
        DB_LOG_BOOL("timeout", observed == 0),
    };
    if (observed == 0) {
        db_log_fail(loop->backend, "window_resize_error", resize_fields,
                    DB_LOG_FIELD_COUNT(resize_fields));
    }
    db_log_info(loop->backend, "presentation_resize", resize_fields,
                DB_LOG_FIELD_COUNT(resize_fields));
    if (loop->resolved_runtime != NULL) {
        const db_presentation_transform_t transform =
            db_display_presentation_transform(new_framebuffer.width,
                                              new_framebuffer.height);
        db_display_log_presentation_contract(
            loop->backend, loop->resolved_runtime, &transform);
    }
    loop->resize_applied = 1;
}

static db_display_frame_loop_result_t
db_glfw_loop_frame_adapter(void *user_data, uint32_t frame_index,
                           double elapsed_ms) {
    const db_glfw_loop_t *loop = (const db_glfw_loop_t *)user_data;
    if ((loop == NULL) || (loop->frame_fn == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    glfwPollEvents();
    return loop->frame_fn(loop->user_data, frame_index, elapsed_ms);
}

db_display_frame_loop_run_result_t db_glfw_run_loop(db_glfw_loop_t *loop) {
    if ((loop == NULL) || (loop->frame_fn == NULL) || (loop->window == NULL)) {
        return (db_display_frame_loop_run_result_t){0};
    }
    const char *const resize_text =
        db_runtime_option_get(DB_RUNTIME_OPT_RESIZE_AT_FRAME);
    loop->resize_enabled =
        DB_BOOL(db_resize_schedule_parse(resize_text, &loop->resize_schedule));
    const db_display_frame_loop_t shared_loop = {
        .backend = loop->backend,
        .fps_cap = loop->fps_cap,
        .frame_limit = loop->frame_limit,
        .user_data = loop,
        .should_continue_fn = db_glfw_loop_should_continue,
        .pre_frame_fn = db_glfw_loop_apply_scheduled_resize,
        .frame_fn = db_glfw_loop_frame_adapter,
    };
    return db_display_run_frame_loop(&shared_loop);
}
