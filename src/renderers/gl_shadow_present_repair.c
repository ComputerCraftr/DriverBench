#include "gl_shadow_present_internal.h"

#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_render_types.h"
#include "gl_common.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void gl_shadow_present_copy_block_bytes(uint8_t *dst_bytes,
                                               const uint8_t *src_bytes,
                                               uint32_t pixel_width,
                                               uint32_t pixel_bytes,
                                               const db_damage_block_t *block) {
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
        const size_t row_offset = db_checked_mul_size(
            "renderer_gl_shadow_present", "surface_copy_row_offset", row_index,
            full_row_stride);
        const size_t col_offset = db_checked_mul_size(
            "renderer_gl_shadow_present", "surface_copy_col_offset",
            (size_t)block->col_start, (size_t)pixel_bytes);
        const size_t offset =
            db_checked_add_size("renderer_gl_shadow_present",
                                "surface_copy_offset", row_offset, col_offset);
        memmove(dst_bytes + offset, src_bytes + offset, row_bytes);
    }
}

static void
db_gl_shadow_present_copy_surface_block(db_pixel_surface_t *dst,
                                        const db_pixel_surface_t *src,
                                        const db_damage_block_t *block) {
    if ((dst == NULL) || (src == NULL) || (block == NULL) ||
        (dst->pixel_width != src->pixel_width) ||
        (dst->pixel_height != src->pixel_height) ||
        (dst->format != src->format)) {
        return;
    }
    const uint32_t pixel_bytes = (uint32_t)db_pixel_surface_pixel_bytes(dst);
    uint8_t *dst_bytes = db_pixel_surface_bytes_mut(dst);
    const uint8_t *src_bytes = db_pixel_surface_bytes_const(src);
    gl_shadow_present_copy_block_bytes(dst_bytes, src_bytes, dst->pixel_width,
                                       pixel_bytes, block);
}

static void gl_shadow_present_copy_pixels_block(
    db_pixel_surface_t *dst, const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *block) {
    const db_pixel_surface_t *const source_surface =
        (source_pixels != NULL) ? source_pixels->surface : NULL;
    if ((dst == NULL) || (source_surface == NULL) || (block == NULL) ||
        (dst->format != source_surface->format)) {
        return;
    }
    const uint32_t pixel_bytes = (uint32_t)db_pixel_surface_pixel_bytes(dst);
    uint8_t *dst_bytes = db_pixel_surface_bytes_mut(dst);
    const uint8_t *src_bytes = db_pixel_surface_bytes_const(source_surface);
    gl_shadow_present_copy_block_bytes(dst_bytes, src_bytes, dst->pixel_width,
                                       pixel_bytes, block);
}

void db_gl_shadow_present_repair_full_upload_target(
    db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_full_upload_target_t *target,
    const db_pixel_surface_t *source_surface,
    db_pixel_block_view_t damage_view) {
    if ((state == NULL) || (target == NULL) || (source_surface == NULL)) {
        return;
    }
    if ((target->pixel_surface.pixel_width != source_surface->pixel_width) ||
        (target->pixel_surface.pixel_height != source_surface->pixel_height) ||
        (target->pixel_surface.format != source_surface->format)) {
        return;
    }
    if ((damage_view.blocks == NULL) || (damage_view.count == 0U)) {
        const db_damage_block_t full_block = db_damage_block_full(
            source_surface->pixel_height, source_surface->pixel_width);
        db_gl_shadow_present_copy_surface_block(&target->pixel_surface,
                                                source_surface, &full_block);
    } else {
        for (size_t i = 0U; i < damage_view.count; i++) {
            db_gl_shadow_present_copy_surface_block(
                &target->pixel_surface, source_surface, &damage_view.blocks[i]);
        }
    }
    db_gl_shadow_present_upload_slot_t *slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
    }
}

void db_gl_shadow_present_repair_full_upload_target_from_pixels(
    db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_full_upload_target_t *target,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    const db_pixel_surface_t *const source_surface =
        (source_pixels != NULL) ? source_pixels->surface : NULL;
    if ((state == NULL) || (target == NULL) || (source_pixels == NULL)) {
        return;
    }
    if ((source_surface == NULL) ||
        (target->pixel_surface.format != source_surface->format)) {
        return;
    }
    if ((damage_blocks == NULL) || (damage_block_count == 0U)) {
        const db_damage_block_t full_block =
            db_damage_block_full(target->pixel_surface.pixel_height,
                                 target->pixel_surface.pixel_width);
        gl_shadow_present_copy_pixels_block(&target->pixel_surface,
                                            source_pixels, &full_block);
    } else {
        for (size_t i = 0U; i < damage_block_count; i++) {
            gl_shadow_present_copy_pixels_block(
                &target->pixel_surface, source_pixels, &damage_blocks[i]);
        }
    }
    db_gl_shadow_present_upload_slot_t *slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
        slot->slot_matches_shadow = 1;
    }
}

int db_gl_shadow_present_requires_full_texture_upload(
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
    const db_pixel_surface_t *source_surface, db_pixel_block_view_t damage_view,
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
        const db_pixel_surface_t *repair_source_surface =
            ((current_target != NULL) &&
             (current_target->slot_index == current_slot_index) &&
             (current_target->slot_surface_valid != 0))
                ? &current_target->pixel_surface
                : source_surface;
        if ((repair_target.slot_surface_valid == 0) ||
            (damage_view.blocks == NULL) || (damage_view.count == 0U)) {
            db_gl_shadow_present_repair_full_upload_target(
                state, &repair_target, repair_source_surface,
                (db_pixel_block_view_t){NULL, 0U});
        } else {
            db_gl_shadow_present_repair_full_upload_target(
                state, &repair_target, repair_source_surface, damage_view);
        }
        db_gl_shadow_present_finish_full_upload_target(state, &repair_target);
        slot = db_gl_shadow_present_slot_or_null(state, slot_index);
        if (slot != NULL) {
            slot->slot_valid = 1;
            slot->slot_matches_shadow = 1;
        }
    }
}
