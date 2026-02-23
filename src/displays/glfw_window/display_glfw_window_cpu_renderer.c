#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../bench_config.h"
#include "../display_env_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "display_glfw_window_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define BACKEND_NAME "display_glfw_window_cpu_renderer"
#define DB_CAP_MODE_CPU_GLFW_WINDOW "cpu_glfw_window"
#define RENDERER_NAME "renderer_cpu_renderer"

static uint32_t db_cpu_glfw_frame_limit(void) {
    return db_glfw_resolve_frame_limit(BACKEND_NAME);
}

#define OPENGL_CONTEXT_VERSION_MAJOR 2
#define OPENGL_CONTEXT_VERSION_MINOR 1

#define BG_A 1.0F
#define BG_B 0.0F
#define BG_G 0.0F
#define BG_R 0.0F

static void db_present_cpu_framebuffer(GLFWwindow *window) {
    uint32_t pixel_width = 0U;
    uint32_t pixel_height = 0U;
    const uint32_t *pixels =
        db_renderer_cpu_renderer_pixels_rgba8(&pixel_width, &pixel_height);
    if ((pixels == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        db_failf(BACKEND_NAME, "cpu renderer returned invalid framebuffer");
    }

    int framebuffer_width_px = 0;
    int framebuffer_height_px = 0;
    glfwGetFramebufferSize(window, &framebuffer_width_px,
                           &framebuffer_height_px);
    glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);
    glClearColor(BG_R, BG_G, BG_B, BG_A);
    glClear(GL_COLOR_BUFFER_BIT);

    const int32_t width_i32 = db_checked_u32_to_i32(
        BACKEND_NAME, "cpu_framebuffer_width", pixel_width);
    const int32_t height_i32 = db_checked_u32_to_i32(
        BACKEND_NAME, "cpu_framebuffer_height", pixel_height);

    glRasterPos2f(-1.0F, 1.0F);
    glPixelZoom((float)framebuffer_width_px / (float)pixel_width,
                -(float)framebuffer_height_px / (float)pixel_height);
    glDrawPixels(width_i32, height_i32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelZoom(1.0F, 1.0F);
}

int main(void) {
    db_validate_runtime_environment(BACKEND_NAME, DB_ENV_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval = db_glfw_resolve_swap_interval();
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME);
    const uint32_t frame_limit = db_cpu_glfw_frame_limit();
    int hash_enabled = 0;
    int hash_every_frame = 0;
    db_glfw_resolve_hash_settings(BACKEND_NAME, &hash_enabled,
                                  &hash_every_frame);

    GLFWwindow *window = db_glfw_create_opengl_window(
        BACKEND_NAME, "CPU Renderer GLFW DriverBench", BENCH_WINDOW_WIDTH_PX,
        BENCH_WINDOW_HEIGHT_PX, OPENGL_CONTEXT_VERSION_MAJOR,
        OPENGL_CONTEXT_VERSION_MINOR, 0, swap_interval);

    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    (void)db_display_log_gl_runtime_api(BACKEND_NAME, runtime_version,
                                        runtime_renderer);

    db_renderer_cpu_renderer_init();

    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    uint64_t frames = 0U;
    double bench_start = db_glfw_time_seconds();
    double next_progress_log_due_ms = 0.0;
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        hash_enabled, hash_every_frame, "bo_hash");

    while (!glfwWindowShouldClose(window) && !db_should_stop()) {
        if ((frame_limit > 0U) && (frames >= frame_limit)) {
            break;
        }

        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();

        const double frame_time_s = (double)frames / BENCH_TARGET_FPS_D;
        db_renderer_cpu_renderer_render_frame(frame_time_s);
        db_present_cpu_framebuffer(window);

        if (hash_enabled != 0) {
            uint32_t pixel_width = 0U;
            uint32_t pixel_height = 0U;
            const uint32_t *pixels = db_renderer_cpu_renderer_pixels_rgba8(
                &pixel_width, &pixel_height);
            if (pixels == NULL) {
                db_failf(BACKEND_NAME,
                         "cpu renderer returned invalid framebuffer");
            }
            const size_t byte_count =
                (size_t)((uint64_t)pixel_width * (uint64_t)pixel_height *
                         sizeof(uint32_t));
            const uint64_t frame_hash = db_fnv1a64_bytes(pixels, byte_count);
            db_display_hash_tracker_record(BACKEND_NAME, &hash_tracker, frames,
                                           frame_hash);
        }

        glfwSwapBuffers(window);
        db_glfw_sleep_to_fps_cap(frame_start_s, fps_cap);
        frames++;

        const double bench_ms =
            (db_glfw_time_seconds() - bench_start) * BENCH_MS_PER_SEC_D;
        db_benchmark_log_periodic(
            "CPU", RENDERER_NAME, BACKEND_NAME, frames, work_unit_count,
            bench_ms, DB_CAP_MODE_CPU_GLFW_WINDOW, &next_progress_log_due_ms,
            BENCH_LOG_INTERVAL_MS_D);
    }

    const double bench_ms =
        (db_glfw_time_seconds() - bench_start) * BENCH_MS_PER_SEC_D;
    db_benchmark_log_final("CPU", RENDERER_NAME, BACKEND_NAME, frames,
                           work_unit_count, bench_ms,
                           DB_CAP_MODE_CPU_GLFW_WINDOW);
    db_display_hash_tracker_log_final(BACKEND_NAME, &hash_tracker);

    db_renderer_cpu_renderer_shutdown();
    db_glfw_destroy_window(window);
    return 0;
}
