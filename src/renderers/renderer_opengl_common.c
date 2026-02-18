#include "renderer_gl_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/db_core.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#else
#define GL_GLEXT_PROTOTYPES
#ifdef __has_include
#if __has_include(<GL/glcorearb.h>)
#include <GL/glcorearb.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif

void db_gl_set_proc_address_loader(
    db_gl_get_proc_address_fn_t get_proc_address) {
    (void)get_proc_address;
}

static int db_gl_supports_map_buffer_range(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_map_buffer_range") ||
           db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
           db_gl_version_text_at_least((const char *)glGetString(GL_VERSION), 3,
                                       0);
}

static int db_gl_supports_map_buffer(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least((const char *)glGetString(GL_VERSION), 1,
                                       5);
}

static int db_gl_supports_buffer_storage(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_buffer_storage") ||
           db_gl_version_text_at_least((const char *)glGetString(GL_VERSION), 4,
                                       4);
}

static int db_gl_supports_vbo(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least((const char *)glGetString(GL_VERSION), 1,
                                       5);
}

int db_gl_has_vbo_support(void) {
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    return db_gl_supports_vbo(exts);
}

static int db_gl_verify_buffer_prefix(const uint8_t *expected,
                                      size_t expected_size) {
    if (expected_size == 0U) {
        return 0;
    }

    uint8_t actual[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)expected_size, actual);

    if (glGetError() != GL_NO_ERROR) {
        return 0;
    }

    return memcmp(expected, actual, expected_size) == 0;
}

static int db_gl_try_init_persistent_upload(size_t bytes,
                                            const float *initial_vertices,
                                            void **mapped_out) {
#if defined(GL_ARB_buffer_storage) && defined(GL_MAP_PERSISTENT_BIT) &&        \
    defined(GL_MAP_COHERENT_BIT)
    const size_t probe_size = db_gl_probe_size(bytes);
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    const GLbitfield map_flags = storage_flags;

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    glBufferStorage(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL, storage_flags);
    if (glGetError() != GL_NO_ERROR) {
        return 0;
    }

    void *mapped =
        glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, map_flags);
    if ((mapped == NULL) || (glGetError() != GL_NO_ERROR)) {
        if (mapped != NULL) {
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        return 0;
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(mapped, initial_vertices, bytes);
    if (!db_gl_verify_buffer_prefix((const uint8_t *)initial_vertices,
                                    probe_size)) {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        return 0;
    }

    *mapped_out = mapped;
    return 1;
#else
    (void)bytes;
    (void)initial_vertices;
    (void)mapped_out;
    return 0;
#endif
}

static int db_gl_probe_map_range_upload(size_t bytes,
                                        const float *initial_vertices) {
#if defined(GL_MAP_INVALIDATE_BUFFER_BIT) && defined(GL_MAP_UNSYNCHRONIZED_BIT)
    const size_t probe_size = db_gl_probe_size(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_fill_probe_pattern(pattern, probe_size);

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    void *dst =
        glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                             GL_MAP_UNSYNCHRONIZED_BIT);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, pattern, probe_size);
    if ((glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return 0;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
                    initial_vertices);
    return glGetError() == GL_NO_ERROR;
#else
    (void)bytes;
    (void)initial_vertices;
    return 0;
#endif
}

static int db_gl_probe_map_buffer_upload(size_t bytes,
                                         const float *initial_vertices) {
    const size_t probe_size = db_gl_probe_size(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_fill_probe_pattern(pattern, probe_size);

    db_gl_clear_errors((db_gl_get_error_fn_t)glGetError);
    void *dst = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if ((dst == NULL) || (glGetError() != GL_NO_ERROR)) {
        return 0;
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, pattern, probe_size);
    if ((glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
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
                                     int allow_persistent_upload,
                                     db_gl_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_opengl_common",
                 "db_gl_probe_upload_capabilities: output is null");
    }

    *out = (db_gl_upload_probe_result_t){0};
    if (db_gl_has_vbo_support() == 0) {
        return;
    }

    const char *exts = (const char *)glGetString(GL_EXTENSIONS);

    if ((allow_persistent_upload != 0) && db_gl_supports_buffer_storage(exts) &&
        db_gl_supports_map_buffer_range(exts) &&
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

    if (db_gl_supports_map_buffer_range(exts) &&
        db_gl_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if (db_gl_supports_map_buffer(exts) &&
        db_gl_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

static void *db_gl_try_map_upload_buffer(size_t bytes, int try_map_range,
                                         int try_map_buffer) {
    if (try_map_range != 0) {
#if defined(GL_MAP_INVALIDATE_BUFFER_BIT) && defined(GL_MAP_UNSYNCHRONIZED_BIT)
        return glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes,
                                GL_MAP_WRITE_BIT |
                                    GL_MAP_INVALIDATE_BUFFER_BIT |
                                    GL_MAP_UNSYNCHRONIZED_BIT);
#endif
    }

    if (try_map_buffer != 0) {
        return glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
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
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            memcpy(dst_base + range->dst_offset_bytes,
                   src_base + range->src_offset_bytes, range->size_bytes);
        }
        return;
    }

    void *mapped_ptr = db_gl_try_map_upload_buffer(
        total_bytes, use_map_range_upload, use_map_buffer_upload);
    if (mapped_ptr != NULL) {
        uint8_t *dst_base = (uint8_t *)mapped_ptr;
        const uint8_t *src_base = (const uint8_t *)source_base;
        for (size_t i = 0; i < range_count; i++) {
            const db_gl_upload_range_t *range = &ranges[i];
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            memcpy(dst_base + range->dst_offset_bytes,
                   src_base + range->src_offset_bytes, range->size_bytes);
        }
        if (glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE) {
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
