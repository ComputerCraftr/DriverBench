#include "renderer_gl_common.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../config/benchmark_config.h"
#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "renderer_benchmark_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#include <dlfcn.h>
#define DB_HAS_DLSYM_PROC_ADDRESS 1
#elifdef __linux__
#include <dlfcn.h>
#define DB_HAS_DLSYM_PROC_ADDRESS 1
#ifdef DB_HAS_LINUX_KMS_ATOMIC
#include <EGL/egl.h>
#define DB_HAS_EGL_GET_PROC_ADDRESS 1
#endif
#endif

#ifndef __APPLE__
#ifdef DB_HAS_OPENGL_DESKTOP
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GLES/gl.h>
#endif
#endif

#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_WRITE_ONLY_OES
#define GL_WRITE_ONLY_OES 0x88B9
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY GL_WRITE_ONLY_OES
#endif

typedef void *(*db_gl_map_buffer_fn_t)(GLenum target, GLenum access);
typedef GLboolean (*db_gl_unmap_buffer_fn_t)(GLenum target);
typedef void (*db_gl_get_buffer_sub_data_fn_t)(GLenum target, GLintptr offset,
                                               GLsizeiptr size, void *data);
typedef void *(*db_gl_map_buffer_range_fn_t)(GLenum target, GLintptr offset,
                                             GLsizeiptr length,
                                             GLbitfield access);
typedef void (*db_gl_buffer_storage_fn_t)(GLenum target, GLsizeiptr size,
                                          const void *data, GLbitfield flags);

typedef struct {
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    db_gl_get_buffer_sub_data_fn_t get_buffer_sub_data;
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_buffer_storage_fn_t buffer_storage;
    int loaded;
} db_gl_upload_proc_table_t;

static db_gl_upload_proc_table_t g_upload_proc_table = {0};
static db_gl_get_proc_address_fn_t g_db_gl_get_proc_address = NULL;

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
    if ((major_l < 0L) || (minor_l < 0L) || (major_l > (long)INT_MAX) ||
        (minor_l > (long)INT_MAX)) {
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

void db_gl_clear_errors(db_gl_get_error_fn_t get_error) {
    if (get_error == NULL) {
        return;
    }
    while (get_error() != 0U) {
    }
}

size_t db_gl_probe_size(size_t bytes) {
    return (bytes < DB_GL_PROBE_PREFIX_BYTES) ? bytes
                                              : DB_GL_PROBE_PREFIX_BYTES;
}

void db_gl_fill_probe_pattern(uint8_t *pattern, size_t count) {
    if (pattern == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        pattern[i] = (uint8_t)(DB_GL_MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }
}

int db_gl_runtime_supports_vbo(const char *version_text, const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_gl_version_text_at_least(version_text, 1, 1);
    }

    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least(version_text, 1, 5);
}

int db_gl_runtime_supports_map_buffer_range(const char *version_text,
                                            const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
               db_gl_version_text_at_least(version_text, 3, 0);
    }

    return db_has_gl_extension_token(exts, "GL_ARB_map_buffer_range") ||
           db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
           db_gl_version_text_at_least(version_text, 3, 0);
}

int db_gl_runtime_supports_map_buffer(const char *version_text,
                                      const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_has_gl_extension_token(exts, "GL_OES_mapbuffer");
    }

    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least(version_text, 1, 5);
}

int db_gl_runtime_supports_buffer_storage(const char *version_text,
                                          const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_has_gl_extension_token(exts, "GL_EXT_buffer_storage");
    }

    return db_has_gl_extension_token(exts, "GL_ARB_buffer_storage") ||
           db_gl_version_text_at_least(version_text, 4, 4);
}

void db_gl_set_proc_address_loader(
    db_gl_get_proc_address_fn_t get_proc_address) {
    g_db_gl_get_proc_address = get_proc_address;
    g_upload_proc_table = (db_gl_upload_proc_table_t){0};
}

static db_gl_generic_proc_t db_gl_get_proc(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    if (g_db_gl_get_proc_address != NULL) {
        db_gl_generic_proc_t proc = g_db_gl_get_proc_address(name);
        if (proc != NULL) {
            return proc;
        }
    }

#ifdef DB_HAS_DLSYM_PROC_ADDRESS
    db_gl_generic_proc_t dlsym_proc =
        (db_gl_generic_proc_t)dlsym(RTLD_DEFAULT, name);
    if (dlsym_proc != NULL) {
        return dlsym_proc;
    }
#endif

#ifdef DB_HAS_EGL_GET_PROC_ADDRESS
    return (db_gl_generic_proc_t)eglGetProcAddress(name);
#else
    return NULL;
#endif
}

static void db_gl_load_upload_proc_table(void) {
    if (g_upload_proc_table.loaded != 0) {
        return;
    }

    g_upload_proc_table.map_buffer =
        (db_gl_map_buffer_fn_t)(db_gl_get_proc("glMapBuffer"));
    if (g_upload_proc_table.map_buffer == NULL) {
        g_upload_proc_table.map_buffer =
            (db_gl_map_buffer_fn_t)(db_gl_get_proc("glMapBufferARB"));
    }
    if (g_upload_proc_table.map_buffer == NULL) {
        g_upload_proc_table.map_buffer =
            (db_gl_map_buffer_fn_t)(db_gl_get_proc("glMapBufferOES"));
    }

    g_upload_proc_table.unmap_buffer =
        (db_gl_unmap_buffer_fn_t)(db_gl_get_proc("glUnmapBuffer"));
    if (g_upload_proc_table.unmap_buffer == NULL) {
        g_upload_proc_table.unmap_buffer =
            (db_gl_unmap_buffer_fn_t)(db_gl_get_proc("glUnmapBufferARB"));
    }
    if (g_upload_proc_table.unmap_buffer == NULL) {
        g_upload_proc_table.unmap_buffer =
            (db_gl_unmap_buffer_fn_t)(db_gl_get_proc("glUnmapBufferOES"));
    }

    g_upload_proc_table.get_buffer_sub_data =
        (db_gl_get_buffer_sub_data_fn_t)(db_gl_get_proc("glGetBufferSubData"));
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        g_upload_proc_table.get_buffer_sub_data =
            (db_gl_get_buffer_sub_data_fn_t)(db_gl_get_proc(
                "glGetBufferSubDataARB"));
    }

    g_upload_proc_table.map_buffer_range =
        (db_gl_map_buffer_range_fn_t)(db_gl_get_proc("glMapBufferRange"));
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)(db_gl_get_proc(
                "glMapBufferRangeEXT"));
    }

    g_upload_proc_table.buffer_storage =
        (db_gl_buffer_storage_fn_t)(db_gl_get_proc("glBufferStorage"));
    if (g_upload_proc_table.buffer_storage == NULL) {
        g_upload_proc_table.buffer_storage =
            (db_gl_buffer_storage_fn_t)(db_gl_get_proc("glBufferStorageEXT"));
    }

#if defined(DB_HAS_OPENGL_DESKTOP) && !defined(__APPLE__)
    if (g_upload_proc_table.map_buffer == NULL) {
        g_upload_proc_table.map_buffer = (db_gl_map_buffer_fn_t)glMapBuffer;
    }
    if (g_upload_proc_table.unmap_buffer == NULL) {
        g_upload_proc_table.unmap_buffer =
            (db_gl_unmap_buffer_fn_t)glUnmapBuffer;
    }
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        g_upload_proc_table.get_buffer_sub_data =
            (db_gl_get_buffer_sub_data_fn_t)glGetBufferSubData;
    }
#if defined(GL_VERSION_3_0) || defined(GL_ARB_map_buffer_range)
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)glMapBufferRange;
    }
#endif
#if defined(GL_VERSION_4_4) || defined(GL_ARB_buffer_storage)
    if (g_upload_proc_table.buffer_storage == NULL) {
        g_upload_proc_table.buffer_storage =
            (db_gl_buffer_storage_fn_t)glBufferStorage;
    }
#endif
#endif

    g_upload_proc_table.loaded = 1;
}

int db_gl_has_vbo_support(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    return db_gl_runtime_supports_vbo(version, exts);
}

static int db_gl_verify_buffer_prefix(const uint8_t *expected,
                                      size_t expected_size) {
    if (expected_size == 0U) {
        return 0;
    }

    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        return 1;
    }

    uint8_t actual[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    g_upload_proc_table.get_buffer_sub_data(GL_ARRAY_BUFFER, 0,
                                            (GLsizeiptr)expected_size, actual);

    if (glGetError() != GL_NO_ERROR) {
        return 0;
    }

    return memcmp(expected, actual, expected_size) == 0;
}

static int db_gl_try_init_persistent_upload(size_t bytes,
                                            const float *initial_vertices,
                                            void **mapped_out) {
    if ((g_upload_proc_table.buffer_storage == NULL) ||
        (g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }
    const size_t probe_size = db_gl_probe_size(bytes);
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    g_upload_proc_table.buffer_storage(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL,
                                       storage_flags);
    if (glGetError() != GL_NO_ERROR) {
        return 0;
    }

    void *mapped = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, storage_flags);
    if ((mapped == NULL) || (glGetError() != GL_NO_ERROR)) {
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        }
        return 0;
    }

    db_copy_bytes(mapped, initial_vertices, bytes);
    if (!db_gl_verify_buffer_prefix((const uint8_t *)initial_vertices,
                                    probe_size)) {
        (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        return 0;
    }

    *mapped_out = mapped;
    return 1;
}

static int db_gl_probe_map_range_upload(size_t bytes,
                                        const float *initial_vertices) {
    if ((g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_probe_size(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_fill_probe_pattern(pattern, probe_size);

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    void *dst = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return 0;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
                    initial_vertices);
    return glGetError() == GL_NO_ERROR;
}

static int db_gl_probe_map_buffer_upload(size_t bytes,
                                         const float *initial_vertices) {
    if ((g_upload_proc_table.map_buffer == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_probe_size(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_fill_probe_pattern(pattern, probe_size);

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    void *dst = g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return 0;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
                    initial_vertices);
    return glGetError() == GL_NO_ERROR;
}

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     db_gl_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_gl_common",
                 "db_gl_probe_upload_capabilities: output is null");
    }

    *out = (db_gl_upload_probe_result_t){0};
    if (db_gl_has_vbo_support() == 0) {
        return;
    }

    db_gl_load_upload_proc_table();

    const char *version = (const char *)glGetString(GL_VERSION);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);

    if (db_gl_runtime_supports_buffer_storage(version, exts) &&
        db_gl_runtime_supports_map_buffer_range(version, exts) &&
        db_gl_try_init_persistent_upload(bytes, initial_vertices,
                                         &out->persistent_mapped_ptr)) {
        out->use_persistent_upload = 1;
        return;
    }

    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, initial_vertices,
                 GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR) {
        return;
    }

    if (db_gl_runtime_supports_map_buffer_range(version, exts) &&
        db_gl_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if (db_gl_runtime_supports_map_buffer(version, exts) &&
        db_gl_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

static void *db_gl_try_map_upload_buffer(size_t bytes, int try_map_range,
                                         int try_map_buffer) {
    if ((try_map_range != 0) &&
        (g_upload_proc_table.map_buffer_range != NULL)) {
        return g_upload_proc_table.map_buffer_range(
            GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                GL_MAP_UNSYNCHRONIZED_BIT);
    }

    if ((try_map_buffer != 0) && (g_upload_proc_table.map_buffer != NULL)) {
        return g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    }

    return NULL;
}

static void db_gl_upload_ranges_subdata(const void *source_base,
                                        const db_gl_upload_range_t *ranges,
                                        size_t range_count) {
    const uint8_t *src_base = (const uint8_t *)source_base;
    for (size_t i = 0; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)range->dst_offset_bytes,
                        (GLsizeiptr)range->size_bytes,
                        src_base + range->src_offset_bytes);
    }
}

void db_gl_upload_ranges(const void *source_base, size_t total_bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload,
                         const db_gl_upload_range_t *ranges,
                         size_t range_count) {
    if ((source_base == NULL) || (ranges == NULL) || (range_count == 0U)) {
        return;
    }

    if ((use_persistent_upload != 0) && (persistent_mapped_ptr != NULL)) {
        uint8_t *dst_base = (uint8_t *)persistent_mapped_ptr;
        const uint8_t *src_base = (const uint8_t *)source_base;
        for (size_t i = 0; i < range_count; i++) {
            const db_gl_upload_range_t *range = &ranges[i];
            db_copy_bytes(dst_base + range->dst_offset_bytes,
                          src_base + range->src_offset_bytes,
                          range->size_bytes);
        }
        return;
    }

    db_gl_load_upload_proc_table();

    void *mapped_ptr = db_gl_try_map_upload_buffer(
        total_bytes, use_map_range_upload, use_map_buffer_upload);
    if ((mapped_ptr != NULL) && (g_upload_proc_table.unmap_buffer != NULL)) {
        uint8_t *dst_base = (uint8_t *)mapped_ptr;
        const uint8_t *src_base = (const uint8_t *)source_base;
        for (size_t i = 0; i < range_count; i++) {
            const db_gl_upload_range_t *range = &ranges[i];
            db_copy_bytes(dst_base + range->dst_offset_bytes,
                          src_base + range->src_offset_bytes,
                          range->size_bytes);
        }
        if (g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) == GL_FALSE) {
            db_gl_upload_ranges_subdata(source_base, ranges, range_count);
        }
        return;
    }

    db_gl_upload_ranges_subdata(source_base, ranges, range_count);
}

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload) {
    const db_gl_upload_range_t full_range = {0U, 0U, bytes};
    db_gl_upload_ranges(source, bytes, use_persistent_upload,
                        persistent_mapped_ptr, use_map_range_upload,
                        use_map_buffer_upload, &full_range, 1U);
}

void db_gl_unmap_current_array_buffer(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.unmap_buffer != NULL) {
        (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
    }
}

int db_init_band_vertices_common(db_gl_vertex_init_t *out_state,
                                 size_t vertex_stride) {
    const size_t vertex_count = (size_t)BENCH_BANDS * DB_RECT_VERTEX_COUNT;
    const size_t float_count = vertex_count * vertex_stride;

    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    *out_state = (db_gl_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->vertex_stride = vertex_stride;
    out_state->pattern = DB_PATTERN_BANDS;
    out_state->work_unit_count = BENCH_BANDS;
    out_state->draw_vertex_count = db_checked_size_to_u32(
        DB_BENCH_COMMON_BACKEND, "bands_draw_vertex_count", vertex_count);
    return 1;
}

int db_init_grid_vertices_common(db_gl_vertex_init_t *out_state,
                                 size_t vertex_stride) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(DB_PATTERN_SNAKE_GRID);
    if ((tile_count_u64 == 0U) || (tile_count_u64 > UINT32_MAX)) {
        return 0;
    }

    const uint64_t vertex_count_u64 = tile_count_u64 * DB_RECT_VERTEX_COUNT;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * (uint64_t)vertex_stride;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_tile_count", tile_count_u64);
    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);
        const size_t base =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * vertex_stride;
        float *unit = &vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, vertex_stride);
        db_set_rect_unit_rgb(
            unit, vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
        if (vertex_stride == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, vertex_stride,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   1.0F);
        }
    }

    *out_state = (db_gl_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->vertex_stride = vertex_stride;
    out_state->work_unit_count = tile_count;
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_draw_vertex_count", vertex_count_u64);
    return 1;
}

int db_init_vertices_for_pattern_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    db_pattern_t pattern, size_t vertex_stride) {
    const int use_grid_init = (pattern == DB_PATTERN_SNAKE_GRID) ||
                              (pattern == DB_PATTERN_SNAKE_RECT) ||
                              (pattern == DB_PATTERN_SNAKE_SHAPES) ||
                              (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                              (pattern == DB_PATTERN_GRADIENT_FILL);
    const int initialized =
        use_grid_init ? db_init_grid_vertices_common(out_state, vertex_stride)
                      : db_init_band_vertices_common(out_state, vertex_stride);
    if (initialized == 0) {
        db_failf(backend_name, "benchmark mode '%s' initialization failed",
                 db_pattern_mode_name(pattern));
    }
    out_state->pattern = pattern;
    return 1;
}

int db_init_vertices_for_runtime_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride) {
    if (runtime_state == NULL) {
        return 0;
    }
    if (!db_init_vertices_for_pattern_common_with_stride(
            backend_name, out_state, runtime_state->pattern, vertex_stride)) {
        return 0;
    }

    out_state->pattern = runtime_state->pattern;
    out_state->work_unit_count = runtime_state->work_unit_count;
    out_state->draw_vertex_count = runtime_state->draw_vertex_count;
    out_state->vertex_stride = vertex_stride;
    if ((runtime_state->pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (runtime_state->pattern == DB_PATTERN_GRADIENT_FILL)) {
        float source_r = 0.0F;
        float source_g = 0.0F;
        float source_b = 0.0F;
        db_palette_cycle_color_rgb(runtime_state->gradient_cycle, &source_r,
                                   &source_g, &source_b);
        db_fill_grid_all_rgb_stride(
            out_state->vertices, out_state->work_unit_count, vertex_stride,
            DB_VERTEX_POSITION_FLOAT_COUNT, source_r, source_g, source_b);
    }
    return 1;
}

void db_fill_band_vertices_pos_rgb_stride(float *out_vertices,
                                          uint32_t band_count, double time_s,
                                          size_t stride_floats,
                                          size_t color_offset_floats) {
    const float inv_band_count = 1.0F / (float)band_count;
    const float band_x_scale = 2.0F * inv_band_count;
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        const float band_f = (float)band_index;
        const float x0 = (band_f * band_x_scale) - 1.0F;
        const float x1 = x0 + band_x_scale;
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_fill_rect_unit_pos(unit, x0, -1.0F, x1, 1.0F, stride_floats);
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
        if (stride_floats == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, stride_floats,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   1.0F);
        }
    }
}

void db_update_band_vertices_rgb_stride(float *out_vertices,
                                        uint32_t band_count, double time_s,
                                        size_t stride_floats,
                                        size_t color_offset_floats) {
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
    }
}
