#include "renderer_opengl_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/db_core.h"
#include "renderer_benchmark_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
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

#define BASE10 10
#define PROBE_PREFIX_BYTES 64U
#define MAP_RANGE_PROBE_XOR_SEED 0xA5U

static int db_gl_parse_version(int *major_out, int *minor_out) {
    const char *version_text = (const char *)glGetString(GL_VERSION);
    if (version_text == NULL) {
        return 0;
    }

    const char *cursor = version_text;
    while ((*cursor != '\0') && (*cursor < '0' || *cursor > '9')) {
        cursor++;
    }
    if (*cursor == '\0') {
        return 0;
    }

    char *parse_end = NULL;
    const long major_l = strtol(cursor, &parse_end, BASE10);
    if ((parse_end == cursor) || (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    const long minor_l = strtol(minor_start, &parse_end, BASE10);
    if (parse_end == minor_start) {
        return 0;
    }

    *major_out = (int)major_l;
    *minor_out = (int)minor_l;
    return 1;
}

static int db_gl_version_at_least(int req_major, int req_minor) {
    int major = 0;
    int minor = 0;
    if (!db_gl_parse_version(&major, &minor)) {
        return 0;
    }
    return (major > req_major) || ((major == req_major) && (minor >= req_minor));
}

static int db_gl_supports_map_buffer_range(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_map_buffer_range") ||
           db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
           db_gl_version_at_least(3, 0);
}

static int db_gl_supports_buffer_storage(const char *exts) {
    return db_has_gl_extension_token(exts, "GL_ARB_buffer_storage") ||
           db_gl_version_at_least(4, 4);
}

static void db_gl_clear_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static size_t db_gl_probe_size(size_t bytes) {
    return (bytes < PROBE_PREFIX_BYTES) ? bytes : PROBE_PREFIX_BYTES;
}

static int db_gl_verify_buffer_prefix(const uint8_t *expected,
                                      size_t expected_size) {
    if (expected_size == 0U) {
        return 0;
    }
    uint8_t actual[PROBE_PREFIX_BYTES] = {0};
    db_gl_clear_errors();
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

    db_gl_clear_errors();
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
    uint8_t pattern[PROBE_PREFIX_BYTES] = {0};
    for (size_t i = 0; i < probe_size; i++) {
        pattern[i] = (uint8_t)(MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }

    db_gl_clear_errors();
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
    uint8_t pattern[PROBE_PREFIX_BYTES] = {0};
    for (size_t i = 0; i < probe_size; i++) {
        pattern[i] = (uint8_t)(MAP_RANGE_PROBE_XOR_SEED ^ (uint8_t)i);
    }

    db_gl_clear_errors();
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

int db_gl15_has_vbo_support(void) {
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
        db_has_gl_extension_token(exts, "GL_OES_vertex_buffer_object")) {
        return 1;
    }
    return db_gl_version_at_least(1, 5);
}

void db_gl15_probe_upload_capabilities(size_t bytes,
                                       const float *initial_vertices,
                                       db_gl15_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_opengl_gl1_5_gles1_1",
                 "db_gl15_probe_upload_capabilities: output is null");
    }

    *out = (db_gl15_upload_probe_result_t){0};
    out->has_vbo = db_gl15_has_vbo_support();
    if (out->has_vbo == 0) {
        return;
    }

    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, initial_vertices,
                 GL_STREAM_DRAW);
    if (glGetError() != GL_NO_ERROR) {
        return;
    }

    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (db_gl_supports_map_buffer_range(exts) &&
        db_gl_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if (db_gl_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

void db_gl3_probe_upload_capabilities(size_t bytes, const float *initial_vertices,
                                      db_gl3_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_opengl_gl3_3",
                 "db_gl3_probe_upload_capabilities: output is null");
    }

    *out = (db_gl3_upload_probe_result_t){0};
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);

    if (db_gl_supports_buffer_storage(exts) &&
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
    }
}

void db_gl_upload_mapped_or_subdata(const void *source, size_t bytes,
                                    void *mapped_ptr) {
    if (mapped_ptr != NULL) {
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        memcpy(mapped_ptr, source, bytes);
        if (glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, source);
        }
        return;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, source);
}
