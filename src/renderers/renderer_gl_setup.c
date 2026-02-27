#include "renderer_gl_common.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#include <dlfcn.h>
#define DB_HAS_DLSYM_PROC_ADDRESS 1
#elifdef __linux__
#include <dlfcn.h>
#define DB_HAS_DLSYM_PROC_ADDRESS 1
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
typedef struct {
    db_gl_bind_buffer_fn_t bind_buffer;
    db_gl_buffer_data_fn_t buffer_data;
    db_gl_buffer_storage_fn_t buffer_storage;
    db_gl_buffer_sub_data_fn_t buffer_sub_data;
    db_gl_delete_buffers_fn_t delete_buffers;
    db_gl_gen_buffers_fn_t gen_buffers;
    db_gl_get_buffer_sub_data_fn_t get_buffer_sub_data;
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    int loaded;
} db_gl_upload_proc_table_t;

static db_gl_upload_proc_table_t g_upload_proc_table = {0};
static db_gl_proc_resolver_fn_t g_proc_resolver = NULL;

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

const char *
db_gl_cap_upload_mode_from_probe(int has_vbo,
                                 const db_gl_upload_probe_result_t *upload) {
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

const char *db_gl_cap_mode_gl3_shader(const db_gl_upload_probe_result_t *upload,
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

int db_gl_runtime_supports_buffer_storage(const char *version_text,
                                          const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_has_gl_extension_token(exts, "GL_EXT_buffer_storage");
    }

    return db_has_gl_extension_token(exts, "GL_ARB_buffer_storage") ||
           db_gl_version_text_at_least(version_text, 4, 4);
}

int db_gl_runtime_supports_map_buffer(const char *version_text,
                                      const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_has_gl_extension_token(exts, "GL_OES_mapbuffer");
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

int db_gl_runtime_supports_pbo(const char *version_text, const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_gl_version_text_at_least(version_text, 3, 0) ||
               db_has_gl_extension_token(exts, "GL_EXT_pixel_buffer_object");
    }

    return db_gl_version_text_at_least(version_text, 2, 1) ||
           db_has_gl_extension_token(exts, "GL_ARB_pixel_buffer_object");
}

int db_gl_runtime_supports_vbo(const char *version_text, const char *exts) {
    if (db_gl_is_es_context(version_text) != 0) {
        return db_gl_version_text_at_least(version_text, 1, 1);
    }

    return db_has_gl_extension_token(exts, "GL_ARB_vertex_buffer_object") ||
           db_gl_version_text_at_least(version_text, 1, 5);
}

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

    if (g_proc_resolver != NULL) {
        db_gl_generic_proc_t resolver_proc = g_proc_resolver(name);
        if (resolver_proc != NULL) {
            return resolver_proc;
        }
    }

#ifdef DB_HAS_DLSYM_PROC_ADDRESS
    db_gl_generic_proc_t dlsym_proc =
        (db_gl_generic_proc_t)dlsym(RTLD_DEFAULT, name);
    if (dlsym_proc != NULL) {
        return dlsym_proc;
    }
#endif

    return NULL;
}

void db_gl_set_proc_resolver(db_gl_proc_resolver_fn_t resolver) {
    if ((g_upload_proc_table.loaded != 0) && (g_proc_resolver != resolver)) {
        db_failf("renderer_gl_common",
                 "db_gl_set_proc_resolver must be called before proc table "
                 "preload");
    }
    g_proc_resolver = resolver;
}

static void db_gl_load_upload_proc_table(void) {
    if (g_upload_proc_table.loaded != 0) {
        return;
    }

    g_upload_proc_table.bind_buffer =
        (db_gl_bind_buffer_fn_t)(db_gl_get_proc("glBindBuffer"));
    if (g_upload_proc_table.bind_buffer == NULL) {
        g_upload_proc_table.bind_buffer =
            (db_gl_bind_buffer_fn_t)(db_gl_get_proc("glBindBufferARB"));
    }
    if (g_upload_proc_table.bind_buffer == NULL) {
        g_upload_proc_table.bind_buffer =
            (db_gl_bind_buffer_fn_t)(db_gl_get_proc("glBindBufferOES"));
    }

    g_upload_proc_table.buffer_data =
        (db_gl_buffer_data_fn_t)(db_gl_get_proc("glBufferData"));
    if (g_upload_proc_table.buffer_data == NULL) {
        g_upload_proc_table.buffer_data =
            (db_gl_buffer_data_fn_t)(db_gl_get_proc("glBufferDataARB"));
    }
    if (g_upload_proc_table.buffer_data == NULL) {
        g_upload_proc_table.buffer_data =
            (db_gl_buffer_data_fn_t)(db_gl_get_proc("glBufferDataOES"));
    }

    g_upload_proc_table.buffer_storage =
        (db_gl_buffer_storage_fn_t)(db_gl_get_proc("glBufferStorage"));
    if (g_upload_proc_table.buffer_storage == NULL) {
        g_upload_proc_table.buffer_storage =
            (db_gl_buffer_storage_fn_t)(db_gl_get_proc("glBufferStorageEXT"));
    }

    g_upload_proc_table.buffer_sub_data =
        (db_gl_buffer_sub_data_fn_t)(db_gl_get_proc("glBufferSubData"));
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        g_upload_proc_table.buffer_sub_data =
            (db_gl_buffer_sub_data_fn_t)(db_gl_get_proc("glBufferSubDataARB"));
    }
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        g_upload_proc_table.buffer_sub_data =
            (db_gl_buffer_sub_data_fn_t)(db_gl_get_proc("glBufferSubDataOES"));
    }

    g_upload_proc_table.delete_buffers =
        (db_gl_delete_buffers_fn_t)(db_gl_get_proc("glDeleteBuffers"));
    if (g_upload_proc_table.delete_buffers == NULL) {
        g_upload_proc_table.delete_buffers =
            (db_gl_delete_buffers_fn_t)(db_gl_get_proc("glDeleteBuffersARB"));
    }
    if (g_upload_proc_table.delete_buffers == NULL) {
        g_upload_proc_table.delete_buffers =
            (db_gl_delete_buffers_fn_t)(db_gl_get_proc("glDeleteBuffersOES"));
    }

    g_upload_proc_table.gen_buffers =
        (db_gl_gen_buffers_fn_t)(db_gl_get_proc("glGenBuffers"));
    if (g_upload_proc_table.gen_buffers == NULL) {
        g_upload_proc_table.gen_buffers =
            (db_gl_gen_buffers_fn_t)(db_gl_get_proc("glGenBuffersARB"));
    }
    if (g_upload_proc_table.gen_buffers == NULL) {
        g_upload_proc_table.gen_buffers =
            (db_gl_gen_buffers_fn_t)(db_gl_get_proc("glGenBuffersOES"));
    }

    g_upload_proc_table.get_buffer_sub_data =
        (db_gl_get_buffer_sub_data_fn_t)(db_gl_get_proc("glGetBufferSubData"));
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        g_upload_proc_table.get_buffer_sub_data =
            (db_gl_get_buffer_sub_data_fn_t)(db_gl_get_proc(
                "glGetBufferSubDataARB"));
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

    g_upload_proc_table.map_buffer_range =
        (db_gl_map_buffer_range_fn_t)(db_gl_get_proc("glMapBufferRange"));
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)(db_gl_get_proc(
                "glMapBufferRangeEXT"));
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

#if defined(DB_HAS_OPENGL_DESKTOP) && !defined(__APPLE__)
    if (g_upload_proc_table.bind_buffer == NULL) {
        g_upload_proc_table.bind_buffer = (db_gl_bind_buffer_fn_t)glBindBuffer;
    }
    if (g_upload_proc_table.buffer_data == NULL) {
        g_upload_proc_table.buffer_data = (db_gl_buffer_data_fn_t)glBufferData;
    }
#if defined(GL_VERSION_4_4) || defined(GL_ARB_buffer_storage)
    if (g_upload_proc_table.buffer_storage == NULL) {
        g_upload_proc_table.buffer_storage =
            (db_gl_buffer_storage_fn_t)glBufferStorage;
    }
#endif
    if (g_upload_proc_table.buffer_sub_data == NULL) {
        g_upload_proc_table.buffer_sub_data =
            (db_gl_buffer_sub_data_fn_t)glBufferSubData;
    }
    if (g_upload_proc_table.delete_buffers == NULL) {
        g_upload_proc_table.delete_buffers =
            (db_gl_delete_buffers_fn_t)glDeleteBuffers;
    }
    if (g_upload_proc_table.gen_buffers == NULL) {
        g_upload_proc_table.gen_buffers = (db_gl_gen_buffers_fn_t)glGenBuffers;
    }
    if (g_upload_proc_table.get_buffer_sub_data == NULL) {
        g_upload_proc_table.get_buffer_sub_data =
            (db_gl_get_buffer_sub_data_fn_t)glGetBufferSubData;
    }
    if (g_upload_proc_table.map_buffer == NULL) {
        g_upload_proc_table.map_buffer = (db_gl_map_buffer_fn_t)glMapBuffer;
    }
#if defined(GL_VERSION_3_0) || defined(GL_ARB_map_buffer_range)
    if (g_upload_proc_table.map_buffer_range == NULL) {
        g_upload_proc_table.map_buffer_range =
            (db_gl_map_buffer_range_fn_t)glMapBufferRange;
    }
#endif
    if (g_upload_proc_table.unmap_buffer == NULL) {
        g_upload_proc_table.unmap_buffer =
            (db_gl_unmap_buffer_fn_t)glUnmapBuffer;
    }
#endif

    g_upload_proc_table.loaded = 1;
}

int db_gl_vbo_bind(unsigned int buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_bind");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return 0;
    }
    g_upload_proc_table.bind_buffer(GL_ARRAY_BUFFER, (GLuint)buffer);
    return 1;
}

int db_gl_bind_array_buffer_cached(unsigned int buffer,
                                   unsigned int *cached_buffer) {
    if ((cached_buffer != NULL) && (*cached_buffer == buffer)) {
        return 1;
    }
    if (db_gl_vbo_bind(buffer) == 0) {
        return 0;
    }
    if (cached_buffer != NULL) {
        *cached_buffer = buffer;
    }
    return 1;
}

int db_gl_vbo_create_or_zero(unsigned int *out_buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_create_or_zero");
    if (out_buffer == NULL) {
        return 0;
    }
    *out_buffer = 0U;
    if (g_upload_proc_table.gen_buffers == NULL) {
        return 0;
    }
    GLuint buffer = 0U;
    g_upload_proc_table.gen_buffers(1, &buffer);
    *out_buffer = (unsigned int)buffer;
    return (buffer != 0U) ? 1 : 0;
}

void db_gl_vbo_delete_if_valid(unsigned int buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_delete_if_valid");
    if ((buffer == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_buffer = (GLuint)buffer;
    g_upload_proc_table.delete_buffers(1, &gl_buffer);
}

int db_gl_vbo_init_data(size_t bytes, const void *data, unsigned int usage) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_init_data");
    if (g_upload_proc_table.buffer_data == NULL) {
        return 0;
    }
    g_upload_proc_table.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data,
                                    (GLenum)usage);
    return (glGetError() == GL_NO_ERROR) ? 1 : 0;
}

int db_gl_vbo_init_static_data(size_t bytes, const void *data) {
    return db_gl_vbo_init_data(bytes, data, GL_STATIC_DRAW);
}

int db_gl_context_supports_pbo_upload(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_context_supports_pbo_upload");
    return (g_upload_proc_table.bind_buffer != NULL) &&
           (g_upload_proc_table.buffer_data != NULL) &&
           (g_upload_proc_table.buffer_sub_data != NULL) &&
           (g_upload_proc_table.gen_buffers != NULL) &&
           (g_upload_proc_table.delete_buffers != NULL);
}

int db_gl_context_supports_vbo(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    return db_gl_runtime_supports_vbo(version, exts);
}

void db_gl_quad_init(float *verts) {
    verts[DB_GL_QUAD_V0_X] = -1.0F;
    verts[DB_GL_QUAD_V0_Y] = -1.0F;
    verts[DB_GL_QUAD_V1_X] = 1.0F;
    verts[DB_GL_QUAD_V1_Y] = -1.0F;
    verts[DB_GL_QUAD_V2_X] = -1.0F;
    verts[DB_GL_QUAD_V2_Y] = 1.0F;
    verts[DB_GL_QUAD_V3_X] = 1.0F;
    verts[DB_GL_QUAD_V3_Y] = 1.0F;
}

void db_gl_set_viewport_px(int width_px, int height_px) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return;
    }
    glViewport(0, 0, (GLsizei)width_px, (GLsizei)height_px);
}

static void db_gl_texture_set_nearest_clamp_2d(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#ifdef GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
#endif
}

int db_gl_texture_allocate_rgba(unsigned int texture, int width, int height,
                                unsigned int internal_format,
                                const void *pixels) {
    if ((texture == 0U) || (width <= 0) || (height <= 0)) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    db_gl_texture_set_nearest_clamp_2d();
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, (GLsizei)width,
                 (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return (glGetError() == GL_NO_ERROR) ? 1 : 0;
}

int db_gl_texture_create_rgba(unsigned int *out_texture, int width, int height,
                              unsigned int internal_format,
                              const void *pixels) {
    if (out_texture == NULL) {
        return 0;
    }
    *out_texture = 0U;
    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    if (texture == 0U) {
        return 0;
    }
    if (db_gl_texture_allocate_rgba((unsigned int)texture, width, height,
                                    internal_format, pixels) == 0) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    *out_texture = (unsigned int)texture;
    return 1;
}

void db_gl_texture_delete_if_valid(unsigned int *texture) {
    if ((texture == NULL) || (*texture == 0U)) {
        return;
    }
    const GLuint gl_texture = (GLuint)(*texture);
    glDeleteTextures(1, &gl_texture);
    *texture = 0U;
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

void db_gl_preload_upload_proc_table(void) { db_gl_load_upload_proc_table(); }

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
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
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

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
    return glGetError() == GL_NO_ERROR;
}

static int db_gl_probe_map_buffer_upload(size_t bytes,
                                         const float *initial_vertices) {
    if ((g_upload_proc_table.map_buffer == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
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

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
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
    if (db_gl_context_supports_vbo() == 0) {
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

    if (g_upload_proc_table.buffer_data == NULL) {
        return;
    }
    g_upload_proc_table.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)bytes,
                                    initial_vertices, GL_DYNAMIC_DRAW);
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

    db_gl_upload_ranges_subdata_target(gl_target, source_base, ranges,
                                       range_count);
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
