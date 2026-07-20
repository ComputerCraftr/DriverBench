#include "core/db_format_contract.h"
#include "core/db_log.h"
#ifdef DB_HAS_VULKAN_API
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../../core/db_qualification_contracts.h"
#include "../../core/db_render_result.h"
#include "../../core/db_renderer_diagnostics.h"
#include "../../core/db_run_session.h"
#include "../../core/db_trace.h"
#include "../../driverbench_config.h"
#include "../../renderers/damage_trace.h"
#include "../../renderers/gl_common.h"
#include "core/db_render_types.h"
#ifdef DB_HAS_VULKAN_API
#endif
#include "../../config/benchmark_config.h"
#include "../../renderers/gl_hash_readback.h"
#include "../../renderers/opengl_gl1_5_gles1_1/gl1_renderer.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_hash_common.h"
#include "../display_presentation_policy.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#include "../gl_display_runtime.h"
#include "glfw_window_common.h"
#include "glfw_window_internal.h"
enum { DB_PRESENTATION_BUFFER_AGE_QUALIFICATION_FRAMES = 4U };
typedef struct {
    const char *backend_name;
    const char *capability_mode;
    const char *renderer_name;
    db_display_gl_renderer_ops_t renderer_ops;
    db_display_frame_step_t frame_step;
    db_display_hash_tracker_t *state_hash_tracker;
    db_display_hash_tracker_t *framebuffer_hash_tracker;
    db_pixel_format_t framebuffer_hash_format;
    db_display_resolved_format_config_t format;
    db_gl1_direct_window_capabilities_t direct_window_capabilities;
    db_gl_framebuffer_hash_scratch_t *hash_scratch;
    double next_progress_log_due_ms;
    int state_hash_enabled;
    int output_hash_enabled;
    int uses_native_buffer_age;
    int debug_clear_default_framebuffer;
    uint32_t renderer_preserved_framebuffer_count;
    db_presentation_buffer_age_t last_buffer_age;
    db_presentation_damage_history_t damage_history;
    db_presentation_damage_history_t pending_damage_history;
    db_grid_block_t
        presentation_logical[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    db_damage_block_t
        presentation_pixels[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    uint32_t last_framebuffer_width;
    uint32_t last_framebuffer_height;
    uint32_t presentation_generation;
    db_renderer_target_t pending_target;
    db_gl1_target_request_t gl1_target_request;
    int buffer_age_logged;
    int buffer_age_qualified;
    int buffer_age_validation_pending;
    uint32_t buffer_age_validation_count;
    uint64_t buffer_age_expected_hash;
    uint64_t pending_buffer_age_expected_hash;
    uint64_t pending_pre_swap_hash;
    db_glfw_framebuffer_extent_t current_extent;
    db_presentation_buffer_age_t current_buffer_age;
    int pending_history_valid;
    int pending_seed_buffer_age_validation;
    uint32_t work_unit_count;
    GLFWwindow *window;
    db_run_session_t *session;
} db_glfw_opengl_loop_ctx_t;

static void db_glfw_log_buffer_age_if_needed(
    db_glfw_opengl_loop_ctx_t *ctx,
    const db_presentation_buffer_age_t *buffer_age) {
    const int trace_each_frame = DB_BOOL(db_trace_config_current().damage > 0);
    if ((ctx->buffer_age_logged == 0) || (trace_each_frame != 0) ||
        (db_glfw_presentation_buffer_age_changed(&ctx->last_buffer_age,
                                                 buffer_age) != 0)) {
        db_glfw_log_presentation_buffer_age(ctx->backend_name, buffer_age);
    }
    ctx->last_buffer_age = *buffer_age;
    ctx->buffer_age_logged = 1;
}
static GLFWwindow *db_glfw_create_renderer_window(
    const char *backend_name, const db_display_gl_context_policy_t *policy,
    int swap_interval, db_glfw_window_visibility_t visibility,
    int *out_context_is_gles) {
    if (policy == NULL) {
        return NULL;
    }
    if (policy->allow_gles1_1_fallback != 0) {
        return db_glfw_create_gl1_5_or_gles1_1_window(
            backend_name, "OpenGL 1.5/GLES1.1 GLFW DriverBench",
            BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
            policy->requested_gl_major, policy->requested_gl_minor,
            swap_interval, out_context_is_gles, visibility);
    }
    if (out_context_is_gles != NULL) {
        *out_context_is_gles = 0;
    }
    return db_glfw_create_opengl_window(
        backend_name, "OpenGL 3.3 Shader GLFW DriverBench",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX,
        policy->requested_gl_major, policy->requested_gl_minor, 1,
        swap_interval, visibility);
}
static uint64_t db_glfw_hash_native_default_framebuffer_or_fail(
    const char *backend_name, GLFWwindow *window, db_pixel_format_t format,
    db_gl_framebuffer_hash_scratch_t *scratch) {
    if ((backend_name == NULL) || (window == NULL)) {
        DB_RUNTIME_FAIL("display_glfw_window",
                        "invalid presented default-framebuffer hash inputs");
    }

    const db_glfw_framebuffer_extent_t extent =
        db_glfw_get_framebuffer_extent(window, backend_name);
    if (extent.valid == 0) {
        DB_RUNTIME_FAIL(backend_name, "invalid GLFW framebuffer extent");
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        return db_gl_hash_framebuffer_rgba16f_or_fail(
            backend_name, extent.width, extent.height, scratch, 1);
    }
    const uint8_t *pixels = db_gl_read_framebuffer_rgba8_or_fail(
        backend_name, extent.width, extent.height, scratch);
    const size_t stride_bytes = db_checked_mul_size(
        backend_name, "fb_row_bytes",
        db_checked_u32_to_size(backend_name, "fb_row_pixels", extent.width),
        DB_RGBA8_BYTES_PER_PIXEL);
    const uint64_t hash = db_hash_rgba8_pixels_canonical(
        pixels, extent.width, extent.height, stride_bytes, 1);
    (void)db_gl_upload_stream_end_read(&scratch->stream, backend_name);
    return hash;
}

static int db_glfw_opengl_acquire(void *user_data, uint32_t frame_index,
                                  db_presenter_facts_t *facts) {
    (void)frame_index;
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (facts == NULL)) {
        return 0;
    }
    const db_glfw_framebuffer_extent_t extent =
        db_glfw_get_framebuffer_extent(ctx->window, ctx->backend_name);
    if (extent.valid == 0) {
        return 0;
    }
    db_presentation_buffer_age_t buffer_age =
        db_presentation_buffer_age_resolve(
            DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U,
            ctx->renderer_preserved_framebuffer_count);
    if (ctx->uses_native_buffer_age != 0) {
        buffer_age = db_glfw_query_presentation_buffer_age(
            ctx->window, ctx->renderer_preserved_framebuffer_count);
        if (buffer_age.provider == DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE) {
            ctx->uses_native_buffer_age = 0;
            db_presentation_damage_history_reset(&ctx->damage_history);
        } else if ((ctx->buffer_age_qualified == 0) &&
                   (ctx->buffer_age_validation_pending != 0) &&
                   (buffer_age.valid != 0) && (buffer_age.raw_age == 1U)) {
            const uint64_t acquired_hash =
                db_glfw_hash_native_default_framebuffer_or_fail(
                    ctx->backend_name, ctx->window,
                    ctx->framebuffer_hash_format, ctx->hash_scratch);
            if (acquired_hash == ctx->buffer_age_expected_hash) {
                ctx->buffer_age_validation_count++;
                ctx->buffer_age_qualified =
                    DB_BOOL(ctx->buffer_age_validation_count >=
                            DB_PRESENTATION_BUFFER_AGE_QUALIFICATION_FRAMES);
            } else {
                ctx->uses_native_buffer_age = 0;
                db_presentation_damage_history_reset(&ctx->damage_history);
                buffer_age.valid = 0;
                buffer_age.force_full_repair = 1;
                buffer_age.fallback_reason = "content_validation_failed";
            }
            ctx->buffer_age_validation_pending = 0;
        }
        if ((ctx->uses_native_buffer_age != 0) &&
            (ctx->buffer_age_qualified == 0)) {
            buffer_age.valid = 0;
            buffer_age.force_full_repair = 1;
            buffer_age.fallback_reason = "content_validation_pending";
        }
        db_glfw_log_buffer_age_if_needed(ctx, &buffer_age);
    }
    const int framebuffer_resized =
        DB_BOOL((ctx->last_framebuffer_width != 0U) &&
                ((ctx->last_framebuffer_width != extent.width) ||
                 (ctx->last_framebuffer_height != extent.height)));
    if (framebuffer_resized != 0) {
        db_presentation_damage_history_reset(&ctx->damage_history);
        ctx->buffer_age_qualified = 0;
        ctx->buffer_age_validation_pending = 0;
        ctx->buffer_age_validation_count = 0U;
        buffer_age.force_full_repair = 1;
        buffer_age.valid = 0;
        buffer_age.fallback_reason = "framebuffer_resized";
        ctx->presentation_generation =
            db_checked_add_u32("display_glfw_window", "presentation_generation",
                               ctx->presentation_generation, 1U);
    }
    ctx->last_framebuffer_width = extent.width;
    ctx->last_framebuffer_height = extent.height;
    ctx->current_extent = extent;
    ctx->current_buffer_age = buffer_age;
    int channel_bits[4] = {0};
    int sample_count = 0;
    db_gl_query_default_framebuffer_format(channel_bits, &sample_count);
    *facts = (db_presenter_facts_t){
        .destination_width = extent.width,
        .destination_height = extent.height,
        .native_hash_format = ctx->framebuffer_hash_format,
        .generation = ctx->presentation_generation,
        .gl =
            {
                .native_width = extent.width,
                .native_height = extent.height,
                .native_format = ctx->format.native_output_format,
                .channel_bits =
                    {
                        db_nonnegative_int_to_u32_or_zero(channel_bits[0]),
                        db_nonnegative_int_to_u32_or_zero(channel_bits[1]),
                        db_nonnegative_int_to_u32_or_zero(channel_bits[2]),
                        db_nonnegative_int_to_u32_or_zero(channel_bits[3]),
                    },
                .sample_count = db_nonnegative_int_to_u32_or_zero(sample_count),
                .buffer_age = buffer_age.raw_age,
                .generation = ctx->presentation_generation,
                .platform_conversion_required =
                    DB_BOOL((ctx->format.surface_pixel_format !=
                             DB_PIXEL_FORMAT_RGBA8) ||
                            (ctx->format.native_output_format !=
                             DB_NATIVE_OUTPUT_XRGB8888) ||
                            (ctx->format.native_hdr_enabled != 0)),
                .valid = 1,
            },
        .raw_buffer_age = buffer_age.raw_age,
        .replay_depth = buffer_age.effective_replay_depth,
        .buffer_age_valid = buffer_age.valid,
        .prior_content_state = DB_TARGET_CONTENT_UNCHANGED,
        .valid = 1,
    };
    return 1;
}

static int
db_glfw_opengl_presenter_validate(void *user_data,
                                  const db_presenter_facts_t *facts) {
    const db_glfw_opengl_loop_ctx_t *const ctx =
        (const db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (facts == NULL)) {
        return 0;
    }
    const db_glfw_framebuffer_extent_t extent =
        db_glfw_get_framebuffer_extent(ctx->window, ctx->backend_name);
    return DB_BOOL((extent.valid != 0) &&
                   (extent.width == facts->destination_width) &&
                   (extent.height == facts->destination_height) &&
                   (ctx->presentation_generation == facts->generation));
}

static db_present_result_t
db_glfw_opengl_present(void *user_data, const db_frame_plan_t *plan,
                       const db_renderer_frame_output_t *output) {
    (void)plan;
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (output == NULL) || (output->result.success == 0)) {
        return DB_PRESENT_FATAL;
    }
    glfwSwapBuffers(ctx->window);
    return DB_PRESENT_ACCEPTED;
}

static int
db_glfw_opengl_preflight(void *user_data, const db_presenter_facts_t *presenter,
                         const db_frame_requirements_t *requirements,
                         const db_qualification_snapshot_t *qualification,
                         db_renderer_preflight_t *preflight) {
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    (void)requirements;
    if ((ctx == NULL) || (presenter == NULL) || (preflight == NULL)) {
        return 0;
    }
    const int is_gl1 =
        DB_BOOL(ctx->renderer_ops.renderer == DB_GL_RENDERER_GL1_5_GLES1_1);
    db_gl1_direct_window_capabilities_t direct_capabilities =
        ctx->direct_window_capabilities;
    direct_capabilities.pre_swap_readback_qualified = ctx->buffer_age_qualified;
    db_render_target_strategy_t previous_strategy =
        DB_RENDER_TARGET_CPU_SURFACE;
    uint64_t previous_target_generation = 0U;
    int direct_window_lineage_valid = 0;
    if (is_gl1 != 0) {
        db_gl1_replay_preflight_facts(&previous_strategy,
                                      &previous_target_generation,
                                      &direct_window_lineage_valid);
    }
    return db_renderer_preflight_policy_resolve(
        &(const db_renderer_preflight_policy_input_t){
            .profile = is_gl1 != 0 ? DB_RENDERER_PREFLIGHT_GL1_WINDOW
                                   : DB_RENDERER_PREFLIGHT_GL3_PERSISTENT,
            .gl1_target_request = ctx->gl1_target_request,
            .working_format = ctx->format.surface_pixel_format,
            .gl1_direct_window = direct_capabilities,
            .previous_strategy = previous_strategy,
            .previous_target_generation = previous_target_generation,
            .direct_window_lineage_valid = direct_window_lineage_valid,
            .plan_request =
                {
                    .presentation_replay_depth = presenter->replay_depth,
                },
        },
        presenter, qualification, preflight);
}

static int db_glfw_opengl_provision(void *user_data,
                                    const db_renderer_preflight_t *preflight,
                                    db_renderer_target_t *target) {
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (preflight == NULL) || (target == NULL)) {
        return 0;
    }
    *target = db_renderer_target_from_preflight(
        preflight, (uint64_t)preflight->target_strategy + 1U,
        ctx->presentation_generation);
    ctx->pending_target = *target;
    return 1;
}

static int db_glfw_opengl_target_validate(void *user_data,
                                          const db_renderer_target_t *target) {
    const db_glfw_opengl_loop_ctx_t *const ctx =
        (const db_glfw_opengl_loop_ctx_t *)user_data;
    return DB_BOOL((ctx != NULL) && (target != NULL) && (target->valid != 0) &&
                   (target->generation == ctx->presentation_generation));
}

static db_renderer_execute_status_t
db_glfw_opengl_execute(void *user_data, const db_frame_plan_t *plan,
                       const db_renderer_target_t *target,
                       db_renderer_frame_output_t *output) {
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (plan == NULL) || (target == NULL) ||
        (target->valid == 0) || (output == NULL)) {
        return DB_RENDER_FATAL;
    }
    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        ctx->debug_clear_default_framebuffer);
    ctx->pending_damage_history = ctx->damage_history;
    ctx->pending_history_valid = 1;
    db_pixel_block_view_t presentation_damage = {0};
    int force_full_presentation = 1;
    if (ctx->renderer_ops.renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        const size_t logical_count = db_presentation_damage_history_resolve_ir(
            &ctx->pending_damage_history, &ctx->current_buffer_age,
            &plan->update_ir, plan->update_metadata.damage_region,
            plan->grid_rows, plan->grid_cols, ctx->presentation_logical,
            DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full_presentation);
        size_t pixel_count = 0U;
        for (size_t index = 0U;
             (index < logical_count) &&
             (pixel_count < DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME);
             index++) {
            if (db_grid_block_to_pixel_block(
                    plan->grid_cols, plan->grid_rows,
                    &ctx->presentation_logical[index],
                    ctx->current_extent.width, ctx->current_extent.height,
                    &ctx->presentation_pixels[pixel_count]) != 0) {
                pixel_count++;
            }
        }
        presentation_damage = (db_pixel_block_view_t){
            .blocks = ctx->presentation_pixels,
            .count = pixel_count,
        };
    }
    const db_gl_presentation_frame_t presentation = {
        .destination_width = ctx->current_extent.width,
        .destination_height = ctx->current_extent.height,
        .damage = presentation_damage,
        .buffer_age = ctx->current_buffer_age,
        .force_full = force_full_presentation,
        .repair_reason = ctx->current_buffer_age.fallback_reason,
    };
    if (db_display_gl_render_frame(ctx->renderer_ops.renderer, plan, target,
                                   &presentation) == 0) {
        output->target_content = DB_TARGET_CONTENT_PARTIALLY_MODIFIED;
        return DB_RENDER_FATAL;
    }
    output->result = db_render_result_success();
    ctx->renderer_ops.execution_report(&output->result.execution);
    if (db_display_frame_step_should_hash_output(&ctx->frame_step,
                                                 plan->frame_index) != 0) {
        output->result.working_hash =
            db_glfw_hash_canonical_default_framebuffer_or_fail(
                ctx->backend_name, ctx->window, plan->grid_cols,
                plan->grid_rows, ctx->hash_scratch);
        output->result.working_hash_valid = 1;
    }
    const int trace_damage = db_damage_trace_enabled();
    ctx->pending_seed_buffer_age_validation =
        DB_BOOL((ctx->uses_native_buffer_age != 0) &&
                (ctx->current_buffer_age.provider !=
                 DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE) &&
                (ctx->buffer_age_qualified == 0));
    if ((trace_damage != 0) || (ctx->pending_seed_buffer_age_validation != 0)) {
        ctx->pending_pre_swap_hash =
            db_glfw_hash_native_default_framebuffer_or_fail(
                ctx->backend_name, ctx->window, ctx->framebuffer_hash_format,
                ctx->hash_scratch);
    }
    ctx->pending_buffer_age_expected_hash = ctx->pending_pre_swap_hash;
    output->target_content = DB_TARGET_CONTENT_VALID_UNCOMMITTED;
    return DB_RENDER_EXECUTED;
}

static void db_glfw_opengl_finalize(void *user_data,
                                    const db_frame_plan_t *plan,
                                    const db_renderer_frame_output_t *output,
                                    int commit) {
    (void)plan;
    (void)output;
    db_glfw_opengl_loop_ctx_t *const ctx =
        (db_glfw_opengl_loop_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }
    if ((commit != 0) && (ctx->pending_history_valid != 0)) {
        ctx->damage_history = ctx->pending_damage_history;
        if (ctx->pending_seed_buffer_age_validation != 0) {
            ctx->buffer_age_expected_hash =
                ctx->pending_buffer_age_expected_hash;
            ctx->buffer_age_validation_pending = 1;
        }
    }
    if (ctx->renderer_ops.renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_gl1_finalize_frame(commit, ctx->pending_target.strategy,
                              ctx->pending_target.target_generation);
    }
    ctx->pending_history_valid = 0;
    ctx->pending_seed_buffer_age_validation = 0;
}

static db_display_frame_loop_result_t
db_glfw_opengl_frame(void *user_data, uint32_t frame_index, double elapsed_ms) {
    db_glfw_opengl_loop_ctx_t *ctx = (db_glfw_opengl_loop_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->session == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    const db_run_step_result_t run_result = db_run_session_step(ctx->session);
    db_committed_frame_summary_t summary = {0};
    const db_display_frame_loop_result_t frame_result =
        db_display_frame_loop_from_run_step(&run_result, &summary);
    if (frame_result != DB_DISPLAY_FRAME_LOOP_CONTINUE) {
        return frame_result;
    }
    const int hash_output =
        db_display_frame_step_should_hash_output(&ctx->frame_step, frame_index);
    const int hash_state =
        db_display_frame_step_should_hash_state(&ctx->frame_step, frame_index);
    if (db_damage_trace_enabled() != 0) {
        const db_damage_block_t full_block = db_damage_block_full(
            ctx->current_extent.height, ctx->current_extent.width);
        (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
            .frame_index = frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_DISPLAY,
            .stage = DB_DAMAGE_TRACE_STAGE_RENDER_TARGET,
            .operation = DB_DAMAGE_TRACE_OP_READBACK,
            .source = DB_DAMAGE_TRACE_BUFFER_GL_DEFAULT_FRAMEBUFFER,
            .destination = DB_DAMAGE_TRACE_BUFFER_GL_DEFAULT_FRAMEBUFFER,
            .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
            .width = ctx->current_extent.width,
            .height = ctx->current_extent.height,
            .pixel_format = ctx->framebuffer_hash_format,
            .blocks = &full_block,
            .block_count = 1U,
            .destination_hash = ctx->pending_pre_swap_hash,
            .mode = "pre_swap",
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        });
    }

    db_display_gl_frame_step(&ctx->frame_step, frame_index, elapsed_ms,
                             hash_state, summary.expected_state_hash,
                             hash_output, summary.working_hash);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static int db_glfw_run_opengl_renderer_loop(
    GLFWwindow *window, const char *backend_name,
    const db_display_gl_renderer_ops_t *renderer_ops,
    const db_display_renderer_runtime_t *resolved_runtime,
    int uses_native_buffer_age) {
    if ((window == NULL) || (backend_name == NULL) || (renderer_ops == NULL) ||
        (resolved_runtime == NULL)) {
        return 0;
    }

    renderer_ops->init(&resolved_runtime->renderer);
    const char *capability_mode = renderer_ops->runtime_capability_mode();
    const uint32_t work_unit_count = renderer_ops->work_unit_count();
    const char *renderer_name = renderer_ops->renderer_name;
    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_display_dual_hash_trackers_t hash_trackers =
        db_display_dual_hash_trackers_create_from_resolved_runtime(
            backend_name, resolved_runtime, DB_DISPLAY_HASH_KEY_STATE,
            DB_DISPLAY_HASH_KEY_FRAMEBUFFER);
    db_gl_framebuffer_hash_scratch_t hash_scratch = {0};

    db_glfw_opengl_loop_ctx_t loop_ctx = {
        .backend_name = backend_name,
        .capability_mode = capability_mode,
        .renderer_name = renderer_name,
        .frame_step = {0},
        .state_hash_tracker = &hash_trackers.state,
        .framebuffer_hash_tracker = &hash_trackers.output,
        .framebuffer_hash_format =
            resolved_runtime->renderer.format.framebuffer_hash_format,
        .format = resolved_runtime->renderer.format,
        .direct_window_capabilities =
            {
                .can_control_dither = 1,
                .can_control_srgb = DB_BOOL(
                    db_gl_is_es_context(db_gl_get_version_string()) == 0),
                .can_select_required_buffers = 1,
                .fixed_function_raster_qualified = 1,
            },
        .presentation_generation = 1U,
        .gl1_target_request = resolved_runtime->renderer.diagnostics.gl1_target,
        .hash_scratch = &hash_scratch,
        .renderer_ops = *renderer_ops,
        .next_progress_log_due_ms = 0.0,
        .state_hash_enabled =
            resolved_runtime->hash_settings.state_hash_enabled,
        .output_hash_enabled =
            resolved_runtime->hash_settings.output_hash_enabled,
        // Only GL1 draws into preserved default-framebuffer contents. GL3
        // presents its persistent FBO in full and must never pay buffer-age
        // qualification readbacks.
        .uses_native_buffer_age =
            DB_BOOL(db_display_gl_uses_default_framebuffer_history(
                        renderer_ops->renderer) &&
                    (uses_native_buffer_age != 0)),
        .debug_clear_default_framebuffer =
            resolved_runtime->display.debug_clear_default_framebuffer,
        .renderer_preserved_framebuffer_count =
            resolved_runtime->renderer.preserved_framebuffer_count,
        .work_unit_count = work_unit_count,
        .window = window,
    };
    const db_renderer_diagnostic_config_t *const diagnostics =
        &resolved_runtime->renderer.diagnostics;
    const int diagnostic_forced =
        (renderer_ops->renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
            ? (diagnostics->gl1_gradient != DB_GL1_GRADIENT_AUTO) ||
                  (diagnostics->gl1_target != DB_GL1_TARGET_AUTO)
            : diagnostics->gl3_gradient != DB_GL3_GRADIENT_AUTO;
    if (db_run_session_create(
            &(const db_run_session_config_t){
                .benchmark =
                    {
                        .benchmark_configuration = &resolved_runtime->benchmark,
                        .working_format = resolved_runtime->renderer.format
                                              .surface_pixel_format,
                    },
                .presenter_ops =
                    {
                        .acquire = db_glfw_opengl_acquire,
                        .validate = db_glfw_opengl_presenter_validate,
                        .present = db_glfw_opengl_present,
                    },
                .renderer_ops =
                    {
                        .preflight = db_glfw_opengl_preflight,
                        .provision = db_glfw_opengl_provision,
                        .validate = db_glfw_opengl_target_validate,
                        .execute = db_glfw_opengl_execute,
                        .finalize = db_glfw_opengl_finalize,
                    },
                .qualification_ops = *renderer_ops->qualification_ops,
                .qualification_query =
                    {
                        .ignore_cache = diagnostics->ignore_conformance_cache,
                        .rerun_probe = diagnostics->rerun_conformance_probe,
                        .diagnostic_forced = diagnostic_forced,
                    },
                .presenter_context = &loop_ctx,
                .renderer_context = &loop_ctx,
                .fps_cap = resolved_runtime->display.fps_cap,
                .frame_limit = resolved_runtime->display.frame_limit,
                .recent_metrics_enabled = db_display_dual_metrics_enabled(),
            },
            &loop_ctx.session) != DB_RUN_SESSION_OK) {
        DB_RUNTIME_FAIL(backend_name, "failed to initialize run session");
    }
    loop_ctx.frame_step = db_display_frame_step_make(
        db_dispatch_api_name(DB_API_OPENGL), loop_ctx.backend_name,
        loop_ctx.renderer_name, loop_ctx.framebuffer_hash_tracker,
        loop_ctx.state_hash_tracker, &loop_ctx.next_progress_log_due_ms,
        loop_ctx.work_unit_count, loop_ctx.output_hash_enabled,
        loop_ctx.state_hash_enabled, resolved_runtime->display.frame_limit);
    db_glfw_loop_t loop = {
        .backend = backend_name,
        .frame_fn = db_glfw_opengl_frame,
        .fps_cap = 0.0,
        .frame_limit = resolved_runtime->display.frame_limit,
        .user_data = &loop_ctx,
        .window = window,
        .resolved_runtime = resolved_runtime,
    };
    const db_display_frame_loop_run_result_t loop_result =
        db_glfw_run_loop(&loop);
    const uint64_t frames = loop_result.frames;

    const double bench_ms =
        DB_TO_F64(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_OPENGL), renderer_name, backend_name,
        frames, work_unit_count, bench_ms, renderer_ops->draw_stats,
        renderer_ops->execution_report);
    db_run_session_destroy(loop_ctx.session);
    db_display_dual_hash_trackers_log_final(backend_name, &hash_trackers);

    renderer_ops->shutdown();
    db_gl_hash_scratch_release(&hash_scratch);
    return 0;
}

int db_run_glfw_window_opengl(db_gl_renderer_t renderer,
                              const db_cli_config_t *cfg) {
    db_cli_config_t effective_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0};
    const db_glfw_window_visibility_t visibility =
        (effective_cfg.glfw_window_hidden != 0) ? DB_GLFW_WINDOW_HIDDEN
                                                : DB_GLFW_WINDOW_VISIBLE;
    const int true_offscreen_backend =
        DB_BOOL(effective_cfg.display == DB_OFFSCREEN_DISPLAY);
    const char *backend_name = (true_offscreen_backend != 0)
                                   ? DB_BACKEND_NAME_DISPLAY_OFFSCREEN
                                   : DB_BACKEND_NAME_DISPLAY_GLFW_WINDOW_GL;
    db_validate_runtime_environment(backend_name,
                                    DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const int swap_interval = DB_BOOL(effective_cfg.vsync_enabled);
    int context_is_gles = 0;
    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(renderer);
    const db_display_gl_context_policy_t context_policy =
        db_display_gl_context_policy_for_renderer(renderer);
    const uint32_t max_preserved_framebuffer_count =
        db_display_gl_max_preserved_framebuffer_count(renderer);
    const uint32_t default_preserved_framebuffer_count =
        (true_offscreen_backend != 0)
            ? DB_MIN(2U, max_preserved_framebuffer_count)
            : max_preserved_framebuffer_count;
    db_display_gl_policy_resolution_t policy_resolution = {
        .effective_cfg = effective_cfg,
        .preserved_framebuffer_count = default_preserved_framebuffer_count,
        .policy_reason_code = DB_DISPLAY_GL_POLICY_REASON_NONE,
        .policy_reason_text = NULL,
    };
    const db_display_default_framebuffer_preserve_info_t default_fb_preserve = {
        0};

    db_display_resolve_opengl_display_policy(
        renderer, &effective_cfg, true_offscreen_backend,
        default_preserved_framebuffer_count, max_preserved_framebuffer_count,
        &default_fb_preserve, &policy_resolution);
    effective_cfg = policy_resolution.effective_cfg;
    if (policy_resolution.policy_reason_text != NULL) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("action", "force_full_draw"),
            DB_LOG_I64("reason", policy_resolution.policy_reason_code),
            DB_LOG_U64("preserved_framebuffer_count",
                       policy_resolution.preserved_framebuffer_count),
        };
        db_log_info(backend_name, "display_policy", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }

    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            backend_name, &effective_cfg,
            policy_resolution.preserved_framebuffer_count, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    GLFWwindow *window = db_glfw_create_renderer_window(
        backend_name, &context_policy, swap_interval, visibility,
        &context_is_gles);

    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)glfwGetProcAddress, renderer, backend_name,
        (context_policy.allow_gles1_1_fallback != 0) ? context_is_gles : -1);
    db_display_apply_native_output_capability_or_fail(
        backend_name, &resolved_runtime, &g_glfw_native_output_capability);
    const db_glfw_framebuffer_extent_t initial_extent =
        db_glfw_get_framebuffer_extent(window, backend_name);
    const db_presentation_transform_t presentation =
        db_display_presentation_transform(initial_extent.width,
                                          initial_extent.height);
    resolved_runtime.presentation = presentation;
    db_display_log_presentation_contract(backend_name, &resolved_runtime,
                                         &presentation);
    const int run_status = db_glfw_run_opengl_renderer_loop(
        window, backend_name, &renderer_ops, &resolved_runtime,
        DB_BOOL(true_offscreen_backend == 0));
    db_glfw_destroy_window(window);
    return run_status;
}
