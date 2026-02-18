#include "renderer_gl_common.h"

#include <stdint.h>
#include <string.h>

#include "../core/db_core.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#include <dlfcn.h>
#define DB_HAS_DLSYM_PROC_ADDRESS 1
#elifdef __linux__
#include <EGL/egl.h>
#define DB_HAS_EGL_GET_PROC_ADDRESS 1
#endif

#ifndef __APPLE__
#if defined(__has_include) && __has_include(<GL/gl.h>)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GLES/gl.h>
#endif
#endif

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

typedef void *(*db_gl_map_buffer_range_fn_t)(GLenum target, GLintptr offset,
                                             GLsizeiptr length,
                                             GLbitfield access);
typedef void *(*db_gl_map_buffer_fn_t)(GLenum target, GLenum access);
typedef GLboolean (*db_gl_unmap_buffer_fn_t)(GLenum target);
typedef void (*db_gl_get_buffer_sub_data_fn_t)(GLenum target, GLintptr offset,
                                               GLsizeiptr size, void *data);

typedef struct {
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    db_gl_get_buffer_sub_data_fn_t get_buffer_sub_data;
    int loaded;
} db_gl_upload_proc_table_t;

static db_gl_upload_proc_table_t g_upload_proc_table = {0};
static db_gl_get_proc_address_fn_t g_db_gl_get_proc_address = NULL;

void db_gl_set_proc_address_loader(
    db_gl_get_proc_address_fn_t get_proc_address) {
    g_db_gl_get_proc_address = get_proc_address;
    g_upload_proc_table.loaded = 0;
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
    return (db_gl_generic_proc_t)dlsym(RTLD_DEFAULT, name);
#elifdef DB_HAS_EGL_GET_PROC_ADDRESS
    db_gl_generic_proc_t egl_proc =
        (db_gl_generic_proc_t)eglGetProcAddress(name);
    if (egl_proc != NULL) {
        return egl_proc;
    }
#endif
    return NULL;
}

static void db_gl_load_upload_proc_table(void) {
    if (g_upload_proc_table.loaded != 0) {
        return;
    }

    g_upload_proc_table.map_buffer_range =
        (db_gl_map_buffer_range_fn_t)(db_gl_get_proc("glMapBufferRange"));
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)(db_gl_get_proc(
                "glMapBufferRangeEXT"));
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
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        g_upload_proc_table.get_buffer_sub_data =
            (db_gl_get_buffer_sub_data_fn_t)(db_gl_get_proc(
                "glGetBufferSubDataEXT"));
    }

    g_upload_proc_table.loaded = 1;
}

static int db_gl_supports_map_buffer_range(const char *exts) {
    const char *version = (const char *)glGetString(GL_VERSION);
    if (db_gl_is_es_context(version) != 0) {
        return db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
               db_gl_version_text_at_least(version, 3, 0);
    }

    return db_has_gl_extension_token(exts, "GL_ARB_map_buffer_range") ||
           db_has_gl_extension_token(exts, "GL_EXT_map_buffer_range") ||
           db_gl_version_text_at_least(version, 3, 0);
}

static int db_gl_supports_map_buffer(const char *exts) {
    if (db_gl_is_es_context((const char *)glGetString(GL_VERSION)) != 0) {
        return db_has_gl_extension_token(exts, "GL_OES_mapbuffer");
    }

    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least((const char *)glGetString(GL_VERSION), 1,
                                       5);
}

static int db_gl_supports_vbo(const char *exts) {
    if (db_gl_is_es_context((const char *)glGetString(GL_VERSION)) != 0) {
        // GLES 1.1+ has buffer objects in core.
        return db_gl_version_text_at_least(
            (const char *)glGetString(GL_VERSION), 1, 1);
    }

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

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, pattern, probe_size);
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
    if ((try_map_range != 0) &&
        (g_upload_proc_table.map_buffer_range != NULL)) {
        return g_upload_proc_table.map_buffer_range(
            GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                GL_MAP_UNSYNCHRONIZED_BIT);
    }

    if ((try_map_buffer != 0) && (g_upload_proc_table.map_buffer != NULL)) {
        return g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER,
                                              GL_WRITE_ONLY_OES);
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
    (void)use_persistent_upload;
    (void)persistent_mapped_ptr;
    if ((source_base == NULL) || (ranges == NULL) || (range_count == 0U)) {
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
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            memcpy(dst_base + range->dst_offset_bytes,
                   src_base + range->src_offset_bytes, range->size_bytes);
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
