#include "gl3_renderer.h"

#include "core/db_conformance.h"
#include "core/db_log.h"
#include "core/db_qualification_contracts.h"
#include "core/db_renderer_diagnostics.h"
#include "core/db_renderer_runtime_contract.h"
#include "core/db_renderer_support.h"
#include "gl3_qualification.h"
#include "gl3_target.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_gradient_divergence.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_ir.h"
#include "../damage_trace.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_hash_readback.h"
#include "../renderer_viewport_common.h"
#include "core/db_raster_geometry.h"
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
#define ATTR_COLOR_LOC 2U
#define ATTR_END_COLOR_LOC 3U
#define ATTR_GRADIENT_LOC 4U
#define ATTR_RECT_LOC 1U
#define ATTR_POSITION_LOC 0U
#define GL3_INSTANCE_FLOAT_COUNT 14U
#define GL3_INSTANCE_START_COLOR_OFFSET 4U
#define GL3_INSTANCE_END_COLOR_OFFSET 7U
#define GL3_INSTANCE_MODE_OFFSET 10U
#define GL3_INSTANCE_AXIS_START_OFFSET 11U
#define GL3_INSTANCE_AXIS_END_OFFSET 12U
#define GL3_INSTANCE_GRID_ROWS_OFFSET 13U
#define GL3_UNIT_QUAD_FLOAT_COUNT 12U
#define GL3_UNIT_QUAD_VERTEX_COUNT 6U
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    db_gl_vertex_init_t vertex;
    unsigned int vao;
    db_gl_buffer_cache_t buffers;
    db_gl_upload_stream_t stream;
} gl3_geometry_stream_t;

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
    gl3_geometry_stream_t geometry;
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
    const uint64_t hash = db_gl_hash_framebuffer_rgba16f_or_fail(
        BACKEND_NAME,
        db_checked_int_to_u32(BACKEND_NAME, "backing_width",
                              g_state.target.width),
        db_checked_int_to_u32(BACKEND_NAME, "backing_height",
                              g_state.target.height),
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

static void gl3_bind_main_vbo_layout(void) {
    (void)db_gl_upload_stream_bind(&g_state.geometry.stream);
    const int32_t unit_stride = db_checked_size_to_i32(
        BACKEND_NAME, "unit_vertex_stride", 2U * sizeof(float));
    const int32_t instance_stride =
        db_checked_size_to_i32(BACKEND_NAME, "instance_stride",
                               GL3_INSTANCE_FLOAT_COUNT * sizeof(float));
    const size_t instance_base = GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float);
    db_gl_vertex_attrib_pointer_2f(ATTR_POSITION_LOC, unit_stride, 0U);
    db_gl_vertex_attrib_pointer_4f(ATTR_RECT_LOC, instance_stride,
                                   instance_base);
    db_gl_vertex_attrib_pointer_3f(
        ATTR_COLOR_LOC, instance_stride,
        instance_base + (GL3_INSTANCE_START_COLOR_OFFSET * sizeof(float)));
    db_gl_vertex_attrib_pointer_3f(
        ATTR_END_COLOR_LOC, instance_stride,
        instance_base + (GL3_INSTANCE_END_COLOR_OFFSET * sizeof(float)));
    db_gl_vertex_attrib_pointer_4f(
        ATTR_GRADIENT_LOC, instance_stride,
        instance_base + (GL3_INSTANCE_MODE_OFFSET * sizeof(float)));
    if ((db_gl_vertex_attrib_divisor(ATTR_POSITION_LOC, 0U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_RECT_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_COLOR_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_END_COLOR_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_GRADIENT_LOC, 1U) == 0)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "GL3 instancing is unavailable");
    }
}

static int gl3_append_instance(db_render_ir_rect_t rect,
                               db_render_ir_color_t start_color,
                               db_render_ir_color_t end_color, float mode,
                               int32_t axis_start, int32_t axis_end,
                               size_t index) {
    const size_t capacity = (size_t)g_state.geometry.vertex.draw_vertex_count /
                            DB_RECT_VERTEX_COUNT;
    if (index >= capacity) {
        return 0;
    }
    db_grid_block_t grid_block = {0};
    if (db_render_ir_rect_to_grid_block(rect, g_state.runtime.grid_cols,
                                        g_state.runtime.grid_rows,
                                        &grid_block) == 0) {
        return 0;
    }
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    db_grid_block_bounds_ndc_for_extent(g_state.runtime.grid_cols,
                                        g_state.runtime.grid_rows, &grid_block,
                                        &x0, &y0, &x1, &y1);
    float start[3] = {0.0F, 0.0F, 0.0F};
    float end[3] = {0.0F, 0.0F, 0.0F};
    db_rgb_f64_quantize_f16_to_f32_rgb3(start_color.rgba, start);
    db_rgb_f64_quantize_f16_to_f32_rgb3(end_color.rgba, end);
    float *const instance =
        &g_state.geometry.vertex.vertices[GL3_UNIT_QUAD_FLOAT_COUNT +
                                          (index * GL3_INSTANCE_FLOAT_COUNT)];
    instance[0] = x0;
    instance[1] = y0;
    instance[2] = x1 - x0;
    instance[3] = y1 - y0;
    memcpy(&instance[GL3_INSTANCE_START_COLOR_OFFSET], start, sizeof(start));
    memcpy(&instance[GL3_INSTANCE_END_COLOR_OFFSET], end, sizeof(end));
    instance[GL3_INSTANCE_MODE_OFFSET] = mode;
    instance[GL3_INSTANCE_AXIS_START_OFFSET] = db_i32_to_f32(axis_start);
    instance[GL3_INSTANCE_AXIS_END_OFFSET] = db_i32_to_f32(axis_end);
    instance[GL3_INSTANCE_GRID_ROWS_OFFSET] =
        db_u32_to_f32(g_state.runtime.grid_rows);
    return 1;
}

static size_t gl3_append_ir_instances(const db_render_ir_view_t *ir,
                                      size_t count,
                                      uint32_t *semantic_gradients,
                                      uint32_t *fallback_instances) {
    if (ir == NULL) {
        return count;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, ir);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            break;
        case DB_RENDER_IR_OP_CLEAR: {
            const db_render_ir_clear_command_t *const clear =
                (const db_render_ir_clear_command_t *)command;
            const db_render_ir_rect_t rect = {
                .width = db_checked_u32_to_i32(BACKEND_NAME, "clear_width",
                                               g_state.runtime.grid_cols),
                .height = db_checked_u32_to_i32(BACKEND_NAME, "clear_height",
                                                g_state.runtime.grid_rows),
            };
            if (gl3_append_instance(rect, clear->color, clear->color, 0.0F, 0,
                                    0, count++) == 0) {
                return SIZE_MAX;
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                (const db_render_ir_fill_command_t *)command;
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    ir->fills[fills->first_fill + index];
                if (gl3_append_instance(fill.rect, fill.color, fill.color, 0.0F,
                                        0, 0, count++) == 0) {
                    return SIZE_MAX;
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
            db_render_ir_color_t start = gradient->start_color;
            db_render_ir_color_t end = gradient->end_color;
            if (gradient->reverse_stops != 0U) {
                const db_render_ir_color_t swap = start;
                start = end;
                end = swap;
            }
            if ((command->clip_region == DB_RENDER_IR_INVALID_ID) &&
                (g_state.presentation.applied.implementation ==
                 DB_GRADIENT_IMPLEMENTATION_SEMANTIC)) {
                if (gl3_append_instance(gradient->bounds, start, end, 1.0F,
                                        gradient->axis_start,
                                        gradient->axis_end, count++) == 0) {
                    return SIZE_MAX;
                }
                (*semantic_gradients)++;
                break;
            }
            for (int32_t row = gradient->bounds.y;
                 row < gradient->bounds.y + gradient->bounds.height; row++) {
                const db_render_ir_rect_t row_rect = {
                    .x = gradient->bounds.x,
                    .y = row,
                    .width = gradient->bounds.width,
                    .height = 1,
                };
                db_gradient_vector_t vector = {0};
                if (db_gradient_vector_evaluate(gradient, row_rect, row,
                                                &vector) !=
                    DB_GRADIENT_VECTOR_OK) {
                    return SIZE_MAX;
                }
                const db_render_ir_color_t color = {
                    .rgba = {vector.canonical_rgba[0], vector.canonical_rgba[1],
                             vector.canonical_rgba[2],
                             vector.canonical_rgba[3]},
                };
                if (gl3_append_instance(row_rect, color, color, 0.0F, 0, 0,
                                        count++) == 0) {
                    return SIZE_MAX;
                }
                (*fallback_instances)++;
            }
            break;
        }
        }
    }
    return count;
}

static uint32_t draw_ir_fills(const db_render_ir_view_t *first_ir,
                              const db_render_ir_view_t *second_ir,
                              uint32_t *semantic_gradients,
                              uint32_t *fallback_instances) {
    size_t fill_count = gl3_append_ir_instances(
        first_ir, 0U, semantic_gradients, fallback_instances);
    if (fill_count != SIZE_MAX) {
        fill_count = gl3_append_ir_instances(
            second_ir, fill_count, semantic_gradients, fallback_instances);
    }
    if (fill_count == SIZE_MAX) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 IR capacity exceeded");
    }
    if (fill_count == 0U) {
        return 0U;
    }
    if (db_gl_upload_stream_wait(&g_state.geometry.stream) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "canonical GL3 geometry stream reuse timed out");
    }
    const size_t stride = GL3_INSTANCE_FLOAT_COUNT;
    const size_t float_count = db_checked_mul_size(
        BACKEND_NAME, "canonical_instance_float_count", fill_count, stride);
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "canonical_instance_bytes", float_count, sizeof(float));
    const size_t instance_offset = GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float);
    if (db_gl_upload_stream_write(
            &g_state.geometry.stream, BACKEND_NAME,
            &g_state.geometry.vertex.vertices[GL3_UNIT_QUAD_FLOAT_COUNT],
            g_state.geometry.buffers.vbo_bytes, instance_offset,
            byte_count) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 geometry upload failed");
    }
    gl3_bind_main_vbo_layout();
    db_gl_set_scissor_enabled(0);
    const uint32_t instance_count = db_checked_size_to_u32(
        BACKEND_NAME, "canonical_instance_count", fill_count);
    if (db_gl_draw_arrays_triangles_instanced(0U, GL3_UNIT_QUAD_VERTEX_COUNT,
                                              instance_count) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 instanced draw failed");
    }
    db_gl_upload_stream_record_sync(&g_state.geometry.stream);
    return instance_count;
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
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_execution_config(BACKEND_NAME, &init_state,
                                               &runtime_state,
                                               GL3_INSTANCE_FLOAT_COUNT)) {
        return 0;
    }

    g_state.geometry.vertex = init_state;
    static const float unit_quad[GL3_UNIT_QUAD_FLOAT_COUNT] = {
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
    } else if (g_state.diagnostics.gl3_gradient == DB_GL3_GRADIENT_ROW_FILL) {
        g_state.presentation.applied.implementation =
            DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
        g_state.presentation.applied.diagnostic_forced = 1;
    }
    if (db_init_vertices_for_mode(resolved_runtime) == 0) {
        runtime_failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_gen_vertex_arrays(1, &g_state.geometry.vao);
    const size_t vertex_float_count =
        db_checked_mul_size(BACKEND_NAME, "vertex_float_count",
                            (size_t)g_state.geometry.vertex.draw_vertex_count,
                            GL3_INSTANCE_FLOAT_COUNT);
    g_state.geometry.buffers.vbo_bytes = db_checked_mul_size(
        BACKEND_NAME, "vertex_buffer_bytes", vertex_float_count, sizeof(float));
    db_gl_geometry_stream_init_result_t stream_init = {0};
    if (db_gl_geometry_stream_init(
            &g_state.geometry.stream, &stream_init, BACKEND_NAME,
            g_state.geometry.buffers.vbo_bytes,
            g_state.geometry.vertex.vertices, g_state.geometry.vertex.vertices,
            GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float), 1, 0) == 0) {
        runtime_failf("failed to initialize GL3 vertex stream");
    }
    if (g_state.geometry.stream.buffer == 0U) {
        runtime_failf("failed to create GL array buffer");
    }
    db_gl_bind_vertex_array(g_state.geometry.vao);
    if (db_gl_upload_stream_bind(&g_state.geometry.stream) == 0) {
        runtime_failf("failed to bind GL array buffer");
    }

    db_gl_enable_vertex_attrib_array(ATTR_POSITION_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_RECT_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_COLOR_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_END_COLOR_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_GRADIENT_LOC);
    gl3_bind_main_vbo_layout();

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
        forced, output);
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
        (snapshot->implementation !=
         DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES)) {
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
void db_gl3_render_frame(const db_frame_plan_t *plan, int viewport_width_px,
                         int viewport_height_px) {
    if (plan == NULL) {
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
    db_gl_use_program(g_state.presentation.draw_program);
    const db_render_ir_view_t *const rebuild_draw_ir =
        ((rebuild != 0) && (use_raster_seed == 0)) ? &plan->rebuild_ir : NULL;
    uint32_t semantic_gradients = 0U;
    uint32_t fallback_instances = 0U;
    const uint32_t total_gradient_commands =
        plan->update_metadata.gradient_count +
        ((rebuild != 0) ? plan->rebuild_metadata.gradient_count : 0U);
    uint32_t draw_instances =
        draw_ir_fills(rebuild_draw_ir, &plan->update_ir, &semantic_gradients,
                      &fallback_instances);
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
                                GL3_INSTANCE_FLOAT_COUNT),
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
        .gradient_implementation =
            (gradient_commands > 0U)
                ? g_state.presentation.applied.implementation
                : DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
        .qualification_source = g_state.presentation.applied.source,
        .cache_status = g_state.presentation.applied.cache_status,
        .qualification_lane_count = g_state.presentation.applied.lane_count,
        .qualification_reason = g_state.presentation.applied.reason,
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
