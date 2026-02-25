#include "renderer_gl_common.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../config/benchmark_config.h"
#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "renderer_benchmark_common.h"
#include "renderer_snake_common.h"
#include "renderer_snake_shape_common.h"

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
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
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
typedef void (*db_gl_bind_buffer_fn_t)(GLenum target, GLuint buffer);
typedef void (*db_gl_buffer_data_fn_t)(GLenum target, GLsizeiptr size,
                                       const void *data, GLenum usage);
typedef void (*db_gl_buffer_sub_data_fn_t)(GLenum target, GLintptr offset,
                                           GLsizeiptr size, const void *data);
typedef void (*db_gl_gen_buffers_fn_t)(GLsizei count, GLuint *buffers);
typedef void (*db_gl_delete_buffers_fn_t)(GLsizei count, const GLuint *buffers);
#ifdef DB_HAS_GLFW
typedef void (*db_glfw_proc_t)(void);
extern db_glfw_proc_t glfwGetProcAddress(const char *procname);
#endif

typedef struct {
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    db_gl_get_buffer_sub_data_fn_t get_buffer_sub_data;
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_buffer_storage_fn_t buffer_storage;
    db_gl_bind_buffer_fn_t bind_buffer;
    db_gl_buffer_data_fn_t buffer_data;
    db_gl_buffer_sub_data_fn_t buffer_sub_data;
    db_gl_gen_buffers_fn_t gen_buffers;
    db_gl_delete_buffers_fn_t delete_buffers;
    int loaded;
} db_gl_upload_proc_table_t;

static db_gl_upload_proc_table_t g_upload_proc_table = {0};
static void db_gl_load_upload_proc_table(void);
static void db_gl_require_upload_proc_table_loaded(const char *func_name);

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

int db_gl_runtime_supports_pbo(const char *version_text, const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_gl_version_text_at_least(version_text, 3, 0) ||
               db_has_gl_extension_token(exts, "GL_EXT_pixel_buffer_object");
    }

    return db_gl_version_text_at_least(version_text, 2, 1) ||
           db_has_gl_extension_token(exts, "GL_ARB_pixel_buffer_object");
}

void db_gl_preload_upload_proc_table(void) { db_gl_load_upload_proc_table(); }

static void db_gl_require_upload_proc_table_loaded(const char *func_name) {
    if (g_upload_proc_table.loaded == 0) {
        db_failf("renderer_gl_common",
                 "%s requires preloaded GL proc table; call "
                 "db_gl_preload_upload_proc_table() during init",
                 func_name);
    }
}

static db_gl_generic_proc_t db_gl_get_proc(const char *name) {
    if (name == NULL) {
        return NULL;
    }

#ifdef DB_HAS_DLSYM_PROC_ADDRESS
    db_gl_generic_proc_t dlsym_proc =
        (db_gl_generic_proc_t)dlsym(RTLD_DEFAULT, name);
    if (dlsym_proc != NULL) {
        return dlsym_proc;
    }
#endif

#ifdef DB_HAS_GLFW
    db_gl_generic_proc_t glfw_proc =
        (db_gl_generic_proc_t)glfwGetProcAddress(name);
    if (glfw_proc != NULL) {
        return glfw_proc;
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

    g_upload_proc_table.bind_buffer =
        (db_gl_bind_buffer_fn_t)(db_gl_get_proc("glBindBuffer"));
    if (g_upload_proc_table.bind_buffer == NULL) {
        g_upload_proc_table.bind_buffer =
            (db_gl_bind_buffer_fn_t)(db_gl_get_proc("glBindBufferARB"));
    }

    g_upload_proc_table.buffer_data =
        (db_gl_buffer_data_fn_t)(db_gl_get_proc("glBufferData"));
    if (g_upload_proc_table.buffer_data == NULL) {
        g_upload_proc_table.buffer_data =
            (db_gl_buffer_data_fn_t)(db_gl_get_proc("glBufferDataARB"));
    }

    g_upload_proc_table.buffer_sub_data =
        (db_gl_buffer_sub_data_fn_t)(db_gl_get_proc("glBufferSubData"));
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        g_upload_proc_table.buffer_sub_data =
            (db_gl_buffer_sub_data_fn_t)(db_gl_get_proc("glBufferSubDataARB"));
    }

    g_upload_proc_table.gen_buffers =
        (db_gl_gen_buffers_fn_t)(db_gl_get_proc("glGenBuffers"));
    if (g_upload_proc_table.gen_buffers == NULL) {
        g_upload_proc_table.gen_buffers =
            (db_gl_gen_buffers_fn_t)(db_gl_get_proc("glGenBuffersARB"));
    }

    g_upload_proc_table.delete_buffers =
        (db_gl_delete_buffers_fn_t)(db_gl_get_proc("glDeleteBuffers"));
    if (g_upload_proc_table.delete_buffers == NULL) {
        g_upload_proc_table.delete_buffers =
            (db_gl_delete_buffers_fn_t)(db_gl_get_proc("glDeleteBuffersARB"));
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
    if (g_upload_proc_table.bind_buffer == NULL) {
        g_upload_proc_table.bind_buffer = (db_gl_bind_buffer_fn_t)glBindBuffer;
    }
    if (g_upload_proc_table.buffer_data == NULL) {
        g_upload_proc_table.buffer_data = (db_gl_buffer_data_fn_t)glBufferData;
    }
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        g_upload_proc_table.buffer_sub_data =
            (db_gl_buffer_sub_data_fn_t)glBufferSubData;
    }
    if (g_upload_proc_table.gen_buffers == NULL) {
        g_upload_proc_table.gen_buffers = (db_gl_gen_buffers_fn_t)glGenBuffers;
    }
    if (g_upload_proc_table.delete_buffers == NULL) {
        g_upload_proc_table.delete_buffers =
            (db_gl_delete_buffers_fn_t)glDeleteBuffers;
    }
#endif

    g_upload_proc_table.loaded = 1;
}

int db_gl_has_vbo_support(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    return db_gl_runtime_supports_vbo(version, exts);
}

int db_gl_has_pbo_upload_support(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_has_pbo_upload_support");
    return (g_upload_proc_table.bind_buffer != NULL) &&
           (g_upload_proc_table.buffer_data != NULL) &&
           (g_upload_proc_table.buffer_sub_data != NULL) &&
           (g_upload_proc_table.gen_buffers != NULL) &&
           (g_upload_proc_table.delete_buffers != NULL);
}

unsigned int db_gl_pbo_create_or_zero(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_create_or_zero");
    if (g_upload_proc_table.gen_buffers == NULL) {
        return 0U;
    }
    GLuint pbo = 0U;
    g_upload_proc_table.gen_buffers(1, &pbo);
    return (unsigned int)pbo;
}

void db_gl_pbo_delete_if_valid(unsigned int pbo) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_delete_if_valid");
    if ((pbo == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_pbo = (GLuint)pbo;
    g_upload_proc_table.delete_buffers(1, &gl_pbo);
}

void db_gl_pbo_unbind_unpack(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_unbind_unpack");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return;
    }
    g_upload_proc_table.bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0U);
}

static int db_gl_verify_buffer_prefix(const uint8_t *expected,
                                      size_t expected_size) {
    if (expected_size == 0U) {
        return 0;
    }

    db_gl_require_upload_proc_table_loaded("db_gl_verify_buffer_prefix");
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

    db_gl_require_upload_proc_table_loaded("db_gl_probe_upload_capabilities");

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

static void
db_gl_upload_ranges_subdata_target(GLenum target, const void *source_base,
                                   const db_gl_upload_range_t *ranges,
                                   size_t range_count) {
    const uint8_t *src_base = (const uint8_t *)source_base;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        g_upload_proc_table.buffer_sub_data(
            target, (GLintptr)range->dst_offset_bytes,
            (GLsizeiptr)range->size_bytes, src_base + range->src_offset_bytes);
    }
}

void db_gl_upload_ranges_target(
    const void *source_base, size_t total_bytes,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_gl_upload_target_t target, unsigned int target_buffer,
    int use_persistent_upload, void *persistent_mapped_ptr,
    int use_map_range_upload, int use_map_buffer_upload) {
    if ((source_base == NULL) || (ranges == NULL) || (range_count == 0U)) {
        return;
    }

    const int is_vbo = (target == DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER) ? 1 : 0;
    const GLenum gl_target = (target == DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER)
                                 ? GL_PIXEL_UNPACK_BUFFER
                                 : GL_ARRAY_BUFFER;

    if ((is_vbo != 0) && (use_persistent_upload != 0) &&
        (persistent_mapped_ptr != NULL)) {
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

    db_gl_require_upload_proc_table_loaded("db_gl_upload_ranges_target");
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        return;
    }

    if (is_vbo == 0) {
        if ((target_buffer == 0U) ||
            (g_upload_proc_table.bind_buffer == NULL) ||
            (g_upload_proc_table.buffer_data == NULL) ||
            (total_bytes > (size_t)PTRDIFF_MAX)) {
            return;
        }
        const GLuint gl_buffer = (GLuint)target_buffer;
        g_upload_proc_table.bind_buffer(gl_target, gl_buffer);
        g_upload_proc_table.buffer_data(gl_target, (GLsizeiptr)total_bytes,
                                        NULL, GL_STREAM_DRAW);
        db_gl_upload_ranges_subdata_target(gl_target, source_base, ranges,
                                           range_count);
        return;
    }

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
            db_gl_upload_ranges_subdata_target(gl_target, source_base, ranges,
                                               range_count);
        }
        return;
    }

    if (g_upload_proc_table.buffer_sub_data != NULL) {
        db_gl_upload_ranges_subdata_target(gl_target, source_base, ranges,
                                           range_count);
    } else {
        db_gl_upload_ranges_subdata(source_base, ranges, range_count);
    }
}

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload) {
    const db_gl_upload_range_t full_range = {0U, 0U, bytes};
    db_gl_upload_ranges_target(source, bytes, &full_range, 1U,
                               DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                               use_persistent_upload, persistent_mapped_ptr,
                               use_map_range_upload, use_map_buffer_upload);
}

void db_gl_unmap_current_array_buffer(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_unmap_current_array_buffer");
    if (g_upload_proc_table.unmap_buffer != NULL) {
        (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
    }
}

size_t db_gl_collect_row_upload_ranges(
    uint32_t row_unit_width, uint32_t row_count_total, size_t unit_stride_bytes,
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_dirty_row_range_t *out_rows, db_gl_upload_range_t *out_ranges,
    size_t out_capacity) {
    if ((row_unit_width == 0U) || (row_count_total == 0U) ||
        (unit_stride_bytes == 0U) || (out_ranges == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }

    if (unit_stride_bytes > (SIZE_MAX / (size_t)row_unit_width)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)row_unit_width * unit_stride_bytes;
    if (row_bytes > (SIZE_MAX / (size_t)row_count_total)) {
        return 0U;
    }

    if (dirty_count == 0U) {
        return 0U;
    }

    size_t span_count = 0U;
    for (size_t i = 0U; (i < dirty_count) && (span_count < out_capacity); i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count = dirty_ranges[i].row_count;
        if ((row_start >= row_count_total) || (row_count == 0U)) {
            continue;
        }
        const uint32_t clamped_count =
            db_u32_min(row_count, row_count_total - row_start);
        if (clamped_count == 0U) {
            continue;
        }
        const size_t byte_offset = row_bytes * (size_t)row_start;
        const size_t byte_size = row_bytes * (size_t)clamped_count;
        if (out_rows != NULL) {
            out_rows[span_count] = (db_dirty_row_range_t){
                .row_start = row_start, .row_count = clamped_count};
        }
        out_ranges[span_count] = (db_gl_upload_range_t){
            .dst_offset_bytes = byte_offset,
            .src_offset_bytes = byte_offset,
            .size_bytes = byte_size,
        };
        span_count++;
    }
    return span_count;
}

size_t db_gl_collect_span_upload_ranges(
    uint32_t row_unit_width, size_t dst_unit_stride_bytes,
    size_t src_unit_stride_bytes, const db_snake_col_span_t *spans,
    size_t span_count, db_gl_upload_range_t *out_ranges, size_t out_capacity) {
    if ((row_unit_width == 0U) || (dst_unit_stride_bytes == 0U) ||
        (src_unit_stride_bytes == 0U) || (spans == NULL) ||
        (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }

    size_t upload_count = 0U;
    for (size_t i = 0U; (i < span_count) && (upload_count < out_capacity);
         i++) {
        const uint32_t row = spans[i].row;
        const uint32_t col_start = spans[i].col_start;
        const uint32_t col_end = spans[i].col_end;
        if ((col_end <= col_start) || (col_end > row_unit_width)) {
            continue;
        }
        const uint32_t span_units = col_end - col_start;
        const uint32_t first_unit = (row * row_unit_width) + col_start;
        const size_t dst_offset = (size_t)first_unit * dst_unit_stride_bytes;
        const size_t src_offset = (size_t)first_unit * src_unit_stride_bytes;
        const size_t span_bytes = (size_t)span_units * dst_unit_stride_bytes;
        out_ranges[upload_count] = (db_gl_upload_range_t){
            .dst_offset_bytes = dst_offset,
            .src_offset_bytes = src_offset,
            .size_bytes = span_bytes,
        };
        upload_count++;
    }
    return upload_count;
}

size_t
db_gl_collect_damage_upload_ranges(const db_gl_damage_upload_plan_t *plan,
                                   db_gl_upload_range_t *out_ranges,
                                   size_t out_capacity) {
    if ((plan == NULL) || (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }
    if (plan->force_full_upload != 0) {
        if (plan->total_bytes == 0U) {
            return 0U;
        }
        out_ranges[0] = (db_gl_upload_range_t){
            .dst_offset_bytes = 0U,
            .src_offset_bytes = 0U,
            .size_bytes = plan->total_bytes,
        };
        return 1U;
    }
    if ((plan->spans != NULL) && (plan->span_count > 0U)) {
        return db_gl_collect_span_upload_ranges(
            plan->row_unit_width, plan->unit_stride_bytes,
            plan->unit_stride_bytes, plan->spans, plan->span_count, out_ranges,
            out_capacity);
    }
    if ((plan->dirty_rows == NULL) || (plan->dirty_row_count == 0U)) {
        return 0U;
    }
    return db_gl_collect_row_upload_ranges(
        plan->row_unit_width, plan->row_count_total, plan->unit_stride_bytes,
        plan->dirty_rows, plan->dirty_row_count, NULL, out_ranges,
        out_capacity);
}

size_t
db_gl_collect_pattern_upload_ranges(const db_gl_pattern_upload_collect_t *ctx,
                                    db_gl_upload_range_t *out_ranges,
                                    size_t out_capacity) {
    if ((ctx == NULL) || (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }

    db_gl_damage_upload_plan_t upload_plan = {
        .row_unit_width = ctx->cols,
        .row_count_total = ctx->rows,
        .unit_stride_bytes = ctx->upload_tile_bytes,
        .total_bytes = ctx->upload_bytes,
        .force_full_upload = 0,
        .dirty_rows = NULL,
        .dirty_row_count = 0U,
        .spans = NULL,
        .span_count = 0U,
    };

    if (ctx->use_damage_row_ranges != 0) {
        upload_plan.dirty_rows = ctx->damage_row_ranges;
        upload_plan.dirty_row_count = ctx->damage_row_count;
        upload_plan.force_full_upload = ctx->force_full_upload;
        return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                  out_capacity);
    }

    if ((ctx->pattern == DB_PATTERN_SNAKE_GRID) ||
        (ctx->pattern == DB_PATTERN_SNAKE_RECT) ||
        (ctx->pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (ctx->pattern == DB_PATTERN_SNAKE_GRID);
        const db_snake_plan_t empty_plan = {0};
        const db_snake_plan_t *plan =
            (ctx->snake_plan != NULL) ? ctx->snake_plan : &empty_plan;
        if ((is_grid == 0) &&
            ((ctx->force_full_upload != 0) || (plan->batch_size == 0U))) {
            upload_plan.force_full_upload = 1;
            return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                      1U);
        }
        const db_snake_region_t region =
            (is_grid != 0)
                ? (db_snake_region_t){
                      .x = 0U,
                      .y = 0U,
                      .width = ctx->cols,
                      .height = ctx->rows,
                      .color_r = 0.0F,
                      .color_g = 0.0F,
                      .color_b = 0.0F,
                  }
                : db_snake_region_from_index(ctx->pattern_seed,
                                             plan->active_shape_index);
        if ((region.width == 0U) || (region.height == 0U) ||
            (ctx->snake_spans == NULL)) {
            return 0U;
        }
        const uint32_t settled_count =
            (is_grid != 0) ? plan->prev_count : ctx->snake_prev_count;
        const size_t max_ranges =
            (size_t)settled_count + (size_t)plan->batch_size;
        if ((max_ranges == 0U) || (max_ranges > ctx->snake_scratch_capacity)) {
            return 0U;
        }
        db_snake_shape_cache_t shape_cache = {0};
        const db_snake_shape_cache_t *shape_cache_ptr = NULL;
        if ((ctx->pattern == DB_PATTERN_SNAKE_SHAPES) &&
            (ctx->snake_row_bounds != NULL)) {
            const db_snake_shape_kind_t shape_kind =
                db_snake_shapes_kind_from_index(ctx->pattern_seed,
                                                plan->active_shape_index,
                                                DB_PALETTE_SALT);
            if (db_snake_shape_cache_init_from_index(
                    &shape_cache, ctx->snake_row_bounds,
                    ctx->snake_row_bounds_capacity, ctx->pattern_seed,
                    plan->active_shape_index, DB_PALETTE_SALT, &region,
                    shape_kind) != 0) {
                shape_cache_ptr = &shape_cache;
            }
        }
        const size_t span_count = db_snake_collect_damage_spans(
            ctx->snake_spans, max_ranges, &region,
            (is_grid != 0) ? plan->prev_start : ctx->snake_prev_start,
            settled_count, plan->active_cursor, plan->batch_size,
            shape_cache_ptr);
        upload_plan.spans = ctx->snake_spans;
        upload_plan.span_count = span_count;
        return db_gl_collect_damage_upload_ranges(
            &upload_plan, out_ranges,
            (max_ranges < out_capacity) ? max_ranges : out_capacity);
    }

    if ((ctx->pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (ctx->pattern == DB_PATTERN_GRADIENT_FILL)) {
        upload_plan.dirty_rows = ctx->damage_row_ranges;
        upload_plan.dirty_row_count = ctx->damage_row_count;
        upload_plan.force_full_upload = ctx->force_full_upload;
        return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                  out_capacity);
    }

    upload_plan.force_full_upload = 1;
    return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges, 1U);
}

size_t db_gl_for_each_upload_row_span(const char *backend_name,
                                      uint32_t row_unit_width,
                                      const db_gl_upload_range_t *ranges,
                                      size_t range_count,
                                      db_gl_upload_row_span_apply_fn_t apply_fn,
                                      void *user_data) {
    if ((backend_name == NULL) || (row_unit_width == 0U) || (ranges == NULL) ||
        (range_count == 0U) || (apply_fn == NULL)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)db_checked_mul_u32(
        backend_name, "upload_row_bytes", row_unit_width, 4U);
    if (row_bytes == 0U) {
        return 0U;
    }

    size_t applied_count = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->size_bytes % row_bytes) != 0U) ||
            ((range->dst_offset_bytes % row_bytes) != 0U)) {
            continue;
        }
        const db_gl_upload_row_span_t span = {
            .range = *range,
            .rows =
                (db_dirty_row_range_t){
                    .row_start = db_checked_size_to_u32(
                        backend_name, "upload_row_start",
                        range->dst_offset_bytes / row_bytes),
                    .row_count =
                        db_checked_size_to_u32(backend_name, "upload_row_count",
                                               range->size_bytes / row_bytes),
                },
        };
        apply_fn(&span, user_data);
        applied_count++;
    }
    return applied_count;
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
