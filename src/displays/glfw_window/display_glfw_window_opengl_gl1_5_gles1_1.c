#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdint.h>

#include "../../core/db_core.h"
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../../renderers/renderer_gl_common.h"
#include "../bench_config.h"
#include "../display_gl_runtime_common.h"
#include "display_glfw_window_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define BACKEND_NAME "display_glfw_window_opengl_gl1_5_gles1_1"
#define RENDERER_NAME "renderer_opengl_gl1_5_gles1_1"
#define REMOTE_DISPLAY_OVERRIDE_ENV "DRIVERBENCH_ALLOW_REMOTE_DISPLAY"

#define OPENGL_CONTEXT_VERSION_MAJOR 2
#define OPENGL_CONTEXT_VERSION_MINOR 1

#define BG_R 0.04F
#define BG_G 0.04F
#define BG_B 0.07F
#define BG_A 1.0F

int main(void) {
    db_validate_runtime_environment(BACKEND_NAME, REMOTE_DISPLAY_OVERRIDE_ENV);
    db_install_signal_handlers();

    int swap_interval = db_glfw_resolve_swap_interval();
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME);
    int is_gles = 0;
    GLFWwindow *window = db_glfw_create_gl15_or_gles11_window(
        BACKEND_NAME, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        OPENGL_CONTEXT_VERSION_MAJOR, OPENGL_CONTEXT_VERSION_MINOR,
        swap_interval, &is_gles);
    db_gl_set_proc_address_loader(
        (db_gl_get_proc_address_fn_t)glfwGetProcAddress);

    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    const int runtime_is_gles = db_display_log_gl_runtime_api(
        BACKEND_NAME, runtime_version, runtime_renderer);
    if (runtime_is_gles != 0) {
        db_display_validate_gles_1x_runtime_or_fail(BACKEND_NAME,
                                                    runtime_version);
    }
    if ((is_gles != 0) && (runtime_is_gles == 0)) {
        db_infof(BACKEND_NAME, "context creation reported GLES fallback, but "
                               "runtime API is OpenGL");
    }

    db_renderer_opengl_gl1_5_gles1_1_init();
    const char *capability_mode =
        db_renderer_opengl_gl1_5_gles1_1_capability_mode();
    const uint32_t work_unit_count =
        db_renderer_opengl_gl1_5_gles1_1_work_unit_count();

    uint64_t frames = 0;
    double bench_start = db_glfw_time_seconds();
    double next_progress_log_due_ms = 0.0;

    while (!glfwWindowShouldClose(window) && !db_should_stop()) {
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
        db_renderer_opengl_gl1_5_gles1_1_render_frame(frame_time_s);

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

    db_renderer_opengl_gl1_5_gles1_1_shutdown();
    db_glfw_destroy_window(window);
    return 0;
}
