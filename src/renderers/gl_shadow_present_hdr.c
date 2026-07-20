#include "gl_shadow_present_internal.h"

#include "core/db_buffer_convert.h"
#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { DB_HDR_ALPHA_SHIFT = 30U };

void db_gl_shadow_present_prepare_hdr_upload_workspace(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (backend == NULL)) {
        return;
    }
    const size_t required_pixels = db_checked_mul_size(
        backend, "hdr_encoded_upload_pixels", pixel_width, pixel_height);
    if (required_pixels <= state->encoded_upload_scratch_capacity) {
        return;
    }
    const size_t required_bytes = db_checked_mul_size(
        backend, "hdr_encoded_upload_bytes", required_pixels,
        sizeof(*state->encoded_upload_scratch));
    void *const resized =
        realloc(state->encoded_upload_scratch, required_bytes);
    if (resized == NULL) {
        DB_RUNTIME_FAIL(backend,
                        "failed to allocate transient HDR upload workspace");
    }
    state->encoded_upload_scratch = (uint32_t *)resized;
    state->encoded_upload_scratch_capacity = required_pixels;
}

static uint32_t *
db_gl_shadow_present_hdr_scratch_or_fail(db_gl_shadow_present_state_t *state,
                                         const char *backend,
                                         size_t required_pixels) {
    if ((required_pixels > state->encoded_upload_scratch_capacity) ||
        (state->encoded_upload_scratch == NULL)) {
        DB_RUNTIME_FAIL(backend, "HDR upload workspace was not provisioned");
    }
    return state->encoded_upload_scratch;
}

static void
db_gl_shadow_present_encode_hdr_block(uint32_t *dst,
                                      const db_pixel_surface_t *source,
                                      const db_damage_block_t *block) {
    if (source->format == DB_PIXEL_FORMAT_RGBA16F) {
        db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(
            dst, (const uint16_t *)source->pixels, source->pixel_width,
            block->row_start, block->row_count, block->col_start,
            block->col_count);
        return;
    }
    db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(
        dst, (const uint32_t *)source->pixels, source->pixel_width,
        block->row_start, block->row_count, block->col_start, block->col_count);
}

static int hdr_ir_command_direct(const db_render_ir_command_header_t *command) {
    if (command == NULL) {
        return 0;
    }
    switch ((db_render_ir_opcode_t)command->opcode) {
    case DB_RENDER_IR_OP_BEGIN_TARGET:
    case DB_RENDER_IR_OP_END_TARGET:
        return 1;
    case DB_RENDER_IR_OP_CLEAR:
    case DB_RENDER_IR_OP_FILL_RECTS:
    case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
        return DB_BOOL(
            (command->composite == DB_RENDER_IR_COMPOSITE_SOURCE) &&
            ((command->flags & DB_RENDER_IR_COMMAND_OPAQUE_SOURCE) != 0U) &&
            (command->clip_region == DB_RENDER_IR_INVALID_ID));
    case DB_RENDER_IR_OP_UPLOAD_IMAGE:
    case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
        return 0;
    }
    return 0;
}

int db_gl_shadow_present_hdr_ir_direct_eligible(const db_frame_plan_t *plan) {
    if ((plan == NULL) || (plan->rebuild_required != 0) ||
        (plan->external_bindings.count != 0U)) {
        return 0;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &plan->update_ir);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (hdr_ir_command_direct(command) == 0) {
            return 0;
        }
    }
    return 1;
}

static uint32_t hdr_pack_color(db_render_ir_color_t color) {
    return db_pack_xrgb2101010_from_linear_srgb(color.rgba) |
           (UINT32_C(3) << DB_HDR_ALPHA_SHIFT);
}

static int hdr_rect_to_pixel_block(const db_frame_plan_t *plan,
                                   db_render_ir_rect_t rect,
                                   db_damage_block_t *block) {
    db_grid_block_t grid = {0};
    return DB_BOOL(db_render_ir_rect_to_grid_block(
                       rect, plan->grid_cols, plan->grid_rows, &grid) != 0 &&
                   db_grid_block_to_pixel_block(
                       plan->grid_cols, plan->grid_rows, &grid,
                       plan->pixel_width, plan->pixel_height, block) != 0);
}

static int hdr_upload_encoded_block(db_gl_shadow_present_state_t *state,
                                    const char *backend,
                                    const db_damage_block_t *block,
                                    const uint32_t *encoded) {
    const size_t pixel_count = db_checked_mul_size(
        backend, "hdr_semantic_pixels", block->row_count, block->col_count);
    const size_t byte_count = db_checked_mul_size(
        backend, "hdr_semantic_bytes", pixel_count, sizeof(uint32_t));
    db_gl_upload_stream_t *const stream =
        db_gl_shadow_present_acquire_unpack_stream(state, backend, byte_count);
    const int use_pbo = DB_BOOL(
        (stream != NULL) &&
        (db_gl_stream_upload_uses_buffer_object(&stream->capability) != 0) &&
        (db_gl_upload_stream_write(stream, backend, encoded, byte_count, 0U,
                                   byte_count) != 0) &&
        (db_gl_upload_stream_bind(stream) != 0));
    db_gl_shadow_upload_trace_capture_upload_span(
        &state->upload_trace, block, 0U, byte_count,
        (use_pbo != 0) ? "hdr_semantic_pbo" : "hdr_semantic_client");
    db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(
        block->col_start, block->row_start, block->col_count, block->row_count,
        (use_pbo != 0) ? (const uint32_t *)db_gl_vbo_offset_ptr(0U) : encoded);
    if (use_pbo != 0) {
        db_gl_upload_stream_record_sync(stream);
        state->unpack_write_index = (state->unpack_write_index + 1U) %
                                    DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT;
    }
    db_gl_upload_state_reset_unpack();
    state->last_encoded_span_bytes =
        db_checked_add_u64(backend, "hdr_encoded_span_bytes",
                           state->last_encoded_span_bytes, byte_count);
    return 1;
}

static int hdr_upload_solid(db_gl_shadow_present_state_t *state,
                            const char *backend, const db_frame_plan_t *plan,
                            db_render_ir_rect_t rect,
                            db_render_ir_color_t color) {
    db_damage_block_t block = {0};
    if (hdr_rect_to_pixel_block(plan, rect, &block) == 0) {
        return 0;
    }
    const size_t pixel_count = db_checked_mul_size(
        backend, "hdr_semantic_pixels", block.row_count, block.col_count);
    uint32_t *const encoded =
        db_gl_shadow_present_hdr_scratch_or_fail(state, backend, pixel_count);
    const uint32_t packed = hdr_pack_color(color);
    for (size_t index = 0U; index < pixel_count; index++) {
        encoded[index] = packed;
    }
    return hdr_upload_encoded_block(state, backend, &block, encoded);
}

static int
hdr_upload_gradient(db_gl_shadow_present_state_t *state, const char *backend,
                    const db_frame_plan_t *plan,
                    const db_render_ir_linear_gradient_command_t *gradient) {
    db_damage_block_t block = {0};
    if (hdr_rect_to_pixel_block(plan, gradient->bounds, &block) == 0) {
        return 0;
    }
    const size_t pixel_count = db_checked_mul_size(
        backend, "hdr_gradient_pixels", block.row_count, block.col_count);
    uint32_t *const encoded =
        db_gl_shadow_present_hdr_scratch_or_fail(state, backend, pixel_count);
    const int64_t logical_row_end =
        (int64_t)gradient->bounds.y + gradient->bounds.height;
    for (int32_t logical_row = gradient->bounds.y;
         (int64_t)logical_row < logical_row_end; logical_row++) {
        db_damage_block_t row_block = {0};
        if (hdr_rect_to_pixel_block(
                plan,
                (db_render_ir_rect_t){.x = gradient->bounds.x,
                                      .y = logical_row,
                                      .width = gradient->bounds.width,
                                      .height = 1},
                &row_block) == 0) {
            return 0;
        }
        const uint32_t packed = hdr_pack_color(
            db_render_ir_linear_gradient_color_at(gradient, logical_row));
        const uint32_t relative_row = row_block.row_start - block.row_start;
        for (uint32_t pixel_row = 0U; pixel_row < row_block.row_count;
             pixel_row++) {
            const size_t row_offset = db_checked_mul_size(
                backend, "hdr_gradient_row_offset",
                (size_t)relative_row + (size_t)pixel_row, block.col_count);
            for (uint32_t col = 0U; col < block.col_count; col++) {
                encoded[row_offset + col] = packed;
            }
        }
    }
    return hdr_upload_encoded_block(state, backend, &block, encoded);
}

int db_gl_shadow_present_upload_hdr_ir(db_gl_shadow_present_state_t *state,
                                       const char *backend,
                                       const db_pixel_surface_t *source,
                                       const db_frame_plan_t *plan) {
    if ((state == NULL) || (backend == NULL) || (source == NULL) ||
        (source->pixels == NULL) ||
        (db_gl_shadow_present_hdr_ir_direct_eligible(plan) == 0)) {
        return 0;
    }
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, &plan->update_ir);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
            break;
        case DB_RENDER_IR_OP_CLEAR: {
            const db_render_ir_clear_command_t *const clear =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            if (hdr_upload_solid(
                    state, backend, plan,
                    (db_render_ir_rect_t){
                        .width = db_checked_u32_to_i32(
                            backend, "hdr_clear_width", plan->grid_cols),
                        .height = db_checked_u32_to_i32(
                            backend, "hdr_clear_height", plan->grid_rows),
                    },
                    clear->color) == 0) {
                return 0;
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    plan->update_ir.fills[fills->first_fill + index];
                if (hdr_upload_solid(state, backend, plan, fill.rect,
                                     fill.color) == 0) {
                    return 0;
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            if (hdr_upload_gradient(state, backend, plan, gradient) == 0) {
                return 0;
            }
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            return 0;
        }
    }
    db_gl_shadow_upload_trace_note_execution(&state->upload_trace,
                                             "hdr_semantic_spans");
    return 1;
}

void db_gl_shadow_present_upload_hdr_damage_blocks(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_pixel_surface_t *source, const db_damage_block_t *blocks,
    size_t block_count) {
    if ((state == NULL) || (backend == NULL) || (source == NULL) ||
        (source->pixels == NULL) || (blocks == NULL)) {
        return;
    }
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    int used_pbo = 0;
    int uploaded = 0;
    for (size_t index = 0U; index < block_count; index++) {
        db_damage_block_t block = blocks[index];
        if ((block.row_start >= source->pixel_height) ||
            (block.col_start >= source->pixel_width)) {
            continue;
        }
        block.row_count =
            DB_MIN(block.row_count, source->pixel_height - block.row_start);
        block.col_count =
            DB_MIN(block.col_count, source->pixel_width - block.col_start);
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        const size_t pixel_count = db_checked_mul_size(
            backend, "hdr_encoded_upload_pixels", (size_t)block.row_count,
            (size_t)block.col_count);
        const size_t byte_count = db_checked_mul_size(
            backend, "hdr_encoded_upload_bytes", pixel_count, sizeof(uint32_t));
        db_gl_upload_stream_t *const stream =
            db_gl_shadow_present_acquire_unpack_stream(state, backend,
                                                       byte_count);
        uint32_t *encoded = NULL;
        int stream_ready = 0;
        int stream_uses_buffer = 0;
        if (stream != NULL) {
            stream_uses_buffer =
                db_gl_stream_upload_uses_buffer_object(&stream->capability);
            uint8_t *const mapped = db_gl_upload_stream_begin_write(
                stream, backend, 0U, byte_count);
            if ((mapped != NULL) &&
                (db_pointer_is_aligned(mapped, _Alignof(uint32_t)) != 0)) {
                encoded = DB_ASSUME_ALIGNED(mapped, _Alignof(uint32_t));
                db_gl_shadow_present_encode_hdr_block(encoded, source, &block);
                stream_ready = db_gl_upload_stream_end_write(stream, backend);
                if ((stream_ready != 0) && (stream_uses_buffer != 0)) {
                    stream_ready = db_gl_upload_stream_bind(stream);
                }
            } else if (mapped != NULL) {
                (void)db_gl_upload_stream_end_write(stream, backend);
            }
        }
        if (stream_ready == 0) {
            encoded = db_gl_shadow_present_hdr_scratch_or_fail(state, backend,
                                                               pixel_count);
            db_gl_shadow_present_encode_hdr_block(encoded, source, &block);
            stream_uses_buffer = (stream != NULL)
                                     ? db_gl_stream_upload_uses_buffer_object(
                                           &stream->capability)
                                     : 0;
            stream_ready = DB_BOOL(
                (stream != NULL) && (stream_uses_buffer != 0) &&
                (db_gl_upload_stream_write(stream, backend, encoded, byte_count,
                                           0U, byte_count) != 0) &&
                (db_gl_upload_stream_bind(stream) != 0));
        }
        db_gl_shadow_upload_trace_capture_upload_span(
            &state->upload_trace, &block, 0U, byte_count,
            ((stream_ready != 0) && (stream_uses_buffer != 0))
                ? "hdr_encoded_pbo"
                : "hdr_encoded_client");
        if ((stream_ready != 0) && (stream_uses_buffer != 0)) {
            db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(
                block.col_start, block.row_start, block.col_count,
                block.row_count, (const uint32_t *)db_gl_vbo_offset_ptr(0U));
            db_gl_upload_stream_record_sync(stream);
            state->unpack_write_index =
                (state->unpack_write_index + 1U) %
                DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT;
            used_pbo = 1;
        } else {
            db_gl_upload_state_reset_unpack();
            db_gl_set_unpack_alignment_1();
            db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(
                block.col_start, block.row_start, block.col_count,
                block.row_count, encoded);
        }
        uploaded = 1;
    }
    db_gl_upload_state_reset_unpack();
    if (uploaded != 0) {
        db_gl_shadow_upload_trace_note_execution(
            &state->upload_trace,
            (used_pbo != 0) ? "hdr_pq_pbo_upload" : "hdr_pq_client_upload");
    }
}
