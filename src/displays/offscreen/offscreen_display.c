#include "core/db_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_frame_source.h"
#include "../../core/db_hash.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/cpu_renderer.h"
#include "../../renderers/gl_common.h"
#include "../../renderers/renderer_identity.h"
#include "core/db_render_types.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/vk_renderer.h"
#endif
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_hash_common.h"
#include "../display_presentation_policy.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#ifdef DB_HAS_GLFW
#include "../../renderers/gl_api.h"
#include "../gl_display_runtime.h"
#define GLFW_INCLUDE_NONE
#include "../glfw_window/glfw_window_common.h"
#include <GLFW/glfw3.h>
#endif

typedef struct {
    const db_display_frame_step_t *frame_step;
    const db_display_gl_renderer_ops_t *renderer_ops;
    const char *backend_name;
    int framebuffer_width_px;
    int framebuffer_height_px;
    unsigned int offscreen_fbo;
    db_frame_source_t *core;
} db_offscreen_gl3_loop_ctx_t;

typedef struct {
    const db_display_frame_step_t *frame_step;
    db_pixel_surface_t surface;
    db_frame_source_t *core;
} db_offscreen_cpu_loop_ctx_t;

#ifdef DB_HAS_VULKAN_API
typedef struct {
    const char *backend_name;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *output_hash_tracker;
    int state_hash_enabled;
    int output_hash_enabled;
    db_frame_source_t *core;
} db_offscreen_vulkan_loop_ctx_t;
#endif

static uint64_t
db_offscreen_cpu_surface_hash(const db_pixel_surface_t *surface) {
    if ((surface == NULL) || (surface->pixels == NULL) ||
        (surface->pixel_width == 0U) || (surface->pixel_height == 0U)) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                        "invalid offscreen cpu surface");
    }
    const size_t pixel_bytes =
        (surface->format == DB_PIXEL_FORMAT_RGBA16F) ? 8U : 4U;
    const size_t stride_bytes = db_checked_mul_size(
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN, "offscreen_stride_bytes",
        (size_t)surface->pixel_width, pixel_bytes);
    return db_hash_working_rgba8(surface->pixels, surface->format,
                                 surface->pixel_width, surface->pixel_height,
                                 stride_bytes, 0);
}

static db_pixel_surface_t
db_offscreen_cpu_surface_create(db_pixel_format_t format, uint32_t pixel_width,
                                uint32_t pixel_height) {
    const size_t pixel_count = db_checked_mul_size(
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN, "offscreen_pixel_count",
        (size_t)pixel_width, (size_t)pixel_height);
    if ((pixel_width == 0U) || (pixel_height == 0U) || (pixel_count == 0U)) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                        "invalid offscreen cpu surface size: %ux%u",
                        pixel_width, pixel_height);
    }
    db_pixel_surface_t surface = {
        .pixel_width = pixel_width,
        .pixel_height = pixel_height,
        .pixels = NULL,
        .format = format,
    };
    if (db_display_pixel_format_uses_rgba16f(format) != 0) {
        surface.pixels = db_calloc_or_fail(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, "offscreen_cpu_pixels_rgba16f",
            db_checked_mul_size(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                                "offscreen_rgba16f_channel_count", pixel_count,
                                DB_RGBA16F_CHANNELS_PER_PIXEL),
            sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES);
    } else {
        surface.pixels = db_calloc_or_fail(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, "offscreen_cpu_pixels_rgba8",
            pixel_count, sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    }
    return surface;
}

static void db_offscreen_cpu_surface_destroy(db_pixel_surface_t *surface) {
    if (surface == NULL) {
        return;
    }
    free(surface->pixels);
    *surface = (db_pixel_surface_t){0};
}

static db_display_frame_loop_result_t
db_offscreen_cpu_frame_step(void *user_data, uint32_t frame_index,
                            double elapsed_ms) {
    db_offscreen_cpu_loop_ctx_t *ctx = (db_offscreen_cpu_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->frame_step == NULL) || (ctx->core == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_frame_plan_t plan;
    db_frame_source_generate(ctx->core, frame_index, NULL, &plan);
    (void)db_cpu_render_frame_to_surface(&plan, &ctx->surface, NULL);
    uint64_t output_hash = 0U;
    if ((ctx->frame_step->output_hash_enabled != 0) &&
        (ctx->frame_step->output_hash_tracker != NULL)) {
        output_hash = db_offscreen_cpu_surface_hash(&ctx->surface);
        db_frame_source_commit_success_with_hash(ctx->core, &plan, output_hash);
    } else {
        db_frame_source_commit_success(ctx->core, &plan);
    }
    if ((ctx->frame_step->state_hash_enabled != 0) &&
        (ctx->frame_step->state_hash_tracker != NULL)) {
        db_display_hash_tracker_record(ctx->frame_step->state_hash_tracker,
                                       plan.expected_state_hash);
    }
    if ((ctx->frame_step->output_hash_enabled != 0) &&
        (ctx->frame_step->output_hash_tracker != NULL)) {
        db_display_hash_tracker_record(ctx->frame_step->output_hash_tracker,
                                       output_hash);
    }
    db_log_progress_periodic(
        ctx->frame_step->api_name, ctx->frame_step->renderer_name,
        ctx->frame_step->backend, (uint64_t)frame_index + 1U,
        ctx->frame_step->work_unit_count, elapsed_ms,
        ctx->frame_step->next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

#ifdef DB_HAS_VULKAN_API
static db_display_frame_loop_result_t
db_offscreen_vulkan_frame_step(void *user_data, uint32_t frame_index,
                               double elapsed_ms) {
    (void)elapsed_ms;
    const db_offscreen_vulkan_loop_ctx_t *ctx =
        (const db_offscreen_vulkan_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->core == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_frame_plan_t plan;
    db_frame_source_generate(ctx->core, frame_index, NULL, &plan);
    const db_vk_frame_result_t frame_result = db_vk_render_frame(&plan);
    uint64_t output_hash = 0U;
    if ((frame_result == DB_VK_FRAME_OK) && (ctx->output_hash_enabled != 0)) {
        output_hash = db_vk_output_hash();
        db_frame_source_commit_success_with_hash(ctx->core, &plan, output_hash);
    } else if (frame_result == DB_VK_FRAME_OK) {
        db_frame_source_commit_success(ctx->core, &plan);
    }
    if ((ctx != NULL) && (ctx->state_hash_enabled != 0) &&
        (frame_result == DB_VK_FRAME_OK)) {
        db_display_hash_tracker_record(ctx->state_hash_tracker,
                                       plan.expected_state_hash);
    }
    if ((ctx != NULL) && (ctx->output_hash_enabled != 0) &&
        (frame_result == DB_VK_FRAME_OK)) {
        db_display_hash_tracker_record(ctx->output_hash_tracker, output_hash);
    }
    if ((ctx != NULL) && (frame_result == DB_VK_FRAME_STOP)) {
        DB_RUNTIME_STATUS(ctx->backend_name, "renderer requested stop");
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    if (frame_result == DB_VK_FRAME_RETRY) {
        return DB_DISPLAY_FRAME_LOOP_RETRY;
    }
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_offscreen_vulkan(const db_cli_config_t *cfg) {
    db_install_signal_handlers();
    const db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    const db_vk_wsi_config_t headless_wsi_config = {
        .backend_name = DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
    };
    (void)db_vk_init(&headless_wsi_config,
                     (cfg != NULL) ? cfg->vsync_enabled
                                   : BENCH_DEFAULT_VSYNC_ENABLED,
                     &resolved_runtime.renderer);
    db_vk_set_output_hash_enabled(
        resolved_runtime.hash_settings.output_hash_enabled);
    db_frame_source_t core;
    db_frame_source_init(
        &core, &(const db_frame_source_config_t){
                   .benchmark_configuration = &resolved_runtime.benchmark,
                   .working_format =
                       resolved_runtime.renderer.format.surface_pixel_format,
               });

    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_resolved_runtime(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, &resolved_runtime,
            DB_DISPLAY_HASH_KEY_STATE, DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_offscreen_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
        .state_hash_tracker = &hash_trackers.state,
        .output_hash_tracker = &hash_trackers.output,
        .state_hash_enabled = resolved_runtime.hash_settings.state_hash_enabled,
        .output_hash_enabled =
            resolved_runtime.hash_settings.output_hash_enabled,
        .core = &core,
    };
    const db_display_frame_loop_t loop = {
        .backend = DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .user_data = &loop_ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = NULL,
        .frame_fn = db_offscreen_vulkan_frame_step,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_display_run_frame_loop(&loop);
    db_vk_set_present_metrics(
        loop_result.frame_ema_ms, loop_result.jitter_ema_ms,
        loop_result.frame_p50_ms, loop_result.frame_p95_ms,
        loop_result.frame_p99_ms, loop_result.retries);
    db_vk_shutdown();
    db_frame_source_shutdown(&core);
    db_display_dual_hash_trackers_log_final(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                                            &hash_trackers);
    return EXIT_SUCCESS;
}
#endif

#ifdef DB_HAS_GLFW
static void offscreen_gl3_pre_frame(void *user_data, uint32_t frame_index) {
    (void)user_data;
    (void)frame_index;
    glfwPollEvents();
}

static db_display_frame_loop_result_t
db_offscreen_gl3_frame_step(void *user_data, uint32_t frame_index,
                            double elapsed_ms) {
    db_offscreen_gl3_loop_ctx_t *ctx = (db_offscreen_gl3_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->frame_step == NULL) ||
        (ctx->renderer_ops == NULL) || (ctx->core == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }

    db_frame_plan_t plan;
    db_frame_source_generate(ctx->core, frame_index, NULL, &plan);

    db_gl_bind_framebuffer(GL_FRAMEBUFFER, ctx->offscreen_fbo);
    db_gl_set_viewport_px(ctx->framebuffer_width_px,
                          ctx->framebuffer_height_px);
    const db_gl_presentation_frame_t presentation = {
        .destination_width = db_checked_int_to_u32(ctx->frame_step->backend,
                                                   "framebuffer_width_px",
                                                   ctx->framebuffer_width_px),
        .destination_height = db_checked_int_to_u32(ctx->frame_step->backend,
                                                    "framebuffer_height_px",
                                                    ctx->framebuffer_height_px),
        .force_full = 1,
        .repair_reason = "offscreen_full",
    };
    db_display_gl_render_frame(ctx->renderer_ops->renderer, &plan,
                               &presentation);
    uint64_t output_hash_value = 0U;
    if (ctx->frame_step->output_hash_enabled != 0) {
        output_hash_value = ctx->renderer_ops->working_hash();
        db_frame_source_commit_success_with_hash(ctx->core, &plan,
                                                 output_hash_value);
    } else {
        db_frame_source_commit_success(ctx->core, &plan);
    }
    db_display_gl_frame_step(ctx->frame_step, frame_index, elapsed_ms, 1,
                             plan.expected_state_hash, 1, output_hash_value);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_offscreen_glfw_gl1(const db_cli_config_t *cfg) {
    db_cli_config_t glfw_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0};
    glfw_cfg.glfw_window_hidden = 1;
    if (db_display_should_force_hidden_glfw_offscreen_full_draw(
            DB_GL_RENDERER_GL1_5_GLES1_1, &glfw_cfg) != 0) {
        // The offscreen GL1 route is implemented with a hidden GLFW window as a
        // deterministic harness, not as a preserved-default-framebuffer
        // presentation path.
        glfw_cfg.backbuffer_draw_full = 1;
        db_runtime_option_set_backbuffer_draw_full(1);
    }
    return db_run_glfw_window(DB_API_OPENGL, DB_GL_RENDERER_GL1_5_GLES1_1,
                              &glfw_cfg);
}

static int db_run_offscreen_gl3_fbo(const db_cli_config_t *cfg) {
    db_install_signal_handlers();
    const uint64_t start_ns = db_now_ns_monotonic();
    const db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);
    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(DB_GL_RENDERER_GL3_3);

    GLFWwindow *window = db_glfw_create_opengl_window(
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
        "OpenGL 3.3 Offscreen FBO DriverBench", BENCH_WINDOW_WIDTH_PX,
        BENCH_WINDOW_HEIGHT_PX, 3, 3, 1, 0, DB_GLFW_WINDOW_HIDDEN);
    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, DB_GL_RENDERER_GL3_3,
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, -1);
    if (db_gl_context_probe_texture_float_support() == 0) {
        DB_RUNTIME_FAIL(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
            "GL3 offscreen float hashing requires texture-float support");
    }

    renderer_ops.init(&resolved_runtime.renderer);
    const uint32_t work_unit_count = renderer_ops.work_unit_count();

    const int offscreen_width = BENCH_WINDOW_WIDTH_PX;
    const int offscreen_height = BENCH_WINDOW_HEIGHT_PX;
    unsigned int offscreen_texture = 0U;
    unsigned int offscreen_fbo = 0U;
    if (db_gl_texture_create_rgba16f(&offscreen_texture, offscreen_width,
                                     offscreen_height, NULL) == 0) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
                        "failed to create GL3 offscreen RGBA16F color texture");
    }
    db_gl_gen_framebuffers(1, &offscreen_fbo);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, offscreen_fbo);
    db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, offscreen_texture, 0);
    if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
                        "failed to create GL3 offscreen framebuffer");
    }

    db_frame_source_t core;
    db_frame_source_init(
        &core, &(const db_frame_source_config_t){
                   .benchmark_configuration = &resolved_runtime.benchmark,
                   .working_format =
                       resolved_runtime.renderer.format.surface_pixel_format,
               });

    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_resolved_runtime(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, &resolved_runtime,
            DB_DISPLAY_HASH_KEY_STATE, DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    double next_progress_log_due_ms = 0.0;
    const db_display_frame_step_t frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_OPENGL),
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, renderer_ops.renderer_name,
        &hash_trackers.output, &hash_trackers.state, &next_progress_log_due_ms,
        work_unit_count, resolved_runtime.hash_settings.output_hash_enabled,
        resolved_runtime.hash_settings.state_hash_enabled);
    db_offscreen_gl3_loop_ctx_t loop_ctx = {
        .frame_step = &frame_step,
        .renderer_ops = &renderer_ops,
        .backend_name = DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
        .framebuffer_width_px = offscreen_width,
        .framebuffer_height_px = offscreen_height,
        .offscreen_fbo = offscreen_fbo,
        .core = &core,
    };
    const db_display_frame_loop_t loop = {
        .backend = DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .user_data = &loop_ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = offscreen_gl3_pre_frame,
        .frame_fn = db_offscreen_gl3_frame_step,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_display_run_frame_loop(&loop);
    const uint64_t frames = loop_result.frames;

    const double total_ms =
        (double)(db_now_ns_monotonic() - start_ns) / DB_NS_PER_MS;
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_OPENGL), renderer_ops.renderer_name,
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, frames, work_unit_count,
        total_ms, renderer_ops.draw_stats);
    db_display_dual_hash_trackers_log_final(
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN_GL3_FBO, &hash_trackers);

    if (offscreen_fbo != 0U) {
        db_gl_delete_framebuffers(1, &offscreen_fbo);
    }
    if (offscreen_texture != 0U) {
        db_gl_texture_delete_if_valid(&offscreen_texture);
    }
    db_frame_source_shutdown(&core);
    renderer_ops.shutdown();
    db_glfw_destroy_window(window);
    return EXIT_SUCCESS;
}
#endif

static int db_run_offscreen_cpu(const db_cli_config_t *cfg) {
    const uint64_t start_ns = db_now_ns_monotonic();
    db_install_signal_handlers();

    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);
    db_cpu_init(&resolved_runtime.renderer);
    const uint32_t work_unit_count = db_cpu_work_unit_count();

    db_frame_source_t core;
    db_frame_source_init(
        &core, &(const db_frame_source_config_t){
                   .benchmark_configuration = &resolved_runtime.benchmark,
                   .working_format =
                       resolved_runtime.renderer.format.surface_pixel_format,
               });

    double next_progress_log_due_ms = 0.0;
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_resolved_runtime(
            DB_BACKEND_NAME_DISPLAY_OFFSCREEN, &resolved_runtime,
            DB_DISPLAY_HASH_KEY_STATE, DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    const db_display_frame_step_t frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_CPU), DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
        db_renderer_name_cpu(), &hash_trackers.output, &hash_trackers.state,
        &next_progress_log_due_ms, work_unit_count,
        resolved_runtime.hash_settings.output_hash_enabled,
        resolved_runtime.hash_settings.state_hash_enabled);
    db_offscreen_cpu_loop_ctx_t loop_ctx = {
        .frame_step = &frame_step,
        .surface = db_offscreen_cpu_surface_create(
            resolved_runtime.renderer.format.surface_pixel_format,
            resolved_runtime.renderer.execution.grid_cols,
            resolved_runtime.renderer.execution.grid_rows),
        .core = &core,
    };
    const db_display_frame_loop_t loop = {
        .backend = DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .user_data = &loop_ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = NULL,
        .frame_fn = db_offscreen_cpu_frame_step,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_display_run_frame_loop(&loop);
    const uint64_t frames = loop_result.frames;

    if (resolved_runtime.hash_settings.output_hash_enabled != 0) {
        hash_trackers.output.final_hash =
            db_offscreen_cpu_surface_hash(&loop_ctx.surface);
    }

    const double total_ms =
        (double)(db_now_ns_monotonic() - start_ns) / DB_NS_PER_MS;
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN, frames, work_unit_count, total_ms,
        NULL);
    db_display_dual_hash_trackers_log_final(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                                            &hash_trackers);
    db_offscreen_cpu_surface_destroy(&loop_ctx.surface);
    db_frame_source_shutdown(&core);
    db_cpu_shutdown();
    return EXIT_SUCCESS;
}

int db_run_offscreen(db_api_t api, db_gl_renderer_t renderer,
                     const db_cli_config_t *cfg) {
    db_dispatch_validate_backend_or_fail(DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
                                         DB_OFFSCREEN_DISPLAY, api, renderer);

    if (api == DB_API_CPU) {
        return db_run_offscreen_cpu(cfg);
    }

#ifdef DB_HAS_VULKAN_API
    if (api == DB_API_VULKAN) {
        return db_run_offscreen_vulkan(cfg);
    }
#endif

#ifdef DB_HAS_GLFW
    const db_offscreen_gl_route_t gl_route =
        db_dispatch_offscreen_gl_route(renderer);
    if (gl_route == DB_OFFSCREEN_GL_ROUTE_GLFW_HIDDEN) {
        return db_run_offscreen_glfw_gl1(cfg);
    }
    if (gl_route == DB_OFFSCREEN_GL_ROUTE_GL3_FBO) {
        return db_run_offscreen_gl3_fbo(cfg);
    }
#endif

    DB_RUNTIME_FAIL(
        DB_BACKEND_NAME_DISPLAY_OFFSCREEN,
        "unsupported offscreen configuration (api=%s, "
        "renderer=%s); supported: CPU offscreen, "
        "GL1 via GLFW hidden-window offscreen, GL3 via offscreen FBO",
        db_dispatch_api_name(api), db_dispatch_gl_renderer_name(renderer));
    return EXIT_FAILURE;
}
