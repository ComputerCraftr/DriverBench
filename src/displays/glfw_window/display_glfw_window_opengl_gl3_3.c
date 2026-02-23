#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdint.h>

#include "../../core/db_core.h"
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../bench_config.h"
#include "../display_env_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_hash_common.h"
#include "display_glfw_window_common.h"

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

#include "display_glfw_window_gl_hash_common.h"

#define BACKEND_NAME "display_glfw_window_opengl_gl3_3"

#define OPENGL_CONTEXT_VERSION_MAJOR 3
#define OPENGL_CONTEXT_VERSION_MINOR 3

#define BG_A 1.0F
#define BG_B 0.07F
#define BG_G 0.04F
#define BG_R 0.04F
#define RENDERER_NAME "renderer_opengl_gl3_3"

int main(void) {
    db_validate_runtime_environment(BACKEND_NAME, DB_ENV_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    int swap_interval = db_glfw_resolve_swap_interval();
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME);
    const uint32_t frame_limit = db_glfw_resolve_frame_limit(BACKEND_NAME);
    int hash_enabled = 0;
    int hash_every_frame = 0;
    db_glfw_resolve_hash_settings(BACKEND_NAME, &hash_enabled,
                                  &hash_every_frame);
    GLFWwindow *window = db_glfw_create_opengl_window(
        BACKEND_NAME, "OpenGL 3.3 Shader GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        OPENGL_CONTEXT_VERSION_MAJOR, OPENGL_CONTEXT_VERSION_MINOR, 1,
        swap_interval);
    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    (void)db_display_log_gl_runtime_api(BACKEND_NAME, runtime_version,
                                        runtime_renderer);

    db_renderer_opengl_gl3_3_init();
    const char *capability_mode = db_renderer_opengl_gl3_3_capability_mode();
    const uint32_t work_unit_count = db_renderer_opengl_gl3_3_work_unit_count();

    uint64_t frames = 0;
    double bench_start = db_glfw_time_seconds();
    double next_progress_log_due_ms = 0.0;
    db_display_hash_tracker_t hash_tracker = db_display_hash_tracker_create(
        hash_enabled, hash_every_frame, "framebuffer_hash");
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};

    while (!glfwWindowShouldClose(window) && !db_should_stop()) {
        if ((frame_limit > 0U) && (frames >= frame_limit)) {
            break;
        }
        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();
        int framebuffer_width_px = 0;
        int framebuffer_height_px = 0;
        glfwGetFramebufferSize(window, &framebuffer_width_px,
                               &framebuffer_height_px);
        glViewport(0, 0, framebuffer_width_px, framebuffer_height_px);
        glClearColor(BG_R, BG_G, BG_B, BG_A);
        glClear(GL_COLOR_BUFFER_BIT);

        double frame_time_s = (double)frames / BENCH_TARGET_FPS_D;
        db_renderer_opengl_gl3_3_render_frame(frame_time_s);

        if (hash_enabled != 0) {
            const uint64_t frame_hash = db_gl_hash_framebuffer_rgba8_or_fail(
                BACKEND_NAME, framebuffer_width_px, framebuffer_height_px,
                &hash_scratch);
            db_display_hash_tracker_record(BACKEND_NAME, &hash_tracker, frames,
                                           frame_hash);
        }

        glfwSwapBuffers(window);
        db_glfw_sleep_to_fps_cap(frame_start_s, fps_cap);
        frames++;

        double bench_ms =
            (db_glfw_time_seconds() - bench_start) * BENCH_MS_PER_SEC_D;
        db_benchmark_log_periodic("OpenGL", RENDERER_NAME, BACKEND_NAME, frames,
                                  work_unit_count, bench_ms, capability_mode,
                                  &next_progress_log_due_ms,
                                  BENCH_LOG_INTERVAL_MS_D);
    }

    double bench_ms =
        (db_glfw_time_seconds() - bench_start) * BENCH_MS_PER_SEC_D;
    db_benchmark_log_final("OpenGL", RENDERER_NAME, BACKEND_NAME, frames,
                           work_unit_count, bench_ms, capability_mode);
    db_display_hash_tracker_log_final(BACKEND_NAME, &hash_tracker);

    db_renderer_opengl_gl3_3_shutdown();
    db_glfw_destroy_window(window);
    db_gl_hash_scratch_release(&hash_scratch);
    return 0;
}
