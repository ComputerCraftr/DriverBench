#include "../core/db_core.h"
#include "../core/db_geometry.h"
#include "../core/db_numeric.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_render_types.h"
#include "core/db_trace.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include "gl_shadow_present_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *db_gl_shadow_full_upload_target_mode_name(
    db_gl_shadow_full_upload_target_mode_t mode) {
    switch (mode) {
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO:
        return "mapped_pbo";
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_CLIENT_BUFFER_THEN_SUBDATA:
        return "client_buffer_then_subdata";
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD:
        return "direct_client_texture_upload";
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_NONE:
        break;
    }
    return "none";
}

static void gl_shadow_present_texture_sub_image_2d(
    db_gl_shadow_present_texture_format_t texture_format, uint32_t col_start,
    uint32_t row_start, uint32_t col_count, uint32_t row_count,
    const void *pixels) {
    switch (texture_format) {
    case DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2:
        db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(col_start, row_start,
                                                     col_count, row_count,
                                                     (const uint32_t *)pixels);
        break;
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F:
        db_gl_texture_sub_image_2d_rgba16f(col_start, row_start, col_count,
                                           row_count, pixels);
        break;
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8:
        db_gl_texture_sub_image_2d_rgba(col_start, row_start, col_count,
                                        row_count, pixels);
        break;
    }
}

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
        const size_t offset =
            db_checked_mul_size("renderer_gl_shadow_present",
                                "surface_copy_row_offset", row_index,
                                full_row_stride) +
            db_checked_mul_size("renderer_gl_shadow_present",
                                "surface_copy_col_offset",
                                (size_t)block->col_start, (size_t)pixel_bytes);
        memcpy(dst_bytes + offset, src_bytes + offset, row_bytes);
    }
}

static void
db_gl_shadow_present_copy_surface_block(const db_pixel_surface_t *dst,
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
    const db_pixel_surface_t *dst,
    const db_gl_pixel_upload_payload_t *source_pixels,
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
    const db_gl_shadow_present_full_upload_target_t *target,
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

static void gl_shadow_present_repair_full_upload_target_from_pixels(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target,
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

static int gl_shadow_present_requires_full_texture_upload(
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

void db_gl_shadow_present_present_replace_pixels(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    const db_pixel_surface_t *const source_surface =
        (source_pixels != NULL) ? source_pixels->surface : NULL;
    if ((state == NULL) || (backend == NULL) || (source_surface == NULL) ||
        (source_surface->pixel_width == 0U) ||
        (source_surface->pixel_height == 0U)) {
        return;
    }
    const uint32_t pixel_width = source_surface->pixel_width;
    const uint32_t pixel_height = source_surface->pixel_height;
    db_gl_shadow_upload_trace_capture_pixel_payload(&state->upload_trace,
                                                    source_pixels);
    db_gl_shadow_present_prepare_texture(state, backend, pixel_width,
                                         pixel_height);
    if (state->texture == 0U) {
        DB_RUNTIME_FAIL(backend,
                        "shared shadow present texture is not initialized");
    }

    const int requires_full_upload =
        gl_shadow_present_requires_full_texture_upload(state, damage_blocks,
                                                       damage_block_count);
    if (requires_full_upload != 0) {
        const db_damage_block_t full_block =
            db_damage_block_full(pixel_height, pixel_width);
        db_gl_shadow_present_upload_damage_blocks(state, backend, source_pixels,
                                                  &full_block, 1U);
        state->texture_valid = 1;
        state->texture_needs_full_upload = 0;
    } else if ((damage_blocks != NULL) && (damage_block_count > 0U)) {
        db_gl_shadow_present_upload_damage_blocks(
            state, backend, source_pixels, damage_blocks, damage_block_count);
        state->texture_valid = 1;
    }

    db_gl_shadow_present_draw(state, pixel_width, pixel_height);
}

void db_gl_shadow_present_present_replace_pixels_direct_client(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if (state == NULL) {
        return;
    }

    const db_gl_present_upload_profile_t saved_profile = state->upload_profile;
    const db_gl_shadow_present_preserve_mode_t saved_preserve_mode =
        state->preserve_mode;
    db_gl_stream_upload_capability_t
        saved_unpack_capabilities[DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT] = {
            0};
    db_gl_stream_upload_capability_t
        saved_slot_capabilities[DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS] = {0};
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS; i++) {
        saved_slot_capabilities[i] = state->upload_slots[i].stream.capability;
    }

    db_gl_stream_upload_force_client_fallback(
        &state->upload_profile.effective_full, 1);
    db_gl_stream_upload_force_client_fallback(
        &state->upload_profile.effective_partial, 1);
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT; i++) {
        saved_unpack_capabilities[i] = state->unpack_streams[i].capability;
        db_gl_stream_upload_force_client_fallback(
            &state->unpack_streams[i].capability, 1);
    }
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS; i++) {
        db_gl_stream_upload_force_client_fallback(
            &state->upload_slots[i].stream.capability, 1);
    }
    state->preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS;

    db_gl_shadow_present_present_replace_pixels(
        state, backend, source_pixels, damage_blocks, damage_block_count);

    state->upload_profile = saved_profile;
    state->preserve_mode = saved_preserve_mode;
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT; i++) {
        state->unpack_streams[i].capability = saved_unpack_capabilities[i];
    }
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS; i++) {
        state->upload_slots[i].stream.capability = saved_slot_capabilities[i];
    }
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
    if (target->mode == DB_GL_SHADOW_FULL_UPLOAD_TARGET_NONE) {
        DB_RUNTIME_FAIL(backend, "invalid full upload target mode for slot=%u",
                        target->slot_index);
    }

    const db_gl_pixel_upload_payload_t upload_payload =
        db_gl_pixel_upload_payload_from_surface(&target->pixel_surface);
    db_gl_shadow_upload_trace_capture_pixel_payload(&state->upload_trace,
                                                    &upload_payload);
    db_gl_shadow_upload_trace_capture_full_upload_attempt(
        &state->upload_trace, target->slot_index, target->total_bytes,
        "shadow_full_upload_target",
        db_gl_shadow_full_upload_target_mode_name(target->mode),
        db_gl_stream_upload_name(&slot->stream.capability, 0, 1));
    const db_trace_config_t trace_config = db_trace_config_current();
    const int capture_surface_hashes = DB_BOOL(
        (trace_config.damage != 0) || (trace_config.shadow_upload != 0));
    const uint64_t target_surface_hash =
        (capture_surface_hashes != 0)
            ? db_gl_pixel_surface_hash_canonical(&target->pixel_surface)
            : 0U;
    const uint64_t fallback_source_hash =
        (capture_surface_hashes != 0) ? db_gl_pixel_surface_hash_canonical(
                                            target->fallback_source_surface)
                                      : 0U;
    const db_damage_block_t full_block =
        db_damage_block_full(pixel_height, pixel_width);
    const int finalized_upload_target =
        db_gl_shadow_present_finish_full_upload_target(state, target);

    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    if (finalized_upload_target == 0) {
        db_gl_shadow_upload_trace_note_fallback(
            &state->upload_trace, "full_upload_target_finalize_failed");
        if (target->fallback_source_surface != NULL) {
            db_gl_shadow_upload_trace_capture_upload_span(
                &state->upload_trace, &full_block, 0U, target->total_bytes,
                "full_upload_target_fallback_source");
            gl_shadow_present_texture_sub_image_2d(
                state->selected_texture_format, 0U, 0U, pixel_width,
                pixel_height,
                db_pixel_surface_data_const(target->fallback_source_surface));
            db_gl_shadow_upload_trace_note_execution(
                &state->upload_trace, "direct_client_texture_upload");
            db_gl_shadow_upload_trace_note_surface_hashes(
                &state->upload_trace, fallback_source_hash, target_surface_hash,
                fallback_source_hash);
        } else {
            db_gl_shadow_upload_trace_note_execution(&state->upload_trace,
                                                     "failed_no_fallback");
        }
    } else if ((target->mode == DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO) ||
               (target->mode ==
                DB_GL_SHADOW_FULL_UPLOAD_TARGET_CLIENT_BUFFER_THEN_SUBDATA)) {
        (void)db_gl_upload_stream_bind(&slot->stream);
        db_gl_shadow_upload_trace_capture_upload_span(
            &state->upload_trace, &full_block, 0U, target->total_bytes,
            "shadow_full_upload_target");
        gl_shadow_present_texture_sub_image_2d(
            state->selected_texture_format, 0U, 0U, pixel_width, pixel_height,
            db_gl_vbo_offset_ptr(0U));
        db_gl_shadow_upload_trace_note_execution(
            &state->upload_trace,
            (target->mode == DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO)
                ? "mapped_pbo"
                : "client_buffer_then_subdata");
        db_gl_shadow_upload_trace_note_surface_hashes(
            &state->upload_trace, target_surface_hash, target_surface_hash,
            fallback_source_hash);
        db_gl_upload_state_reset_unpack();
    } else {
        db_gl_shadow_upload_trace_capture_upload_span(
            &state->upload_trace, &full_block, 0U, target->total_bytes,
            "full_upload_target");
        gl_shadow_present_texture_sub_image_2d(
            state->selected_texture_format, 0U, 0U, pixel_width, pixel_height,
            db_pixel_surface_data_const(&target->pixel_surface));
        db_gl_shadow_upload_trace_note_execution(
            &state->upload_trace, "direct_client_texture_upload");
        db_gl_shadow_upload_trace_note_surface_hashes(
            &state->upload_trace, target_surface_hash, target_surface_hash,
            fallback_source_hash);
    }
    state->texture_valid = 1;
    state->texture_needs_full_upload = 0;
    slot->slot_valid = 1;
    if (target->preserve_slot_contents != 0) {
        slot->slot_matches_shadow = 1;
    }
    state->present_slot_index = target->slot_index;
    state->write_slot_index =
        db_gl_shadow_present_next_write_slot_after_present(
            state->preserve_mode, target->preserve_slot_contents,
            target->slot_index, state->slot_count);
    db_gl_shadow_present_mark_texture_slot(state, target->slot_index);
    db_gl_shadow_present_draw(state, pixel_width, pixel_height);
    if ((target->mode !=
         DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD) &&
        (db_gl_stream_upload_sync_enabled(&slot->stream.capability) != 0) &&
        (g_upload_proc_table.fence_sync != NULL)) {
        db_gl_upload_stream_record_sync(&slot->stream);
    }
}

void db_gl_shadow_present_upload_damage_blocks(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *blocks, size_t block_count) {
    const db_pixel_surface_t *const source_surface =
        (source_pixels != NULL) ? source_pixels->surface : NULL;
    if ((state == NULL) || (backend == NULL) || (state->texture == 0U) ||
        (source_surface == NULL) || (source_surface->pixel_width == 0U) ||
        (source_surface->pixel_height == 0U) || (blocks == NULL) ||
        (block_count == 0U)) {
        return;
    }
    const uint32_t pixel_width = source_surface->pixel_width;
    const uint32_t pixel_height = source_surface->pixel_height;
    db_gl_shadow_upload_trace_capture_pixel_payload(&state->upload_trace,
                                                    source_pixels);
    if (state->hdr_output_enabled != 0) {
        db_gl_shadow_present_upload_hdr_damage_blocks(
            state, backend, source_surface, blocks, block_count);
        return;
    }

    const uint32_t pixel_bytes =
        (uint32_t)db_pixel_surface_pixel_bytes(source_surface);
    const size_t row_bytes_size = db_checked_mul_size(
        backend, "shadow_row_bytes", (size_t)pixel_width, (size_t)pixel_bytes);
    const size_t total_bytes =
        db_checked_mul_size(backend, "shadow_upload_total_bytes",
                            row_bytes_size, (size_t)pixel_height);
    if (total_bytes > PTRDIFF_MAX) {
        DB_RUNTIME_FAIL(backend, "shadow_upload_total_bytes too large: %zu",
                        total_bytes);
    }
    db_gl_upload_stream_t *unpack_stream =
        &state->unpack_streams[state->unpack_write_index %
                               DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT];
    const int use_pbo = DB_BOOL((db_gl_stream_upload_uses_buffer_object(
                                     &unpack_stream->capability) != 0) &&
                                (unpack_stream->buffer != 0U));
    const int use_unpack_row_length =
        DB_BOOL(state->runtime_supports_unpack_row_length_upload);
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
            gl_shadow_present_repair_full_upload_target_from_pixels(
                state, &repair_target, source_pixels, blocks, block_count);
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
        DB_BOOL((source_slot != NULL) &&
                (repair_target.mode !=
                 DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD));
    const uint8_t *source_pixels_ptr =
        db_pixel_surface_bytes_const(source_surface);
    int upload_executed = 0;
    if (use_slot_surface != 0) {
        source_pixels_ptr =
            db_pixel_surface_bytes_const(&repair_target.pixel_surface);
    }
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    if (use_slot_pbo != 0) {
        (void)db_gl_upload_stream_bind(&source_slot->stream);
    }
    for (size_t i = 0U; i < block_count; i++) {
        const db_damage_block_t block = blocks[i];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        const uint32_t row_end = DB_MIN(
            pixel_height, db_checked_add_u32(backend, "shadow_row_end",
                                             block.row_start, block.row_count));
        const uint32_t col_end = DB_MIN(
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
        if ((block.col_start == 0U) && (col_count == pixel_width)) {
            const size_t block_bytes = (size_t)row_count * (size_t)row_bytes;
            db_gl_set_unpack_row_length_pixels(0U);
            db_gl_shadow_upload_trace_capture_upload_span(
                &state->upload_trace, &block, src_offset_bytes, block_bytes,
                "canonical_surface_tightly_packed");
            if (use_slot_pbo != 0) {
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, 0U, block.row_start,
                    pixel_width, row_count,
                    db_gl_vbo_offset_ptr(src_offset_bytes));
                upload_executed = 1;
            } else if (use_pbo != 0) {
                unpack_stream = db_gl_shadow_present_acquire_unpack_stream(
                    state, backend, block_bytes);
                const int stream_ready =
                    DB_BOOL((unpack_stream != NULL) &&
                            (db_gl_stream_upload_uses_buffer_object(
                                 &unpack_stream->capability) != 0) &&
                            (db_gl_upload_stream_write(
                                 unpack_stream, backend,
                                 source_pixels_ptr + src_offset_bytes,
                                 block_bytes, 0U, block_bytes) != 0));
                if (stream_ready != 0) {
                    if (db_gl_upload_stream_bind(unpack_stream) == 0) {
                        DB_RUNTIME_FAIL(backend,
                                        "failed to bind populated unpack PBO");
                    }
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, 0U, block.row_start,
                        pixel_width, row_count, db_gl_vbo_offset_ptr(0U));
                    upload_executed = 1;
                    db_gl_upload_stream_record_sync(unpack_stream);
                    state->unpack_write_index =
                        (state->unpack_write_index + 1U) %
                        DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT;
                } else {
                    db_gl_upload_state_reset_unpack();
                    db_gl_set_unpack_alignment_1();
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, 0U, block.row_start,
                        pixel_width, row_count,
                        db_pixel_surface_data_at_offset_const(
                            source_surface, src_offset_bytes));
                    upload_executed = 1;
                }
            } else {
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, 0U, block.row_start,
                    pixel_width, row_count,
                    db_pixel_surface_data_at_offset_const(
                        use_slot_surface != 0 ? &repair_target.pixel_surface
                                              : source_surface,
                        src_offset_bytes));
                upload_executed = 1;
            }
            continue;
        }
        if (use_unpack_row_length != 0) {
            db_gl_set_unpack_row_length_pixels(pixel_width);
            if (use_slot_pbo != 0) {
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, block.col_start,
                    block.row_start, col_count, row_count,
                    db_gl_vbo_offset_ptr(src_offset_bytes));
                upload_executed = 1;
            } else if (use_pbo != 0) {
                const size_t block_bytes =
                    ((size_t)(row_count - 1U) * (size_t)row_bytes) +
                    (size_t)block_row_bytes;
                db_gl_shadow_upload_trace_capture_upload_span(
                    &state->upload_trace, &block, src_offset_bytes, block_bytes,
                    "canonical_surface");
                unpack_stream = db_gl_shadow_present_acquire_unpack_stream(
                    state, backend, block_bytes);
                const int stream_ready =
                    DB_BOOL((unpack_stream != NULL) &&
                            (db_gl_stream_upload_uses_buffer_object(
                                 &unpack_stream->capability) != 0) &&
                            (db_gl_upload_stream_write(
                                 unpack_stream, backend,
                                 source_pixels_ptr + src_offset_bytes,
                                 block_bytes, 0U, block_bytes) != 0));
                if (stream_ready != 0) {
                    if (db_gl_upload_stream_bind(unpack_stream) == 0) {
                        DB_RUNTIME_FAIL(backend,
                                        "failed to bind populated unpack PBO");
                    }
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, block.col_start,
                        block.row_start, col_count, row_count,
                        db_gl_vbo_offset_ptr(0U));
                    upload_executed = 1;
                    db_gl_upload_stream_record_sync(unpack_stream);
                    state->unpack_write_index =
                        (state->unpack_write_index + 1U) %
                        DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT;
                } else {
                    db_gl_upload_state_reset_unpack();
                    db_gl_set_unpack_alignment_1();
                    db_gl_set_unpack_row_length_pixels(pixel_width);
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, block.col_start,
                        block.row_start, col_count, row_count,
                        db_pixel_surface_data_at_offset_const(
                            source_surface, src_offset_bytes));
                    upload_executed = 1;
                }
            } else {
                const size_t block_bytes =
                    ((size_t)(row_count - 1U) * (size_t)row_bytes) +
                    (size_t)block_row_bytes;
                db_gl_shadow_upload_trace_capture_upload_span(
                    &state->upload_trace, &block, src_offset_bytes, block_bytes,
                    "canonical_surface_direct");
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, block.col_start,
                    block.row_start, col_count, row_count,
                    db_pixel_surface_data_at_offset_const(
                        use_slot_surface != 0 ? &repair_target.pixel_surface
                                              : source_surface,
                        src_offset_bytes));
                upload_executed = 1;
            }
            db_gl_set_unpack_row_length_pixels(0U);
            continue;
        }
        for (uint32_t row = block.row_start; row < row_end; row++) {
            const size_t row_src_offset_bytes =
                ((size_t)row * (size_t)row_bytes) +
                ((size_t)block.col_start * (size_t)pixel_bytes);
            if (use_slot_pbo != 0) {
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, block.col_start, row,
                    col_count, 1U, db_gl_vbo_offset_ptr(row_src_offset_bytes));
                upload_executed = 1;
            } else if (use_pbo != 0) {
                db_gl_shadow_upload_trace_capture_upload_span(
                    &state->upload_trace,
                    &(const db_damage_block_t){
                        .row_start = row,
                        .row_count = 1U,
                        .col_start = block.col_start,
                        .col_count = col_count,
                    },
                    row_src_offset_bytes, (size_t)block_row_bytes,
                    "canonical_surface");
                unpack_stream = db_gl_shadow_present_acquire_unpack_stream(
                    state, backend, (size_t)block_row_bytes);
                const int stream_ready =
                    DB_BOOL((unpack_stream != NULL) &&
                            (db_gl_stream_upload_uses_buffer_object(
                                 &unpack_stream->capability) != 0) &&
                            (db_gl_upload_stream_write(
                                 unpack_stream, backend,
                                 source_pixels_ptr + row_src_offset_bytes,
                                 (size_t)block_row_bytes, 0U,
                                 (size_t)block_row_bytes) != 0));
                if (stream_ready != 0) {
                    if (db_gl_upload_stream_bind(unpack_stream) == 0) {
                        DB_RUNTIME_FAIL(backend,
                                        "failed to bind populated unpack PBO");
                    }
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, block.col_start, row,
                        col_count, 1U, db_gl_vbo_offset_ptr(0U));
                    upload_executed = 1;
                    db_gl_upload_stream_record_sync(unpack_stream);
                    state->unpack_write_index =
                        (state->unpack_write_index + 1U) %
                        DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT;
                } else {
                    db_gl_upload_state_reset_unpack();
                    db_gl_set_unpack_alignment_1();
                    gl_shadow_present_texture_sub_image_2d(
                        state->selected_texture_format, block.col_start, row,
                        col_count, 1U,
                        db_pixel_surface_data_at_offset_const(
                            source_surface, row_src_offset_bytes));
                    upload_executed = 1;
                }
            } else {
                db_gl_shadow_upload_trace_capture_upload_span(
                    &state->upload_trace,
                    &(const db_damage_block_t){
                        .row_start = row,
                        .row_count = 1U,
                        .col_start = block.col_start,
                        .col_count = col_count,
                    },
                    row_src_offset_bytes, (size_t)block_row_bytes,
                    "canonical_surface_direct");
                gl_shadow_present_texture_sub_image_2d(
                    state->selected_texture_format, block.col_start, row,
                    col_count, 1U,
                    db_pixel_surface_data_at_offset_const(
                        use_slot_surface != 0 ? &repair_target.pixel_surface
                                              : source_surface,
                        row_src_offset_bytes));
                upload_executed = 1;
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
    if (upload_executed != 0) {
        db_gl_shadow_upload_trace_note_execution(
            &state->upload_trace, ((use_slot_pbo != 0) || (use_pbo != 0))
                                      ? "pbo_upload"
                                      : "client_upload");
    }
}

void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame) {
    if ((frame == NULL) || (frame->state == NULL) || (frame->backend == NULL) ||
        (frame->pixel_width == 0U) || (frame->pixel_height == 0U)) {
        return;
    }
    if (frame->source_pixels.surface == NULL) {
        DB_RUNTIME_FAIL(frame->backend,
                        "shared shadow present pixels are missing");
    }
    db_gl_shadow_upload_trace_reset(&frame->state->upload_trace);
    db_gl_shadow_present_present_replace_pixels(
        frame->state, frame->backend, &frame->source_pixels,
        frame->damage_blocks, frame->damage_block_count);
}

void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    if ((state->present_damage_configured != 0) &&
        (state->present_damage_full == 0) &&
        (state->present_damage_block_count == 0U)) {
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
    if (state->presentation_quad_uses_vbo != 0) {
        (void)db_gl_upload_stream_bind(&state->presentation_quad_stream);
        db_gl_set_vertex_pointer_2f(0, db_gl_vbo_offset_ptr(0U));
        (void)db_gl_upload_stream_unbind_target(
            DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER);
    } else {
        db_gl_set_vertex_pointer_2f(0, state->vertices);
    }
    db_gl_set_color_pointer_f(4, 0, state->colors);
    db_gl_set_texcoord_pointer_2f(0, state->texcoords);
    db_gl_texture_bind_2d(state->texture);
    const int use_damage = DB_BOOL((state->present_damage_configured != 0) &&
                                   (state->present_damage_full == 0) &&
                                   (state->present_damage_blocks != NULL) &&
                                   (state->present_damage_block_count > 0U));
    if (use_damage != 0) {
        db_gl_set_scissor_enabled(1);
        for (size_t index = 0U; index < state->present_damage_block_count;
             index++) {
            const db_damage_block_t block = state->present_damage_blocks[index];
            if ((block.row_count == 0U) || (block.col_count == 0U) ||
                (block.row_start >= pixel_height) ||
                (block.col_start >= pixel_width)) {
                continue;
            }
            const uint32_t row_count =
                DB_MIN(block.row_count, pixel_height - block.row_start);
            const uint32_t col_count =
                DB_MIN(block.col_count, pixel_width - block.col_start);
            const uint32_t scissor_y =
                pixel_height - (block.row_start + row_count);
            db_gl_set_scissor(block.col_start, scissor_y, col_count, row_count);
            db_gl_draw_arrays_triangle_strip(0, 4);
        }
        db_gl_set_scissor_enabled(0);
    } else {
        db_gl_draw_arrays_triangle_strip(0, 4);
    }
    db_gl_finish_textured_present_state();
}

void db_gl_shadow_present_set_draw_damage(db_gl_shadow_present_state_t *state,
                                          db_pixel_block_view_t damage,
                                          int force_full) {
    if (state == NULL) {
        return;
    }
    state->present_damage_blocks = damage.blocks;
    state->present_damage_block_count = damage.count;
    state->present_damage_full = DB_BOOL(force_full);
    state->present_damage_configured = 1;
}
