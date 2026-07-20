#include "gl3_renderer.h"

#include "core/db_conformance.h"
#include "core/db_frame_contracts.h"
#include "core/db_log.h"
#include "core/db_qualification_contracts.h"
#include "core/db_renderer_diagnostics.h"
#include "core/db_renderer_runtime_contract.h"
#include "core/db_renderer_support.h"
#include "gl3_exact_lookup.h"
#include "gl3_execute.h"
#include "gl3_qualification.h"
#include "gl3_target.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_ir.h"
#include "../damage_trace.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_hash_readback.h"
#include "../renderer_viewport_common.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "db_embedded_shaders.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    unsigned int draw_program;
    unsigned int present_program;
    int sampler_location;
    int hdr_output_location;
    int hdr_output_enabled;
    db_renderer_applied_selection_t applied;
    db_gl_viewport_cache_t viewport;
} gl3_presentation_pipeline_t;

typedef struct {
    db_gl_framebuffer_hash_scratch_t scratch;
} gl3_hash_workspace_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    db_render_execution_report_t execution;
} gl3_telemetry_t;

typedef struct {
    db_renderer_execution_config_t runtime;
    db_renderer_diagnostic_config_t diagnostics;
    gl3_persistent_target_t target;
    db_gl3_geometry_stream_t geometry;
    gl3_exact_lookup_t exact_lookup;
    gl3_presentation_pipeline_t presentation;
    gl3_hash_workspace_t hash;
    gl3_telemetry_t telemetry;
} renderer_state_t;

static renderer_state_t g_state = {0};

uint64_t db_gl3_working_hash(void) {
    if ((g_state.target.fbo == 0U) || (g_state.target.width <= 0) ||
        (g_state.target.height <= 0)) {
        return 0U;
    }
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.target.fbo);
    const uint32_t width = db_checked_int_to_u32(BACKEND_NAME, "backing_width",
                                                 g_state.target.width);
    const uint32_t height = db_checked_int_to_u32(
        BACKEND_NAME, "backing_height", g_state.target.height);
    const uint64_t hash =
        (g_state.target.format == DB_PIXEL_FORMAT_RGBA16F)
            ? db_gl_hash_framebuffer_rgba16f_or_fail(
                  BACKEND_NAME, width, height, &g_state.hash.scratch, 1)
            : db_gl_hash_framebuffer_rgba8_or_fail(BACKEND_NAME, width, height,
                                                   &g_state.hash.scratch, 1);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, 0U);
    return hash;
}

static void gl3_trace_full_frame(uint32_t frame_index,
                                 db_damage_trace_stage_t stage,
                                 db_damage_trace_operation_t operation,
                                 db_damage_trace_buffer_t source,
                                 db_damage_trace_buffer_t destination,
                                 uint32_t destination_index) {
    if (db_damage_trace_enabled() == 0) {
        return;
    }
    const uint32_t width =
        db_checked_int_to_u32(BACKEND_NAME, "trace_width",
                              g_state.presentation.viewport.last_viewport_w);
    const uint32_t height =
        db_checked_int_to_u32(BACKEND_NAME, "trace_height",
                              g_state.presentation.viewport.last_viewport_h);
    const db_damage_block_t full_block = db_damage_block_full(height, width);
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
        .stage = stage,
        .operation = operation,
        .source = source,
        .destination = destination,
        .destination_index = destination_index,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = width,
        .height = height,
        .pixel_format = g_state.target.format,
        .blocks = &full_block,
        .block_count = 1U,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        .target = "gl3_backing",
        .target_generation = g_state.target.generation,
        .present_method = (stage == DB_DAMAGE_TRACE_STAGE_PRESENT)
                              ? "sample_fullscreen"
                              : "none",
    });
}

static void gl3_refresh_capability_mode(void) {
    const db_gl_runtime_draw_mode_t draw_mode =
        ((g_state.runtime.backbuffer_draw_full != 0) ||
         (g_state.runtime.pipeline.uses_history_pipeline == 0))
            ? DB_GL_RUNTIME_DRAW_FULL_PRESENT
            : DB_GL_RUNTIME_DRAW_DIRTY_REPLAY;
    const db_gl_runtime_mode_desc_t mode = db_gl_runtime_mode_desc_renderer(
        draw_mode,
        db_gl_stream_upload_uses_buffer_object(
            &g_state.geometry.stream.capability) &&
            (g_state.geometry.stream.buffer != 0U),
        &g_state.geometry.stream.capability,
        g_state.runtime.backbuffer_replay_enabled);
    db_gl_runtime_mode_format_renderer(
        g_state.telemetry.capability_mode,
        sizeof(g_state.telemetry.capability_mode), &mode);
}

static int db_init_vertices_for_mode(
    const db_renderer_runtime_contract_t *resolved_runtime) {
    if (resolved_runtime == NULL) {
        return 0;
    }
    const db_renderer_execution_config_t runtime_state =
        resolved_runtime->execution;
    size_t float_count = 0U;
    size_t byte_count = 0U;
    if ((db_gl3_geometry_storage_layout(runtime_state.work_unit_count,
                                        &float_count, &byte_count) == 0) ||
        (runtime_state.work_unit_count > (UINT32_MAX / DB_RECT_VERTEX_COUNT))) {
        return 0;
    }
    float *const vertices = (float *)calloc(float_count, sizeof(*vertices));
    if (vertices == NULL) {
        return 0;
    }
    g_state.geometry.vertex = (db_gl_vertex_init_t){
        .vertices = vertices,
        .vertex_stride = DB_GL3_INSTANCE_FLOAT_COUNT,
        .work_unit_count = runtime_state.work_unit_count,
        .draw_vertex_count =
            runtime_state.work_unit_count * DB_RECT_VERTEX_COUNT,
    };
    g_state.geometry.instance_capacity = runtime_state.work_unit_count;
    g_state.geometry.buffers.vbo_bytes = byte_count;
    static const float unit_quad[DB_GL3_UNIT_QUAD_FLOAT_COUNT] = {
        0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F,
    };
    memcpy(g_state.geometry.vertex.vertices, unit_quad, sizeof(unit_quad));
    g_state.runtime = runtime_state;
    return 1;
}

void db_gl3_init(const db_renderer_runtime_contract_t *resolved_runtime) {
    g_state = (renderer_state_t){0};
    if (resolved_runtime == NULL) {
        runtime_failf("missing resolved runtime");
    }
    g_state.target.format = resolved_runtime->format.surface_pixel_format;
    g_state.diagnostics = resolved_runtime->diagnostics;
    if (g_state.diagnostics.gl3_gradient == DB_GL3_GRADIENT_SEMANTIC) {
        g_state.presentation.applied.implementation =
            DB_GRADIENT_IMPLEMENTATION_SEMANTIC;
        g_state.presentation.applied.diagnostic_forced = 1;
    } else if (g_state.diagnostics.gl3_gradient ==
               DB_GL3_GRADIENT_EXACT_LOOKUP) {
        g_state.presentation.applied.implementation =
            DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP;
        g_state.presentation.applied.diagnostic_forced = 1;
    } else if (g_state.diagnostics.gl3_gradient == DB_GL3_GRADIENT_ROW_FILL) {
        g_state.presentation.applied.implementation =
            DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
        g_state.presentation.applied.diagnostic_forced = 1;
    }
    if (db_init_vertices_for_mode(resolved_runtime) == 0) {
        runtime_failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_gen_vertex_arrays(1, &g_state.geometry.vao);
    db_gl_geometry_stream_init_result_t stream_init = {0};
    if (db_gl_geometry_stream_init(
            &g_state.geometry.stream, &stream_init, BACKEND_NAME,
            g_state.geometry.buffers.vbo_bytes,
            g_state.geometry.vertex.vertices, g_state.geometry.vertex.vertices,
            DB_GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float), 1, 0) == 0) {
        runtime_failf("failed to initialize GL3 vertex stream");
    }
    if (g_state.geometry.stream.buffer == 0U) {
        runtime_failf("failed to create GL array buffer");
    }
    db_gl_bind_vertex_array(g_state.geometry.vao);
    if (db_gl_upload_stream_bind(&g_state.geometry.stream) == 0) {
        runtime_failf("failed to bind GL array buffer");
    }

    db_gl3_execute_bind_layout(&(const db_gl3_execute_context_t){
        .runtime = &g_state.runtime,
        .target = &g_state.target,
        .geometry = &g_state.geometry,
        .exact_lookup = &g_state.exact_lookup,
        .draw_program = g_state.presentation.draw_program,
    });
    const size_t lookup_row_capacity = g_state.geometry.instance_capacity;
    if (db_gl3_exact_lookup_init(&g_state.exact_lookup, g_state.target.format,
                                 lookup_row_capacity) == 0) {
        runtime_failf("failed to initialize bounded GL3 exact lookup");
    }

    gl3_refresh_capability_mode();
    db_log_renderer_capability(
        BACKEND_NAME,
        ((g_state.runtime.backbuffer_draw_full != 0) ||
         (g_state.runtime.pipeline.uses_history_pipeline == 0))
            ? "full_present"
            : "dirty_replay",
        db_gl_stream_upload_uses_buffer_object(
            &g_state.geometry.stream.capability)
            ? "buffer_object"
            : "client_arrays",
        g_state.runtime.backbuffer_replay_enabled, "geometry_stream");
    db_gl_upload_stream_log_selection(&g_state.geometry.stream, BACKEND_NAME,
                                      "rectangle_geometry");
    g_state.presentation.draw_program = db_gl3_build_program(
        db_gl3_ir_execute_vert_source, db_gl3_ir_execute_frag_source);
    g_state.presentation.present_program = db_gl3_build_program(
        db_gl3_presentation_vert_source, db_gl3_presentation_frag_source);
    g_state.presentation.sampler_location = db_gl_get_uniform_location(
        g_state.presentation.present_program, "backing_texture");
    g_state.presentation.hdr_output_location = db_gl_get_uniform_location(
        g_state.presentation.present_program, "hdr_output_enabled");
    g_state.presentation.hdr_output_enabled =
        resolved_runtime->format.native_hdr_enabled;
    db_gl_use_program(g_state.presentation.draw_program);
}

static int gl3_qualification_describe(
    void *renderer, db_renderer_qualification_descriptor_store_t *output) {
    (void)renderer;
    const int forced = g_state.diagnostics.gl3_gradient != DB_GL3_GRADIENT_AUTO;
    return db_gl3_describe_qualification(
        g_state.target.format, g_state.runtime.grid_cols,
        g_state.runtime.grid_rows, g_state.presentation.applied.implementation,
        forced, g_state.exact_lookup.available,
        db_gl3_exact_lookup_implementation_hash(&g_state.exact_lookup), output);
}

static db_renderer_prepare_status_t
gl3_qualification_prepare(void *renderer,
                          const db_qualification_snapshot_t *snapshot,
                          db_renderer_selection_candidate_t *candidate) {
    (void)renderer;
    if ((snapshot == NULL) || (candidate == NULL) ||
        ((snapshot->production_qualified == 0) &&
         (snapshot->diagnostic_forced == 0))) {
        return DB_RENDERER_PREPARE_UNAVAILABLE;
    }
    if ((snapshot->implementation != DB_GRADIENT_IMPLEMENTATION_SEMANTIC) &&
        (snapshot->implementation != DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) &&
        (snapshot->implementation !=
         DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES)) {
        return DB_RENDERER_PREPARE_UNAVAILABLE;
    }
    if ((snapshot->implementation == DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) &&
        (g_state.exact_lookup.available == 0)) {
        return DB_RENDERER_PREPARE_UNAVAILABLE;
    }
    *candidate = (db_renderer_selection_candidate_t){
        .snapshot = *snapshot,
        .renderer_generation = g_state.target.generation,
        .prepared = 1,
    };
    return DB_RENDERER_PREPARE_OK;
}

static db_renderer_commit_status_t
gl3_qualification_commit(void *renderer,
                         db_renderer_selection_candidate_t *candidate,
                         db_renderer_applied_selection_t *applied) {
    (void)renderer;
    if ((candidate == NULL) || (applied == NULL) ||
        (candidate->prepared == 0)) {
        return DB_RENDERER_COMMIT_FAILED;
    }
    if (candidate->renderer_generation != g_state.target.generation) {
        return DB_RENDERER_COMMIT_STALE;
    }
    *applied = (db_renderer_applied_selection_t){
        .generation = candidate->snapshot.generation,
        .implementation = candidate->snapshot.implementation,
        .retained_lanes = candidate->snapshot.retained_lanes,
        .lane_count = candidate->snapshot.lane_count,
        .strategy = candidate->snapshot.strategy,
        .source = candidate->snapshot.source,
        .cache_status = candidate->snapshot.cache_status,
        .production_qualified = candidate->snapshot.production_qualified,
        .diagnostic_forced = candidate->snapshot.diagnostic_forced,
    };
    (void)db_snprintf(applied->reason, sizeof(applied->reason), "%s",
                      candidate->snapshot.reason);
    g_state.presentation.applied = *applied;
    candidate->prepared = 0;
    return DB_RENDERER_COMMIT_OK;
}

static void
gl3_qualification_abort(void *renderer,
                        db_renderer_selection_candidate_t *candidate) {
    (void)renderer;
    if (candidate != NULL) {
        *candidate = (db_renderer_selection_candidate_t){0};
    }
}

const db_renderer_qualification_ops_t *db_gl3_qualification_ops(void) {
    static const db_renderer_qualification_ops_t operations = {
        .describe = gl3_qualification_describe,
        .prepare_apply = gl3_qualification_prepare,
        .commit_apply = gl3_qualification_commit,
        .abort_apply = gl3_qualification_abort,
    };
    return &operations;
}
void db_gl3_render_frame(const db_frame_plan_t *plan,
                         const db_renderer_target_t *target,
                         int viewport_width_px, int viewport_height_px) {
    if ((plan == NULL) || (target == NULL) || (target->valid == 0) ||
        (target->strategy != DB_RENDER_TARGET_GL3_PERSISTENT_FBO)) {
        return;
    }
    (void)db_renderer_resolve_viewport_state(
        BACKEND_NAME, plan->grid_cols, plan->grid_rows, &viewport_width_px,
        &viewport_height_px, &g_state.presentation.viewport.last_viewport_w,
        &g_state.presentation.viewport.last_viewport_h);
    if ((plan->update_metadata.status != DB_RENDER_IR_OK) ||
        (plan->rebuild_metadata.status != DB_RENDER_IR_OK)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical render IR is invalid");
    }

    int presentation_fbo = 0;
    db_gl_get_integerv(GL_DRAW_FRAMEBUFFER_BINDING, &presentation_fbo);
    const uint32_t presentation_fbo_u32 = db_checked_int_to_u32(
        BACKEND_NAME, "presentation_fbo", presentation_fbo);

    const int backing_width = db_checked_u32_to_i32(
        BACKEND_NAME, "logical_raster_width", plan->pixel_width);
    const int backing_height = db_checked_u32_to_i32(
        BACKEND_NAME, "logical_raster_height", plan->pixel_height);
    const int recreated =
        db_gl3_target_ensure(&g_state.target, backing_width, backing_height);
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_GL3, "gl3_backing",
                                    g_state.target.generation, plan);
    const int rebuild =
        DB_BOOL((recreated != 0) || (g_state.target.valid == 0) ||
                (plan->rebuild_required != 0));
    const int use_raster_seed =
        DB_BOOL((rebuild != 0) && (plan->external_bindings.count > 0U));
    if (use_raster_seed != 0) {
        db_gl3_target_restore(&g_state.target, plan);
    }
    if ((rebuild != 0) && (use_raster_seed == 0) &&
        (plan->rebuild_ir.fill_count == 0U) &&
        (db_render_ir_final_damage_covers(&plan->update_ir, plan->grid_cols,
                                          plan->grid_rows) == 0)) {
        runtime_failf(
            "persistent backing rebuild has no authoritative geometry");
    }
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.target.fbo);
    db_gl_set_viewport_px(backing_width, backing_height);
    const db_render_ir_view_t *const rebuild_draw_ir =
        ((rebuild != 0) && (use_raster_seed == 0)) ? &plan->rebuild_ir : NULL;
    uint32_t semantic_gradients = 0U;
    uint32_t exact_gradients = 0U;
    uint32_t fallback_instances = 0U;
    size_t lookup_upload_bytes = 0U;
    const uint32_t total_gradient_commands =
        plan->update_metadata.gradient_count +
        ((rebuild != 0) ? plan->rebuild_metadata.gradient_count : 0U);
    const db_gl3_execute_context_t execute_context = {
        .runtime = &g_state.runtime,
        .target = &g_state.target,
        .geometry = &g_state.geometry,
        .exact_lookup = &g_state.exact_lookup,
        .draw_program = g_state.presentation.draw_program,
    };
    uint32_t draw_instances = db_gl3_execute_ir(
        &execute_context, rebuild_draw_ir, &plan->update_ir,
        target->gradient_path, &semantic_gradients, &exact_gradients,
        &lookup_upload_bytes, &fallback_instances);
    if ((total_gradient_commands > 0U) && (rebuild != 0) &&
        (g_state.diagnostics.gl3_gradient == DB_GL3_GRADIENT_SEMANTIC)) {
        (void)db_gl3_qualify_current_target(
            plan, g_state.target.format, &g_state.hash.scratch,
            g_state.diagnostics.gradient_divergence_path);
    }
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = plan->frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
        .stage = DB_DAMAGE_TRACE_STAGE_RENDERER_WRITE,
        .operation = (rebuild != 0) ? DB_DAMAGE_TRACE_OP_REBUILD
                                    : DB_DAMAGE_TRACE_OP_INCREMENTAL,
        .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
        .destination = DB_DAMAGE_TRACE_BUFFER_GL_FBO,
        .space = DB_DAMAGE_TRACE_SPACE_GRID,
        .width = plan->grid_cols,
        .height = plan->grid_rows,
        .pixel_format = g_state.target.format,
        .transfer_size_bytes = db_checked_mul_size(
            BACKEND_NAME, "trace_vertex_bytes",
            db_checked_mul_size(BACKEND_NAME, "trace_instance_float_count",
                                (size_t)draw_instances,
                                DB_GL3_INSTANCE_FLOAT_COUNT),
            sizeof(float)),
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        .target = "gl3_backing",
        .target_generation = g_state.target.generation,
    });
    g_state.target.valid = 1;
    if (rebuild != 0) {
        db_damage_trace_emit_target_lifecycle(
            &(const db_target_lifecycle_event_t){
                .backend = DB_DAMAGE_TRACE_BACKEND_GL3,
                .action = DB_TARGET_LIFECYCLE_REBUILD,
                .target = "gl3_backing",
                .target_id = 1U,
                .generation = g_state.target.generation,
                .new_width = (uint32_t)g_state.target.width,
                .new_height = (uint32_t)g_state.target.height,
                .format = g_state.target.format,
                .cause = (recreated != 0) ? "target_recreated" : "frame_plan",
                .valid_before = DB_BOOL(recreated == 0),
                .valid_after = 1,
            });
    }
    gl3_trace_full_frame(plan->frame_index, DB_DAMAGE_TRACE_STAGE_RENDER_TARGET,
                         DB_DAMAGE_TRACE_OP_DRAW, DB_DAMAGE_TRACE_BUFFER_GL_FBO,
                         DB_DAMAGE_TRACE_BUFFER_GL_FBO, 0U);

    db_gl_bind_framebuffer(GL_FRAMEBUFFER, presentation_fbo_u32);
    db_gl_set_viewport_px(g_state.presentation.viewport.last_viewport_w,
                          g_state.presentation.viewport.last_viewport_h);
    db_gl_use_program(g_state.presentation.present_program);
    db_gl_active_texture(GL_TEXTURE0);
    db_gl_texture_bind_2d(g_state.target.texture);
    db_gl_uniform1i(g_state.presentation.sampler_location, 0);
    db_gl_uniform1i(g_state.presentation.hdr_output_location,
                    g_state.presentation.hdr_output_enabled);
    db_gl_draw_arrays_triangles(0U, 3U);
    db_gl_texture_bind_2d(0U);

    const uint32_t work_count = draw_instances;
    const uint32_t gradient_commands = total_gradient_commands;
    const uint32_t solid_commands =
        plan->update_metadata.solid_command_count +
        ((rebuild != 0) ? plan->rebuild_metadata.solid_command_count : 0U);
    db_render_operation_path_t gradient_path = DB_RENDER_OPERATION_NONE;
    if (semantic_gradients > 0U) {
        gradient_path = DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT;
    } else if (exact_gradients > 0U) {
        gradient_path = DB_RENDER_OPERATION_GL3_EXACT_LOOKUP;
    } else if (gradient_commands > 0U) {
        gradient_path = DB_RENDER_OPERATION_GL3_ROW_FILL;
    }
    g_state.telemetry.execution = (db_render_execution_report_t){
        .target_strategy = DB_RENDER_TARGET_GL3_PERSISTENT_FBO,
        .solid_path = DB_RENDER_OPERATION_GL3_INSTANCED_SOLID,
        .gradient_path = gradient_path,
        .solid_commands = solid_commands,
        .gradient_commands = gradient_commands,
        .solid_draws = DB_BOOL(draw_instances > 0U),
        .gradient_draws = DB_BOOL(gradient_commands > 0U),
        .fallback_instances = fallback_instances,
        .lookup_words = db_checked_size_to_u32(
            BACKEND_NAME, "lookup_words",
            db_checked_mul_size(BACKEND_NAME, "lookup_word_count",
                                g_state.exact_lookup.row_count,
                                g_state.exact_lookup.words_per_row)),
        .lookup_upload_bytes = lookup_upload_bytes,
        .command_upload_bytes = db_checked_mul_size(
            BACKEND_NAME, "command_upload_bytes",
            db_checked_mul_size(BACKEND_NAME, "command_upload_floats",
                                (size_t)draw_instances,
                                DB_GL3_INSTANCE_FLOAT_COUNT),
            sizeof(float)),
        .gradient_implementation =
            (gradient_commands > 0U)
                ? g_state.presentation.applied.implementation
                : DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
        .qualification_source = g_state.presentation.applied.source,
        .cache_status = g_state.presentation.applied.cache_status,
        .qualification_lane_count = g_state.presentation.applied.lane_count,
        .qualification_reason = g_state.presentation.applied.reason,
        .strategy_reason =
            db_renderer_strategy_reason_name(target->strategy_reason),
        .strategy_generation = target->strategy_generation,
        .qualification_generation = target->qualification_generation,
        .target_generation = target->target_generation,
        .qualified =
            DB_BOOL((gradient_commands == 0U) ||
                    ((g_state.presentation.applied.generation != 0U) &&
                     (g_state.presentation.applied.diagnostic_forced == 0))),
        .diagnostic_forced = g_state.presentation.applied.diagnostic_forced,
    };
    const int full_draw = rebuild;
    db_renderer_record_draw_stats_for_work(
        &g_state.telemetry.frame.full_draw_frames,
        &g_state.telemetry.frame.dirty_draw_frames, full_draw,
        DB_BOOL(full_draw == 0), work_count);
    gl3_trace_full_frame(plan->frame_index, DB_DAMAGE_TRACE_STAGE_PRESENT,
                         DB_DAMAGE_TRACE_OP_PRESENT,
                         DB_DAMAGE_TRACE_BUFFER_GL_FBO,
                         DB_DAMAGE_TRACE_BUFFER_GL_DEFAULT_FRAMEBUFFER, 0U);
    g_state.telemetry.frame.state_hash = plan->expected_state_hash;
    g_state.telemetry.frame.frame_index++;
}

void db_gl3_shutdown(void) {
    db_gl3_exact_lookup_shutdown(&g_state.exact_lookup);
    db_gl_upload_stream_shutdown(&g_state.geometry.stream);
    db_gl_hash_scratch_release(&g_state.hash.scratch);
    db_gl3_target_destroy(&g_state.target, "shutdown");
    db_gl_delete_program(g_state.presentation.present_program);
    db_gl_delete_program(g_state.presentation.draw_program);
    db_gl_delete_vertex_arrays(1, &g_state.geometry.vao);
    free(g_state.geometry.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_gl3_capability_mode(void) {
    if (g_state.telemetry.capability_mode[0] == '\0') {
        gl3_refresh_capability_mode();
    }
    return g_state.telemetry.capability_mode;
}

uint32_t db_gl3_work_unit_count(void) {
    return g_state.runtime.work_unit_count;
}

uint64_t db_gl3_state_hash(void) { return g_state.telemetry.frame.state_hash; }

void db_gl3_draw_stats(db_renderer_draw_path_stats_t *stats) {
    db_renderer_copy_draw_path_stats(&g_state.telemetry.frame, stats);
}

void db_gl3_execution_report(db_render_execution_report_t *report) {
    if (report != NULL) {
        *report = g_state.telemetry.execution;
    }
}
