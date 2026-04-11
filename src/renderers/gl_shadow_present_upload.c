#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_render_types.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include "gl_shadow_present_internal.h"
#include <stddef.h>
#include <stdint.h>

void db_gl_set_unpack_row_length_pixels(uint32_t pixel_count) {
    db_gl_load_upload_proc_table();
    const int i_pixel_count = db_checked_u32_to_i32(
        "db_gl_set_unpack_row_length_pixels", "pixel_count", pixel_count);
    if ((g_unpack_row_length_state_valid != 0) &&
        (g_unpack_row_length_state == i_pixel_count)) {
        return;
    }
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ROW_LENGTH,
                                         (GLint)i_pixel_count);
        g_unpack_row_length_state = i_pixel_count;
        g_unpack_row_length_state_valid = 1;
    }
}

static int gl_shadow_present_prepare_full_upload_storage(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (backend == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return 0;
    }
    const size_t row_bytes = db_checked_mul_size(
        backend, "full_upload_row_bytes", (size_t)pixel_width,
        (size_t)db_gl_shadow_present_pixel_bytes(state));
    const size_t total_bytes = db_checked_mul_size(
        backend, "full_upload_total_bytes", row_bytes, (size_t)pixel_height);
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        db_gl_shadow_present_upload_slot_t *const slot =
            &state->upload_slots[i];
        slot->stream.capability = state->upload_profile.effective_full;
        db_gl_stream_upload_disable_persistent_for_target(
            &slot->stream.capability, slot->stream.target);
        (void)db_gl_upload_stream_create_owned_buffer(&slot->stream, backend);
        const int size_changed =
            DB_BOOL(slot->stream.active_bytes != total_bytes);
        if (size_changed != 0) {
            slot->stream.active_bytes = total_bytes;
            slot->slot_valid = 0;
            slot->slot_matches_shadow = 0;
            slot->slot_matches_presented_texture = 0;
            db_gl_shadow_present_delete_slot_sync(slot);
        }
        if (db_gl_upload_stream_prepare_storage(&slot->stream, backend,
                                                total_bytes) == 0) {
            state->upload_profile.effective_full = slot->stream.capability;
            db_gl_shadow_present_refresh_effective_mode(state, 1);
            DB_RUNTIME_ERROR(backend,
                             "shadow full upload storage prepare failed; "
                             "forcing expensive client full-upload fallback "
                             "slot=%u bytes=%zu",
                             i, total_bytes);
            return 0;
        }
    }
    return 1;
}

int db_gl_shadow_present_prepare_unpack_upload_storage(
    db_gl_shadow_present_state_t *state, const char *backend,
    size_t required_bytes) {
    if ((state == NULL) || (backend == NULL)) {
        return 0;
    }
    db_gl_upload_stream_t *const stream =
        &state->unpack_streams[state->unpack_write_index %
                               DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT];
    stream->capability = state->upload_profile.effective_partial;
    db_gl_stream_upload_disable_persistent_for_target(&stream->capability,
                                                      stream->target);
    (void)db_gl_upload_stream_create_owned_buffer(stream, backend);
    if (db_gl_upload_stream_prepare_storage(stream, backend, required_bytes) ==
        0) {
        state->upload_profile.effective_partial = stream->capability;
        db_gl_shadow_present_refresh_effective_mode(state, 0);
        DB_RUNTIME_ERROR(backend,
                         "shadow partial upload storage prepare failed; "
                         "forcing expensive client partial-upload fallback "
                         "bytes=%zu",
                         required_bytes);
        return 0;
    }
    return 1;
}

db_gl_upload_stream_t *
db_gl_shadow_present_acquire_unpack_stream(db_gl_shadow_present_state_t *state,
                                           const char *backend,
                                           size_t required_bytes) {
    if ((state == NULL) || (backend == NULL)) {
        return NULL;
    }
    db_gl_upload_stream_t *const stream =
        &state->unpack_streams[state->unpack_write_index %
                               DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT];
    if (db_gl_upload_stream_wait(stream) == 0) {
        return NULL;
    }
    if (db_gl_shadow_present_prepare_unpack_upload_storage(
            state, backend, required_bytes) == 0) {
        return NULL;
    }
    return stream;
}

int db_gl_shadow_present_begin_full_upload_target_slot_offset(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_slot_contents,
    uint32_t slot_offset, db_gl_shadow_present_full_upload_target_t *target) {
    if ((state == NULL) || (backend == NULL) || (target == NULL) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        return 0;
    }
    *target = (db_gl_shadow_present_full_upload_target_t){0};
    db_gl_shadow_present_prepare_texture(state, backend, pixel_width,
                                         pixel_height);
    if (gl_shadow_present_prepare_full_upload_storage(
            state, backend, pixel_width, pixel_height) == 0) {
        return 0;
    }
    target->pixel_surface.pixel_width = pixel_width;
    target->pixel_surface.pixel_height = pixel_height;
    target->pixel_surface.format =
        db_gl_pixel_format_from_texture_format(state->selected_texture_format);
    target->preserve_slot_contents = DB_BOOL(preserve_slot_contents);
    db_gl_shadow_present_full_upload_slot_choice_t choice = {0};
    if (db_gl_shadow_present_choose_full_upload_slot(
            state, preserve_slot_contents, slot_offset,
            db_gl_shadow_present_ring_busy_mask(state), &choice) == 0) {
        return 0;
    }
    const uint32_t slot_index = choice.slot_index;
    db_gl_shadow_present_upload_slot_t *const slot =
        db_gl_shadow_present_slot_or_null(state, slot_index);
    if (slot == NULL) {
        return 0;
    }
    target->slot_index = slot_index;
    target->slot_surface_valid = slot->slot_valid;
    target->total_bytes = slot->stream.active_bytes;
    slot->stream.capability = state->upload_profile.effective_full;
    db_gl_stream_upload_disable_persistent_for_target(&slot->stream.capability,
                                                      slot->stream.target);
    db_gl_shadow_upload_trace_capture_pixel_payload(
        &state->upload_trace,
        &(const db_gl_pixel_upload_payload_t){
            .surface = &target->pixel_surface,
            .format = db_gl_texture_format_from_pixel_format(
                target->pixel_surface.format),
            .row_stride_bytes = db_checked_mul_size(
                backend, "shadow_trace_row_stride",
                (size_t)target->pixel_surface.pixel_width,
                db_pixel_surface_pixel_bytes(&target->pixel_surface)),
            .total_bytes = target->total_bytes,
        });
    if ((db_gl_stream_upload_uses_buffer_object(&slot->stream.capability) !=
         0) &&
        (slot->stream.buffer != 0U) &&
        (g_upload_proc_table.bind_buffer != NULL)) {
        if ((preserve_slot_contents != 0) &&
            (state->preserve_mode ==
             DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) &&
            (slot_offset == 0U)) {
            if (choice.requires_blocking_reclaim != 0) {
                db_gl_shadow_present_wait_slot_sync(slot);
            } else if (db_gl_shadow_present_slot_sync_pending_nonblocking(
                           slot) != 0) {
                return 0;
            }
        } else {
            db_gl_shadow_present_wait_slot_sync(slot);
        }
        void *mapped = db_gl_upload_stream_begin_write(
            &slot->stream, backend, 0U, slot->stream.active_bytes);
        if (mapped != NULL) {
            target->mode = DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO;
            target->pixel_surface.pixels = mapped;
            return 1;
        }
        if (slot->stream.last_failure_gl_error != GL_NO_ERROR) {
            db_gl_error_trace_t *const error_trace =
                &state->upload_trace.error_trace;
            if (error_trace->count < DB_GL_ERROR_TRACE_CAPACITY) {
                error_trace->records[error_trace->count++] =
                    (db_gl_error_record_t){
                        .error_code = slot->stream.last_failure_gl_error,
                        .phase = "shadow_full_upload",
                        .target = db_gl_upload_target_name(slot->stream.target),
                        .context = "begin_full_upload_target",
                    };
            }
        } else {
            (void)db_gl_error_trace_drain(
                &state->upload_trace.error_trace, "shadow_full_upload",
                db_gl_upload_target_name(slot->stream.target),
                "begin_full_upload_target");
        }
        if (slot->stream.client_storage != NULL) {
            target->mode =
                DB_GL_SHADOW_FULL_UPLOAD_TARGET_CLIENT_BUFFER_THEN_SUBDATA;
            target->pixel_surface.pixels = slot->stream.client_storage;
            db_gl_shadow_upload_trace_note_fallback(
                &state->upload_trace, "shadow_full_upload_client_fallback");
            return 1;
        }
        {
            const int first_fallback =
                DB_BOOL(state->upload_trace.fallback_mode_label == NULL);
            db_gl_shadow_upload_trace_note_fallback(
                &state->upload_trace, "shadow_full_upload_client_fallback");
            if (first_fallback != 0) {
                DB_RUNTIME_ERROR(
                    backend,
                    "shadow full upload PBO mapping failed; "
                    "forcing expensive client full-upload fallback "
                    "slot=%u bytes=%zu",
                    slot_index, slot->stream.active_bytes);
            }
        }
    }
    if (slot->stream.client_storage == NULL) {
        return 0;
    }
    target->mode = DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD;
    target->pixel_surface.pixels = slot->stream.client_storage;
    return 1;
}

int db_gl_shadow_present_begin_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_slot_contents,
    db_gl_shadow_present_full_upload_target_t *target) {
    return db_gl_shadow_present_begin_full_upload_target_slot_offset(
        state, backend, pixel_width, pixel_height, preserve_slot_contents, 0U,
        target);
}

int db_gl_shadow_present_finish_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target) {
    if ((state == NULL) || (target == NULL)) {
        return 0;
    }
    switch (target->mode) {
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO: {
        db_gl_shadow_present_upload_slot_t *const slot =
            db_gl_shadow_present_slot_or_null(state, target->slot_index);
        if (slot != NULL) {
            if (db_gl_upload_stream_end_write(&slot->stream,
                                              "shadow_present") == 0) {
                return 0;
            }
        }
        break;
    }
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_CLIENT_BUFFER_THEN_SUBDATA: {
        db_gl_shadow_present_upload_slot_t *const slot =
            db_gl_shadow_present_slot_or_null(state, target->slot_index);
        if ((slot != NULL) && (slot->stream.client_storage != NULL)) {
            if (db_gl_upload_stream_write(&slot->stream, "shadow_present",
                                          slot->stream.client_storage,
                                          slot->stream.active_bytes, 0U,
                                          slot->stream.active_bytes) == 0) {
                return 0;
            }
        }
        break;
    }
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD:
    case DB_GL_SHADOW_FULL_UPLOAD_TARGET_NONE:
        break;
    }
    db_gl_shadow_present_upload_slot_t *const slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
    }
    return 1;
}

void db_gl_set_viewport_px(int width_px, int height_px) {
    if ((width_px <= 0) || (height_px <= 0) ||
        (g_upload_proc_table.viewport == NULL)) {
        return;
    }
    g_upload_proc_table.viewport(0, 0, width_px, height_px);
}
