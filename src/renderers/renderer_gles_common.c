#include "renderer_opengl_common.h"

#include <stdint.h>
#include <string.h>

#include "../core/db_core.h"
#include "renderer_benchmark_common.h"

#include <EGL/egl.h>
#include <GLES/gl.h>

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_WRITE_ONLY_OES
#define GL_WRITE_ONLY_OES 0x88B9
#endif

#define PROBE_PREFIX_BYTES 64U
#define MAP_RANGE_PROBE_XOR_SEED 0xA5U

typedef void *(*db_gl_map_buffer_range_fn_t)(GLenum target, GLintptr offset,
                                             GLsizeiptr length,
                                             GLbitfield access);
typedef void *(*db_gl_map_buffer_fn_t)(GLenum target, GLenum access);
typedef GLboolean (*db_gl_unmap_buffer_fn_t)(GLenum target);

typedef struct {
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    int loaded;
} db_gl_upload_proc_table_t;

static db_gl_upload_proc_table_t g_upload_proc_table = {0};

static void db_gl_clear_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static void db_gl_load_upload_proc_table(void) {
    if (g_upload_proc_table.loaded != 0) {
        return;
    }

    g_upload_proc_table.map_buffer_range =
        (db_gl_map_buffer_range_fn_t)(eglGetProcAddress("glMapBufferRange"));
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)(eglGetProcAddress(
                "glMapBufferRangeEXT"));
    }

    g_upload_proc_table.map_buffer =
        (db_gl_map_buffer_fn_t)(eglGetProcAddress("glMapBufferOES"));

    g_upload_proc_table.unmap_buffer =
        (db_gl_unmap_buffer_fn_t)(eglGetProcAddress("glUnmapBuffer"));
    if (g_upload_proc_table.unmap_buffer == NULL) {
        g_upload_proc_table.unmap_buffer =
            (db_gl_unmap_buffer_fn_t)(eglGetProcAddress("glUnmapBufferOES"));
    }

    g_upload_proc_table.loaded = 1;
}

static size_t db_gl_probe_size(size_t bytes) {
    return (bytes < PROBE_PREFIX_BYTES) ? bytes : PROBE_PREFIX_BYTES;
}

static int db_gl_supports_map_buffer_range(const char *exts, int major,
                                           int minor) {
    if (db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range")) {
        return 1;
    }
    return (major > 3) || ((major == 3) && (minor >= 0));
}

static int db_gl_supports_map_buffer(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_OES_mapbuffer");
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

    uint8_t pattern[PROBE_PREFIX_BYTES] = {0};
    for (size_t i = 0; i < probe_size; i++) {
        pattern[i] = (uint8_t)(MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }

    db_gl_clear_errors();
    void *dst = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (glGetError() != GL_NO_ERROR)) {
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

    uint8_t pattern[PROBE_PREFIX_BYTES] = {0};
    for (size_t i = 0; i < probe_size; i++) {
        pattern[i] = (uint8_t)(MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }

    db_gl_clear_errors();
    void *dst =
        g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
                    initial_vertices);
    return glGetError() == GL_NO_ERROR;
}

int db_gl_has_vbo_support(void) {
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (db_has_gl_extension_token(exts, "GL_OES_vertex_buffer_object")) {
        return 1;
    }

    int major = 0;
    int minor = 0;
    if (!db_parse_gl_version_numbers((const char *)glGetString(GL_VERSION),
                                     &major, &minor)) {
        return 0;
    }

    // GLES 2.0+ has VBOs in core; GLES 1.x relies on OES extension above.
    return major >= 2;
}

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     int allow_persistent_upload,
                                     db_gl_upload_probe_result_t *out) {
    (void)allow_persistent_upload;
    if (out == NULL) {
        db_failf("renderer_gles_common",
                 "db_gl_probe_upload_capabilities: output is null");
    }

    *out = (db_gl_upload_probe_result_t){0};
    if (db_gl_has_vbo_support() == 0) {
        return;
    }

    db_gl_load_upload_proc_table();

    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    int major = 0;
    int minor = 0;
    const int has_version = db_parse_gl_version_numbers(
        (const char *)glGetString(GL_VERSION), &major, &minor);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, initial_vertices,
                 GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR) {
        return;
    }

    if ((has_version != 0) &&
        db_gl_supports_map_buffer_range(exts, major, minor) &&
        db_gl_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if ((has_version != 0) && db_gl_supports_map_buffer(exts) &&
        db_gl_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload) {
    const db_gl_upload_range_t full_range = {0U, 0U, bytes};
    db_gl_upload_ranges(source, bytes, use_persistent_upload,
                        persistent_mapped_ptr, use_map_range_upload,
                        use_map_buffer_upload, &full_range, 1U);
}

void db_gl_upload_ranges(const void *source_base, size_t total_bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload,
                         const db_gl_upload_range_t *ranges,
                         size_t range_count) {
    (void)use_persistent_upload;
    (void)persistent_mapped_ptr;
    if ((source_base == NULL) || (ranges == NULL) || (range_count == 0U)) {
        return;
    }

    db_gl_load_upload_proc_table();

    if ((use_map_range_upload != 0) &&
        (g_upload_proc_table.map_buffer_range != NULL) &&
        (g_upload_proc_table.unmap_buffer != NULL)) {
        void *mapped_ptr = g_upload_proc_table.map_buffer_range(
            GL_ARRAY_BUFFER, 0, (GLsizeiptr)total_bytes,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                GL_MAP_UNSYNCHRONIZED_BIT);
        if (mapped_ptr != NULL) {
            uint8_t *dst_base = (uint8_t *)mapped_ptr;
            const uint8_t *src_base = (const uint8_t *)source_base;
            for (size_t i = 0; i < range_count; i++) {
                const db_gl_upload_range_t *range = &ranges[i];
                // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
                memcpy(dst_base + range->dst_offset_bytes,
                       src_base + range->src_offset_bytes, range->size_bytes);
            }
            if (g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) == GL_TRUE) {
                return;
            }
        }
    }

    if ((use_map_buffer_upload != 0) &&
        (g_upload_proc_table.map_buffer != NULL) &&
        (g_upload_proc_table.unmap_buffer != NULL)) {
        void *mapped_ptr =
            g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES);
        if (mapped_ptr != NULL) {
            uint8_t *dst_base = (uint8_t *)mapped_ptr;
            const uint8_t *src_base = (const uint8_t *)source_base;
            for (size_t i = 0; i < range_count; i++) {
                const db_gl_upload_range_t *range = &ranges[i];
                // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
                memcpy(dst_base + range->dst_offset_bytes,
                       src_base + range->src_offset_bytes, range->size_bytes);
            }
            if (g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) == GL_TRUE) {
                return;
            }
        }
    }

    const uint8_t *src_base = (const uint8_t *)source_base;
    for (size_t i = 0; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)range->dst_offset_bytes,
                        (GLsizeiptr)range->size_bytes,
                        src_base + range->src_offset_bytes);
    }
}
