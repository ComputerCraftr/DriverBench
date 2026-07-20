#include "gl3_execute.h"

#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_raster_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "gl3_exact_lookup.h"
#include "renderers/gl_common.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define ATTR_COLOR_LOC 2U
#define ATTR_END_COLOR_LOC 3U
#define ATTR_GRADIENT_LOC 4U
#define ATTR_LOOKUP_LOC 5U
#define ATTR_RECT_LOC 1U
#define ATTR_POSITION_LOC 0U
#define GL3_INSTANCE_START_COLOR_OFFSET 4U
#define GL3_INSTANCE_END_COLOR_OFFSET 7U
#define GL3_INSTANCE_MODE_OFFSET 10U
#define GL3_INSTANCE_AXIS_START_OFFSET 11U
#define GL3_INSTANCE_AXIS_END_OFFSET 12U
#define GL3_INSTANCE_GRID_ROWS_OFFSET 13U
#define GL3_INSTANCE_LOOKUP_BASE_OFFSET 14U
#define GL3_INSTANCE_LOOKUP_ORIGIN_OFFSET 15U
#define GL3_UNIT_QUAD_VERTEX_COUNT 6U

int db_gl3_geometry_storage_layout(size_t instance_capacity,
                                   size_t *float_count, size_t *byte_count) {
    size_t instance_floats = 0U;
    size_t total_floats = 0U;
    size_t total_bytes = 0U;
    if ((float_count == NULL) || (byte_count == NULL) ||
        (instance_capacity == 0U) ||
        (db_try_mul_size(instance_capacity, DB_GL3_INSTANCE_FLOAT_COUNT,
                         &instance_floats) == 0) ||
        (db_try_add_size(DB_GL3_UNIT_QUAD_FLOAT_COUNT, instance_floats,
                         &total_floats) == 0) ||
        (db_try_mul_size(total_floats, sizeof(float), &total_bytes) == 0)) {
        return 0;
    }
    *float_count = total_floats;
    *byte_count = total_bytes;
    return 1;
}

static void gl3_quantize_instance_rgb(const db_gl3_execute_context_t *context,
                                      const double *rgba, float *rgb) {
    if (context->target->format == DB_PIXEL_FORMAT_RGBA16F) {
        db_rgb_f64_quantize_f16_to_f32_rgb3(rgba, rgb);
        return;
    }
    uint8_t bytes[4] = {0};
    db_rgba01_to_u8_rgba4(rgba, bytes);
    for (size_t channel = 0U; channel < 3U; channel++) {
        rgb[channel] = db_double_to_f32(db_u8_to_unit_f64(bytes[channel]));
    }
}

void db_gl3_execute_bind_layout(const db_gl3_execute_context_t *context) {
    if ((context == NULL) || (context->geometry == NULL)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "missing GL3 execution context");
    }
    (void)db_gl_upload_stream_bind(&context->geometry->stream);
    const int32_t unit_stride = db_checked_size_to_i32(
        BACKEND_NAME, "unit_vertex_stride", 2U * sizeof(float));
    const int32_t instance_stride =
        db_checked_size_to_i32(BACKEND_NAME, "instance_stride",
                               DB_GL3_INSTANCE_FLOAT_COUNT * sizeof(float));
    const size_t instance_base = DB_GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float);
    db_gl_enable_vertex_attrib_array(ATTR_POSITION_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_RECT_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_COLOR_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_END_COLOR_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_GRADIENT_LOC);
    db_gl_enable_vertex_attrib_array(ATTR_LOOKUP_LOC);
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
    if (db_gl_vertex_attrib_pointer_2ui(
            ATTR_LOOKUP_LOC, instance_stride,
            instance_base +
                (GL3_INSTANCE_LOOKUP_BASE_OFFSET * sizeof(float))) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "GL3 integer instance attributes are unavailable");
    }
    if ((db_gl_vertex_attrib_divisor(ATTR_POSITION_LOC, 0U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_RECT_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_COLOR_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_END_COLOR_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_GRADIENT_LOC, 1U) == 0) ||
        (db_gl_vertex_attrib_divisor(ATTR_LOOKUP_LOC, 1U) == 0)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "GL3 instancing is unavailable");
    }
}

static int gl3_append_instance(const db_gl3_execute_context_t *context,
                               db_render_ir_rect_t rect,
                               db_render_ir_color_t start_color,
                               db_render_ir_color_t end_color, float mode,
                               int32_t axis_start, int32_t axis_end,
                               uint32_t lookup_base, int32_t lookup_origin,
                               size_t index) {
    const size_t capacity = context->geometry->instance_capacity;
    if (index >= capacity) {
        return 0;
    }
    db_grid_block_t grid_block = {0};
    if (db_render_ir_rect_to_grid_block(rect, context->runtime->grid_cols,
                                        context->runtime->grid_rows,
                                        &grid_block) == 0) {
        return 0;
    }
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    db_grid_block_bounds_ndc_for_extent(context->runtime->grid_cols,
                                        context->runtime->grid_rows,
                                        &grid_block, &x0, &y0, &x1, &y1);
    float start[3] = {0.0F, 0.0F, 0.0F};
    float end[3] = {0.0F, 0.0F, 0.0F};
    gl3_quantize_instance_rgb(context, start_color.rgba, start);
    gl3_quantize_instance_rgb(context, end_color.rgba, end);
    float *const instance =
        &context->geometry->vertex
             .vertices[DB_GL3_UNIT_QUAD_FLOAT_COUNT +
                       (index * DB_GL3_INSTANCE_FLOAT_COUNT)];
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
        db_u32_to_f32(context->runtime->grid_rows);
    const uint32_t lookup_origin_u32 =
        db_checked_i32_to_u32(BACKEND_NAME, "lookup_origin", lookup_origin);
    memcpy(&instance[GL3_INSTANCE_LOOKUP_BASE_OFFSET], &lookup_base,
           sizeof(lookup_base));
    memcpy(&instance[GL3_INSTANCE_LOOKUP_ORIGIN_OFFSET], &lookup_origin_u32,
           sizeof(lookup_origin_u32));
    return 1;
}

static size_t gl3_append_ir_instances(const db_gl3_execute_context_t *context,
                                      const db_render_ir_view_t *ir,
                                      size_t count,
                                      db_render_operation_path_t gradient_path,
                                      uint32_t *semantic_gradients,
                                      uint32_t *exact_gradients,
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
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            const db_render_ir_rect_t rect = {
                .width = db_checked_u32_to_i32(BACKEND_NAME, "clear_width",
                                               context->runtime->grid_cols),
                .height = db_checked_u32_to_i32(BACKEND_NAME, "clear_height",
                                                context->runtime->grid_rows),
            };
            if (gl3_append_instance(context, rect, clear->color, clear->color,
                                    0.0F, 0, 0, 0U, 0, count++) == 0) {
                return SIZE_MAX;
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    ir->fills[fills->first_fill + index];
                if (gl3_append_instance(context, fill.rect, fill.color,
                                        fill.color, 0.0F, 0, 0, 0U, 0,
                                        count++) == 0) {
                    return SIZE_MAX;
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            db_render_ir_color_t start = gradient->start_color;
            db_render_ir_color_t end = gradient->end_color;
            if (gradient->reverse_stops != 0U) {
                const db_render_ir_color_t swap = start;
                start = end;
                end = swap;
            }
            if ((command->clip_region == DB_RENDER_IR_INVALID_ID) &&
                (gradient_path == DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT)) {
                if (gl3_append_instance(context, gradient->bounds, start, end,
                                        1.0F, gradient->axis_start,
                                        gradient->axis_end, 0U, 0,
                                        count++) == 0) {
                    return SIZE_MAX;
                }
                (*semantic_gradients)++;
                break;
            }
            if ((command->clip_region == DB_RENDER_IR_INVALID_ID) &&
                (gradient_path == DB_RENDER_OPERATION_GL3_EXACT_LOOKUP)) {
                uint32_t lookup_base = 0U;
                if ((db_gl3_exact_lookup_append(context->exact_lookup, gradient,
                                                &lookup_base) == 0) ||
                    (gl3_append_instance(context, gradient->bounds, start, end,
                                         2.0F, gradient->axis_start,
                                         gradient->axis_end, lookup_base,
                                         gradient->bounds.y, count++) == 0)) {
                    return SIZE_MAX;
                }
                (*exact_gradients)++;
                break;
            }
            db_render_ir_rect_iterator_t rows = {0};
            db_render_ir_rect_iterator_begin_command(&rows, ir, command);
            db_render_ir_fill_t fill = {0};
            while (db_render_ir_rect_iterator_next(&rows, &fill) != 0) {
                if (gl3_append_instance(context, fill.rect, fill.color,
                                        fill.color, 0.0F, 0, 0, 0U, 0,
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

uint32_t db_gl3_execute_ir(const db_gl3_execute_context_t *context,
                           const db_render_ir_view_t *first_ir,
                           const db_render_ir_view_t *second_ir,
                           db_render_operation_path_t gradient_path,
                           uint32_t *semantic_gradients,
                           uint32_t *exact_gradients,
                           size_t *lookup_upload_bytes,
                           uint32_t *fallback_instances) {
    if ((context == NULL) || (context->runtime == NULL) ||
        (context->target == NULL) || (context->geometry == NULL) ||
        (context->exact_lookup == NULL) || (semantic_gradients == NULL) ||
        (exact_gradients == NULL) || (lookup_upload_bytes == NULL) ||
        (fallback_instances == NULL)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "invalid GL3 execution input");
    }
    db_gl3_exact_lookup_reset(context->exact_lookup);
    size_t fill_count = gl3_append_ir_instances(
        context, first_ir, 0U, gradient_path, semantic_gradients,
        exact_gradients, fallback_instances);
    if (fill_count != SIZE_MAX) {
        fill_count = gl3_append_ir_instances(
            context, second_ir, fill_count, gradient_path, semantic_gradients,
            exact_gradients, fallback_instances);
    }
    if (fill_count == SIZE_MAX) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 IR capacity exceeded");
    }
    if (fill_count == 0U) {
        return 0U;
    }
    if ((*exact_gradients > 0U) &&
        (db_gl3_exact_lookup_upload(context->exact_lookup,
                                    lookup_upload_bytes) == 0)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 lookup upload failed");
    }
    if (db_gl_upload_stream_wait(&context->geometry->stream) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "canonical GL3 geometry stream reuse timed out");
    }
    const size_t float_count =
        db_checked_mul_size(BACKEND_NAME, "canonical_instance_float_count",
                            fill_count, DB_GL3_INSTANCE_FLOAT_COUNT);
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "canonical_instance_bytes", float_count, sizeof(float));
    const size_t instance_offset = DB_GL3_UNIT_QUAD_FLOAT_COUNT * sizeof(float);
    if (db_gl_upload_stream_write(
            &context->geometry->stream, BACKEND_NAME,
            &context->geometry->vertex.vertices[DB_GL3_UNIT_QUAD_FLOAT_COUNT],
            context->geometry->buffers.vbo_bytes, instance_offset,
            byte_count) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 geometry upload failed");
    }
    db_gl3_execute_bind_layout(context);
    db_gl_set_scissor_enabled(0);
    if (*exact_gradients > 0U) {
        db_gl3_exact_lookup_bind(context->exact_lookup);
    } else {
        db_gl_use_program(context->draw_program);
    }
    const uint32_t instance_count = db_checked_size_to_u32(
        BACKEND_NAME, "canonical_instance_count", fill_count);
    if (db_gl_draw_arrays_triangles_instanced(0U, GL3_UNIT_QUAD_VERTEX_COUNT,
                                              instance_count) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "canonical GL3 instanced draw failed");
    }
    db_gl_upload_stream_record_sync(&context->geometry->stream);
    return instance_count;
}
