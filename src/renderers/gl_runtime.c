#include "../core/db_core.h"
#include "../core/db_geometry.h"
#include "../core/db_hash.h"
#include "../core/db_log.h"
#include "../core/db_numeric.h"
#include "../core/db_poll_policy.h"
#include "core/db_format_contract.h"
#include "core/db_render_types.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 1) Version/extension parsing and capability advertisement.
int db_parse_gl_version_numbers(const char *version_text, int *major_out,
                                int *minor_out) {
    if ((version_text == NULL) || (major_out == NULL) || (minor_out == NULL)) {
        return 0;
    }

    const char *cursor = version_text;
    while ((*cursor != '\0') && ((*cursor < '0') || (*cursor > '9'))) {
        cursor++;
    }
    if (*cursor == '\0') {
        return 0;
    }

    const char *parse_end = NULL;
    long major_l = 0L;
    if ((db_parse_long_prefix(cursor, DB_PARSE_BASE_DECIMAL, &major_l,
                              &parse_end) == 0) ||
        (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    long minor_l = 0L;
    if (db_parse_long_prefix(minor_start, DB_PARSE_BASE_DECIMAL, &minor_l,
                             &parse_end) == 0) {
        return 0;
    }
    if ((major_l < 0L) || (minor_l < 0L) || (major_l > INT_MAX) ||
        (minor_l > INT_MAX)) {
        return 0;
    }

    *major_out =
        db_checked_long_to_int("db_parse_gl_version_numbers", "major", major_l);
    *minor_out =
        db_checked_long_to_int("db_parse_gl_version_numbers", "minor", minor_l);
    return 1;
}

int db_gl_version_text_at_least(const char *version_text, int req_major,
                                int req_minor) {
    int major = 0;
    int minor = 0;
    if (!db_parse_gl_version_numbers(version_text, &major, &minor)) {
        return 0;
    }
    return (major > req_major) ||
           ((major == req_major) && (minor >= req_minor));
}

int db_gl_is_es_context(const char *version_text) {
    return (version_text != NULL) &&
           (strstr(version_text, "OpenGL ES") != NULL);
}

db_gl_pixel_upload_payload_t
db_gl_pixel_upload_payload_from_surface(const db_pixel_surface_t *surface) {
    if (surface == NULL) {
        return (db_gl_pixel_upload_payload_t){0};
    }
    const size_t row_stride_bytes = db_checked_mul_size(
        "renderer_gl_runtime", "pixel_payload_row_stride",
        (size_t)surface->pixel_width, db_pixel_surface_pixel_bytes(surface));
    db_gl_pixel_upload_payload_t out = {0};
    out.surface = surface;
    out.format = (db_pixel_surface_uses_rgba16f(surface) != 0)
                     ? DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F
                     : DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    out.row_stride_bytes = row_stride_bytes;
    out.total_bytes = db_checked_mul_size(
        "renderer_gl_runtime", "pixel_payload_total_bytes", row_stride_bytes,
        db_checked_u32_to_size("renderer_gl_runtime", "pixel_payload_height",
                               surface->pixel_height));
    return out;
}

uint64_t db_gl_pixel_surface_hash_canonical(const db_pixel_surface_t *surface) {
    if ((surface == NULL) || (surface->pixels == NULL) ||
        (surface->pixel_width == 0U) || (surface->pixel_height == 0U)) {
        return 0U;
    }
    const size_t row_stride_bytes = db_checked_mul_size(
        "renderer_gl_runtime", "pixel_surface_hash_row_stride",
        db_checked_u32_to_size("renderer_gl_runtime", "pixel_surface_hash_w",
                               surface->pixel_width),
        db_pixel_surface_pixel_bytes(surface));
    return db_hash_working_rgba8(db_pixel_surface_bytes_const(surface),
                                 surface->format, surface->pixel_width,
                                 surface->pixel_height, row_stride_bytes, 1);
}

void db_gl_error_trace_reset(db_gl_error_trace_t *trace) {
    if (trace == NULL) {
        return;
    }
    *trace = (db_gl_error_trace_t){0};
}

size_t db_gl_error_trace_drain(db_gl_error_trace_t *trace, const char *phase,
                               const char *target, const char *context) {
    size_t drained_count = 0U;
    if (g_upload_proc_table.get_error == NULL) {
        return 0U;
    }
    const db_poll_policy_t *const policy =
        db_progress_policy_get(DB_PROGRESS_GL_ERROR_DRAIN);
    for (uint32_t index = 0U; index < policy->max_attempts; index++) {
        const uint32_t error_code = (uint32_t)g_upload_proc_table.get_error();
        if (error_code == (uint32_t)GL_NO_ERROR) {
            break;
        }
        if ((trace != NULL) && (trace->count < DB_GL_ERROR_TRACE_CAPACITY)) {
            trace->records[trace->count++] = (db_gl_error_record_t){
                .error_code = error_code,
                .phase = phase,
                .target = target,
                .context = context,
            };
        }
        drained_count++;
    }
    if (drained_count == policy->max_attempts) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "error_queue_limit"),
            DB_LOG_U64("drained_count", drained_count),
            DB_LOG_BOOL("truncated", 1),
            DB_LOG_STRING("phase", phase),
            DB_LOG_STRING("target", target),
            DB_LOG_STRING("context", context),
        };
        db_log_error("renderer_gl_runtime", "gl_error_drain", fields,
                     DB_LOG_FIELD_COUNT(fields));
    }
    return drained_count;
}

void db_gl_shadow_upload_trace_reset(db_gl_shadow_upload_trace_t *trace) {
    if (trace == NULL) {
        return;
    }
    *trace = (db_gl_shadow_upload_trace_t){0};
}

void db_gl_shadow_upload_trace_capture_pixel_payload(
    db_gl_shadow_upload_trace_t *trace,
    const db_gl_pixel_upload_payload_t *payload) {
    if ((trace == NULL) || (payload == NULL)) {
        return;
    }
    trace->pixel_payload = *payload;
    trace->pixel_width =
        (payload->surface != NULL) ? payload->surface->pixel_width : 0U;
    trace->pixel_height =
        (payload->surface != NULL) ? payload->surface->pixel_height : 0U;
    trace->pixel_payload.surface = NULL;
}

void db_gl_shadow_upload_trace_capture_full_upload_attempt(
    db_gl_shadow_upload_trace_t *trace, uint32_t slot_index, size_t total_bytes,
    const char *source_label, const char *target_mode_label,
    const char *upload_mode_label) {
    if (trace == NULL) {
        return;
    }
    trace->full_upload_attempted = 1;
    trace->slot_index = slot_index;
    trace->total_bytes = total_bytes;
    trace->source_label = source_label;
    trace->target_mode_label = target_mode_label;
    trace->upload_mode_label = upload_mode_label;
}

void db_gl_shadow_upload_trace_note_history(db_gl_shadow_upload_trace_t *trace,
                                            uint32_t required_previous_frames,
                                            size_t historical_block_count,
                                            size_t repair_block_count,
                                            const char *history_source_label) {
    if (trace == NULL) {
        return;
    }
    trace->required_previous_frames = required_previous_frames;
    trace->historical_block_count = historical_block_count;
    trace->repair_block_count = repair_block_count;
    trace->history_source_label = history_source_label;
}

void db_gl_shadow_upload_trace_note_fallback(db_gl_shadow_upload_trace_t *trace,
                                             const char *fallback_mode_label) {
    if (trace == NULL) {
        return;
    }
    trace->fallback_mode_label = fallback_mode_label;
}

void db_gl_shadow_upload_trace_note_execution(
    db_gl_shadow_upload_trace_t *trace,
    const char *executed_upload_mode_label) {
    if (trace == NULL) {
        return;
    }
    trace->full_upload_executed = 1;
    trace->executed_upload_mode_label = executed_upload_mode_label;
}

void db_gl_shadow_upload_trace_note_surface_hashes(
    db_gl_shadow_upload_trace_t *trace, uint64_t upload_source_hash,
    uint64_t target_surface_hash, uint64_t fallback_source_hash) {
    if (trace == NULL) {
        return;
    }
    trace->upload_source_hash = upload_source_hash;
    trace->target_surface_hash = target_surface_hash;
    trace->fallback_source_hash = fallback_source_hash;
}

void db_gl_shadow_upload_trace_note_seed(db_gl_shadow_upload_trace_t *trace,
                                         const char *seed_source_label) {
    if (trace == NULL) {
        return;
    }
    trace->seeded_shadow_ring = 1;
    trace->seed_source_label = seed_source_label;
}

void db_gl_shadow_upload_trace_capture_upload_span(
    db_gl_shadow_upload_trace_t *trace, const db_damage_block_t *block,
    size_t offset_bytes, size_t size_bytes, const char *source_label) {
    if ((trace == NULL) || (block == NULL) ||
        (trace->upload_span_count >= DB_GL_DIRTY_TRACE_UPLOAD_SPAN_CAPACITY)) {
        return;
    }
    trace->upload_spans[trace->upload_span_count] =
        (db_gl_upload_span_trace_t){0};
    trace->upload_spans[trace->upload_span_count].block = *block;
    trace->upload_spans[trace->upload_span_count].offset_bytes = offset_bytes;
    trace->upload_spans[trace->upload_span_count].size_bytes = size_bytes;
    trace->upload_spans[trace->upload_span_count].source_label = source_label;
    trace->upload_span_count++;
}

// 1) Capability utility helpers (error clear/probe pattern/capability strings).
void db_gl_probe_drain_errors(void) {
    (void)db_gl_error_trace_drain(NULL, "probe", "gl", "drain_errors");
}

int db_gl_probe_step_error_free(void) {
    return (db_gl_get_error_value() == GL_NO_ERROR);
}

int db_gl_probe_finish(int success) {
    db_gl_probe_drain_errors();
    return DB_BOOL(success);
}

static const uint8_t db_gl_probe_channel_high_threshold = 200U;
static const uint8_t db_gl_probe_channel_low_threshold = 80U;

int db_gl_probe_rgb_matches(const uint8_t *pixels, size_t offset, uint8_t red,
                            uint8_t green, uint8_t blue) {
    if (pixels == NULL) {
        return 0;
    }
    const uint8_t red_value = pixels[offset + 0U];
    const uint8_t green_value = pixels[offset + 1U];
    const uint8_t blue_value = pixels[offset + 2U];
    const int red_matches =
        (red != 0U) ? (red_value > db_gl_probe_channel_high_threshold)
                    : (red_value < db_gl_probe_channel_low_threshold);
    const int green_matches =
        (green != 0U) ? (green_value > db_gl_probe_channel_high_threshold)
                      : (green_value < db_gl_probe_channel_low_threshold);
    const int blue_matches =
        (blue != 0U) ? (blue_value > db_gl_probe_channel_high_threshold)
                     : (blue_value < db_gl_probe_channel_low_threshold);
    return red_matches && green_matches && blue_matches;
}

void db_gl_upload_probe_fill_pattern(uint8_t *pattern, size_t count) {
    if (pattern == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        pattern[i] = (uint8_t)(DB_GL_MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }
}

const char *db_gl_runtime_draw_mode_name(db_gl_runtime_draw_mode_t mode) {
    switch (mode) {
    case DB_GL_RUNTIME_DRAW_FF_RECT_FILL:
        return "ff_rect_fill";
    case DB_GL_RUNTIME_DRAW_DIRTY_REPLAY:
        return "dirty_replay";
    case DB_GL_RUNTIME_DRAW_SHADOW_FALLBACK:
        return "shadow_fallback";
    case DB_GL_RUNTIME_DRAW_FULL_PRESENT:
    default:
        return "full_present";
    }
}

const char *db_gl_shadow_present_preserve_mode_name(
    db_gl_shadow_present_preserve_mode_t preserve_mode) {
    switch (preserve_mode) {
    case DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS:
        return "replace";
    case DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE:
        return "single_source";
    case DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT:
        return "ring";
    default:
        return "unknown";
    }
}

const char *
db_gl_stream_upload_storage_name(db_gl_stream_upload_storage_t storage) {
    switch (storage) {
    case DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT:
        return "buffer_object";
    case DB_GL_STREAM_UPLOAD_STORAGE_CLIENT:
    default:
        return "client";
    }
}

const char *db_gl_upload_target_name(db_gl_upload_target_t target) {
    switch (target) {
    case DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER:
        return "pbo_unpack";
    case DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER:
        return "pbo_pack";
    case DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER:
    default:
        return "vbo_array";
    }
}

const char *db_gl_stream_upload_mode_name(db_gl_stream_upload_mode_t mode) {
    switch (mode) {
    case DB_GL_STREAM_UPLOAD_MODE_PERSISTENT:
        return "persistent";
    case DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE:
        return "map_range";
    case DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER:
        return "map_buffer";
    case DB_GL_STREAM_UPLOAD_MODE_SUB_DATA:
    default:
        return "sub_data";
    }
}

const char *
db_gl_upload_failure_reason_name(db_gl_upload_failure_reason_t reason) {
    switch (reason) {
    case DB_GL_UPLOAD_FAILURE_PROBE_REJECTED:
        return "probe_rejected";
    case DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC:
        return "storage_alloc";
    case DB_GL_UPLOAD_FAILURE_MAP_NULL:
        return "map_null";
    case DB_GL_UPLOAD_FAILURE_UNMAP_FAILED:
        return "unmap_failed";
    case DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE:
        return "api_unavailable";
    case DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE:
        return "target_acquire";
    case DB_GL_UPLOAD_FAILURE_CANARY_MISMATCH:
        return "canary_mismatch";
    case DB_GL_UPLOAD_FAILURE_NONE:
    default:
        return "none";
    }
}

const char *db_gl_upload_probe_step_name(db_gl_upload_probe_step_t step) {
    switch (step) {
    case DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE:
        return "map_range";
    case DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE_UNMAP:
        return "map_range_unmap";
    case DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER:
        return "map_buffer";
    case DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER_UNMAP:
        return "map_buffer_unmap";
    case DB_GL_UPLOAD_PROBE_STEP_CANARY_VERIFY:
        return "canary_verify";
    case DB_GL_UPLOAD_PROBE_STEP_RESTORE:
        return "restore";
    case DB_GL_UPLOAD_PROBE_STEP_NONE:
    default:
        return "none";
    }
}

const char *
db_gl_stream_upload_name(const db_gl_stream_upload_capability_t *capability,
                         int client_arrays, int upload_enabled) {
    if (upload_enabled == 0) {
        return "none";
    }
    if (client_arrays != 0) {
        return "client_arrays";
    }
    if (db_gl_stream_upload_uses_buffer_object(capability) == 0) {
        return "client_upload";
    }
    if (db_gl_stream_upload_uses_persistent(capability) != 0) {
        return "persistent";
    }
    if (db_gl_stream_upload_uses_map_range(capability) != 0) {
        return "map_range";
    }
    if (db_gl_stream_upload_uses_map_buffer(capability) != 0) {
        return "map_buffer";
    }
    return "buffer_object";
}

const char *db_gl_present_buffer_mode_name(db_gl_present_buffer_mode_t mode) {
    switch (mode) {
    case DB_GL_PRESENT_BUFFER_MODE_REPLACE:
        return "replace";
    case DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE:
        return "single_source";
    case DB_GL_PRESENT_BUFFER_MODE_RING:
        return "ring";
    case DB_GL_PRESENT_BUFFER_MODE_AUTO:
    default:
        return "auto";
    }
}

int db_gl_present_buffer_mode_parse(const char *text,
                                    db_gl_present_buffer_mode_t *out_mode) {
    if ((text == NULL) || (out_mode == NULL)) {
        return 0;
    }
    if (strcmp(text, "auto") == 0) {
        *out_mode = DB_GL_PRESENT_BUFFER_MODE_AUTO;
        return 1;
    }
    if (strcmp(text, "replace") == 0) {
        *out_mode = DB_GL_PRESENT_BUFFER_MODE_REPLACE;
        return 1;
    }
    if (strcmp(text, "single_source") == 0) {
        *out_mode = DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE;
        return 1;
    }
    if (strcmp(text, "ring") == 0) {
        *out_mode = DB_GL_PRESENT_BUFFER_MODE_RING;
        return 1;
    }
    return 0;
}

db_gl_stream_upload_capability_t db_gl_stream_upload_capability_for_role(
    const db_gl_stream_upload_capability_t *base_capability,
    db_gl_upload_role_t role) {
    db_gl_stream_upload_capability_t capability =
        (base_capability != NULL) ? *base_capability
                                  : (db_gl_stream_upload_capability_t){0};
    capability.partial_updates_supported =
        DB_BOOL(role != DB_GL_UPLOAD_ROLE_PRESENT_FULL);
    capability.alignment_bytes = DB_CACHELINE_ALIGNMENT_BYTES;
    return capability;
}

void db_gl_stream_upload_force_client_fallback(
    db_gl_stream_upload_capability_t *capability, int disable_sync) {
    if (capability == NULL) {
        return;
    }
    capability->effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT;
    capability->effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA;
    if (disable_sync != 0) {
        capability->sync_enabled = 0;
    }
}

int db_gl_stream_upload_demote(db_gl_stream_upload_capability_t *capability,
                               db_gl_upload_failure_reason_t reason,
                               int disable_sync) {
    if (capability == NULL) {
        return 0;
    }
    capability->demotion_reason = reason;
    if (capability->effective_storage !=
        DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT) {
        if (disable_sync != 0) {
            capability->sync_enabled = 0;
        }
        return 0;
    }
    switch (capability->effective_mode) {
    case DB_GL_STREAM_UPLOAD_MODE_PERSISTENT:
        capability->effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
        capability->sync_enabled = 0;
        return 1;
    case DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE:
        capability->effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER;
        capability->sync_enabled = 0;
        return 1;
    case DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER:
        capability->effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA;
        capability->sync_enabled = 0;
        return 1;
    case DB_GL_STREAM_UPLOAD_MODE_SUB_DATA:
    default:
        db_gl_stream_upload_force_client_fallback(capability, disable_sync);
        return 1;
    }
}

void db_gl_stream_upload_disable_persistent_for_target(
    db_gl_stream_upload_capability_t *capability,
    db_gl_upload_target_t target) {
    if ((capability == NULL) ||
        (target != DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) ||
        (capability->effective_mode != DB_GL_STREAM_UPLOAD_MODE_PERSISTENT)) {
        return;
    }
    capability->effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
    if (capability->supported_mode == DB_GL_STREAM_UPLOAD_MODE_PERSISTENT) {
        capability->supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
    }
}

int db_gl_stream_upload_uses_buffer_object(
    const db_gl_stream_upload_capability_t *capability) {
    return (capability != NULL) && (capability->effective_storage ==
                                    DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT);
}

int db_gl_stream_upload_uses_map_range(
    const db_gl_stream_upload_capability_t *capability) {
    return (capability != NULL) &&
           (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE);
}

int db_gl_stream_upload_uses_map_buffer(
    const db_gl_stream_upload_capability_t *capability) {
    return (capability != NULL) &&
           (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER);
}

int db_gl_stream_upload_uses_persistent(
    const db_gl_stream_upload_capability_t *capability) {
    return (capability != NULL) &&
           (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_PERSISTENT);
}

int db_gl_stream_upload_sync_enabled(
    const db_gl_stream_upload_capability_t *capability) {
    return (capability != NULL) && (capability->sync_enabled != 0);
}

void db_gl_present_mode_resolve(const db_gl_present_mode_request_t *request,
                                db_gl_present_mode_resolution_t *out) {
    if (out == NULL) {
        return;
    }
    *out = (db_gl_present_mode_resolution_t){0};
    if (request == NULL) {
        out->reason = "missing request";
        return;
    }
    out->valid = 1;
    out->effective_backbuffer_draw_mode =
        request->requested_backbuffer_draw_mode;
    out->effective_present_upload = request->present_upload;
    switch (request->requested_present_buffer_mode) {
    case DB_GL_PRESENT_BUFFER_MODE_REPLACE:
        out->requested_preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS;
        out->effective_preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS;
        return;
    case DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE:
        out->requested_preserve_mode =
            DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
        out->effective_preserve_mode =
            DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
        return;
    case DB_GL_PRESENT_BUFFER_MODE_RING:
        out->requested_preserve_mode =
            DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT;
        if ((request->preserved_framebuffer_count > 1U) &&
            (db_gl_stream_upload_uses_buffer_object(&request->present_upload) !=
             0)) {
            out->effective_preserve_mode =
                DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT;
            return;
        }
        out->effective_preserve_mode =
            DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
        out->downgraded = 1;
        out->reason = (request->preserved_framebuffer_count > 1U)
                          ? "ring upload fallback required"
                          : "preserved ring unavailable";
        return;
    case DB_GL_PRESENT_BUFFER_MODE_AUTO:
    default:
        out->requested_preserve_mode =
            (request->prefer_ring_for_preserved_draw != 0)
                ? DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT
                : DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
        if ((out->requested_preserve_mode ==
             DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) &&
            ((request->preserved_framebuffer_count <= 1U) ||
             (db_gl_stream_upload_uses_buffer_object(
                  &request->present_upload) == 0))) {
            out->effective_preserve_mode =
                DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE;
            out->downgraded = 1;
            out->reason = (request->preserved_framebuffer_count > 1U)
                              ? "client upload fallback"
                              : "preserved ring unavailable";
            return;
        }
        out->effective_preserve_mode = out->requested_preserve_mode;
        return;
    }
}

db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_renderer(
    db_gl_runtime_draw_mode_t draw_mode, int has_vbo,
    const db_gl_stream_upload_capability_t *upload, int backbuffer_replay) {
    return (db_gl_runtime_mode_desc_t){
        .draw_mode = draw_mode,
        .geometry_upload_enabled = 1,
        .geometry_uses_client_arrays = (has_vbo == 0),
        .geometry_upload =
            (upload != NULL)
                ? *upload
                : (db_gl_stream_upload_capability_t){
                      .target = DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
                      .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                  },
        .full_present_upload =
            (db_gl_stream_upload_capability_t){
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
        .partial_present_upload =
            (db_gl_stream_upload_capability_t){
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
        .preserve_mode = DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS,
        .backbuffer_replay = (backbuffer_replay != 0),
    };
}

db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_present(
    const db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_preserve_mode_t preserve_mode) {
    db_gl_runtime_draw_mode_t draw_mode = DB_GL_RUNTIME_DRAW_SHADOW_FALLBACK;
    if (preserve_mode == DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS) {
        draw_mode = DB_GL_RUNTIME_DRAW_FULL_PRESENT;
    } else if (preserve_mode == DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT) {
        draw_mode = DB_GL_RUNTIME_DRAW_DIRTY_REPLAY;
    }
    return (db_gl_runtime_mode_desc_t){
        .draw_mode = draw_mode,
        .geometry_upload_enabled = 0,
        .geometry_uses_client_arrays = 0,
        .full_present_upload =
            (state != NULL)
                ? state->upload_profile.effective_full
                : (db_gl_stream_upload_capability_t){
                      .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                      .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                  },
        .partial_present_upload =
            (state != NULL)
                ? state->upload_profile.effective_partial
                : (db_gl_stream_upload_capability_t){
                      .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                      .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                      .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                      .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                  },
        .preserve_mode = preserve_mode,
        .backbuffer_replay = 0,
    };
}

void db_gl_runtime_mode_format_renderer(char *output, size_t output_size,
                                        const db_gl_runtime_mode_desc_t *mode) {
    if ((output == NULL) || (output_size == 0U) || (mode == NULL)) {
        return;
    }
    (void)db_snprintf(
        output, output_size, "draw=%s, geometry=%s, replay=%s",
        db_gl_runtime_draw_mode_name(mode->draw_mode),
        db_gl_stream_upload_name(&mode->geometry_upload,
                                 mode->geometry_uses_client_arrays,
                                 mode->geometry_upload_enabled),
        (mode->backbuffer_replay != 0) ? "yes" : "no");
}

void db_gl_runtime_mode_format_present(char *output, size_t output_size,
                                       const db_gl_runtime_mode_desc_t *mode) {
    if ((output == NULL) || (output_size == 0U) || (mode == NULL)) {
        return;
    }
    (void)db_snprintf(
        output, output_size,
        "full_present_upload=%s, partial_present_upload=%s, preserve=%s",
        db_gl_stream_upload_name(&mode->full_present_upload, 0, 1),
        db_gl_stream_upload_name(&mode->partial_present_upload, 0, 1),
        db_gl_shadow_present_preserve_mode_name(mode->preserve_mode));
}
