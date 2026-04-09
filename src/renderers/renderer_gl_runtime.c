#include "../core/db_core.h"
#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 1) Version/extension parsing and capability advertisement.
int db_has_gl_extension_token(const char *exts, const char *needle) {
    if ((exts == NULL) || (needle == NULL)) {
        return 0;
    }
    const size_t needle_len = strlen(needle);
    const char *ext_ptr = exts;
    while ((ext_ptr = strstr(ext_ptr, needle)) != NULL) {
        if (((ext_ptr == exts) || (ext_ptr[-1] == ' ')) &&
            ((ext_ptr[needle_len] == '\0') || (ext_ptr[needle_len] == ' '))) {
            return 1;
        }
        ext_ptr += needle_len;
    }
    return 0;
}

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

    char *parse_end = NULL;
    const long major_l = strtol(cursor, &parse_end, 10);
    if ((parse_end == cursor) || (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    const long minor_l = strtol(minor_start, &parse_end, 10);
    if (parse_end == minor_start) {
        return 0;
    }
    if ((major_l < 0L) || (minor_l < 0L) || (major_l > INT_MAX) ||
        (minor_l > INT_MAX)) {
        return 0;
    }

    *major_out = (int)major_l;
    *minor_out = (int)minor_l;
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

// 1) Capability utility helpers (error clear/probe pattern/capability strings).
void db_gl_probe_drain_errors(void) {
    if (g_upload_proc_table.get_error == NULL) {
        return;
    }
    while (g_upload_proc_table.get_error() != GL_NO_ERROR) {
    }
}

int db_gl_probe_step_error_free(void) {
    return (g_upload_proc_table.get_error != NULL) &&
           (g_upload_proc_table.get_error() == GL_NO_ERROR);
}

int db_gl_probe_finish(int success) {
    db_gl_probe_drain_errors();
    return (success != 0) ? 1 : 0;
}

static const uint8_t db_gl_probe_channel_high_threshold = 200U;
static const uint8_t db_gl_probe_channel_low_threshold = 80U;

size_t db_gl_probe_rgba_pixel_offset(size_t width, size_t col, size_t row) {
    return ((row * width) + col) * 4U;
}

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

size_t db_gl_upload_probe_size_bytes(size_t bytes) {
    return (bytes < DB_GL_PROBE_PREFIX_BYTES) ? bytes
                                              : DB_GL_PROBE_PREFIX_BYTES;
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

db_gl_stream_upload_capability_t db_gl_stream_upload_capability_from_probe(
    db_gl_upload_target_t target, const db_gl_upload_probe_result_t *probe,
    int enable_sync) {
    db_gl_stream_upload_capability_t capability = {
        .target = target,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
        .sync_supported = (enable_sync != 0) ? 1 : 0,
        .sync_enabled = 0,
    };
    if ((probe != NULL) && (probe->use_persistent_upload != 0)) {
        capability.supported_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.effective_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_PERSISTENT;
        capability.sync_enabled = capability.sync_supported;
        db_gl_stream_upload_disable_persistent_for_target(&capability, target);
        return capability;
    }
    if ((probe != NULL) && (probe->use_map_range_upload != 0)) {
        capability.supported_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.effective_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE;
        capability.sync_enabled = capability.sync_supported;
        return capability;
    }
    if ((probe != NULL) && (probe->use_map_buffer_upload != 0)) {
        capability.supported_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.effective_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER;
        capability.sync_enabled = capability.sync_supported;
        return capability;
    }
    if (target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) {
        capability.supported_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
        capability.effective_storage =
            DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT;
    }
    return capability;
}

db_gl_stream_upload_capability_t db_gl_stream_upload_capability_for_role(
    db_gl_upload_target_t target, const db_gl_upload_probe_result_t *probe,
    int enable_sync, db_gl_upload_role_t role,
    db_gl_shadow_present_texture_format_t texture_format) {
    db_gl_stream_upload_capability_t capability =
        db_gl_stream_upload_capability_from_probe(target, probe, enable_sync);
#ifdef __APPLE__
    if ((role == DB_GL_UPLOAD_ROLE_PRESENT_FULL) &&
        (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER) &&
        (texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)) {
        capability.effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT;
        capability.effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA;
        capability.sync_enabled = 0;
    }
#else
    (void)role;
    (void)texture_format;
#endif
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
    return ((capability != NULL) && (capability->effective_storage ==
                                     DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT))
               ? 1
               : 0;
}

int db_gl_stream_upload_uses_map_range(
    const db_gl_stream_upload_capability_t *capability) {
    return ((capability != NULL) &&
            (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE))
               ? 1
               : 0;
}

int db_gl_stream_upload_uses_map_buffer(
    const db_gl_stream_upload_capability_t *capability) {
    return ((capability != NULL) &&
            (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER))
               ? 1
               : 0;
}

int db_gl_stream_upload_uses_persistent(
    const db_gl_stream_upload_capability_t *capability) {
    return ((capability != NULL) &&
            (capability->effective_mode == DB_GL_STREAM_UPLOAD_MODE_PERSISTENT))
               ? 1
               : 0;
}

int db_gl_stream_upload_sync_enabled(
    const db_gl_stream_upload_capability_t *capability) {
    return ((capability != NULL) && (capability->sync_enabled != 0)) ? 1 : 0;
}

int db_gl_present_mode_validate_request(
    int is_cpu_api, int is_glfw_window_display, int is_gl1_renderer,
    db_gl_backbuffer_draw_mode_t backbuffer_draw_mode,
    db_gl_present_buffer_mode_t present_buffer_mode, const char **out_reason) {
    if (out_reason != NULL) {
        *out_reason = NULL;
    }
    if (present_buffer_mode == DB_GL_PRESENT_BUFFER_MODE_AUTO) {
        return 1;
    }
    if (is_cpu_api != 0) {
        if (is_glfw_window_display == 0) {
            if (out_reason != NULL) {
                *out_reason = "--present-buffer-mode is only supported for CPU "
                              "with --display glfw_window";
            }
            return 0;
        }
        if (present_buffer_mode != DB_GL_PRESENT_BUFFER_MODE_REPLACE) {
            if (out_reason != NULL) {
                *out_reason =
                    "--present-buffer-mode for CPU GLFW must be replace";
            }
            return 0;
        }
        return 1;
    }
    if (is_glfw_window_display == 0) {
        if (out_reason != NULL) {
            *out_reason = "--present-buffer-mode is only supported for "
                          "--display glfw_window";
        }
        return 0;
    }
    if (is_gl1_renderer == 0) {
        if (out_reason != NULL) {
            *out_reason = "--present-buffer-mode is only supported for "
                          "--renderer gl1_5_gles1_1";
        }
        return 0;
    }
    if (present_buffer_mode == DB_GL_PRESENT_BUFFER_MODE_REPLACE) {
        if (backbuffer_draw_mode != DB_GL_BACKBUFFER_DRAW_FULL) {
            if (out_reason != NULL) {
                *out_reason = "--present-buffer-mode replace requires "
                              "--backbuffer-draw-mode full";
            }
            return 0;
        }
        if (out_reason != NULL) {
            *out_reason = "--present-buffer-mode replace is incompatible with "
                          "GL1 preserved full-present";
        }
        return 0;
    }
    return 1;
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
    int uses_ff_rect_draw_mode, int uses_history_draw, int has_vbo,
    const db_gl_upload_probe_result_t *upload, int backbuffer_replay) {
    db_gl_runtime_draw_mode_t draw_mode = DB_GL_RUNTIME_DRAW_FULL_PRESENT;
    if (uses_ff_rect_draw_mode != 0) {
        draw_mode = DB_GL_RUNTIME_DRAW_FF_RECT_FILL;
    } else if (uses_history_draw != 0) {
        draw_mode = DB_GL_RUNTIME_DRAW_DIRTY_REPLAY;
    }
    return (db_gl_runtime_mode_desc_t){
        .draw_mode = draw_mode,
        .geometry_upload_enabled = 1,
        .geometry_uses_client_arrays = (has_vbo == 0) ? 1 : 0,
        .geometry_upload = db_gl_stream_upload_capability_from_probe(
            DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, upload, 0),
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
        .backbuffer_replay = (backbuffer_replay != 0) ? 1 : 0,
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
                ? state->effective_full_upload_capability
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
                ? state->effective_partial_upload_capability
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

int db_gl_runtime_has_extension(const db_gl_runtime_metadata_t *runtime,
                                const char *extension_name) {
    if ((runtime == NULL) || (runtime->has_valid_extensions == 0) ||
        (extension_name == NULL)) {
        return 0;
    }
    if (runtime->uses_indexed_extension_query == 0) {
        return db_has_gl_extension_token(runtime->extensions_text,
                                         extension_name);
    }
    if ((g_upload_proc_table.get_stringi == NULL) ||
        (g_upload_proc_table.get_integerv == NULL)) {
        return 0;
    }
    GLint extension_count = 0;
    g_upload_proc_table.get_integerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint extension_index = 0; extension_index < extension_count;
         extension_index++) {
        const char *const runtime_extension =
            (const char *)g_upload_proc_table.get_stringi(
                GL_EXTENSIONS, (GLuint)extension_index);
        if ((runtime_extension != NULL) &&
            (strcmp(runtime_extension, extension_name) == 0)) {
            return 1;
        }
    }
    return 0;
}

int db_gl_runtime_has_usable_version(const db_gl_runtime_metadata_t *runtime) {
    return (runtime != NULL) && (runtime->has_valid_version != 0);
}

int db_gl_runtime_is_es_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es != 0);
}

static int
db_gl_runtime_is_desktop_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es == 0);
}

int db_gl_runtime_version_at_least(const db_gl_runtime_metadata_t *runtime,
                                   int req_major, int req_minor) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           ((runtime->version_major > req_major) ||
            ((runtime->version_major == req_major) &&
             (runtime->version_minor >= req_minor)));
}

int db_gl_runtime_supports_desktop_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_desktop_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

int db_gl_runtime_supports_es_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_es_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

db_gl_runtime_metadata_t db_gl_runtime_metadata_load(void) {
    db_gl_runtime_metadata_t runtime = {0};
    if (g_upload_proc_table.get_string == NULL) {
        return runtime;
    }
    runtime.version_text =
        (const char *)g_upload_proc_table.get_string(GL_VERSION);
    runtime.has_valid_version = db_parse_gl_version_numbers(
        runtime.version_text, &runtime.version_major, &runtime.version_minor);
    if (runtime.has_valid_version == 0) {
        runtime.version_text = NULL;
        return runtime;
    }
    runtime.is_es = db_gl_is_es_context(runtime.version_text);
    if (runtime.is_es != 0) {
        runtime.extensions_text =
            (const char *)g_upload_proc_table.get_string(GL_EXTENSIONS);
        runtime.has_valid_extensions =
            (runtime.extensions_text != NULL) ? 1 : 0;
        return runtime;
    }
    if (db_gl_runtime_version_at_least(&runtime, 3, 0)) {
        runtime.uses_indexed_extension_query =
            ((g_upload_proc_table.get_stringi != NULL) &&
             (g_upload_proc_table.get_integerv != NULL))
                ? 1
                : 0;
        runtime.has_valid_extensions = runtime.uses_indexed_extension_query;
        return runtime;
    }
    // Desktop GL before 3.0 uses the legacy GL_EXTENSIONS string query.
    if (runtime.version_major < 3) {
        runtime.extensions_text =
            (const char *)g_upload_proc_table.get_string(GL_EXTENSIONS);
        runtime.has_valid_extensions =
            (runtime.extensions_text != NULL) ? 1 : 0;
    }
    return runtime;
}

int db_gl_extensions_advertise_buffer_storage(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_has_extension(runtime, "GL_EXT_buffer_storage");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 4, 4, "GL_ARB_buffer_storage");
}

int db_gl_extensions_advertise_map_buffer(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_has_extension(runtime, "GL_OES_mapbuffer");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 1, 5, "GL_ARB_vertex_buffer_object");
}

int db_gl_extensions_advertise_map_buffer_range(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_EXT_map_buffer_range");
    }
    return db_gl_runtime_version_at_least(runtime, 3, 0) ||
           db_gl_runtime_has_extension(runtime, "GL_ARB_map_buffer_range") ||
           db_gl_runtime_has_extension(runtime, "GL_EXT_map_buffer_range");
}

int db_gl_extensions_advertise_pbo(const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_EXT_pixel_buffer_object");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 2, 1, "GL_ARB_pixel_buffer_object");
}

int db_gl_extensions_advertise_texture_float(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_OES_texture_float");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 3, 0, "GL_ARB_texture_float");
}

int db_gl_extensions_advertise_vbo(const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_version_at_least(runtime, 1, 1);
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 1, 5, "GL_ARB_vertex_buffer_object");
}

// 2) Proc resolver and proc table loading.
