#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_identity.h"
#include "../display_cpu_hash_common.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_hash_readback_common.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_hash_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#ifdef DB_HAS_GLFW
#include "../../renderers/renderer_gl_api.h"
#include "../display_gl_runtime_common.h"
#define GLFW_INCLUDE_NONE
#include "../glfw_window/display_glfw_window_common.h"
#include <GLFW/glfw3.h>
#endif

#define BACKEND_NAME "display_offscreen"
#define BACKEND_NAME_GL3_OFFSCREEN "display_offscreen_gl3_fbo"

typedef struct {
    const db_display_frame_step_t *frame_step;
    const db_display_gl_renderer_ops_t *renderer_ops;
    const char *backend_name;
    db_gl_framebuffer_hash_f16_scratch_t *hash_scratch;
    int framebuffer_width_px;
    int framebuffer_height_px;
    unsigned int offscreen_fbo;
} db_offscreen_gl3_loop_ctx_t;

typedef struct {
    const db_display_frame_step_t *frame_step;
} db_offscreen_cpu_loop_ctx_t;

static uint64_t db_offscreen_cpu_bo_hash(void) {
    return db_display_cpu_renderer_bo_hash_or_fail(BACKEND_NAME);
}

static db_display_frame_loop_result_t
db_offscreen_cpu_frame_step(void *user_data, uint32_t frame_index,
                            double elapsed_ms) {
    db_offscreen_cpu_loop_ctx_t *ctx = (db_offscreen_cpu_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->frame_step == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_display_cpu_render_present_and_hash(ctx->frame_step, frame_index,
                                           elapsed_ms, NULL, NULL,
                                           db_offscreen_cpu_bo_hash);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

#ifdef DB_HAS_GLFW
static void db_offscreen_gl3_pre_frame(void *user_data, uint32_t frame_index) {
    (void)user_data;
    (void)frame_index;
    db_glfw_poll_events();
}

static db_display_frame_loop_result_t
db_offscreen_gl3_frame_step(void *user_data, uint32_t frame_index,
                            double elapsed_ms) {
    db_offscreen_gl3_loop_ctx_t *ctx = (db_offscreen_gl3_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->frame_step == NULL) ||
        (ctx->renderer_ops == NULL) || (ctx->hash_scratch == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }

    db_gl_bind_framebuffer(GL_FRAMEBUFFER, ctx->offscreen_fbo);
    db_gl_set_viewport_px(ctx->framebuffer_width_px,
                          ctx->framebuffer_height_px);
    ctx->renderer_ops->render_frame_glfw(frame_index, ctx->framebuffer_width_px,
                                         ctx->framebuffer_height_px);
    const db_display_gl_hash_rgba16f_cb_ctx_t output_hash_ctx = {
        .backend_name = ctx->backend_name,
        .framebuffer_width_px = ctx->framebuffer_width_px,
        .framebuffer_height_px = ctx->framebuffer_height_px,
        .scratch = ctx->hash_scratch,
    };
    db_display_gl_frame_step_with_hash_fns(
        ctx->frame_step, frame_index, elapsed_ms,
        db_display_gl_renderer_ops_state_hash_cb, (void *)ctx->renderer_ops,
        db_display_gl_hash_rgba16f_framebuffer_cb, (void *)&output_hash_ctx);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_offscreen_glfw_gl1(const db_cli_config_t *cfg) {
    db_cli_config_t glfw_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0};
    glfw_cfg.offscreen_enabled = 1;
    return db_run_glfw_window(DB_API_OPENGL, DB_GL_RENDERER_GL1_5_GLES1_1,
                              &glfw_cfg);
}

static int db_run_offscreen_gl3_fbo(const db_cli_config_t *cfg) {
    db_install_signal_handlers();
    const uint64_t start_ns = db_now_ns_monotonic();
    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;
    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(DB_GL_RENDERER_GL3_3);

    GLFWwindow *window = db_glfw_create_opengl_window(
        BACKEND_NAME_GL3_OFFSCREEN, "OpenGL 3.3 Offscreen FBO DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, 3, 3, 1, 0, 1);
    (void)db_display_prepare_and_validate_gl_runtime(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, DB_GL_RENDERER_GL3_3,
        BACKEND_NAME_GL3_OFFSCREEN, DB_DISPLAY_GL_RUNTIME_LOG_ENABLED, -1, NULL,
        NULL);
    if (db_gl_context_probe_texture_float_support() == 0) {
        db_failf(BACKEND_NAME_GL3_OFFSCREEN,
                 "GL3 offscreen float hashing requires texture-float support");
    }

    renderer_ops.init();
    const char *capability_mode = renderer_ops.runtime_capability_mode();
    const uint32_t work_unit_count = renderer_ops.work_unit_count();

    const int offscreen_width = BENCH_WINDOW_WIDTH_PX;
    const int offscreen_height = BENCH_WINDOW_HEIGHT_PX;
    unsigned int offscreen_texture = 0U;
    unsigned int offscreen_fbo = 0U;
    if (db_gl_texture_create_rgba16f(&offscreen_texture, offscreen_width,
                                     offscreen_height, NULL) == 0) {
        db_failf(BACKEND_NAME_GL3_OFFSCREEN,
                 "failed to create GL3 offscreen RGBA16F color texture");
    }
    db_gl_gen_framebuffers(1, &offscreen_fbo);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, offscreen_fbo);
    db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, offscreen_texture, 0);
    if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        db_failf(BACKEND_NAME_GL3_OFFSCREEN,
                 "failed to create GL3 offscreen framebuffer");
    }

    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            BACKEND_NAME_GL3_OFFSCREEN, &runtime_hash_cfg, "state_hash",
            "fbo_hash16f");
    db_gl_framebuffer_hash_f16_scratch_t hash_scratch = {0};
    double next_progress_log_due_ms = 0.0;
    const db_display_frame_step_t frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_OPENGL), BACKEND_NAME_GL3_OFFSCREEN,
        capability_mode, renderer_ops.renderer_name, &hash_trackers.output,
        &hash_trackers.state, &next_progress_log_due_ms, work_unit_count,
        hash_settings.output_hash_enabled, hash_settings.state_hash_enabled);
    db_offscreen_gl3_loop_ctx_t loop_ctx = {
        .frame_step = &frame_step,
        .renderer_ops = &renderer_ops,
        .backend_name = BACKEND_NAME_GL3_OFFSCREEN,
        .hash_scratch = &hash_scratch,
        .framebuffer_width_px = offscreen_width,
        .framebuffer_height_px = offscreen_height,
        .offscreen_fbo = offscreen_fbo,
    };
    const db_display_frame_loop_t loop = {
        .backend = BACKEND_NAME_GL3_OFFSCREEN,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = db_offscreen_gl3_pre_frame,
        .frame_fn = db_offscreen_gl3_frame_step,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_display_run_frame_loop(&loop);
    const uint64_t frames = loop_result.frames;

    if (hash_settings.state_hash_enabled != 0) {
        hash_trackers.state.final_hash = renderer_ops.state_hash();
    }
    if (hash_settings.output_hash_enabled != 0) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, offscreen_fbo);
        hash_trackers.output.final_hash =
            db_gl_hash_framebuffer_rgba16f_or_fail(
                BACKEND_NAME_GL3_OFFSCREEN, offscreen_width, offscreen_height,
                &hash_scratch, 1);
    }

    const double total_ms =
        (double)(db_now_ns_monotonic() - start_ns) / DB_NS_PER_MS;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL),
                           renderer_ops.renderer_name,
                           BACKEND_NAME_GL3_OFFSCREEN, frames, work_unit_count,
                           total_ms, capability_mode);
    db_display_dual_hash_trackers_log_final(BACKEND_NAME_GL3_OFFSCREEN,
                                            &hash_trackers);

    db_gl_hash_f16_scratch_release(&hash_scratch);
    if (offscreen_fbo != 0U) {
        db_gl_delete_framebuffers(1, &offscreen_fbo);
    }
    if (offscreen_texture != 0U) {
        db_gl_texture_delete_if_valid(&offscreen_texture);
    }
    renderer_ops.shutdown();
    db_glfw_destroy_window(window);
    return EXIT_SUCCESS;
}
#endif

static int db_run_offscreen_cpu(const db_cli_config_t *cfg) {
    const uint64_t start_ns = db_now_ns_monotonic();
    db_install_signal_handlers();

    const db_display_runtime_hash_config_t runtime_hash_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0);
    const db_display_runtime_config_t runtime_cfg = runtime_hash_cfg.runtime;
    const db_display_hash_settings_t hash_settings =
        runtime_hash_cfg.hash_settings;

    db_renderer_cpu_renderer_init();
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();

    double next_progress_log_due_ms = 0.0;
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_runtime(
            BACKEND_NAME, &runtime_hash_cfg, "state_hash", "bo_hash");
    const db_display_frame_step_t frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_CPU), BACKEND_NAME, capability_mode,
        db_renderer_name_cpu(), &hash_trackers.output, &hash_trackers.state,
        &next_progress_log_due_ms, work_unit_count,
        hash_settings.output_hash_enabled, hash_settings.state_hash_enabled);
    db_offscreen_cpu_loop_ctx_t loop_ctx = {
        .frame_step = &frame_step,
    };
    const db_display_frame_loop_t loop = {
        .backend = BACKEND_NAME,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .user_data = &loop_ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = NULL,
        .frame_fn = db_offscreen_cpu_frame_step,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_display_run_frame_loop(&loop);
    const uint64_t frames = loop_result.frames;

    db_display_dual_hash_trackers_finalize(&hash_trackers, &hash_settings,
                                           db_renderer_cpu_renderer_state_hash,
                                           db_offscreen_cpu_bo_hash);

    const double total_ms =
        (double)(db_now_ns_monotonic() - start_ns) / DB_NS_PER_MS;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU),
                           db_renderer_name_cpu(), BACKEND_NAME, frames,
                           work_unit_count, total_ms, capability_mode);
    db_display_dual_hash_trackers_log_final(BACKEND_NAME, &hash_trackers);
    db_renderer_cpu_renderer_shutdown();
    return EXIT_SUCCESS;
}

int db_run_offscreen(db_api_t api, db_gl_renderer_t renderer,
                     const db_cli_config_t *cfg) {
    db_dispatch_validate_backend_or_fail(BACKEND_NAME, DB_DISPLAY_OFFSCREEN,
                                         api, renderer);

    if (api == DB_API_CPU) {
        return db_run_offscreen_cpu(cfg);
    }

#ifdef DB_HAS_GLFW
    const db_display_offscreen_gl_route_t gl_route =
        db_dispatch_offscreen_gl_route(renderer);
    if (gl_route == DB_DISPLAY_OFFSCREEN_GL_ROUTE_GLFW_HIDDEN) {
        return db_run_offscreen_glfw_gl1(cfg);
    }
    if (gl_route == DB_DISPLAY_OFFSCREEN_GL_ROUTE_GL3_FBO) {
        return db_run_offscreen_gl3_fbo(cfg);
    }
#endif

    db_failf(BACKEND_NAME,
             "unsupported offscreen configuration (api=%s, "
             "renderer=%s); supported: CPU offscreen, "
             "GL1 via GLFW hidden-window offscreen, GL3 via offscreen FBO",
             db_dispatch_api_name(api), db_dispatch_gl_renderer_name(renderer));
    return EXIT_FAILURE;
}
