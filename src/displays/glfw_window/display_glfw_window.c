#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#ifdef DB_HAS_OPENGL_API
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../../renderers/renderer_gl_common.h"
#endif
#include "../../renderers/renderer_identity.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#endif
#include "../../config/benchmark_config.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "display_glfw_window_common.h"
#ifdef DB_HAS_OPENGL_API
#include "display_glfw_window_gl_hash_common.h"
#endif

#ifdef DB_HAS_OPENGL_API
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#ifdef __has_include
#if __has_include(<GL/glcorearb.h>)
#include <GL/glcorearb.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif
#endif

#define BACKEND_NAME_CPU "display_glfw_window_cpu_renderer"
#ifdef DB_HAS_OPENGL_API
#define BACKEND_NAME_GL "display_glfw_window_opengl"
#endif
#ifdef DB_HAS_VULKAN_API
#define BACKEND_NAME_VK "display_glfw_window_vulkan"
#endif

typedef enum {
    DB_GLFW_LOOP_CONTINUE = 0,
    DB_GLFW_LOOP_STOP = 1,
} db_glfw_loop_result_t;

typedef db_glfw_loop_result_t (*db_glfw_frame_fn_t)(void *user_data,
                                                    uint64_t frame_index);

typedef struct {
    const char *backend;
    db_glfw_frame_fn_t frame_fn;
    double fps_cap;
    uint32_t frame_limit;
    void *user_data;
    GLFWwindow *window;
} db_glfw_loop_t;

static uint64_t db_glfw_run_loop(const db_glfw_loop_t *loop) {
    uint64_t frames = 0U;
    while (!glfwWindowShouldClose(loop->window) && !db_should_stop()) {
        if ((loop->frame_limit > 0U) && (frames >= loop->frame_limit)) {
            break;
        }
        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();
        if (loop->frame_fn(loop->user_data, frames) == DB_GLFW_LOOP_STOP) {
            break;
        }
        db_glfw_sleep_to_fps_cap(loop->backend, frame_start_s, loop->fps_cap);
        frames++;
    }
    return frames;
}

#ifdef DB_HAS_OPENGL_API
static void db_present_cpu_framebuffer(GLFWwindow *window) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    const uint32_t *pixels =
        db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    if ((pixels == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        db_failf(BACKEND_NAME_CPU, "cpu renderer returned invalid framebuffer");
    }

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);
    glClearColor(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                 BENCH_CLEAR_COLOR_B_F, BENCH_CLEAR_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);

    const int32_t width_i32 = db_checked_u32_to_i32(
        BACKEND_NAME_CPU, "cpu_framebuffer_width", pixel_width);
    const int32_t height_i32 = db_checked_u32_to_i32(
        BACKEND_NAME_CPU, "cpu_framebuffer_height", pixel_height);

    glRasterPos2f(-1.0F, 1.0F);
    glPixelZoom((float)framebuffer_width_px / (float)pixel_width,
                -(float)framebuffer_height_px / (float)pixel_height);
    glDrawPixels(width_i32, height_i32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelZoom(1.0F, 1.0F);
}

typedef struct {
    double bench_start;
    double next_progress_log_due_ms;
    db_display_hash_tracker_t *hash_tracker;
    int hash_enabled;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_cpu_loop_ctx_t;

static db_glfw_loop_result_t db_glfw_cpu_frame(void *user_data,
                                               uint64_t frame_index) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    const double frame_time_s = (double)frame_index / BENCH_TARGET_FPS_D;
    db_renderer_cpu_renderer_render_frame(frame_time_s);
    db_present_cpu_framebuffer(ctx->window);

    if (ctx->hash_enabled != 0) {
        uint32_t pixel_width = 0U;
        uint32_t pixel_height = 0U;
        const uint32_t *pixels =
            db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
        if (pixels == NULL) {
            db_failf(BACKEND_NAME_CPU,
                     "cpu renderer returned invalid framebuffer");
        }
        const size_t byte_count =
            (size_t)((uint64_t)pixel_width * (uint64_t)pixel_height *
                     sizeof(uint32_t));
        const uint64_t frame_hash = db_fnv1a64_bytes(pixels, byte_count);
        db_display_hash_tracker_record(BACKEND_NAME_CPU, ctx->hash_tracker,
                                       frame_index, frame_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(db_dispatch_api_name(DB_API_CPU),
                              db_renderer_name_cpu(), BACKEND_NAME_CPU,
                              logged_frames, ctx->work_unit_count, bench_ms,
                              "cpu_glfw_window", &ctx->next_progress_log_due_ms,
                              BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(void) {
    db_validate_runtime_environment(BACKEND_NAME_CPU,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval = db_glfw_resolve_swap_interval(BACKEND_NAME_CPU);
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME_CPU);
    const uint32_t frame_limit = db_glfw_resolve_frame_limit(BACKEND_NAME_CPU);
    int hash_enabled = 0;
    int hash_every_frame = 0;
    db_glfw_resolve_hash_settings(BACKEND_NAME_CPU, &hash_enabled,
                                  &hash_every_frame);

    GLFWwindow *window = db_glfw_create_opengl_window(
        BACKEND_NAME_CPU, "CPU Renderer GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, 2, 1, 0, swap_interval);

    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    (void)db_display_log_gl_runtime_api(BACKEND_NAME_CPU, runtime_version,
                                        runtime_renderer);

    db_renderer_cpu_renderer_init();

    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        hash_enabled, hash_every_frame, "bo_hash");
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .bench_start = bench_start,
        .next_progress_log_due_ms = 0.0,
        .hash_tracker = &hash_tracker,
        .hash_enabled = hash_enabled,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME_CPU, frames,
                           work_unit_count, bench_ms, "cpu_glfw_window");
    db_display_hash_tracker_log_final(BACKEND_NAME_CPU, &hash_tracker);

    db_renderer_cpu_renderer_shutdown();
    db_glfw_destroy_window(window);
    return 0;
}
#else
static int db_run_glfw_window_cpu(void) {
    db_failf(BACKEND_NAME_CPU,
             "CPU + glfw_window presentation requires OpenGL support in this "
             "build");
    return 1;
}
#endif

#ifdef DB_HAS_OPENGL_API
static const char *db_gl_backend_name(db_gl_renderer_t renderer) {
    (void)renderer;
    return BACKEND_NAME_GL;
}

static const char *db_gl_renderer_name(db_gl_renderer_t renderer) {
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
               ? db_renderer_name_opengl_gl1_5_gles1_1()
               : db_renderer_name_opengl_gl3_3();
}

static void db_gl_renderer_init(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_init();
        return;
    }
    db_renderer_opengl_gl3_3_init();
}

static void db_gl_renderer_render_frame(db_gl_renderer_t renderer,
                                        double frame_time_s) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_render_frame(frame_time_s);
        return;
    }
    db_renderer_opengl_gl3_3_render_frame(frame_time_s);
}

static void db_gl_renderer_shutdown(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_renderer_opengl_gl1_5_gles1_1_shutdown();
        return;
    }
    db_renderer_opengl_gl3_3_shutdown();
}

static const char *db_gl_renderer_capability_mode(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_capability_mode();
    }
    return db_renderer_opengl_gl3_3_capability_mode();
}

static uint32_t db_gl_renderer_work_unit_count(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return db_renderer_opengl_gl1_5_gles1_1_work_unit_count();
    }
    return db_renderer_opengl_gl3_3_work_unit_count();
}

typedef struct {
    const char *backend_name;
    const char *capability_mode;
    const char *renderer_name;
    db_display_hash_tracker_t *hash_tracker;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    db_gl_renderer_t renderer;
    double bench_start;
    double next_progress_log_due_ms;
    int hash_enabled;
    uint32_t work_unit_count;
    GLFWwindow *window;
} db_glfw_opengl_loop_ctx_t;

static db_glfw_loop_result_t db_glfw_opengl_frame(void *user_data,
                                                  uint64_t frame_index) {
    db_glfw_opengl_loop_ctx_t *ctx = (db_glfw_opengl_loop_ctx_t *)user_data;
    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(ctx->window, &framebuffer_width_px,
                           &framebuffer_height_px);
    glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);
    glClearColor(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                 BENCH_CLEAR_COLOR_B_F, BENCH_CLEAR_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);

    const double frame_time_s = (double)frame_index / BENCH_TARGET_FPS_D;
    db_gl_renderer_render_frame(ctx->renderer, frame_time_s);

    if (ctx->hash_enabled != 0) {
        const uint64_t frame_hash = db_gl_hash_framebuffer_rgba8_or_fail(
            ctx->backend_name, framebuffer_width_px, framebuffer_height_px,
            ctx->hash_scratch);
        db_display_hash_tracker_record(ctx->backend_name, ctx->hash_tracker,
                                       frame_index, frame_hash);
    }

    glfwSwapBuffers(ctx->window);
    const uint64_t logged_frames = frame_index + 1U;
    const double bench_ms =
        (db_glfw_time_seconds() - ctx->bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_periodic(
        db_dispatch_api_name(DB_API_OPENGL), ctx->renderer_name,
        ctx->backend_name, logged_frames, ctx->work_unit_count, bench_ms,
        ctx->capability_mode, &ctx->next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_opengl(db_gl_renderer_t renderer) {
    const char *backend_name = db_gl_backend_name(renderer);
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval = db_glfw_resolve_swap_interval(backend_name);
    const double fps_cap = db_glfw_resolve_fps_cap(backend_name);
    const uint32_t frame_limit = db_glfw_resolve_frame_limit(backend_name);
    int hash_enabled = 0;
    int hash_every_frame = 0;
    db_glfw_resolve_hash_settings(backend_name, &hash_enabled,
                                  &hash_every_frame);

    GLFWwindow *window = NULL;
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        const int gl_legacy_context_major = 2;
        const int gl_legacy_context_minor = 1;
        int is_gles = 0;
        window = db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            gl_legacy_context_major, gl_legacy_context_minor, swap_interval,
            &is_gles);
        db_gl_set_proc_address_loader(
            (db_gl_get_proc_address_fn_t)glfwGetProcAddress);
        const char *runtime_version = (const char *)glGetString(GL_VERSION);
        const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
        const int runtime_is_gles = db_display_log_gl_runtime_api(
            backend_name, runtime_version, runtime_renderer);
        if (runtime_is_gles != 0) {
            db_display_validate_gles_1x_runtime_or_fail(backend_name,
                                                        runtime_version);
        }
        if ((is_gles != 0) && (runtime_is_gles == 0)) {
            db_infof(backend_name, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
        }
    } else {
        const int gl3_context_major = 3;
        const int gl3_context_minor = 3;
        window = db_glfw_create_opengl_window(
            backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, gl3_context_major,
            gl3_context_minor, 1, swap_interval);
        const char *runtime_version = (const char *)glGetString(GL_VERSION);
        const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
        (void)db_display_log_gl_runtime_api(backend_name, runtime_version,
                                            runtime_renderer);
    }

    db_gl_renderer_init(renderer);
    const char *capability_mode = db_gl_renderer_capability_mode(renderer);
    const uint32_t work_unit_count = db_gl_renderer_work_unit_count(renderer);
    const double bench_start = db_glfw_time_seconds();
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        hash_enabled, hash_every_frame, "framebuffer_hash");
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};
    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = db_gl_renderer_name(renderer),
        .hash_tracker = &hash_tracker,
        .hash_scratch = &hash_scratch,
        .renderer = renderer,
        .bench_start = bench_start,
        .next_progress_log_due_ms = 0.0,
        .hash_enabled = hash_enabled,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = &loop_ctx,
        .window = window,
    };
    const uint64_t frames = db_glfw_run_loop(&loop);

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * DB_MS_PER_SECOND_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL),
                           db_gl_renderer_name(renderer), backend_name, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(backend_name, &hash_tracker);

    db_gl_renderer_shutdown(renderer);
    db_glfw_destroy_window(window);
    db_gl_hash_scratch_release(&hash_scratch);
    return 0;
}
#endif

#ifdef DB_HAS_VULKAN_API
// NOLINTBEGIN(misc-include-cleaner)
static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count, void *user_data) {
    (void)user_data;
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface,
                                          void *user_data) {
    (void)user_data;
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height, void *user_data) {
    (void)user_data;
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

static db_glfw_loop_result_t db_glfw_vulkan_frame(void *user_data,
                                                  uint64_t frame_index) {
    (void)frame_index;
    const char *backend_name = (const char *)user_data;
    const db_vk_frame_result_t frame_result =
        db_renderer_vulkan_1_2_multi_gpu_render_frame();
    if (frame_result == DB_VK_FRAME_STOP) {
        db_infof(backend_name, "renderer requested stop");
        return DB_GLFW_LOOP_STOP;
    }
    return DB_GLFW_LOOP_CONTINUE;
}

static int db_run_glfw_window_vulkan(void) {
    db_validate_runtime_environment(BACKEND_NAME_VK,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME_VK);
    const uint32_t frame_limit = db_glfw_resolve_frame_limit(BACKEND_NAME_VK);

    GLFWwindow *window = db_glfw_create_no_api_window(
        BACKEND_NAME_VK, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(BACKEND_NAME_VK, runtime_api_version,
                                      "(selected by renderer)");

    const db_vk_wsi_config_t wsi_config = {
        .window_handle = window,
        .user_data = (void *)BACKEND_NAME_VK,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    db_renderer_vulkan_1_2_multi_gpu_init(&wsi_config);
    const db_glfw_loop_t loop = {
        .backend = BACKEND_NAME_VK,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .user_data = (void *)BACKEND_NAME_VK,
        .window = window,
    };
    (void)db_glfw_run_loop(&loop);
    db_renderer_vulkan_1_2_multi_gpu_shutdown();
    db_glfw_destroy_window(window);
    return 0;
}
// NOLINTEND(misc-include-cleaner)
#endif

int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer) {
    if (api == DB_API_CPU) {
        return db_run_glfw_window_cpu();
    }
#ifdef DB_HAS_VULKAN_API
    if (api == DB_API_VULKAN) {
        return db_run_glfw_window_vulkan();
    }
#endif
#ifdef DB_HAS_OPENGL_API
    return db_run_glfw_window_opengl(renderer);
#else
    (void)renderer;
    db_failf(BACKEND_NAME_CPU, "OpenGL backend is unavailable in this build");
    return 1;
#endif
}
