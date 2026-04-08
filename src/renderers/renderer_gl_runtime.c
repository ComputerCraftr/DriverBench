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

const char *db_gl_capability_mode_upload_from_probe(
    int has_vbo, const db_gl_upload_probe_result_t *upload) {
    if (has_vbo == 0) {
        return DB_GL_CAP_UPLOAD_CLIENT_ARRAY;
    }
    if ((upload != NULL) && (upload->use_persistent_upload != 0)) {
        return DB_GL_CAP_UPLOAD_VBO_PERSISTENT;
    }
    if ((upload != NULL) && (upload->use_map_range_upload != 0)) {
        return DB_GL_CAP_UPLOAD_VBO_MAP_RANGE;
    }
    if ((upload != NULL) && (upload->use_map_buffer_upload != 0)) {
        return DB_GL_CAP_UPLOAD_VBO_MAP_BUFFER;
    }
    return DB_GL_CAP_UPLOAD_VBO;
}

const char *
db_gl_capability_mode_gl3_shader(const db_gl_upload_probe_result_t *upload,
                                 int uses_history_texture) {
    if (uses_history_texture != 0) {
        return DB_GL_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW;
    }
    if ((upload != NULL) && (upload->use_persistent_upload != 0)) {
        return DB_GL_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT;
    }
    if ((upload != NULL) && (upload->use_map_range_upload != 0)) {
        return DB_GL_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE;
    }
    if ((upload != NULL) && (upload->use_map_buffer_upload != 0)) {
        return DB_GL_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER;
    }
    return DB_GL_CAP_MODE_OPENGL_SHADER_VBO;
}

const char *
db_gl_capability_mode_draw_select(int uses_ff_rect_draw_mode,
                                  int uses_history_draw,
                                  int use_shader_history_draw_name) {
    if (uses_ff_rect_draw_mode != 0) {
        return DB_GL_CAP_DRAW_FF_RECT_FILL;
    }
    if (uses_history_draw != 0) {
        return (use_shader_history_draw_name != 0)
                   ? DB_GL_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW
                   : DB_GL_CAP_DRAW_HISTORY_DIRTY;
    }
    return DB_GL_CAP_DRAW_TILES_FULL;
}

const char *db_gl_capability_mode_upload_select(int suppress_upload,
                                                const char *upload_mode) {
    if (suppress_upload != 0) {
        return DB_GL_CAP_UPLOAD_NONE;
    }
    return (upload_mode != NULL) ? upload_mode : DB_GL_CAP_UPLOAD_NONE;
}

void db_gl_capability_mode_compose(char *output, size_t output_size,
                                   const char *draw_mode,
                                   const char *upload_mode,
                                   int backbuffer_replay) {
    if ((output == NULL) || (output_size == 0U)) {
        return;
    }
    const char *draw =
        (draw_mode != NULL) ? draw_mode : DB_GL_CAP_DRAW_TILES_FULL;
    const char *upload =
        (upload_mode != NULL) ? upload_mode : DB_GL_CAP_UPLOAD_NONE;
    (void)db_snprintf(output, output_size, "%s(%s,backbuffer_replay=%s)", draw,
                      upload, (backbuffer_replay != 0) ? "yes" : "no");
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
