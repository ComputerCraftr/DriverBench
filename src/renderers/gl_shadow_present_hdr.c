#include "gl_shadow_present_internal.h"

#include "core/db_buffer_convert.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static uint32_t *
db_gl_shadow_present_hdr_scratch_or_fail(db_gl_shadow_present_state_t *state,
                                         const char *backend,
                                         size_t required_pixels) {
    if (required_pixels <= state->encoded_upload_scratch_capacity) {
        return state->encoded_upload_scratch;
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
        uint32_t *const encoded = db_gl_shadow_present_hdr_scratch_or_fail(
            state, backend, pixel_count);
        db_gl_shadow_present_encode_hdr_block(encoded, source, &block);

        db_gl_upload_stream_t *const stream =
            db_gl_shadow_present_acquire_unpack_stream(state, backend,
                                                       byte_count);
        const int stream_ready = DB_BOOL(
            (stream != NULL) &&
            (db_gl_stream_upload_uses_buffer_object(&stream->capability) !=
             0) &&
            (db_gl_upload_stream_write(stream, backend, encoded, byte_count, 0U,
                                       byte_count) != 0) &&
            (db_gl_upload_stream_bind(stream) != 0));
        db_gl_shadow_upload_trace_capture_upload_span(
            &state->upload_trace, &block, 0U, byte_count,
            (stream_ready != 0) ? "hdr_encoded_pbo" : "hdr_encoded_client");
        if (stream_ready != 0) {
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
