#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_types.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include "renderer_gl_shadow_present_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint32_t
db_gl_shadow_present_pixel_bytes(const db_gl_shadow_present_state_t *state) {
    if ((state != NULL) && (state->selected_texture_format ==
                            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)) {
        return (uint32_t)(sizeof(uint16_t) * 4U);
    }
    return 4U;
}

static void db_gl_shadow_present_delete_slot_sync(
    db_gl_shadow_present_upload_slot_t *slot) {
    if (slot == NULL) {
        return;
    }
    if ((slot->stream.in_flight_sync != NULL) &&
        (g_upload_proc_table.delete_sync != NULL)) {
        g_upload_proc_table.delete_sync((GLsync)slot->stream.in_flight_sync);
        slot->stream.in_flight_sync = NULL;
    }
}

static void
db_gl_shadow_present_wait_slot_sync(db_gl_shadow_present_upload_slot_t *slot) {
    if (slot == NULL) {
        return;
    }
    db_gl_upload_stream_wait(&slot->stream);
}

static void
db_gl_shadow_present_clear_slot_sync(db_gl_shadow_present_upload_slot_t *slot) {
    db_gl_shadow_present_delete_slot_sync(slot);
}

static int db_gl_shadow_present_slot_sync_pending_nonblocking(
    db_gl_shadow_present_upload_slot_t *slot) {
    if ((slot == NULL) || (slot->stream.in_flight_sync == NULL) ||
        (db_gl_stream_upload_sync_enabled(&slot->stream.capability) == 0)) {
        return 0;
    }
    if ((g_upload_proc_table.client_wait_sync == NULL) ||
        (g_upload_proc_table.delete_sync == NULL)) {
        return 1;
    }
    const GLenum result = g_upload_proc_table.client_wait_sync(
        (GLsync)slot->stream.in_flight_sync, 0U, 0U);
    if ((result == GL_ALREADY_SIGNALED) || (result == GL_CONDITION_SATISFIED) ||
        (result == GL_WAIT_FAILED)) {
        db_gl_shadow_present_clear_slot_sync(slot);
        return 0;
    }
    return 1;
}

static uint32_t
db_gl_shadow_present_ring_busy_mask(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return 0U;
    }
    uint32_t busy_mask = 0U;
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        db_gl_shadow_present_upload_slot_t *slot =
            db_gl_shadow_present_slot_or_null(state, i);
        if (db_gl_shadow_present_slot_sync_pending_nonblocking(slot) != 0) {
            busy_mask |= (1U << i);
        }
    }
    return busy_mask;
}

enum {
    DB_GL_SHADOW_PRESENT_MAX_TRACKED_RING_SLOTS = 31U,
};

uint32_t db_gl_shadow_present_active_slot_count(
    db_gl_shadow_present_preserve_mode_t preserve_mode) {
    return (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) ? 2U
                                                                          : 1U;
}

int db_gl_shadow_present_choose_full_upload_slot(
    const db_gl_shadow_present_state_t *state, int preserve_contents,
    uint32_t slot_offset, uint32_t busy_mask,
    db_gl_shadow_present_full_upload_slot_choice_t *out) {
    if ((state == NULL) || (out == NULL) || (state->slot_count == 0U)) {
        return 0;
    }
    *out = (db_gl_shadow_present_full_upload_slot_choice_t){0};
    if ((preserve_contents != 0) &&
        (state->preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) &&
        (slot_offset == 0U)) {
        db_gl_shadow_present_slot_acquire_t acquire = {0};
        if (db_gl_shadow_present_choose_ring_write_slot(state, busy_mask,
                                                        &acquire) == 0) {
            return 0;
        }
        out->slot_index = acquire.slot_index;
        out->requires_blocking_reclaim = acquire.requires_blocking_reclaim;
        return 1;
    }
    if ((preserve_contents != 0) &&
        (state->preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE)) {
        out->slot_index = state->present_slot_index % state->slot_count;
        return 1;
    }
    out->slot_index =
        (state->write_slot_index + slot_offset) % state->slot_count;
    return 1;
}

uint32_t db_gl_shadow_present_next_write_slot_after_present(
    db_gl_shadow_present_preserve_mode_t preserve_mode, int preserve_contents,
    uint32_t target_slot_index, uint32_t slot_count) {
    if (slot_count == 0U) {
        return 0U;
    }
    if ((preserve_mode == DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS) ||
        ((preserve_contents != 0) &&
         (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE))) {
        return target_slot_index % slot_count;
    }
    return (target_slot_index + 1U) % slot_count;
}

static db_gl_present_buffer_mode_t db_gl_shadow_present_requested_buffer_mode(
    db_gl_shadow_present_preserve_mode_t preserve_mode) {
    if (preserve_mode == DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS) {
        return DB_GL_PRESENT_BUFFER_MODE_REPLACE;
    }
    if (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) {
        return DB_GL_PRESENT_BUFFER_MODE_RING;
    }
    return DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE;
}

static const char *db_gl_shadow_present_fallback_reason(
    const db_gl_shadow_present_state_t *state) {
    if ((state == NULL) ||
        (state->requested_preserve_mode == state->preserve_mode)) {
        return NULL;
    }
    if (db_gl_stream_upload_uses_buffer_object(
            &state->effective_full_upload_capability) != 0) {
        return "preserve ring unavailable";
    }
    return "client upload fallback";
}

int db_gl_shadow_present_choose_ring_write_slot(
    const db_gl_shadow_present_state_t *state, uint32_t busy_mask,
    db_gl_shadow_present_slot_acquire_t *out) {
    if ((state == NULL) || (out == NULL) || (state->slot_count == 0U) ||
        (state->slot_count > DB_GL_SHADOW_PRESENT_MAX_TRACKED_RING_SLOTS)) {
        return 0;
    }
    *out = (db_gl_shadow_present_slot_acquire_t){0};
    const uint32_t preferred = state->write_slot_index % state->slot_count;

    for (uint32_t pass = 0U; pass < 2U; pass++) {
        for (uint32_t step = 0U; step < state->slot_count; step++) {
            const uint32_t slot_index = (preferred + step) % state->slot_count;
            if ((busy_mask & (1U << slot_index)) != 0U) {
                continue;
            }
            const db_gl_shadow_present_upload_slot_t *slot =
                db_gl_shadow_present_slot_const_or_null(state, slot_index);
            if (slot == NULL) {
                continue;
            }
            if ((pass == 0U) && !((slot->slot_valid != 0) &&
                                  (slot->slot_matches_shadow != 0))) {
                continue;
            }
            out->slot_index = slot_index;
            out->reason =
                (pass == 0U) ? "ready_shadow_slot" : "ready_ring_slot";
            return 1;
        }
    }

    out->slot_index = state->present_slot_index % state->slot_count;
    out->fallback_to_single_source = 1;
    out->requires_blocking_reclaim =
        ((busy_mask & (1U << out->slot_index)) != 0U) ? 1 : 0;
    out->reason = "all_ring_slots_busy";
    return 1;
}

static void db_gl_shadow_present_mark_all_slot_surfaces_invalid(
    db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].slot_valid = 0;
        state->upload_slots[i].slot_matches_shadow = 0;
        state->upload_slots[i].slot_matches_presented_texture = 0;
    }
}

static void db_gl_shadow_present_clear_presented_texture_matches(
    db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].slot_matches_presented_texture = 0;
    }
}

static void
db_gl_shadow_present_clear_shadow_matches(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].slot_matches_shadow = 0;
    }
}

static int db_gl_shadow_present_runtime_mode_is_explicit(void) {
    const char *runtime_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE);
    return ((runtime_mode != NULL) && (runtime_mode[0] != '\0') &&
            (strcmp(runtime_mode, "auto") != 0))
               ? 1
               : 0;
}

static void
db_gl_shadow_present_refresh_effective_mode(db_gl_shadow_present_state_t *state,
                                            int invalidate_slots) {
    if (state == NULL) {
        return;
    }
    const db_gl_present_buffer_mode_t requested_mode =
        db_gl_shadow_present_requested_buffer_mode(
            state->requested_preserve_mode);
    db_gl_present_mode_resolution_t resolution = {0};
    db_gl_present_mode_resolve(
        &(db_gl_present_mode_request_t){
            .requested_backbuffer_draw_mode = DB_GL_BACKBUFFER_DRAW_FULL,
            .requested_present_buffer_mode = requested_mode,
            .prefer_ring_for_preserved_draw = 1,
            .preserved_framebuffer_count =
                DB_GL_SHADOW_PRESENT_UPLOAD_RING_SLOTS,
            .present_upload = state->effective_full_upload_capability,
        },
        &resolution);
    const db_gl_shadow_present_preserve_mode_t previous_mode =
        state->preserve_mode;
    const uint32_t previous_slot_count = state->slot_count;
    state->preserve_mode = resolution.effective_preserve_mode;
    state->slot_count =
        db_gl_shadow_present_active_slot_count(state->preserve_mode);
    state->effective_full_upload_capability =
        resolution.effective_present_upload;
    state->unpack_stream.capability =
        state->effective_partial_upload_capability;
    db_gl_stream_upload_disable_persistent_for_target(
        &state->unpack_stream.capability, state->unpack_stream.target);
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].stream.capability =
            state->effective_full_upload_capability;
        db_gl_stream_upload_disable_persistent_for_target(
            &state->upload_slots[i].stream.capability,
            state->upload_slots[i].stream.target);
    }
    if ((db_gl_shadow_present_runtime_mode_is_explicit() != 0) &&
        (resolution.downgraded != 0)) {
        db_failf("renderer_gl_shadow_present",
                 "explicit present buffer mode cannot be honored: requested=%s "
                 "effective=%s reason=%s",
                 db_gl_shadow_present_preserve_mode_name(
                     resolution.requested_preserve_mode),
                 db_gl_shadow_present_preserve_mode_name(
                     resolution.effective_preserve_mode),
                 (resolution.reason != NULL) ? resolution.reason
                                             : "unspecified");
    }
    if ((invalidate_slots == 0) && (previous_mode == state->preserve_mode) &&
        (previous_slot_count == state->slot_count)) {
        return;
    }
    db_gl_shadow_present_clear_presented_texture_matches(state);
    db_gl_shadow_present_clear_shadow_matches(state);
    if (invalidate_slots != 0) {
        db_gl_shadow_present_mark_all_slot_surfaces_invalid(state);
        state->texture_valid = 0;
        state->texture_needs_full_upload = 1;
    }
    if (state->slot_count > 0U) {
        state->write_slot_index = state->present_slot_index % state->slot_count;
    } else {
        state->write_slot_index = 0U;
        state->present_slot_index = 0U;
    }
}

static void db_gl_shadow_present_mark_upload_surfaces_invalid(
    db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->texture_valid = 0;
    state->texture_needs_full_upload = 1;
    db_gl_shadow_present_mark_all_slot_surfaces_invalid(state);
}

db_gl_shadow_present_upload_slot_t *
db_gl_shadow_present_slot_or_null(db_gl_shadow_present_state_t *state,
                                  uint32_t slot_index) {
    if ((state == NULL) || (slot_index >= state->slot_count)) {
        return NULL;
    }
    return &state->upload_slots[slot_index];
}

const db_gl_shadow_present_upload_slot_t *
db_gl_shadow_present_slot_const_or_null(
    const db_gl_shadow_present_state_t *state, uint32_t slot_index) {
    if ((state == NULL) || (slot_index >= state->slot_count)) {
        return NULL;
    }
    return &state->upload_slots[slot_index];
}

int db_gl_shadow_present_texture_slot_index(
    const db_gl_shadow_present_state_t *state, uint32_t *out_slot_index) {
    if ((state == NULL) || (out_slot_index == NULL)) {
        return 0;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        if (state->upload_slots[i].slot_matches_presented_texture != 0) {
            *out_slot_index = i;
            return 1;
        }
    }
    return 0;
}

void db_gl_shadow_present_mark_texture_slot(db_gl_shadow_present_state_t *state,
                                            uint32_t slot_index) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].slot_matches_presented_texture =
            (i == slot_index) ? 1 : 0;
    }
}

void db_gl_shadow_present_init_runtime(db_gl_shadow_present_state_t *state,
                                       int prefer_unpack_pbo,
                                       int selected_content_uses_rgba16f) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    const int runtime_supports_hdr_present =
        (db_gl_context_probe_texture_float_present_support() != 0) ? 1 : 0;
    const db_gl_shadow_present_texture_format_t selected_texture_format =
        ((selected_content_uses_rgba16f != 0) &&
         (runtime_supports_hdr_present != 0))
            ? DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F
            : DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    const int supports_unpack_row_length_upload =
        (selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_probe_shadow_present_partial_upload_support_rgba16f()
            : db_gl_probe_shadow_present_partial_upload_support_rgba8();
    db_gl_upload_probe_result_t unpack_upload_probe = {0};
    const int sync_supported =
        ((g_upload_proc_table.fence_sync != NULL) &&
         (g_upload_proc_table.client_wait_sync != NULL) &&
         (g_upload_proc_table.delete_sync != NULL))
            ? 1
            : 0;
    if ((prefer_unpack_pbo != 0) &&
        (db_gl_context_has_pbo_upload_procs() != 0)) {
        db_gl_context_probe_stream_upload_capabilities(
            DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, DB_GL_PROBE_PREFIX_BYTES,
            &unpack_upload_probe);
    }
    *state = (db_gl_shadow_present_state_t){
        .initialized = 1,
        .backing_valid = 0,
        .texture_valid = 0,
        .texture_needs_full_upload = 1,
        .runtime_supports_unpack_row_length_upload =
            (supports_unpack_row_length_upload != 0) ? 1 : 0,
        .runtime_supports_hdr_present = runtime_supports_hdr_present,
        .uses_exact_size_texture =
            (db_gl_context_supports_shadow_present_exact_size_texture_2d() != 0)
                ? 1
                : 0,
        .requested_full_upload_capability =
            db_gl_stream_upload_capability_from_probe(
                DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &unpack_upload_probe,
                sync_supported),
        .effective_full_upload_capability =
            db_gl_stream_upload_capability_for_role(
                DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &unpack_upload_probe,
                sync_supported, DB_GL_UPLOAD_ROLE_PRESENT_FULL,
                selected_texture_format),
        .requested_partial_upload_capability =
            db_gl_stream_upload_capability_from_probe(
                DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &unpack_upload_probe,
                sync_supported),
        .effective_partial_upload_capability =
            db_gl_stream_upload_capability_for_role(
                DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, &unpack_upload_probe,
                sync_supported, DB_GL_UPLOAD_ROLE_PRESENT_PARTIAL,
                selected_texture_format),
        .slot_count = 1U,
        .write_slot_index = 0U,
        .present_slot_index = 0U,
        .selected_texture_format = selected_texture_format,
        .requested_preserve_mode =
            ((prefer_unpack_pbo != 0) &&
             (db_gl_context_has_pbo_upload_procs() != 0))
                ? DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT
                : DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE,
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE,
    };
    db_gl_upload_stream_init(&state->unpack_stream,
                             DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                             state->effective_partial_upload_capability, 0U, 1);
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        db_gl_upload_stream_init(&state->upload_slots[i].stream,
                                 DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                                 state->effective_full_upload_capability, 0U,
                                 1);
    }
    db_gl_shadow_present_refresh_effective_mode(state, 1);
    db_gl_quad_init(state->vertices);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;
    for (size_t i = 0U; i < (size_t)DB_RECT_VERTEX_COUNT; i++) {
        const size_t base = i * 4U;
        state->colors[base + 0U] = 1.0F;
        state->colors[base + 1U] = 1.0F;
        state->colors[base + 2U] = 1.0F;
        state->colors[base + 3U] = 1.0F;
    }
}

void db_gl_shadow_present_set_preserve_mode(
    db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_preserve_mode_t preserve_mode) {
    if (state == NULL) {
        return;
    }
    db_gl_shadow_present_preserve_mode_t requested = preserve_mode;
    db_gl_present_buffer_mode_t runtime_requested =
        DB_GL_PRESENT_BUFFER_MODE_AUTO;
    const char *runtime_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE);
    if (db_gl_present_buffer_mode_parse(runtime_mode, &runtime_requested) !=
        0) {
        switch (runtime_requested) {
        case DB_GL_PRESENT_BUFFER_MODE_REPLACE:
            requested = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS;
            break;
        case DB_GL_PRESENT_BUFFER_MODE_RING:
            requested = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT;
            break;
        case DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE:
            requested = DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
            break;
        case DB_GL_PRESENT_BUFFER_MODE_AUTO:
        default:
            break;
        }
    }
    state->requested_preserve_mode = requested;
    db_gl_shadow_present_refresh_effective_mode(state, 0);
}

void db_gl_shadow_present_invalidate_presented_texture(
    db_gl_shadow_present_state_t *state, int full_upload_required) {
    if (state == NULL) {
        return;
    }
    state->texture_valid = 0;
    if (full_upload_required != 0) {
        state->texture_needs_full_upload = 1;
    }
    db_gl_shadow_present_clear_presented_texture_matches(state);
}

void db_gl_shadow_present_note_shadow_change(
    db_gl_shadow_present_state_t *state, int full_upload_required) {
    if (state == NULL) {
        return;
    }
    db_gl_shadow_present_clear_shadow_matches(state);
    db_gl_shadow_present_invalidate_presented_texture(state,
                                                      full_upload_required);
}

void db_gl_shadow_present_log_decision(
    const char *backend, const char *present_name, int content_uses_rgba16f,
    int hdr_explicit_requested, const db_gl_shadow_present_state_t *state) {
    if ((backend == NULL) || (present_name == NULL) || (state == NULL)) {
        return;
    }
    const char *const texture_size_mode =
        (state->uses_exact_size_texture != 0) ? "exact_size" : "pow2_fallback";
    const char *const partial_upload_mode =
        (state->runtime_supports_unpack_row_length_upload != 0)
            ? "row_length"
            : "rowwise_fallback";
    const char *fallback_reason = db_gl_shadow_present_fallback_reason(state);
    const char *preserve_mode_name =
        db_gl_shadow_present_preserve_mode_name(state->preserve_mode);
    const char *requested_full_present_upload = db_gl_stream_upload_name(
        &state->requested_full_upload_capability, 0, 1);
    const char *effective_full_present_upload = db_gl_stream_upload_name(
        &state->effective_full_upload_capability, 0, 1);
    const char *requested_partial_present_upload = db_gl_stream_upload_name(
        &state->requested_partial_upload_capability, 0, 1);
    const char *effective_partial_present_upload = db_gl_stream_upload_name(
        &state->effective_partial_upload_capability, 0, 1);
    if (state->selected_texture_format ==
        DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
        db_infof(backend,
                 "%s hdr=enabled reason=float texture present probe passed, "
                 "hdr_explicit=%s, texture_sizing=%s, partial_upload=%s, "
                 "requested_full_present_upload=%s, "
                 "effective_full_present_upload=%s, "
                 "requested_partial_present_upload=%s, "
                 "effective_partial_present_upload=%s, preserve=%s%s%s",
                 present_name, (hdr_explicit_requested != 0) ? "yes" : "no",
                 texture_size_mode, partial_upload_mode,
                 requested_full_present_upload, effective_full_present_upload,
                 requested_partial_present_upload,
                 effective_partial_present_upload, preserve_mode_name,
                 (fallback_reason != NULL) ? ", fallback_reason=" : "",
                 (fallback_reason != NULL) ? fallback_reason : "");
        return;
    }
    if (content_uses_rgba16f == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=content prefers sdr backbuffer, "
                 "texture_sizing=%s, partial_upload=%s, "
                 "requested_full_present_upload=%s, "
                 "effective_full_present_upload=%s, "
                 "requested_partial_present_upload=%s, "
                 "effective_partial_present_upload=%s, preserve=%s%s%s",
                 present_name, texture_size_mode, partial_upload_mode,
                 requested_full_present_upload, effective_full_present_upload,
                 requested_partial_present_upload,
                 effective_partial_present_upload, preserve_mode_name,
                 (fallback_reason != NULL) ? ", fallback_reason=" : "",
                 (fallback_reason != NULL) ? fallback_reason : "");
        return;
    }
    if (state->runtime_supports_hdr_present == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=float texture present probe failed, "
                 "falling back to rgba8, texture_sizing=%s, partial_upload=%s, "
                 "requested_full_present_upload=%s, "
                 "effective_full_present_upload=%s, "
                 "requested_partial_present_upload=%s, "
                 "effective_partial_present_upload=%s, preserve=%s%s%s",
                 present_name, texture_size_mode, partial_upload_mode,
                 requested_full_present_upload, effective_full_present_upload,
                 requested_partial_present_upload,
                 effective_partial_present_upload, preserve_mode_name,
                 (fallback_reason != NULL) ? ", fallback_reason=" : "",
                 (fallback_reason != NULL) ? fallback_reason : "");
        return;
    }
    db_infof(backend,
             "%s hdr=disabled reason=present texture did not select rgba16f "
             "despite supported float present probe, texture_sizing=%s, "
             "partial_upload=%s, requested_full_present_upload=%s, "
             "effective_full_present_upload=%s, "
             "requested_partial_present_upload=%s, "
             "effective_partial_present_upload=%s, preserve=%s%s%s",
             present_name, texture_size_mode, partial_upload_mode,
             requested_full_present_upload, effective_full_present_upload,
             requested_partial_present_upload, effective_partial_present_upload,
             preserve_mode_name,
             (fallback_reason != NULL) ? ", fallback_reason=" : "",
             (fallback_reason != NULL) ? fallback_reason : "");
}

void db_gl_shadow_present_shutdown(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    db_gl_upload_stream_shutdown(&state->unpack_stream);
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        db_gl_shadow_present_upload_slot_t *const slot =
            &state->upload_slots[i];
        db_gl_shadow_present_delete_slot_sync(slot);
        db_gl_upload_stream_shutdown(&slot->stream);
        slot->slot_valid = 0;
        slot->slot_matches_shadow = 0;
        slot->slot_matches_presented_texture = 0;
    }
    db_gl_texture_delete_if_valid(&state->texture);
    *state = (db_gl_shadow_present_state_t){0};
}

void db_gl_shadow_present_prepare_texture(db_gl_shadow_present_state_t *state,
                                          const char *backend,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height) {
    if ((state == NULL) || (backend == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    const uint32_t target_width = (state->uses_exact_size_texture != 0)
                                      ? pixel_width
                                      : db_u32_next_pow2(pixel_width);
    const uint32_t target_height = (state->uses_exact_size_texture != 0)
                                       ? pixel_height
                                       : db_u32_next_pow2(pixel_height);
    const int content_size_changed = (state->content_width != pixel_width) ||
                                     (state->content_height != pixel_height);
    const int needs_recreate = (state->texture == 0U) ||
                               (state->texture_width < target_width) ||
                               (state->texture_height < target_height);
    if (needs_recreate == 0) {
        if (content_size_changed != 0) {
            state->content_width = pixel_width;
            state->content_height = pixel_height;
            db_gl_shadow_present_mark_upload_surfaces_invalid(state);
        }
        return;
    }

    // Texture allocation with NULL pixels must not inherit a bound unpack PBO.
    if (state->unpack_stream.buffer != 0U) {
        db_gl_upload_state_reset_unpack();
    }

    state->texture_width = target_width;
    state->texture_height = target_height;
    state->content_width = pixel_width;
    state->content_height = pixel_height;
    if (state->texture != 0U) {
        db_gl_texture_delete_if_valid(&state->texture);
    }
    const int created =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_texture_create_rgba16f(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL)
            : db_gl_texture_create_rgba8(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL);
    if (created == 0) {
        db_failf(backend, "failed to create shared shadow texture");
    }
    db_gl_shadow_present_mark_upload_surfaces_invalid(state);
}

void db_gl_set_unpack_row_length_pixels(int pixel_count) {
    db_gl_load_upload_proc_table();
    if ((g_unpack_row_length_state_valid != 0) &&
        (g_unpack_row_length_state == pixel_count)) {
        return;
    }
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ROW_LENGTH, pixel_count);
        g_unpack_row_length_state = pixel_count;
        g_unpack_row_length_state_valid = 1;
    }
}

static int db_gl_shadow_present_prepare_full_upload_storage(
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
        slot->stream.capability = state->effective_full_upload_capability;
        db_gl_stream_upload_disable_persistent_for_target(
            &slot->stream.capability, slot->stream.target);
        const int size_changed =
            (slot->stream.active_bytes != total_bytes) ? 1 : 0;
        if (size_changed != 0) {
            slot->stream.active_bytes = total_bytes;
            slot->slot_valid = 0;
            slot->slot_matches_shadow = 0;
            slot->slot_matches_presented_texture = 0;
            db_gl_shadow_present_delete_slot_sync(slot);
        }
        if (db_gl_upload_stream_prepare_storage(&slot->stream, backend,
                                                total_bytes) == 0) {
            db_gl_stream_upload_force_client_fallback(
                &state->effective_full_upload_capability, 1);
            db_gl_shadow_present_refresh_effective_mode(state, 1);
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
    state->unpack_stream.capability =
        state->effective_partial_upload_capability;
    db_gl_stream_upload_disable_persistent_for_target(
        &state->unpack_stream.capability, state->unpack_stream.target);
    if (db_gl_upload_stream_prepare_storage(&state->unpack_stream, backend,
                                            required_bytes) == 0) {
        db_gl_stream_upload_force_client_fallback(
            &state->effective_partial_upload_capability, 1);
        state->unpack_stream.capability =
            state->effective_partial_upload_capability;
        db_gl_stream_upload_disable_persistent_for_target(
            &state->unpack_stream.capability, state->unpack_stream.target);
        return db_gl_upload_stream_prepare_storage(&state->unpack_stream,
                                                   backend, required_bytes);
    }
    return 1;
}

static int db_gl_shadow_present_begin_full_upload_target_impl(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_contents,
    uint32_t slot_offset, db_gl_shadow_present_full_upload_target_t *target) {
    if ((state == NULL) || (backend == NULL) || (target == NULL) ||
        (pixel_width == 0U) || (pixel_height == 0U)) {
        return 0;
    }
    *target = (db_gl_shadow_present_full_upload_target_t){0};
    db_gl_shadow_present_prepare_texture(state, backend, pixel_width,
                                         pixel_height);
    if (db_gl_shadow_present_prepare_full_upload_storage(
            state, backend, pixel_width, pixel_height) == 0) {
        return 0;
    }
    const uint32_t pixel_bytes = db_gl_shadow_present_pixel_bytes(state);
    target->pixel_surface.pixel_width = pixel_width;
    target->pixel_surface.pixel_height = pixel_height;
    target->pixel_surface.uses_rgba16f =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? 1
            : 0;
    target->preserve_contents = (preserve_contents != 0) ? 1 : 0;
    db_gl_shadow_present_full_upload_slot_choice_t choice = {0};
    if (db_gl_shadow_present_choose_full_upload_slot(
            state, preserve_contents, slot_offset,
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
    slot->stream.capability = state->effective_full_upload_capability;
    db_gl_stream_upload_disable_persistent_for_target(&slot->stream.capability,
                                                      slot->stream.target);
    if ((db_gl_stream_upload_uses_buffer_object(&slot->stream.capability) !=
         0) &&
        (slot->stream.buffer != 0U) &&
        (g_upload_proc_table.bind_buffer != NULL)) {
        if ((preserve_contents != 0) &&
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
            &slot->stream, 0U, slot->stream.active_bytes);
        if (mapped != NULL) {
            target->uses_pbo = 1;
            if (pixel_bytes == (uint32_t)(sizeof(uint16_t) * 4U)) {
                target->pixel_surface.pixels_rgba16f = (uint16_t *)mapped;
            } else {
                target->pixel_surface.pixels_rgba8 = (uint32_t *)mapped;
            }
            return 1;
        }
        db_gl_stream_upload_force_client_fallback(
            &state->effective_full_upload_capability, 1);
        db_gl_shadow_present_refresh_effective_mode(state, 1);
        if (db_gl_shadow_present_prepare_full_upload_storage(
                state, backend, pixel_width, pixel_height) == 0) {
            return 0;
        }
    }
    if (slot->stream.client_storage == NULL) {
        return 0;
    }
    if (pixel_bytes == (uint32_t)(sizeof(uint16_t) * 4U)) {
        target->pixel_surface.pixels_rgba16f =
            (uint16_t *)slot->stream.client_storage;
    } else {
        target->pixel_surface.pixels_rgba8 =
            (uint32_t *)slot->stream.client_storage;
    }
    return 1;
}

int db_gl_shadow_present_begin_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_contents,
    db_gl_shadow_present_full_upload_target_t *target) {
    return db_gl_shadow_present_begin_full_upload_target_impl(
        state, backend, pixel_width, pixel_height, preserve_contents, 0U,
        target);
}

int db_gl_shadow_present_begin_full_upload_target_slot_offset(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_contents,
    uint32_t slot_offset, db_gl_shadow_present_full_upload_target_t *target) {
    return db_gl_shadow_present_begin_full_upload_target_impl(
        state, backend, pixel_width, pixel_height, preserve_contents,
        slot_offset, target);
}

void db_gl_shadow_present_finish_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target) {
    if ((state == NULL) || (target == NULL)) {
        return;
    }
    if (target->uses_pbo != 0) {
        db_gl_shadow_present_upload_slot_t *const slot =
            db_gl_shadow_present_slot_or_null(state, target->slot_index);
        if (slot != NULL) {
            db_gl_upload_stream_end_write(&slot->stream);
        }
    }
    db_gl_shadow_present_upload_slot_t *const slot =
        db_gl_shadow_present_slot_or_null(state, target->slot_index);
    if (slot != NULL) {
        slot->slot_valid = 1;
    }
}

void db_gl_set_viewport_px(int width_px, int height_px) {
    if ((width_px <= 0) || (height_px <= 0) ||
        (g_upload_proc_table.viewport == NULL)) {
        return;
    }
    g_upload_proc_table.viewport(0, 0, width_px, height_px);
}
