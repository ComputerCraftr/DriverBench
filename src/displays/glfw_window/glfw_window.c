#include "core/db_format_contract.h"
#include "core/db_log.h"
#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan_core.h>
#endif
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_frame_source.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/cpu_renderer.h"
#include "../../renderers/gl_common.h"
#include "../../renderers/renderer_identity.h"
#include "core/db_render_types.h"
#ifdef DB_HAS_VULKAN_API
#include "../../renderers/vulkan_1_2_multi_gpu/vk_renderer.h"
#endif
#include "../../config/benchmark_config.h"
#include "../../renderers/gl_hash_readback.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_hash_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#include "../gl_display_runtime.h"
#include "glfw_window_common.h"
#include "glfw_window_internal.h"

enum { DB_PRESENTATION_BUFFER_AGE_QUALIFICATION_FRAMES = 4U };

#ifdef DB_HAS_VULKAN_API
typedef struct {
    const char *backend_name;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *output_hash_tracker;
    uint32_t frame_limit;
    db_frame_source_t *core;
    GLFWwindow *window;
} db_glfw_vulkan_loop_ctx_t;
#endif

typedef struct {
    db_gl_shadow_present_state_t shared;
    uint32_t last_viewport_w;
    uint32_t last_viewport_h;
} db_cpu_present_gl_state_t;

typedef struct {
    const char *api_name;
    const char *capability_mode;
    double next_progress_log_due_ms;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    db_pixel_format_t framebuffer_hash_format;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    int state_hash_enabled;
    int output_hash_enabled;
    int debug_clear_default_framebuffer;
    db_cpu_present_gl_state_t *present;
    uint32_t work_unit_count;
    GLFWwindow *window;
    db_frame_source_t *core;
} db_glfw_cpu_loop_ctx_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
} db_glfw_cpu_present_mode_t;

const db_native_output_capability_t g_glfw_native_output_capability = {
    .native_hdr_verified = 0,
    .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
    .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
    .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
    .unavailable_reason = "glfw_native_hdr_chain_unavailable",
};

static void db_glfw_cpu_present_surface(GLFWwindow *window,
                                        db_cpu_present_gl_state_t *state,
                                        const db_frame_plan_t *plan,
                                        int debug_clear_default_framebuffer) {
    if ((window == NULL) || (state == NULL) || (plan == NULL)) {
        return;
    }
    db_glfw_framebuffer_extent_t extent = db_glfw_get_framebuffer_extent(
        window, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU);
    if (extent.valid == 0) {
        extent = (db_glfw_framebuffer_extent_t){
            .width = plan->pixel_width,
            .height = plan->pixel_height,
            .valid = 1,
        };
    }

    const int viewport_changed = (state->last_viewport_w != extent.width) ||
                                 (state->last_viewport_h != extent.height);

    if (viewport_changed != 0) {
        state->last_viewport_w = extent.width;
        state->last_viewport_h = extent.height;
        db_gl_set_viewport_px(
            db_checked_u32_to_i32(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                  "framebuffer_width_px", extent.width),
            db_checked_u32_to_i32(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                  "framebuffer_height_px", extent.height));
    }

    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        debug_clear_default_framebuffer != 0);
    db_gl_shadow_present_full_upload_target_t target = {0};
    const uint32_t pixel_width = plan->pixel_width;
    const uint32_t pixel_height = plan->pixel_height;
    if (db_gl_shadow_present_begin_full_upload_target(
            &state->shared, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
            pixel_width, pixel_height, 0, &target) == 0) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                        "failed to acquire cpu GLFW upload surface");
    }
    (void)db_cpu_render_frame_to_surface_mode(
        plan, &target.pixel_surface, DB_CPU_RENDER_TARGET_REPLACE_SURFACE,
        NULL);
    db_gl_shadow_present_present_full_upload_target(
        &state->shared, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, pixel_width,
        pixel_height, &target);
}

uint64_t db_glfw_hash_canonical_default_framebuffer_or_fail(
    const char *backend_name, GLFWwindow *window, uint32_t canonical_width,
    uint32_t canonical_height, db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((backend_name == NULL) || (window == NULL) || (scratch == NULL)) {
        DB_RUNTIME_FAIL("display_glfw_window",
                        "invalid canonical framebuffer hash inputs");
    }
    const db_glfw_framebuffer_extent_t extent =
        db_glfw_get_framebuffer_extent(window, backend_name);
    if (extent.valid == 0) {
        DB_RUNTIME_FAIL(backend_name, "invalid GLFW framebuffer extent");
    }
    const uint8_t *pixels = db_gl_read_framebuffer_rgba8_or_fail(
        backend_name, extent.width, extent.height, scratch);
    const size_t stride_bytes = db_checked_mul_size(
        backend_name, "canonical_fb_row_bytes",
        db_checked_u32_to_size(backend_name, "canonical_fb_row_pixels",
                               extent.width),
        DB_RGBA8_BYTES_PER_PIXEL);
    const uint64_t hash = db_hash_sdr_framebuffer_rgba8_canonical(
        pixels, extent.width, extent.height, stride_bytes, 1, canonical_width,
        canonical_height);
    (void)db_gl_upload_stream_end_read(&scratch->stream, backend_name);
    return hash;
}

static db_glfw_cpu_present_mode_t db_glfw_cpu_present_mode_or_fail(
    const db_cpu_present_gl_state_t *present,
    const db_display_resolved_format_config_t *format) {
    if ((present == NULL) || (format == NULL)) {
        DB_RUNTIME_FAIL(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                        "missing cpu GLFW present state");
    }
    db_glfw_cpu_present_mode_t mode = {
        .capability_mode = {0},
    };
    const db_gl_runtime_mode_desc_t present_desc =
        db_gl_runtime_mode_desc_present(&present->shared,
                                        present->shared.preserve_mode);
    char present_mode[DB_GL_CAPABILITY_MODE_MAX] = {0};
    db_gl_runtime_mode_format_present(present_mode, sizeof(present_mode),
                                      &present_desc);
    (void)db_snprintf(
        mode.capability_mode, sizeof(mode.capability_mode),
        "cpu_renderer, %s, format=%s", present_mode,
        db_display_pixel_format_name(format->surface_pixel_format));
    return mode;
}

static db_display_frame_loop_result_t
db_glfw_cpu_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    db_glfw_cpu_loop_ctx_t *ctx = (db_glfw_cpu_loop_ctx_t *)user_data;
    if (ctx == NULL || ctx->core == NULL) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_frame_plan_t plan;
    if (db_frame_source_generate(ctx->core, frame_index, NULL, &plan) !=
        DB_FRAME_PLAN_OK) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_glfw_cpu_present_surface(ctx->window, ctx->present, &plan,
                                ctx->debug_clear_default_framebuffer);
    const int hash_output =
        db_display_frame_step_should_hash_output(&ctx->frame_step, frame_index);
    const int hash_state =
        db_display_frame_step_should_hash_state(&ctx->frame_step, frame_index);
    uint64_t output_hash_value = 0U;
    if (hash_output != 0) {
        output_hash_value = db_glfw_hash_canonical_default_framebuffer_or_fail(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, ctx->window,
            plan.grid_cols, plan.grid_rows, ctx->hash_scratch);
        db_frame_source_commit_success_with_hash(ctx->core, &plan,
                                                 output_hash_value);
    } else {
        db_frame_source_commit_success(ctx->core, &plan);
    }
    glfwSwapBuffers(ctx->window);

    db_display_gl_frame_step(&ctx->frame_step, frame_index, elapsed_ms,
                             hash_state, plan.expected_state_hash, hash_output,
                             output_hash_value);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_cpu(const db_cli_config_t *cfg) {
    db_validate_runtime_environment(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();

    const int swap_interval = DB_BOOL((cfg != NULL) && (cfg->vsync_enabled));
    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    db_frame_source_t core;
    db_frame_source_init(
        &core, &(const db_frame_source_config_t){
                   .benchmark_configuration = &resolved_runtime.benchmark,
                   .working_format =
                       resolved_runtime.renderer.format.surface_pixel_format,
               });

    const int gl_legacy_context_major = 2;
    const int gl_legacy_context_minor = 1;
    int is_gles = 0;
    GLFWwindow *window = db_glfw_create_gl1_5_or_gles1_1_window(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        "CPU Renderer GLFW DriverBench", BENCH_WINDOW_WIDTH_PX,
        BENCH_WINDOW_HEIGHT_PX, gl_legacy_context_major,
        gl_legacy_context_minor, swap_interval, &is_gles,
        ((cfg != NULL) && (cfg->glfw_window_hidden != 0))
            ? DB_GLFW_WINDOW_HIDDEN
            : DB_GLFW_WINDOW_VISIBLE);
    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress,
        DB_GL_RENDERER_GL1_5_GLES1_1, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        is_gles);
    db_display_apply_native_output_capability_or_fail(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &resolved_runtime,
        &g_glfw_native_output_capability);
    const db_glfw_framebuffer_extent_t initial_extent =
        db_glfw_get_framebuffer_extent(window,
                                       DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU);
    resolved_runtime.presentation = db_display_presentation_transform(
        initial_extent.width, initial_extent.height);
    db_display_log_presentation_contract(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &resolved_runtime,
        &resolved_runtime.presentation);
    db_cpu_present_gl_state_t present = {0};
    db_gl_shadow_present_init_runtime(&present.shared, 1, 1,
                                      &resolved_runtime.renderer.format, 1U);
    db_gl_shadow_present_set_preserve_mode(
        &present.shared, DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS);
    const db_glfw_cpu_present_mode_t present_mode =
        db_glfw_cpu_present_mode_or_fail(&present,
                                         &resolved_runtime.renderer.format);
    db_cpu_init(&resolved_runtime.renderer);

    db_gl_shadow_present_log_decision(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, "cpu present",
        &resolved_runtime.renderer.format, &present.shared);

    const uint32_t work_unit_count = db_cpu_work_unit_count();
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_resolved_runtime(
            DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &resolved_runtime,
            DB_DISPLAY_HASH_KEY_STATE, DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};
    db_glfw_cpu_loop_ctx_t loop_ctx = {
        .api_name = db_dispatch_api_name(DB_API_CPU),
        .capability_mode = present_mode.capability_mode,
        .next_progress_log_due_ms = 0.0,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .framebuffer_hash_tracker = &hash_trackers.output,
        .framebuffer_hash_format =
            resolved_runtime.renderer.format.framebuffer_hash_format,
        .hash_scratch = &hash_scratch,
        .state_hash_enabled = resolved_runtime.hash_settings.state_hash_enabled,
        .output_hash_enabled =
            resolved_runtime.hash_settings.output_hash_enabled,
        .debug_clear_default_framebuffer =
            resolved_runtime.display.debug_clear_default_framebuffer,
        .present = &present,
        .work_unit_count = work_unit_count,
        .window = window,
        .core = &core,
    };
    loop_ctx.frame_step = db_display_frame_step_make(
        loop_ctx.api_name, DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        db_renderer_name_cpu(), loop_ctx.framebuffer_hash_tracker,
        loop_ctx.state_hash_tracker, &loop_ctx.next_progress_log_due_ms,
        loop_ctx.work_unit_count, loop_ctx.output_hash_enabled,
        loop_ctx.state_hash_enabled, resolved_runtime.display.frame_limit);
    db_glfw_loop_t loop = {
        .backend = DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU,
        .frame_fn = db_glfw_cpu_frame,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
        .resolved_runtime = &resolved_runtime,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    const uint64_t frames = loop_result.frames;

    const double bench_ms =
        DB_TO_F64(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_CPU), db_renderer_name_cpu(),
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, frames, work_unit_count,
        bench_ms, NULL, db_cpu_execution_report);
    db_display_dual_hash_trackers_log_final(
        DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_CPU, &hash_trackers);

    db_gl_hash_scratch_release(&hash_scratch);
    db_frame_source_shutdown(&core);
    db_cpu_shutdown();
    db_gl_shadow_present_shutdown(&present.shared);
    db_glfw_destroy_window(window);
    return 0;
}

#ifdef DB_HAS_VULKAN_API
static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count) {
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface) {
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height) {
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

static db_display_frame_loop_result_t
db_glfw_vulkan_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    (void)elapsed_ms;
    db_glfw_vulkan_loop_ctx_t *ctx = (db_glfw_vulkan_loop_ctx_t *)user_data;
    if (ctx == NULL || ctx->core == NULL) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    db_frame_plan_t plan;
    if (db_frame_source_generate(ctx->core, frame_index, NULL, &plan) !=
        DB_FRAME_PLAN_OK) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    const int hash_output = db_display_hash_tracker_should_sample(
        ctx->output_hash_tracker, frame_index, ctx->frame_limit);
    const int hash_state = db_display_hash_tracker_should_sample(
        ctx->state_hash_tracker, frame_index, ctx->frame_limit);
    db_vk_set_output_hash_enabled(hash_output);
    const db_vk_frame_result_t frame_result = db_vk_render_frame(&plan);
    uint64_t output_hash = 0U;
    if (frame_result == DB_VK_FRAME_OK) {
        if (hash_output != 0) {
            output_hash = db_vk_output_hash();
            db_frame_source_commit_success_with_hash(ctx->core, &plan,
                                                     output_hash);
        } else {
            db_frame_source_commit_success(ctx->core, &plan);
        }
    }
    if ((hash_state != 0) && (frame_result == DB_VK_FRAME_OK)) {
        db_display_hash_tracker_record(ctx->state_hash_tracker,
                                       plan.expected_state_hash);
    }
    if ((hash_output != 0) && (frame_result == DB_VK_FRAME_OK)) {
        db_display_hash_tracker_record(ctx->output_hash_tracker, output_hash);
    }
    if (frame_result == DB_VK_FRAME_STOP) {
        DB_RUNTIME_STATUS(ctx->backend_name, "renderer requested stop");
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    if (frame_result == DB_VK_FRAME_RETRY) {
        return DB_DISPLAY_FRAME_LOOP_RETRY;
    }
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_run_glfw_window_vulkan(const db_cli_config_t *cfg) {
    const db_glfw_window_visibility_t visibility =
        ((cfg != NULL) && (cfg->glfw_window_hidden != 0))
            ? DB_GLFW_WINDOW_HIDDEN
            : DB_GLFW_WINDOW_VISIBLE;
    const int true_offscreen_backend =
        DB_BOOL((cfg != NULL) && (cfg->display == DB_OFFSCREEN_DISPLAY));
    const char *backend_name = (true_offscreen_backend != 0)
                                   ? DB_BACKEND_NAME_DISPLAY_OFFSCREEN
                                   : DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_VK;
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            backend_name, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE);

    GLFWwindow *window = db_glfw_create_no_api_window(
        backend_name, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX, visibility);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(backend_name, runtime_api_version,
                                      "(selected by renderer)");
    const db_glfw_framebuffer_extent_t initial_extent =
        db_glfw_get_framebuffer_extent(window, backend_name);
    const db_presentation_transform_t presentation =
        db_display_presentation_transform(initial_extent.width,
                                          initial_extent.height);
    resolved_runtime.presentation = presentation;

    const db_vk_wsi_config_t wsi_config = {
        .backend_name = backend_name,
        .window_handle = window,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    const db_native_output_capability_t vk_capability = db_vk_init(
        &wsi_config,
        (cfg != NULL) ? cfg->vsync_enabled : BENCH_DEFAULT_VSYNC_ENABLED,
        &resolved_runtime.renderer);
    db_display_apply_native_output_capability_or_fail(
        backend_name, &resolved_runtime, &vk_capability);
    resolved_runtime.renderer.format.hdr_conversion =
        (resolved_runtime.renderer.format.native_hdr_enabled != 0)
            ? DB_HDR_CONVERSION_VULKAN_SHADER
            : DB_HDR_CONVERSION_NONE;
    db_display_log_presentation_contract(backend_name, &resolved_runtime,
                                         &presentation);
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
            backend_name, &resolved_runtime, DB_DISPLAY_HASH_KEY_STATE,
            DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_glfw_vulkan_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .state_hash_tracker = &hash_trackers.state,
        .output_hash_tracker = &hash_trackers.output,
        .frame_limit = resolved_runtime.display.frame_limit,
        .core = &core,
        .window = window,
    };
    db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_vulkan_frame,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
        .resolved_runtime = &resolved_runtime,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    db_vk_set_present_metrics(
        loop_result.frame_ema_ms, loop_result.jitter_ema_ms,
        loop_result.frame_p50_ms, loop_result.frame_p95_ms,
        loop_result.frame_p99_ms, loop_result.retries);
    db_vk_shutdown();
    db_frame_source_shutdown(&core);
    db_display_dual_hash_trackers_log_final(backend_name, &hash_trackers);
    db_glfw_destroy_window(window);
    return 0;
}
#endif

int db_run_glfw_window(db_api_t api, db_gl_renderer_t renderer,
                       const db_cli_config_t *cfg) {
    db_dispatch_validate_backend_or_fail(DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_GL,
                                         DB_GLFW_WINDOW_DISPLAY, api, renderer);
    if (api == DB_API_CPU) {
        return db_run_glfw_window_cpu(cfg);
    }
#ifdef DB_HAS_VULKAN_API
    if (api == DB_API_VULKAN) {
        return db_run_glfw_window_vulkan(cfg);
    }
#endif
    return db_run_glfw_window_opengl(renderer, cfg);
}
