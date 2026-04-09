#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_types.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include "renderer_gl_shadow_present_internal.h"
#include <stddef.h>
#include <stdint.h>

static void db_gl_shadow_present_copy_block_bytes(
    uint8_t *dst_bytes, const uint8_t *src_bytes, uint32_t pixel_width,
    uint32_t pixel_bytes, const db_damage_block_t *block) {
    if ((dst_bytes == NULL) || (src_bytes == NULL) || (block == NULL) ||
        (block->row_count == 0U) || (block->col_count == 0U)) {
        return;
    }
    const size_t row_bytes = db_checked_mul_size(
        "renderer_gl_shadow_present", "surface_copy_row_bytes",
        (size_t)block->col_count, (size_t)pixel_bytes);
    const size_t full_row_stride = db_checked_mul_size(
        "renderer_gl_shadow_present", "surface_copy_full_row_stride",
        (size_t)pixel_width, (size_t)pixel_bytes);
    for (uint32_t row = 0U; row < block->row_count; row++) {
        const size_t row_index = (size_t)block->row_start + (size_t)row;
        const size_t offset =
            db_checked_mul_size("renderer_gl_shadow_present",
                                "surface_copy_row_offset", row_index,
                                full_row_stride) +
            db_checked_mul_size("renderer_gl_shadow_present",
                                "surface_copy_col_offset",
                                (size_t)block->col_start, (size_t)pixel_bytes);
        db_copy_bytes(dst_bytes + offset, src_bytes + offset, row_bytes);
    }
}

static void
db_gl_shadow_present_copy_surface_block(const db_benchmark_pixel_surface_t *dst,
                                        const db_benchmark_pixel_surface_t *src,
                                        const db_damage_block_t *block) {
    if ((dst == NULL) || (src == NULL) || (block == NULL) ||
        (dst->pixel_width != src->pixel_width) ||
        (dst->pixel_height != src->pixel_height) ||
        (dst->uses_rgba16f != src->uses_rgba16f)) {
        return;
    }
    const uint32_t pixel_bytes =
        (dst->uses_rgba16f != 0) ? (uint32_t)(sizeof(uint16_t) * 4U) : 4U;
    uint8_t *dst_bytes =
        (uint8_t *)((dst->uses_rgba16f != 0) ? (void *)dst->pixels_rgba16f
                                             : (void *)dst->pixels_rgba8);
    const uint8_t *src_bytes =
        (const uint8_t *)((src->uses_rgba16f != 0)
                              ? (const void *)src->pixels_rgba16f
                              : (const void *)src->pixels_rgba8);
    db_gl_shadow_present_copy_block_bytes(dst_bytes, src_bytes,
                                          dst->pixel_width, pixel_bytes, block);
}

static void db_gl_shadow_present_copy_pixels_block(
    const db_benchmark_pixel_surface_t *dst, const void *src_pixels,
    int src_uses_rgba16f, const db_damage_block_t *block) {
    if ((dst == NULL) || (src_pixels == NULL) || (block == NULL) ||
        (dst->uses_rgba16f != src_uses_rgba16f)) {
        return;
    }
    const uint32_t pixel_bytes =
        (dst->uses_rgba16f != 0) ? (uint32_t)(sizeof(uint16_t) * 4U) : 4U;
    uint8_t *dst_bytes =
        (uint8_t *)((dst->uses_rgba16f != 0) ? (void *)dst->pixels_rgba16f
                                             : (void *)dst->pixels_rgba8);
    const uint8_t *src_bytes = (const uint8_t *)src_pixels;
    db_gl_shadow_present_copy_block_bytes(dst_bytes, src_bytes,
                                          dst->pixel_width, pixel_bytes, block);
}

void db_gl_shadow_present_repair_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target,
    const db_benchmark_pixel_surface_t *source_surface,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if ((state == NULL) || (target == NULL) || (source_surface == NULL)) {
        return;
    }
    if ((target->pixel_surface.pixel_width != source_surface->pixel_width) ||
        (target->pixel_surface.pixel_height != source_surface->pixel_height) ||
        (target->pixel_surface.uses_rgba16f != source_surface->uses_rgba16f)) {
        return;
    }
    if ((damage_blocks == NULL) || (damage_block_count == 0U)) {
        const db_damage_block_t full_block = db_damage_block_full(
            source_surface->pixel_height, source_surface->pixel_width);
        db_gl_shadow_present_copy_surface_block(&target->pixel_surface,
                                                source_surface, &full_block);
    } else {
        for (size_t i = 0U; i < damage_block_count; i++) {
            db_gl_shadow_present_copy_surface_block(
                &target->pixel_surface, source_surface, &damage_blocks[i]);
        }
    }
    db_gl_shadow_present_upload_slot_t *slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
    }
}

static void db_gl_shadow_present_repair_full_upload_target_from_pixels(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target,
    const void *source_pixels, int source_uses_rgba16f,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if ((state == NULL) || (target == NULL) || (source_pixels == NULL)) {
        return;
    }
    if (target->pixel_surface.uses_rgba16f != source_uses_rgba16f) {
        return;
    }
    if ((damage_blocks == NULL) || (damage_block_count == 0U)) {
        const db_damage_block_t full_block =
            db_damage_block_full(target->pixel_surface.pixel_height,
                                 target->pixel_surface.pixel_width);
        db_gl_shadow_present_copy_pixels_block(
            &target->pixel_surface, source_pixels, source_uses_rgba16f,
            &full_block);
    } else {
        for (size_t i = 0U; i < damage_block_count; i++) {
            db_gl_shadow_present_copy_pixels_block(
                &target->pixel_surface, source_pixels, source_uses_rgba16f,
                &damage_blocks[i]);
        }
    }
    db_gl_shadow_present_upload_slot_t *slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
        slot->slot_matches_shadow = 1;
    }
}

static int db_gl_shadow_present_requires_full_texture_upload(
    const db_gl_shadow_present_state_t *state,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if (state == NULL) {
        return 1;
    }
    if (state->texture_needs_full_upload != 0) {
        return 1;
    }
    if ((state->texture_valid == 0) &&
        ((damage_blocks == NULL) || (damage_block_count == 0U))) {
        return 1;
    }
    return 0;
}

void db_gl_shadow_present_sync_preserved_slots(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_benchmark_pixel_surface_t *source_surface,
    const db_damage_block_t *damage_blocks, size_t damage_block_count,
    const db_gl_shadow_present_full_upload_target_t *current_target) {
    if ((state == NULL) || (backend == NULL) || (source_surface == NULL) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }
    if ((source_surface->pixel_width != pixel_width) ||
        (source_surface->pixel_height != pixel_height)) {
        return;
    }
    uint32_t current_slot_index = state->slot_count;
    if (current_target != NULL) {
        current_slot_index = current_target->slot_index;
        db_gl_shadow_present_upload_slot_t *current_slot =
            db_gl_shadow_present_slot_or_null(state, current_slot_index);
        if (current_slot != NULL) {
            current_slot->slot_valid = 1;
            current_slot->slot_matches_shadow = 1;
        }
    }
    if ((state->preserve_mode != DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) ||
        (state->slot_count < 2U)) {
        return;
    }
    for (uint32_t slot_index = 0U; slot_index < state->slot_count;
         slot_index++) {
        if (slot_index == current_slot_index) {
            continue;
        }
        db_gl_shadow_present_upload_slot_t *slot =
            db_gl_shadow_present_slot_or_null(state, slot_index);
        if ((slot == NULL) ||
            ((slot->slot_matches_shadow != 0) && (slot->slot_valid != 0))) {
            continue;
        }
        const uint32_t slot_offset =
            (slot_index + state->slot_count - state->write_slot_index) %
            state->slot_count;
        db_gl_shadow_present_full_upload_target_t repair_target = {0};
        if (db_gl_shadow_present_begin_full_upload_target_slot_offset(
                state, backend, pixel_width, pixel_height, 1, slot_offset,
                &repair_target) == 0) {
            continue;
        }
        const db_benchmark_pixel_surface_t *repair_source_surface =
            ((current_target != NULL) &&
             (current_target->slot_index == current_slot_index) &&
             (current_target->slot_surface_valid != 0))
                ? &current_target->pixel_surface
                : source_surface;
        if ((repair_target.slot_surface_valid == 0) ||
            (damage_blocks == NULL) || (damage_block_count == 0U)) {
            db_gl_shadow_present_repair_full_upload_target(
                state, &repair_target, repair_source_surface, NULL, 0U);
        } else {
            db_gl_shadow_present_repair_full_upload_target(
                state, &repair_target, repair_source_surface, damage_blocks,
                damage_block_count);
        }
        db_gl_shadow_present_finish_full_upload_target(state, &repair_target);
        slot = db_gl_shadow_present_slot_or_null(state, slot_index);
        if (slot != NULL) {
            slot->slot_valid = 1;
            slot->slot_matches_shadow = 1;
        }
    }
}

void db_gl_shadow_present_present_replace_pixels(
    db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if ((state == NULL) || (backend == NULL) || (selected_pixels == NULL) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }
    db_gl_shadow_present_prepare_texture(state, backend, pixel_width,
                                         pixel_height);
    if (state->texture == 0U) {
        db_failf(backend, "shared shadow present texture is not initialized");
    }

    const int requires_full_upload =
        db_gl_shadow_present_requires_full_texture_upload(state, damage_blocks,
                                                          damage_block_count);
    if ((requires_full_upload != 0) &&
        (db_gl_stream_upload_uses_buffer_object(
             &state->effective_full_upload_capability) != 0)) {
        db_gl_shadow_present_full_upload_target_t target = {0};
        if (db_gl_shadow_present_begin_full_upload_target(
                state, backend, pixel_width, pixel_height, 0, &target) != 0) {
            db_gl_shadow_present_repair_full_upload_target_from_pixels(
                state, &target, selected_pixels,
                target.pixel_surface.uses_rgba16f, damage_blocks,
                damage_block_count);
            db_gl_shadow_present_present_full_upload_target(
                state, backend, pixel_width, pixel_height, &target);
            return;
        }
    }

    if (requires_full_upload != 0) {
        const db_damage_block_t full_block =
            db_damage_block_full(pixel_height, pixel_width);
        db_gl_shadow_present_upload_damage_blocks(
            state, backend, selected_pixels, pixel_width, pixel_height,
            &full_block, 1U);
        state->texture_valid = 1;
        state->texture_needs_full_upload = 0;
    } else if ((damage_blocks != NULL) && (damage_block_count > 0U)) {
        db_gl_shadow_present_upload_damage_blocks(
            state, backend, selected_pixels, pixel_width, pixel_height,
            damage_blocks, damage_block_count);
        state->texture_valid = 1;
    }

    db_gl_shadow_present_draw(state, pixel_width, pixel_height);
}

void db_gl_shadow_present_present_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_gl_shadow_present_full_upload_target_t *target) {
    if ((state == NULL) || (backend == NULL) || (target == NULL) ||
        (pixel_width == 0U) || (pixel_height == 0U) || (state->texture == 0U)) {
        return;
    }
    db_gl_shadow_present_upload_slot_t *slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot == NULL) {
        return;
    }
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    if (target->uses_pbo != 0) {
        (void)db_gl_bind_unpack_buffer_cached(slot->stream.buffer, NULL);
        db_gl_upload_stream_end_write(&slot->stream);
        if (state->selected_texture_format ==
            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
            db_gl_texture_sub_image_2d_rgba16f(
                0, 0,
                db_checked_u32_to_i32(backend, "full_upload_w", pixel_width),
                db_checked_u32_to_i32(backend, "full_upload_h", pixel_height),
                db_gl_vbo_offset_ptr(0U));
        } else {
            db_gl_texture_sub_image_2d_rgba(
                0, 0,
                db_checked_u32_to_i32(backend, "full_upload_w", pixel_width),
                db_checked_u32_to_i32(backend, "full_upload_h", pixel_height),
                db_gl_vbo_offset_ptr(0U));
        }
        db_gl_upload_state_reset_unpack();
    } else if (state->selected_texture_format ==
               DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
        db_gl_texture_sub_image_2d_rgba16f(
            0, 0, db_checked_u32_to_i32(backend, "full_upload_w", pixel_width),
            db_checked_u32_to_i32(backend, "full_upload_h", pixel_height),
            (const void *)target->pixel_surface.pixels_rgba16f);
    } else {
        db_gl_texture_sub_image_2d_rgba(
            0, 0, db_checked_u32_to_i32(backend, "full_upload_w", pixel_width),
            db_checked_u32_to_i32(backend, "full_upload_h", pixel_height),
            (const void *)target->pixel_surface.pixels_rgba8);
    }
    state->texture_valid = 1;
    state->texture_needs_full_upload = 0;
    slot->slot_valid = 1;
    if (target->preserve_contents != 0) {
        slot->slot_matches_shadow = 1;
    }
    state->present_slot_index = target->slot_index;
    state->write_slot_index =
        db_gl_shadow_present_next_write_slot_after_present(
            state->preserve_mode, target->preserve_contents, target->slot_index,
            state->slot_count);
    db_gl_shadow_present_mark_texture_slot(state, target->slot_index);
    db_gl_shadow_present_draw(state, pixel_width, pixel_height);
    if ((target->uses_pbo != 0) &&
        (db_gl_stream_upload_sync_enabled(&slot->stream.capability) != 0) &&
        (g_upload_proc_table.fence_sync != NULL)) {
        db_gl_upload_stream_record_sync(&slot->stream);
    }
}

void db_gl_shadow_present_upload_damage_blocks(
    db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *blocks, size_t block_count) {
    if ((state == NULL) || (backend == NULL) || (state->texture == 0U) ||
        (pixel_width == 0U) || (pixel_height == 0U) || (blocks == NULL) ||
        (block_count == 0U) || (selected_pixels == NULL)) {
        return;
    }

    const uint32_t pixel_bytes =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? (uint32_t)(sizeof(uint16_t) * 4U)
            : 4U;
    const size_t row_bytes_size = db_checked_mul_size(
        backend, "shadow_row_bytes", (size_t)pixel_width, (size_t)pixel_bytes);
    const size_t total_bytes =
        db_checked_mul_size(backend, "shadow_upload_total_bytes",
                            row_bytes_size, (size_t)pixel_height);
    if (total_bytes > PTRDIFF_MAX) {
        db_failf(backend, "shadow_upload_total_bytes too large: %zu",
                 total_bytes);
    }
    const int use_pbo = (db_gl_stream_upload_uses_buffer_object(
                             &state->unpack_stream.capability) != 0) &&
                                (state->unpack_stream.buffer != 0U)
                            ? 1
                            : 0;
    const int use_unpack_row_length =
        (state->runtime_supports_unpack_row_length_upload != 0) ? 1 : 0;
    const uint32_t row_bytes = db_checked_mul_u32(backend, "shadow_row_bytes",
                                                  pixel_width, pixel_bytes);
    db_gl_shadow_present_full_upload_target_t repair_target = {0};
    int use_slot_surface = 0;
    uint32_t upload_slot_index = 0U;
    if ((state->preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) &&
        (db_gl_shadow_present_texture_slot_index(state, &upload_slot_index) !=
         0)) {
        const uint32_t slot_offset =
            (upload_slot_index + state->slot_count - state->write_slot_index) %
            state->slot_count;
        if (db_gl_shadow_present_begin_full_upload_target_slot_offset(
                state, backend, pixel_width, pixel_height, 1, slot_offset,
                &repair_target) != 0) {
            db_gl_shadow_present_repair_full_upload_target_from_pixels(
                state, &repair_target, selected_pixels,
                (state->selected_texture_format ==
                 DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                    ? 1
                    : 0,
                blocks, block_count);
            db_gl_shadow_present_finish_full_upload_target(state,
                                                           &repair_target);
            use_slot_surface = 1;
            db_gl_shadow_present_upload_slot_t *repaired_slot =
                db_gl_shadow_present_slot_or_null(state,
                                                  repair_target.slot_index);
            if (repaired_slot != NULL) {
                repaired_slot->slot_matches_shadow = 1;
            }
        }
    }
    const db_gl_shadow_present_upload_slot_t *source_slot =
        (use_slot_surface != 0) ? db_gl_shadow_present_slot_const_or_null(
                                      state, repair_target.slot_index)
                                : NULL;
    const int use_slot_pbo =
        ((source_slot != NULL) && (repair_target.uses_pbo != 0)) ? 1 : 0;
    const void *source_pixels_ptr = selected_pixels;
    if (use_slot_surface != 0) {
        source_pixels_ptr =
            (state->selected_texture_format ==
             DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                ? (const void *)repair_target.pixel_surface.pixels_rgba16f
                : (const void *)repair_target.pixel_surface.pixels_rgba8;
    }
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    if (use_slot_pbo != 0) {
        (void)db_gl_bind_unpack_buffer_cached(source_slot->stream.buffer, NULL);
    }
    for (size_t i = 0U; i < block_count; i++) {
        const db_damage_block_t block = blocks[i];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        const uint32_t row_end = db_u32_min(
            pixel_height, db_checked_add_u32(backend, "shadow_row_end",
                                             block.row_start, block.row_count));
        const uint32_t col_end = db_u32_min(
            pixel_width, db_checked_add_u32(backend, "shadow_col_end",
                                            block.col_start, block.col_count));
        if ((row_end <= block.row_start) || (col_end <= block.col_start)) {
            continue;
        }
        const uint32_t row_count = row_end - block.row_start;
        const uint32_t col_count = col_end - block.col_start;
        const uint32_t block_row_bytes = db_checked_mul_u32(
            backend, "shadow_block_row_bytes", col_count, pixel_bytes);
        const size_t src_offset_bytes =
            ((size_t)block.row_start * (size_t)row_bytes) +
            ((size_t)block.col_start * (size_t)pixel_bytes);
        if (use_unpack_row_length != 0) {
            db_gl_set_unpack_row_length_pixels(db_checked_u32_to_i32(
                backend, "shadow_unpack_row_length", pixel_width));
            if (use_slot_pbo != 0) {
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(src_offset_bytes));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(src_offset_bytes));
                }
            } else if (use_pbo != 0) {
                const size_t block_bytes =
                    ((size_t)(row_count - 1U) * (size_t)row_bytes) +
                    (size_t)block_row_bytes;
                if (db_gl_shadow_present_prepare_unpack_upload_storage(
                        state, backend, block_bytes) == 0) {
                    continue;
                }
                (void)db_gl_upload_stream_write(
                    &state->unpack_stream, backend,
                    ((const uint8_t *)selected_pixels) + src_offset_bytes,
                    block_bytes, 0U, block_bytes);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                }
            } else {
                const void *pixels_ptr =
                    (state->selected_texture_format ==
                     DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                        ? (const void *)(((const uint16_t *)source_pixels_ptr) +
                                         (src_offset_bytes /
                                          (sizeof(uint16_t) * 4U)))
                        : (const void *)(((const uint8_t *)source_pixels_ptr) +
                                         src_offset_bytes);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                }
            }
            db_gl_set_unpack_row_length_pixels(0);
            continue;
        }
        for (uint32_t row = block.row_start; row < row_end; row++) {
            const size_t row_src_offset_bytes =
                ((size_t)row * (size_t)row_bytes) +
                ((size_t)block.col_start * (size_t)pixel_bytes);
            if (use_slot_pbo != 0) {
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(row_src_offset_bytes));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(row_src_offset_bytes));
                }
            } else if (use_pbo != 0) {
                if (db_gl_shadow_present_prepare_unpack_upload_storage(
                        state, backend, (size_t)block_row_bytes) == 0) {
                    continue;
                }
                (void)db_gl_upload_stream_write(
                    &state->unpack_stream, backend,
                    ((const uint8_t *)selected_pixels) + row_src_offset_bytes,
                    (size_t)block_row_bytes, 0U, (size_t)block_row_bytes);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                }
            } else if (state->selected_texture_format ==
                       DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                const uint16_t *pixels_ptr =
                    ((const uint16_t *)source_pixels_ptr) +
                    (row_src_offset_bytes / (sizeof(uint16_t) * 4U));
                db_gl_texture_sub_image_2d_rgba16f(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            } else {
                const uint8_t *pixels_ptr =
                    ((const uint8_t *)source_pixels_ptr) + row_src_offset_bytes;
                db_gl_texture_sub_image_2d_rgba(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            }
        }
    }
    if (use_unpack_row_length != 0) {
        db_gl_set_unpack_row_length_pixels(0);
    }
    if ((use_slot_surface != 0) && (source_slot != NULL)) {
        db_gl_shadow_present_mark_texture_slot(state, repair_target.slot_index);
    }
    if ((use_slot_pbo != 0) || (use_pbo != 0)) {
        db_gl_upload_state_reset_unpack();
    }
}

void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame) {
    if ((frame == NULL) || (frame->state == NULL) || (frame->backend == NULL) ||
        (frame->pixel_width == 0U) || (frame->pixel_height == 0U)) {
        return;
    }
    if (frame->selected_pixels == NULL) {
        db_failf(frame->backend, "shared shadow present pixels are missing");
    }
    db_gl_shadow_present_present_replace_pixels(
        frame->state, frame->backend, frame->selected_pixels,
        frame->pixel_width, frame->pixel_height, frame->damage_blocks,
        frame->damage_block_count);
}

void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    const float tex_u =
        (state->texture_width == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_width, state->texture_width);
    const float tex_v =
        (state->texture_height == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_height, state->texture_height);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V1_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V1_Y] = tex_v;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = tex_u;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;

    db_gl_prepare_textured_present_state();
    db_gl_set_vertex_pointer_2f(0, state->vertices);
    db_gl_set_color_pointer_f(4, 0, state->colors);
    db_gl_set_texcoord_pointer_2f(0, state->texcoords);
    db_gl_texture_bind_2d(state->texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_finish_textured_present_state();
}
