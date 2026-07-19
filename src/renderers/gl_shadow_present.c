#include "../config/runtime_options.h"
#include "../core/db_numeric.h"
#include "../core/db_progress_policy.h"
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

void db_gl_shadow_present_delete_slot_sync(
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

void db_gl_shadow_present_wait_slot_sync(
    db_gl_shadow_present_upload_slot_t *slot) {
    if (slot == NULL) {
        return;
    }
    if (db_gl_upload_stream_wait(&slot->stream) == 0) {
        DB_RUNTIME_FAIL("shadow_present", "upload slot reuse timed out");
    }
}

static void
db_gl_shadow_present_clear_slot_sync(db_gl_shadow_present_upload_slot_t *slot) {
    db_gl_shadow_present_delete_slot_sync(slot);
}

int db_gl_shadow_present_slot_sync_pending_nonblocking(
    db_gl_shadow_present_upload_slot_t *slot) {
    if ((slot == NULL) || (slot->stream.in_flight_sync == NULL) ||
        (db_gl_stream_upload_sync_enabled(&slot->stream.capability) == 0)) {
        return 0;
    }
    if ((g_upload_proc_table.client_wait_sync == NULL) ||
        (g_upload_proc_table.delete_sync == NULL)) {
        return 1;
    }
    const db_progress_outcome_t result = db_gl_upload_stream_probe_sync(
        slot->stream.in_flight_sync, DB_PROGRESS_GL_SHADOW_SLOT_PROBE);
    if (result.status == DB_PROGRESS_COMPLETED) {
        db_gl_shadow_present_clear_slot_sync(slot);
        return 0;
    }
    if (result.status == DB_PROGRESS_FAILED) {
        DB_RUNTIME_ERROR("shadow_present",
                         "GL slot sync probe failed operation=%s target=%s",
                         "slot_pending_probe",
                         db_gl_upload_target_name(slot->stream.target));
        db_gl_shadow_present_clear_slot_sync(slot);
        return 0;
    }
    return 1;
}

uint32_t
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
    DB_GL_SHADOW_PRESENT_MAX_TRACKED_RING_SLOTS =
        DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS,
};

static uint32_t gl_shadow_present_clamped_preserved_framebuffer_count(
    uint32_t preserved_framebuffer_count) {
    return DB_MIN(DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS,
                  preserved_framebuffer_count);
}

uint32_t db_gl_shadow_present_active_slot_count(
    db_gl_shadow_present_preserve_mode_t preserve_mode,
    uint32_t preserved_framebuffer_count) {
    if (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE) {
        return 1U;
    }
    if (preserve_mode == DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS) {
        return DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS;
    }
    return DB_MAX(2U, gl_shadow_present_clamped_preserved_framebuffer_count(
                          preserved_framebuffer_count));
}

uint32_t db_gl_shadow_present_required_previous_frames(
    const db_gl_shadow_present_state_t *state) {
    if ((state == NULL) ||
        (state->preserve_mode != DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) ||
        (state->slot_count <= 1U)) {
        return 0U;
    }
    return state->slot_count - 1U;
}

int db_gl_shadow_present_choose_full_upload_slot(
    const db_gl_shadow_present_state_t *state, int preserve_slot_contents,
    uint32_t slot_offset, uint32_t busy_mask,
    db_gl_shadow_present_full_upload_slot_choice_t *out) {
    if ((state == NULL) || (out == NULL) || (state->slot_count == 0U)) {
        return 0;
    }
    *out = (db_gl_shadow_present_full_upload_slot_choice_t){0};
    if ((preserve_slot_contents != 0) &&
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
    if ((preserve_slot_contents != 0) &&
        (state->preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE)) {
        out->slot_index = state->present_slot_index % state->slot_count;
        return 1;
    }
    out->slot_index =
        (state->write_slot_index + slot_offset) % state->slot_count;
    return 1;
}

uint32_t db_gl_shadow_present_next_write_slot_after_present(
    db_gl_shadow_present_preserve_mode_t preserve_mode,
    int preserve_slot_contents, uint32_t target_slot_index,
    uint32_t slot_count) {
    if (slot_count == 0U) {
        return 0U;
    }
    if ((preserve_slot_contents != 0) &&
        (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE)) {
        return target_slot_index % slot_count;
    }
    return (target_slot_index + 1U) % slot_count;
}

static db_gl_present_buffer_mode_t gl_shadow_present_requested_buffer_mode(
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
            &state->upload_profile.effective_full) != 0) {
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
        DB_BOOL((busy_mask & (1U << out->slot_index)));
    out->reason = "all_ring_slots_busy";
    return 1;
}

static void gl_shadow_present_mark_all_slot_surfaces_invalid(
    db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].slot_valid = 0;
        state->upload_slots[i].slot_matches_shadow = 0;
    }
}

static void gl_shadow_present_clear_presented_texture_matches(
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

typedef struct {
    db_gl_shadow_present_preserve_mode_t preserve_mode;
    int explicit_mode;
} db_gl_shadow_present_runtime_policy_t;

static db_gl_shadow_present_runtime_policy_t
db_gl_shadow_present_resolve_runtime_policy(
    db_gl_shadow_present_preserve_mode_t default_mode) {
    db_gl_shadow_present_runtime_policy_t policy = {
        .preserve_mode = default_mode,
        .explicit_mode = 0,
    };
    const char *const runtime_text =
        db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE);
    db_gl_present_buffer_mode_t runtime_mode = DB_GL_PRESENT_BUFFER_MODE_AUTO;
    if (db_gl_present_buffer_mode_parse(runtime_text, &runtime_mode) == 0) {
        return policy;
    }
    policy.explicit_mode =
        DB_BOOL(runtime_mode != DB_GL_PRESENT_BUFFER_MODE_AUTO);
    switch (runtime_mode) {
    case DB_GL_PRESENT_BUFFER_MODE_REPLACE:
        policy.preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS;
        break;
    case DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE:
        policy.preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
        break;
    case DB_GL_PRESENT_BUFFER_MODE_RING:
        policy.preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT;
        break;
    case DB_GL_PRESENT_BUFFER_MODE_AUTO:
    default:
        break;
    }
    return policy;
}

void db_gl_shadow_present_refresh_effective_mode(
    db_gl_shadow_present_state_t *state, int invalidate_slots) {
    if (state == NULL) {
        return;
    }
    const db_gl_present_buffer_mode_t requested_mode =
        gl_shadow_present_requested_buffer_mode(state->requested_preserve_mode);
    db_gl_present_mode_resolution_t resolution = {0};
    db_gl_present_mode_resolve(
        &(db_gl_present_mode_request_t){
            .requested_backbuffer_draw_mode = DB_GL_BACKBUFFER_DRAW_FULL,
            .requested_present_buffer_mode = requested_mode,
            .prefer_ring_for_preserved_draw = 1,
            .preserved_framebuffer_count =
                state->requested_preserved_framebuffer_count,
            .present_upload = state->upload_profile.effective_full,
        },
        &resolution);
    const db_gl_shadow_present_preserve_mode_t previous_mode =
        state->preserve_mode;
    const uint32_t previous_slot_count = state->slot_count;
    state->preserve_mode = resolution.effective_preserve_mode;
    state->slot_count = db_gl_shadow_present_active_slot_count(
        state->preserve_mode, state->requested_preserved_framebuffer_count);
    state->upload_profile.effective_full = resolution.effective_present_upload;
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT; i++) {
        state->unpack_streams[i].capability =
            state->upload_profile.effective_partial;
        db_gl_stream_upload_disable_persistent_for_target(
            &state->unpack_streams[i].capability,
            state->unpack_streams[i].target);
    }
    for (uint32_t i = 0U; i < state->slot_count; i++) {
        state->upload_slots[i].stream.capability =
            state->upload_profile.effective_full;
        db_gl_stream_upload_disable_persistent_for_target(
            &state->upload_slots[i].stream.capability,
            state->upload_slots[i].stream.target);
    }
    if ((state->runtime_preserve_mode_explicit != 0) &&
        (resolution.downgraded != 0)) {
        DB_RUNTIME_FAIL(
            "renderer_gl_shadow_present",
            "explicit present buffer mode cannot be honored: requested=%s "
            "effective=%s reason=%s",
            db_gl_shadow_present_preserve_mode_name(
                resolution.requested_preserve_mode),
            db_gl_shadow_present_preserve_mode_name(
                resolution.effective_preserve_mode),
            (resolution.reason != NULL) ? resolution.reason : "unspecified");
    }
    if ((invalidate_slots == 0) && (previous_mode == state->preserve_mode) &&
        (previous_slot_count == state->slot_count)) {
        return;
    }
    gl_shadow_present_clear_presented_texture_matches(state);
    db_gl_shadow_present_clear_shadow_matches(state);
    if (invalidate_slots != 0) {
        gl_shadow_present_mark_all_slot_surfaces_invalid(state);
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

static void gl_shadow_present_mark_upload_surfaces_invalid(
    db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->texture_valid = 0;
    state->texture_needs_full_upload = 1;
    gl_shadow_present_mark_all_slot_surfaces_invalid(state);
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
            DB_BOOL(i == slot_index);
    }
}

void db_gl_shadow_present_init_runtime(
    db_gl_shadow_present_state_t *state, int prefer_unpack_pbo,
    int enable_full_upload_targets,
    const db_display_resolved_format_config_t *resolved_format,
    uint32_t preserved_framebuffer_count) {
    if ((state == NULL) || (resolved_format == NULL) ||
        (state->initialized != 0)) {
        return;
    }
    const int hdr_output_enabled = resolved_format->native_hdr_enabled;
    const db_gl_shadow_present_texture_format_t selected_texture_format =
        (hdr_output_enabled != 0)
            ? DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2
            : resolved_format->present_texture_format;
    const db_pixel_format_t upload_probe_format =
        (selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? DB_PIXEL_FORMAT_RGBA16F
            : DB_PIXEL_FORMAT_RGBA8;
    const int supports_unpack_row_length_upload =
        db_gl_probe_shadow_present_partial_upload_support(upload_probe_format);
    const db_gl_shadow_present_preserve_mode_t default_preserve_mode =
        ((prefer_unpack_pbo != 0) &&
         (db_gl_context_has_pbo_upload_procs() != 0))
            ? DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT
            : DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
    const db_gl_shadow_present_runtime_policy_t runtime_policy =
        db_gl_shadow_present_resolve_runtime_policy(default_preserve_mode);
    const int sync_supported = 1;
    const db_gl_stream_upload_capability_t unpack_probe_capability =
        ((prefer_unpack_pbo != 0) &&
         (db_gl_context_has_pbo_upload_procs() != 0))
            ? db_gl_stream_upload_capability_probe(
                  DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                  DB_GL_PROBE_PREFIX_BYTES, NULL, sync_supported)
            : (db_gl_stream_upload_capability_t){
                  .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                  .requested_storage =
                      DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
                  .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                  .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                  .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
                  .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                  .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
              };
    *state = (db_gl_shadow_present_state_t){
        .initialized = 1,
        .backing_valid = 0,
        .texture_valid = 0,
        .texture_needs_full_upload = 1,
        .runtime_supports_unpack_row_length_upload =
            DB_BOOL(supports_unpack_row_length_upload),
        .hdr_output_enabled = hdr_output_enabled,
        .encoded_present_format = resolved_format->encoded_present_format,
        .hdr_conversion = (hdr_output_enabled != 0)
                              ? DB_HDR_CONVERSION_CPU_FIXED_FUNCTION
                              : DB_HDR_CONVERSION_NONE,
        .uses_exact_size_texture = DB_BOOL(
            db_gl_context_supports_shadow_present_exact_size_texture_2d()),
        .upload_profile =
            {
                .requested_full = unpack_probe_capability,
                .effective_full = db_gl_stream_upload_capability_for_role(
                    &unpack_probe_capability, DB_GL_UPLOAD_ROLE_PRESENT_FULL),
                .requested_partial = unpack_probe_capability,
                .effective_partial = db_gl_stream_upload_capability_for_role(
                    &unpack_probe_capability,
                    DB_GL_UPLOAD_ROLE_PRESENT_PARTIAL),
            },
        .slot_count = DB_BOOL(enable_full_upload_targets != 0),
        .write_slot_index = 0U,
        .present_slot_index = 0U,
        .selected_texture_format = selected_texture_format,
        .requested_preserve_mode = runtime_policy.preserve_mode,
        .preserve_mode = DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE,
        .runtime_preserve_mode = runtime_policy.preserve_mode,
        .runtime_preserve_mode_explicit = runtime_policy.explicit_mode,
        .requested_preserved_framebuffer_count =
            gl_shadow_present_clamped_preserved_framebuffer_count(
                preserved_framebuffer_count),
    };
    state->upload_profile.requested_full.partial_updates_supported = 0;
    state->upload_profile.effective_full.partial_updates_supported = 0;
    state->upload_profile.requested_partial.partial_updates_supported =
        DB_BOOL(supports_unpack_row_length_upload);
    state->upload_profile.effective_partial.partial_updates_supported =
        DB_BOOL(supports_unpack_row_length_upload);
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT; i++) {
        db_gl_upload_stream_init(
            &state->unpack_streams[i], DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
            state->upload_profile.effective_partial, 0U, 1);
        (void)db_gl_upload_stream_create_owned_buffer(
            &state->unpack_streams[i], "renderer_gl_shadow_present");
    }
    if (enable_full_upload_targets != 0) {
        for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS; i++) {
            db_gl_upload_stream_init(&state->upload_slots[i].stream,
                                     DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                                     state->upload_profile.effective_full, 0U,
                                     1);
            (void)db_gl_upload_stream_create_owned_buffer(
                &state->upload_slots[i].stream, "renderer_gl_shadow_present");
        }
    }
    db_gl_shadow_present_refresh_effective_mode(state, 1);
    db_gl_quad_init(state->vertices);
    db_gl_geometry_stream_init_result_t quad_result = {0};
    if (db_gl_geometry_stream_init(&state->presentation_quad_stream,
                                   &quad_result, "renderer_gl_shadow_present",
                                   sizeof(state->vertices), state->vertices,
                                   state->vertices, sizeof(state->vertices), 0,
                                   1) != 0) {
        state->presentation_quad_uses_vbo =
            DB_BOOL(db_gl_stream_upload_uses_buffer_object(
                        &state->presentation_quad_stream.capability) != 0);
    }
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
    state->requested_preserve_mode =
        (state->runtime_preserve_mode_explicit != 0)
            ? state->runtime_preserve_mode
            : preserve_mode;
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
    gl_shadow_present_clear_presented_texture_matches(state);
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
    const char *backend, const char *present_name,
    const db_display_resolved_format_config_t *resolved_format,
    const db_gl_shadow_present_state_t *state) {
    if ((backend == NULL) || (present_name == NULL) ||
        (resolved_format == NULL) || (state == NULL)) {
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
    const char *requested_full_present_upload =
        db_gl_stream_upload_name(&state->upload_profile.requested_full, 0, 1);
    const char *effective_full_present_upload =
        db_gl_stream_upload_name(&state->upload_profile.effective_full, 0, 1);
    const char *requested_partial_present_upload = db_gl_stream_upload_name(
        &state->upload_profile.requested_partial, 0, 1);
    const char *effective_partial_present_upload = db_gl_stream_upload_name(
        &state->upload_profile.effective_partial, 0, 1);
    const db_log_field_t fields[] = {
        DB_LOG_STRING("present", present_name),
        DB_LOG_STRING(
            "working_format",
            db_pixel_format_name(resolved_format->surface_pixel_format)),
        DB_LOG_BOOL("native_hdr", resolved_format->native_hdr_enabled),
        DB_LOG_BOOL("hdr_content_supported",
                    resolved_format->hdr_content_supported),
        DB_LOG_TOKEN(
            "encoded_present_format",
            db_encoded_present_format_name(state->encoded_present_format)),
        DB_LOG_TOKEN("hdr_conversion", db_hdr_conversion_implementation_name(
                                           state->hdr_conversion)),
        DB_LOG_TOKEN("native_format",
                     db_native_output_format_name(
                         resolved_format->native_output_format)),
        DB_LOG_STRING("texture_sizing", texture_size_mode),
        DB_LOG_STRING("partial_upload", partial_upload_mode),
        DB_LOG_STRING("requested_full_upload", requested_full_present_upload),
        DB_LOG_STRING("effective_full_upload", effective_full_present_upload),
        DB_LOG_STRING("requested_partial_upload",
                      requested_partial_present_upload),
        DB_LOG_STRING("effective_partial_upload",
                      effective_partial_present_upload),
        DB_LOG_BOOL("pbo_supported",
                    state->upload_profile.requested_full.supported_storage ==
                        DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT),
        DB_LOG_BOOL(
            "mapping_probe_attempted",
            state->upload_profile.requested_full.mapping_probe_attempted),
        DB_LOG_BOOL("mapping_validated",
                    state->upload_profile.requested_full.mapping_validated),
        DB_LOG_BOOL("canary_validated",
                    state->upload_profile.requested_full.canary_validated),
        DB_LOG_TOKEN(
            "mapping_probe_failure",
            db_gl_upload_probe_step_name(state->upload_profile.requested_full
                                             .mapping_probe_failure_step)),
        DB_LOG_HEX64(
            "mapping_probe_gl_error",
            state->upload_profile.requested_full.mapping_probe_gl_error),
        DB_LOG_TOKEN("upload_demotion_reason",
                     db_gl_upload_failure_reason_name(
                         state->upload_profile.requested_full.demotion_reason)),
        DB_LOG_STRING("preserve", preserve_mode_name),
        DB_LOG_STRING("fallback_reason",
                      (fallback_reason != NULL) ? fallback_reason : "none"),
    };
    db_log_info(backend, "shadow_present_config", fields,
                DB_LOG_FIELD_COUNT(fields));
    db_gl_upload_stream_log_selection(&state->presentation_quad_stream, backend,
                                      "presentation_quad");
    db_gl_upload_stream_log_selection(&state->unpack_streams[0], backend,
                                      "partial_texture_upload");
    if ((state->upload_slots[0].stream.owns_storage != 0) &&
        (state->upload_slots[0].stream.target ==
         DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER)) {
        db_gl_upload_stream_log_selection(&state->upload_slots[0].stream,
                                          backend, "full_texture_upload");
    }
}

void db_gl_shadow_present_shutdown(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT; i++) {
        db_gl_upload_stream_shutdown(&state->unpack_streams[i]);
    }
    db_gl_upload_stream_shutdown(&state->presentation_quad_stream);
    for (uint32_t i = 0U; i < DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS; i++) {
        db_gl_shadow_present_upload_slot_t *const slot =
            &state->upload_slots[i];
        db_gl_shadow_present_delete_slot_sync(slot);
        db_gl_upload_stream_shutdown(&slot->stream);
        slot->slot_valid = 0;
        slot->slot_matches_shadow = 0;
        slot->slot_matches_presented_texture = 0;
    }
    db_gl_texture_delete_if_valid(&state->texture);
    free(state->encoded_upload_scratch);
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
    if ((target_width == 0U) || (target_height == 0U)) {
        DB_RUNTIME_FAIL(backend,
                        "shadow texture extent exceeds u32 power-of-two range");
    }
    const int content_size_changed = (state->content_width != pixel_width) ||
                                     (state->content_height != pixel_height);
    const int needs_recreate = (state->texture == 0U) ||
                               (state->texture_width < target_width) ||
                               (state->texture_height < target_height);
    if (needs_recreate == 0) {
        if (content_size_changed != 0) {
            state->content_width = pixel_width;
            state->content_height = pixel_height;
            gl_shadow_present_mark_upload_surfaces_invalid(state);
        }
        return;
    }

    // Texture allocation with NULL pixels must not inherit a bound unpack PBO.
    db_gl_upload_state_reset_all();
    db_gl_probe_drain_errors();

    state->texture_width = target_width;
    state->texture_height = target_height;
    state->content_width = pixel_width;
    state->content_height = pixel_height;
    if (state->texture != 0U) {
        db_gl_texture_delete_if_valid(&state->texture);
    }
    int created = 0;
    switch (state->selected_texture_format) {
    case DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2:
        created = db_gl_texture_create_rgb10a2_bt2020_pq(
            &state->texture, state->texture_width, state->texture_height, NULL);
        break;
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F:
        created = db_gl_texture_create_rgba16f(
            &state->texture, state->texture_width, state->texture_height, NULL);
        break;
    case DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8:
        created = db_gl_texture_create_rgba8(
            &state->texture, state->texture_width, state->texture_height, NULL);
        break;
    }
    if (created == 0) {
        db_gl_probe_drain_errors();
        DB_RUNTIME_FAIL(backend, "failed to create shared shadow texture");
    }
    gl_shadow_present_mark_upload_surfaces_invalid(state);
}
