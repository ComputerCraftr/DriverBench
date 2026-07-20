#include "gl1_internal.h"

#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_frame_contracts.h"
#include "core/db_frame_plan.h"
#include "core/db_geometry.h"
#include "core/db_gradient_divergence.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_probe_protocol.h"
#include "core/db_qualification_contracts.h"
#include "core/db_raster_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_diagnostics.h"
#include "core/db_replay_policy.h"
#include "renderers/damage_trace.h"
#include "renderers/gl_api.h"
#include "renderers/gl_common.h"
#include "renderers/gl_gradient_qualification.h"
#include "renderers/gl_hash_readback.h"
#include "renderers/gl_probe_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    GL1_NATIVE_VERTEX_FLOATS = 6U,
    GL1_NATIVE_COLOR_OFFSET = 2U,
    GL1_NATIVE_MAX_IR_STREAMS = DB_REPLAY_CAPACITY_MAX + 2U,
};

#define DB_GL1_PROBE_IMPLEMENTATION_DOMAIN UINT32_C(0x47314646)

static db_render_operation_path_t
gradient_operation_path(uint32_t gradient_commands) {
    if (gradient_commands == 0U) {
        return DB_RENDER_OPERATION_NONE;
    }
    if (g_state.native.applied.implementation ==
        DB_GRADIENT_IMPLEMENTATION_SEMANTIC) {
        return DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT;
    }
    return DB_RENDER_OPERATION_GL1_ROW_FILL;
}

static uint64_t
implementation_hash(db_gradient_implementation_t implementation) {
    static const char row_implementation[] = "gl1_fixed_function_row_v1";
    static const char interpolation_implementation[] =
        "gl1_fixed_function_interpolation_v1";
    const char *const implementation_text =
        (implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC)
            ? interpolation_implementation
            : row_implementation;
    return db_fnv1a64_tree(implementation_text, strlen(implementation_text),
                           DB_GL1_PROBE_IMPLEMENTATION_DOMAIN,
                           DB_FNV1A64_OFFSET);
}

static int append_qualification_descriptor(
    db_renderer_qualification_descriptor_store_t *store,
    db_render_target_strategy_t strategy,
    db_gradient_implementation_t implementation) {
    db_renderer_probe_descriptor_t descriptor = {
        .backend = DB_PROBE_BACKEND_GL1,
        .strategy = strategy,
        .implementation = implementation,
        .lane_index = 0U,
        .is_primary = 1,
        .working_format = g_state.backing.format.surface_pixel_format,
        .implementation_hash = implementation_hash(implementation),
        .logical_width = g_state.runtime.grid_cols,
        .logical_height = g_state.runtime.grid_rows,
        .compatibility_validated =
            implementation == DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
    };
    const char *const renderer = db_gl_get_renderer_string();
    const char *const version = db_gl_get_version_string();
    (void)db_snprintf(descriptor.provider, sizeof(descriptor.provider), "%s",
                      "gl_context");
    (void)db_snprintf(descriptor.driver.name, sizeof(descriptor.driver.name),
                      "%s", (renderer != NULL) ? renderer : "unknown");
    (void)db_snprintf(descriptor.driver.info, sizeof(descriptor.driver.info),
                      "%s", (version != NULL) ? version : "unknown");
    return db_qualification_descriptor_store_append(store, &descriptor);
}

static int gl1_qualification_describe(
    void *renderer, db_renderer_qualification_descriptor_store_t *output) {
    (void)renderer;
    if (output == NULL) {
        return 0;
    }
    *output = (db_renderer_qualification_descriptor_store_t){
        .generation =
            {
                .device_generation = 1U,
                .implementation_generation =
                    implementation_hash(DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ^
                    implementation_hash(
                        DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES),
                .target_contract_generation =
                    ((uint64_t)g_state.runtime.grid_cols << 32U) |
                    g_state.runtime.grid_rows,
            },
    };
    const int forced = g_state.diagnostics.gl1_gradient != DB_GL1_GRADIENT_AUTO;
    if (forced != 0) {
        db_gradient_implementation_t implementation =
            DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
        if (g_state.diagnostics.gl1_gradient == DB_GL1_GRADIENT_INTERPOLATED) {
            implementation = DB_GRADIENT_IMPLEMENTATION_SEMANTIC;
        }
        return append_qualification_descriptor(
            output, DB_RENDER_TARGET_GL1_PERSISTENT_FBO, implementation);
    }
    return append_qualification_descriptor(
               output, DB_RENDER_TARGET_GL1_PERSISTENT_FBO,
               DB_GRADIENT_IMPLEMENTATION_SEMANTIC) &&
           append_qualification_descriptor(
               output, DB_RENDER_TARGET_GL1_PERSISTENT_FBO,
               DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES);
}

static db_renderer_prepare_status_t
gl1_qualification_prepare(void *renderer,
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
        .renderer_generation = g_state.native.generation,
        .prepared = 1,
    };
    return DB_RENDERER_PREPARE_OK;
}

static db_renderer_commit_status_t
gl1_qualification_commit(void *renderer,
                         db_renderer_selection_candidate_t *candidate,
                         db_renderer_applied_selection_t *applied) {
    (void)renderer;
    if ((candidate == NULL) || (applied == NULL) ||
        (candidate->prepared == 0)) {
        return DB_RENDERER_COMMIT_FAILED;
    }
    if (candidate->renderer_generation != g_state.native.generation) {
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
    g_state.native.applied = *applied;
    candidate->prepared = 0;
    return DB_RENDERER_COMMIT_OK;
}

static void
gl1_qualification_abort(void *renderer,
                        db_renderer_selection_candidate_t *candidate) {
    (void)renderer;
    if (candidate != NULL) {
        *candidate = (db_renderer_selection_candidate_t){0};
    }
}

const db_renderer_qualification_ops_t *db_gl1_native_qualification_ops(void) {
    static const db_renderer_qualification_ops_t operations = {
        .describe = gl1_qualification_describe,
        .prepare_apply = gl1_qualification_prepare,
        .commit_apply = gl1_qualification_commit,
        .abort_apply = gl1_qualification_abort,
    };
    return &operations;
}

static int qualify_row_fill(const db_frame_plan_t *plan) {
    if ((plan == NULL) || (plan->rebuild_required == 0) ||
        (plan->update_metadata.gradient_count == 0U)) {
        return 0;
    }
    const size_t pixel_bytes =
        (g_state.backing.format.surface_pixel_format == DB_PIXEL_FORMAT_RGBA16F)
            ? DB_RGBA16F_BYTES_PER_PIXEL
            : DB_RGBA8_BYTES_PER_PIXEL;
    size_t pixel_count = 0U;
    size_t byte_count = 0U;
    if ((db_try_mul_size(plan->pixel_width, plan->pixel_height, &pixel_count) ==
         0) ||
        (db_try_mul_size(pixel_count, pixel_bytes, &byte_count) == 0)) {
        return 0;
    }
    if (byte_count == 0U) {
        return 0;
    }
    void *const pixels = malloc(byte_count);
    if (pixels == NULL) {
        return 0;
    }
    const db_pixel_surface_t reference = {
        .pixel_width = plan->pixel_width,
        .pixel_height = plan->pixel_height,
        .pixels = pixels,
        .format = g_state.backing.format.surface_pixel_format,
    };
    const int generated = DB_BOOL(
        db_frame_plan_rasterize_reference(plan, &reference) == DB_RENDER_IR_OK);
    const int conforming =
        generated ? db_gl_qualify_current_framebuffer(
                        BACKEND_NAME, &reference, &g_state.native.hash_scratch,
                        &(const db_gradient_compare_context_t){
                            .extent = {.width = (int32_t)plan->pixel_width,
                                       .height = (int32_t)plan->pixel_height}},
                        g_state.diagnostics.gradient_divergence_path)
                  : 0;
    free(pixels);
    return conforming;
}

static void trace_native_plan(const db_frame_plan_t *plan) {
    if ((plan == NULL) || (db_damage_trace_enabled() == 0)) {
        return;
    }
    const size_t count = 0U;
    (void)db_damage_trace_emit_grid(
        &(const db_damage_trace_event_t){
            .frame_index = plan->frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
            .stage = DB_DAMAGE_TRACE_STAGE_LOGICAL,
            .operation = DB_DAMAGE_TRACE_OP_COPY,
            .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .destination = DB_DAMAGE_TRACE_BUFFER_GL1_SHADOW,
            .space = DB_DAMAGE_TRACE_SPACE_GRID,
            .width = plan->grid_cols,
            .height = plan->grid_rows,
            .pixel_format = g_state.backing.format.surface_pixel_format,
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        },
        NULL, count);
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_GL1, "gl1_backing",
                                    g_state.native.generation, plan);
}

static int restore_seed_texture(const db_frame_plan_t *plan) {
    db_render_ir_upload_command_t upload = {0};
    db_render_ir_external_binding_t source = {0};
    if ((db_render_ir_resolve_full_upload(&plan->rebuild_ir,
                                          plan->external_bindings, &upload,
                                          &source) == 0) ||
        (source.pixels == NULL) || (source.width != g_state.native.width) ||
        (source.height != g_state.native.height) ||
        (source.format != g_state.backing.format.surface_pixel_format)) {
        return 0;
    }
    uint32_t row_length_pixels = 0U;
    if (db_gl_external_binding_unpack_row_length(
            &source, db_gl_context_supports_unpack_row_length_upload(),
            &row_length_pixels) == 0) {
        return 0;
    }
    db_gl_texture_bind_2d(g_state.presentation.shadow.texture);
    db_gl_set_unpack_row_length_pixels(row_length_pixels);
    if (source.format == DB_PIXEL_FORMAT_RGBA16F) {
        db_gl_texture_sub_image_2d_rgba16f(0U, 0U, source.width, source.height,
                                           (const uint16_t *)source.pixels);
    } else {
        db_gl_texture_sub_image_2d_rgba(0U, 0U, source.width, source.height,
                                        (const uint8_t *)source.pixels);
    }
    db_gl_set_unpack_row_length_pixels(0U);
    db_gl_texture_bind_2d(0U);
    return 1;
}

static size_t append_rect_vertices(db_render_ir_rect_t rect,
                                   db_render_ir_color_t bottom_color,
                                   db_render_ir_color_t top_color,
                                   size_t rect_index) {
    if (rect_index >= (g_state.native.vertex_capacity / DB_RECT_VERTEX_COUNT)) {
        return SIZE_MAX;
    }
    db_grid_block_t block = {0};
    if (db_render_ir_rect_to_grid_block(rect, g_state.runtime.grid_cols,
                                        g_state.runtime.grid_rows,
                                        &block) == 0) {
        return SIZE_MAX;
    }
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    db_grid_block_bounds_ndc_for_extent(g_state.runtime.grid_cols,
                                        g_state.runtime.grid_rows, &block, &x0,
                                        &y0, &x1, &y1);
    float *const vertices =
        &g_state.native.vertices[rect_index * DB_RECT_VERTEX_COUNT *
                                 GL1_NATIVE_VERTEX_FLOATS];
    db_fill_rect_unit_pos(vertices, x0, y0, x1, y1, GL1_NATIVE_VERTEX_FLOATS);
    float bottom[3] = {0.0F, 0.0F, 0.0F};
    float top[3] = {0.0F, 0.0F, 0.0F};
    db_rgb_f64_quantize_f16_to_f32_rgb3(bottom_color.rgba, bottom);
    db_rgb_f64_quantize_f16_to_f32_rgb3(top_color.rgba, top);
    static const uint8_t bottom_vertices[] = {0U, 1U, 3U};
    static const uint8_t top_vertices[] = {2U, 4U, 5U};
    for (size_t index = 0U; index < 3U; index++) {
        memcpy(&vertices[((size_t)bottom_vertices[index] *
                          GL1_NATIVE_VERTEX_FLOATS) +
                         GL1_NATIVE_COLOR_OFFSET],
               bottom, sizeof(bottom));
        memcpy(
            &vertices[((size_t)top_vertices[index] * GL1_NATIVE_VERTEX_FLOATS) +
                      GL1_NATIVE_COLOR_OFFSET],
            top, sizeof(top));
    }
    db_set_rect_unit_alpha(vertices, GL1_NATIVE_VERTEX_FLOATS,
                           GL1_NATIVE_VERTEX_FLOATS - 1U, 1.0F);
    return rect_index + 1U;
}

static size_t append_row_vertices(const db_render_ir_view_t *ir,
                                  size_t first_rect) {
    db_render_ir_rect_iterator_t iterator = {0};
    db_render_ir_rect_iterator_begin(&iterator, ir);
    db_render_ir_fill_t fill = {0};
    while (db_render_ir_rect_iterator_next(&iterator, &fill) != 0) {
        first_rect =
            append_rect_vertices(fill.rect, fill.color, fill.color, first_rect);
        if (first_rect == SIZE_MAX) {
            return SIZE_MAX;
        }
    }
    return first_rect;
}

static size_t append_ir_vertices(const db_render_ir_view_t *ir,
                                 size_t first_rect) {
    if (ir == NULL) {
        return first_rect;
    }
    if (g_state.native.applied.implementation !=
        DB_GRADIENT_IMPLEMENTATION_SEMANTIC) {
        return append_row_vertices(ir, first_rect);
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
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            const db_render_ir_rect_t rect = {
                .width = db_checked_u32_to_i32(BACKEND_NAME, "clear_width",
                                               g_state.runtime.grid_cols),
                .height = db_checked_u32_to_i32(BACKEND_NAME, "clear_height",
                                                g_state.runtime.grid_rows),
            };
            first_rect = append_rect_vertices(rect, clear->color, clear->color,
                                              first_rect);
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    ir->fills[fills->first_fill + index];
                first_rect = append_rect_vertices(fill.rect, fill.color,
                                                  fill.color, first_rect);
                if (first_rect == SIZE_MAX) {
                    return SIZE_MAX;
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            if (command->clip_region != DB_RENDER_IR_INVALID_ID) {
                return SIZE_MAX;
            }
            db_render_ir_color_t top = gradient->start_color;
            db_render_ir_color_t bottom = gradient->end_color;
            if (gradient->reverse_stops != 0U) {
                const db_render_ir_color_t swap = top;
                top = bottom;
                bottom = swap;
            }
            first_rect =
                append_rect_vertices(gradient->bounds, bottom, top, first_rect);
            if (first_rect == SIZE_MAX) {
                return SIZE_MAX;
            }
            break;
        }
        }
        if (first_rect == SIZE_MAX) {
            return SIZE_MAX;
        }
    }
    return first_rect;
}

static int draw_ir_sequence(const db_render_ir_view_t *const *streams,
                            size_t stream_count) {
    size_t rect_count = 0U;
    for (size_t index = 0U; index < stream_count; index++) {
        rect_count = append_ir_vertices(streams[index], rect_count);
        if (rect_count == SIZE_MAX) {
            return 0;
        }
    }
    if (rect_count == 0U) {
        return 1;
    }
    const size_t vertex_count = db_checked_mul_size(
        BACKEND_NAME, "native_vertex_count", rect_count, DB_RECT_VERTEX_COUNT);
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "native_vertex_bytes",
        db_checked_mul_size(BACKEND_NAME, "native_vertex_floats", vertex_count,
                            GL1_NATIVE_VERTEX_FLOATS),
        sizeof(float));
    if ((db_gl_upload_stream_wait(&g_state.native.vertex_stream) == 0) ||
        (db_gl_upload_stream_write(&g_state.native.vertex_stream, BACKEND_NAME,
                                   g_state.native.vertices,
                                   g_state.native.storage_bytes, 0U,
                                   byte_count) == 0)) {
        return 0;
    }
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_dither_enabled(0);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(0);
    const size_t stride = GL1_NATIVE_VERTEX_FLOATS * sizeof(float);
    if (g_state.native.vertex_stream.buffer != 0U) {
        (void)db_gl_upload_stream_bind(&g_state.native.vertex_stream);
        db_gl_set_vertex_pointer_2f(stride, db_gl_vbo_offset_ptr(0U));
        db_gl_set_color_pointer_f(
            4U, stride,
            db_gl_vbo_offset_ptr(GL1_NATIVE_COLOR_OFFSET * sizeof(float)));
    } else {
        db_gl_set_vertex_pointer_2f(stride, g_state.native.vertices);
        db_gl_set_color_pointer_f(
            4U, stride, &g_state.native.vertices[GL1_NATIVE_COLOR_OFFSET]);
    }
    db_gl_set_scissor_enabled(0);
    db_gl_draw_arrays_triangles(
        0U, db_checked_size_to_u32(BACKEND_NAME, "native_draw_vertices",
                                   vertex_count));
    (void)db_gl_upload_stream_unbind_target(
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER);
    db_gl_upload_stream_record_sync(&g_state.native.vertex_stream);
    return 1;
}

int db_gl1_native_init(void) {
    const size_t vertex_capacity = db_checked_mul_size(
        BACKEND_NAME, "native_vertex_capacity",
        db_checked_mul_size(BACKEND_NAME, "native_stream_rect_capacity",
                            DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY,
                            GL1_NATIVE_MAX_IR_STREAMS),
        DB_RECT_VERTEX_COUNT);
    const size_t float_capacity =
        db_checked_mul_size(BACKEND_NAME, "native_float_capacity",
                            vertex_capacity, GL1_NATIVE_VERTEX_FLOATS);
    g_state.native.vertices = (float *)db_malloc_or_fail(
        BACKEND_NAME, "native_vertices", float_capacity, sizeof(float));
    g_state.native.vertex_capacity = vertex_capacity;
    g_state.native.storage_bytes = db_checked_mul_size(
        BACKEND_NAME, "native_storage_bytes", float_capacity, sizeof(float));
    db_gl_geometry_stream_init_result_t result = {0};
    if (db_gl_geometry_stream_init(&g_state.native.vertex_stream, &result,
                                   BACKEND_NAME, g_state.native.storage_bytes,
                                   NULL, NULL, 0U, 1, 1) == 0) {
        return 0;
    }
    g_state.native.strategy = GL1_STRATEGY_UNRESOLVED;
    return 1;
}

static int ensure_fbo(uint32_t width, uint32_t height) {
    db_gl_shadow_present_prepare_texture(&g_state.presentation.shadow,
                                         BACKEND_NAME, width, height);
    if ((g_state.native.fbo != 0U) && (g_state.native.width == width) &&
        (g_state.native.height == height)) {
        return 1;
    }
    if (g_state.native.fbo != 0U) {
        db_gl_delete_framebuffers(1, &g_state.native.fbo);
    }
    db_gl_gen_framebuffers(1, &g_state.native.fbo);
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.native.fbo);
    db_gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D,
                                 g_state.presentation.shadow.texture, 0);
    if (db_gl_check_framebuffer_status(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, 0U);
        db_gl_delete_framebuffers(1, &g_state.native.fbo);
        g_state.native.fbo = 0U;
        return 0;
    }
    g_state.native.width = width;
    g_state.native.height = height;
    g_state.native.generation =
        db_checked_add_u32(BACKEND_NAME, "native_target_generation",
                           g_state.native.generation, 1U);
    g_state.native.valid = 0;
    g_state.native.strategy = GL1_STRATEGY_PERSISTENT_FBO;
    return 1;
}

int db_gl1_native_render(const db_frame_plan_t *plan, uint32_t logical_width,
                         const db_renderer_target_t *target,
                         uint32_t logical_height, int presentation_fbo,
                         int viewport_width, int viewport_height) {
    const uint32_t planned_gradient_commands =
        (plan != NULL) ? plan->update_metadata.gradient_count +
                             plan->rebuild_metadata.gradient_count
                       : 0U;
    if ((plan == NULL) || (target == NULL) || (target->valid == 0) ||
        (logical_width == 0U) || (logical_height == 0U) ||
        (viewport_width <= 0) || (viewport_height <= 0) ||
        ((target->strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) &&
         (plan->external_bindings.count > 0U)) ||
        (target->strategy == DB_RENDER_TARGET_GL1_CPU_UPLOAD) ||
        ((target->strategy != DB_RENDER_TARGET_GL1_DIRECT_WINDOW) &&
         (ensure_fbo(logical_width, logical_height) == 0))) {
        return 0;
    }
    if (planned_gradient_commands > 0U) {
        if ((g_state.native.applied.generation == 0U) &&
            (g_state.native.applied.diagnostic_forced == 0)) {
            return 0;
        }
    }
    if (target->strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) {
        if ((g_state.replay.available == 0) ||
            ((uint32_t)viewport_width != logical_width) ||
            ((uint32_t)viewport_height != logical_height) ||
            (g_state.backing.texture_format !=
             DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8)) {
            return 0;
        }
        if ((g_state.native.strategy != GL1_STRATEGY_DIRECT_WINDOW) ||
            (g_state.native.width != logical_width) ||
            (g_state.native.height != logical_height) ||
            (g_state.replay.target_generation != target->target_generation)) {
            g_state.replay.target_generation = target->target_generation;
        }
        db_render_ir_view_t replay_views[DB_REPLAY_CAPACITY_MAX] = {};
        int use_rebuild = 0;
        const size_t replay_count = db_gl1_replay_collect(
            plan, replay_views, DB_REPLAY_CAPACITY_MAX, &use_rebuild);
        if ((use_rebuild != 0) && (plan->rebuild_required == 0)) {
            return 0;
        }
        const db_render_ir_view_t *streams[GL1_NATIVE_MAX_IR_STREAMS] = {0};
        size_t stream_count = 0U;
        if (use_rebuild != 0) {
            streams[stream_count++] = &plan->rebuild_ir;
        } else {
            for (size_t index = 0U; index < replay_count; index++) {
                streams[stream_count++] = &replay_views[index];
            }
        }
        streams[stream_count++] = &plan->update_ir;
        db_gl_bind_framebuffer(GL_FRAMEBUFFER,
                               db_checked_int_to_u32(BACKEND_NAME,
                                                     "presentation_fbo",
                                                     presentation_fbo));
        db_gl_set_dither_enabled(0);
        db_gl_set_framebuffer_srgb_enabled(0);
        db_gl_set_blend_enabled(0);
        db_gl_set_depth_test_enabled(0);
        db_gl_set_cull_face_enabled(0);
        db_gl_set_viewport_px(viewport_width, viewport_height);
        if (draw_ir_sequence(streams, stream_count) == 0) {
            return 0;
        }
        g_state.native.strategy = GL1_STRATEGY_DIRECT_WINDOW;
        g_state.native.direct_fbo = db_checked_int_to_u32(
            BACKEND_NAME, "presentation_fbo", presentation_fbo);
        g_state.native.width = logical_width;
        g_state.native.height = logical_height;
        g_state.native.valid = 1;
        const uint32_t gradient_commands =
            plan->update_metadata.gradient_count +
            ((plan->rebuild_required != 0)
                 ? plan->rebuild_metadata.gradient_count
                 : 0U);
        g_state.telemetry.execution = (db_render_execution_report_t){
            .target_strategy = DB_RENDER_TARGET_GL1_DIRECT_WINDOW,
            .solid_path = DB_RENDER_OPERATION_GL1_FIXED_FUNCTION,
            .gradient_path = gradient_operation_path(gradient_commands),
            .solid_commands = plan->update_metadata.solid_command_count +
                              ((plan->rebuild_required != 0)
                                   ? plan->rebuild_metadata.solid_command_count
                                   : 0U),
            .gradient_commands = gradient_commands,
            .solid_draws = 1U,
            .gradient_draws = DB_BOOL(gradient_commands > 0U),
            .fallback_instances =
                plan->update_metadata.exact_fallback_instance_count,
            .gradient_implementation = g_state.native.applied.implementation,
            .qualification_source = g_state.native.applied.source,
            .cache_status = g_state.native.applied.cache_status,
            .qualification_lane_count = g_state.native.applied.lane_count,
            .qualification_reason = g_state.native.applied.reason,
            .strategy_reason =
                db_renderer_strategy_reason_name(target->strategy_reason),
            .strategy_generation = target->strategy_generation,
            .qualification_generation = target->qualification_generation,
            .target_generation = target->target_generation,
            .qualified =
                DB_BOOL((g_state.native.applied.production_qualified != 0) &&
                        ((gradient_commands == 0U) ||
                         (g_state.native.applied.generation != 0U))),
            .replay_stream_count = db_checked_size_to_u32(
                BACKEND_NAME, "replay_stream_count", replay_count),
            .diagnostic_forced = g_state.native.applied.diagnostic_forced,
        };
        if (db_gl1_replay_prepare(plan, logical_width, logical_height,
                                  g_state.backing.format.surface_pixel_format,
                                  target->target_generation,
                                  use_rebuild) == 0) {
            return 0;
        }
        trace_native_plan(plan);
        return 1;
    }
    const int rebuild =
        DB_BOOL((g_state.native.valid == 0) || (plan->rebuild_required != 0));
    const int restored =
        DB_BOOL((rebuild != 0) && (plan->external_bindings.count > 0U) &&
                (restore_seed_texture(plan) != 0));
    db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.native.fbo);
    db_gl_set_viewport_px(
        db_checked_u32_to_i32(BACKEND_NAME, "logical_width", logical_width),
        db_checked_u32_to_i32(BACKEND_NAME, "logical_height", logical_height));
    const db_render_ir_view_t *const rebuild_ir =
        ((rebuild != 0) && (restored == 0)) ? &plan->rebuild_ir : NULL;
    const db_render_ir_view_t *const streams[] = {rebuild_ir, &plan->update_ir};
    if (draw_ir_sequence(streams, 2U) == 0) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER,
                               db_checked_int_to_u32(BACKEND_NAME,
                                                     "presentation_fbo",
                                                     presentation_fbo));
        return 0;
    }
    if ((planned_gradient_commands > 0U) && (rebuild != 0) &&
        (g_state.native.applied.diagnostic_forced != 0) &&
        (g_state.native.applied.implementation ==
         DB_GRADIENT_IMPLEMENTATION_SEMANTIC)) {
        (void)qualify_row_fill(plan);
    }
    g_state.native.valid = 1;
    g_state.presentation.shadow.backing_valid = 1;
    g_state.presentation.shadow.texture_valid = 1;
    g_state.presentation.shadow.texture_needs_full_upload = 0;
    db_gl_bind_framebuffer(
        GL_FRAMEBUFFER, db_checked_int_to_u32(BACKEND_NAME, "presentation_fbo",
                                              presentation_fbo));
    db_gl_set_viewport_px(viewport_width, viewport_height);
    if ((uint32_t)viewport_width != logical_width ||
        (uint32_t)viewport_height != logical_height) {
        db_gl_shadow_present_set_draw_damage(&g_state.presentation.shadow,
                                             (db_pixel_block_view_t){0}, 1);
    }
    db_gl_shadow_present_draw_framebuffer_texture(
        &g_state.presentation.shadow, logical_width, logical_height);
    const uint32_t gradient_commands =
        plan->update_metadata.gradient_count +
        ((rebuild != 0) ? plan->rebuild_metadata.gradient_count : 0U);
    g_state.telemetry.execution = (db_render_execution_report_t){
        .target_strategy = DB_RENDER_TARGET_GL1_PERSISTENT_FBO,
        .solid_path = DB_RENDER_OPERATION_GL1_FIXED_FUNCTION,
        .gradient_path = gradient_operation_path(gradient_commands),
        .solid_commands =
            plan->update_metadata.solid_command_count +
            ((rebuild != 0) ? plan->rebuild_metadata.solid_command_count : 0U),
        .gradient_commands = gradient_commands,
        .solid_draws = 1U,
        .gradient_draws = DB_BOOL(gradient_commands > 0U),
        .fallback_instances =
            plan->update_metadata.exact_fallback_instance_count,
        .gradient_implementation = g_state.native.applied.implementation,
        .qualification_source = g_state.native.applied.source,
        .cache_status = g_state.native.applied.cache_status,
        .qualification_lane_count = g_state.native.applied.lane_count,
        .qualification_reason = g_state.native.applied.reason,
        .strategy_reason =
            db_renderer_strategy_reason_name(target->strategy_reason),
        .strategy_generation = target->strategy_generation,
        .qualification_generation = target->qualification_generation,
        .target_generation = target->target_generation,
        .qualified =
            DB_BOOL((g_state.native.applied.production_qualified != 0) &&
                    ((gradient_commands == 0U) ||
                     (g_state.native.applied.generation != 0U))),
        .diagnostic_forced = g_state.native.applied.diagnostic_forced,
    };
    db_gl1_replay_prepare_boundary();
    trace_native_plan(plan);
    return 1;
}

void db_gl1_native_shutdown(void) {
    db_gl_upload_stream_shutdown(&g_state.native.vertex_stream);
    db_gl_hash_scratch_release(&g_state.native.hash_scratch);
    if (g_state.native.fbo != 0U) {
        db_gl_delete_framebuffers(1, &g_state.native.fbo);
    }
    free(g_state.native.vertices);
    g_state.native = (gl1_native_target_t){0};
}
