#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_upload_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_common.h"

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

typedef void *(*db_gl_map_buffer_fn_t)(GLenum target, GLenum access);
typedef GLboolean (*db_gl_unmap_buffer_fn_t)(GLenum target);
typedef void (*db_gl_active_texture_fn_t)(GLenum texture);
typedef void (*db_gl_attach_shader_fn_t)(GLuint program, GLuint shader);
typedef void (*db_gl_bind_framebuffer_fn_t)(GLenum target, GLuint framebuffer);
typedef void (*db_gl_bind_vertex_array_fn_t)(GLuint array);
typedef void (*db_gl_clear_color_fn_t)(GLfloat red, GLfloat green, GLfloat blue,
                                       GLfloat alpha);
typedef void (*db_gl_clear_fn_t)(GLbitfield mask);
typedef void (*db_gl_color_pointer_fn_t)(GLint size, GLenum type,
                                         GLsizei stride, const void *pointer);
typedef void (*db_gl_delete_textures_fn_t)(GLsizei count,
                                           const GLuint *textures);
typedef void (*db_gl_disable_client_state_fn_t)(GLenum cap);
typedef void (*db_gl_disable_fn_t)(GLenum cap);
typedef void (*db_gl_draw_arrays_fn_t)(GLenum mode, GLint first, GLsizei count);
typedef void (*db_gl_blit_framebuffer_fn_t)(GLint src_x0, GLint src_y0,
                                            GLint src_x1, GLint src_y1,
                                            GLint dst_x0, GLint dst_y0,
                                            GLint dst_x1, GLint dst_y1,
                                            GLbitfield mask, GLenum filter);
typedef GLenum (*db_gl_check_framebuffer_status_fn_t)(GLenum target);
typedef void (*db_gl_compile_shader_fn_t)(GLuint shader);
typedef GLuint (*db_gl_create_program_fn_t)(void);
typedef GLuint (*db_gl_create_shader_fn_t)(GLenum shader_type);
typedef void (*db_gl_enable_client_state_fn_t)(GLenum cap);
typedef void (*db_gl_enable_fn_t)(GLenum cap);
typedef void (*db_gl_enable_vertex_attrib_array_fn_t)(GLuint index);
typedef GLenum (*db_gl_get_error_raw_fn_t)(void);
typedef void (*db_gl_get_buffer_sub_data_fn_t)(GLenum target, GLintptr offset,
                                               GLsizeiptr size, void *data);
typedef void (*db_gl_delete_framebuffers_fn_t)(GLsizei count,
                                               const GLuint *framebuffers);
typedef void (*db_gl_delete_program_fn_t)(GLuint program);
typedef void (*db_gl_delete_shader_fn_t)(GLuint shader);
typedef void (*db_gl_delete_vertex_arrays_fn_t)(GLsizei count,
                                                const GLuint *arrays);
typedef void (*db_gl_framebuffer_texture_2d_fn_t)(GLenum target,
                                                  GLenum attachment,
                                                  GLenum textarget,
                                                  GLuint texture, GLint level);
typedef void (*db_gl_gen_framebuffers_fn_t)(GLsizei count,
                                            GLuint *framebuffers);
typedef void (*db_gl_gen_vertex_arrays_fn_t)(GLsizei count, GLuint *arrays);
typedef void (*db_gl_get_integerv_fn_t)(GLenum pname, GLint *data);
typedef void (*db_gl_get_program_info_log_fn_t)(GLuint program,
                                                GLsizei max_length,
                                                GLsizei *length,
                                                char *info_log);
typedef void (*db_gl_get_program_iv_fn_t)(GLuint program, GLenum pname,
                                          GLint *params);
typedef void (*db_gl_get_shader_info_log_fn_t)(GLuint shader,
                                               GLsizei max_length,
                                               GLsizei *length, char *info_log);
typedef void (*db_gl_get_shader_iv_fn_t)(GLuint shader, GLenum pname,
                                         GLint *params);
typedef GLint (*db_gl_get_uniform_location_fn_t)(GLuint program,
                                                 const char *name);
typedef const GLubyte *(*db_gl_get_string_raw_fn_t)(GLenum name);
typedef const GLubyte *(*db_gl_get_stringi_raw_fn_t)(GLenum name, GLuint index);
typedef void *(*db_gl_map_buffer_range_fn_t)(GLenum target, GLintptr offset,
                                             GLsizeiptr length,
                                             GLbitfield access);
typedef void (*db_gl_pixel_storei_fn_t)(GLenum pname, GLint param);
typedef void (*db_gl_buffer_storage_fn_t)(GLenum target, GLsizeiptr size,
                                          const void *data, GLbitfield flags);
typedef void (*db_gl_read_pixels_fn_t)(GLint x_px, GLint y_px, GLsizei width,
                                       GLsizei height, GLenum format,
                                       GLenum type, void *pixels);
typedef void (*db_gl_bind_buffer_fn_t)(GLenum target, GLuint buffer);
typedef void (*db_gl_bind_texture_fn_t)(GLenum target, GLuint texture);
typedef void (*db_gl_buffer_data_fn_t)(GLenum target, GLsizeiptr size,
                                       const void *data, GLenum usage);
typedef void (*db_gl_buffer_sub_data_fn_t)(GLenum target, GLintptr offset,
                                           GLsizeiptr size, const void *data);
typedef void (*db_gl_gen_buffers_fn_t)(GLsizei count, GLuint *buffers);
typedef void (*db_gl_delete_buffers_fn_t)(GLsizei count, const GLuint *buffers);
typedef void (*db_gl_gen_textures_fn_t)(GLsizei count, GLuint *textures);
typedef void (*db_gl_tex_coord_pointer_fn_t)(GLint size, GLenum type,
                                             GLsizei stride,
                                             const void *pointer);
typedef void (*db_gl_tex_image_2d_fn_t)(GLenum target, GLint level,
                                        GLint internal_format, GLsizei width,
                                        GLsizei height, GLint border,
                                        GLenum format, GLenum type,
                                        const void *pixels);
typedef void (*db_gl_tex_parameteri_fn_t)(GLenum target, GLenum pname,
                                          GLint param);
typedef void (*db_gl_tex_sub_image_2d_fn_t)(GLenum target, GLint level,
                                            GLint xoffset, GLint yoffset,
                                            GLsizei width, GLsizei height,
                                            GLenum format, GLenum type,
                                            const void *pixels);
typedef void (*db_gl_link_program_fn_t)(GLuint program);
typedef void (*db_gl_shader_source_fn_t)(GLuint shader, GLsizei count,
                                         const char *const *strings,
                                         const GLint *lengths);
typedef void (*db_gl_uniform_1i_fn_t)(GLint location, GLint v0);
typedef void (*db_gl_uniform_1ui_fn_t)(GLint location, GLuint v0);
typedef void (*db_gl_uniform_3f_fn_t)(GLint location, GLfloat v0, GLfloat v1,
                                      GLfloat v2);
typedef void (*db_gl_use_program_fn_t)(GLuint program);
typedef void (*db_gl_vertex_pointer_fn_t)(GLint size, GLenum type,
                                          GLsizei stride, const void *pointer);
typedef void (*db_gl_vertex_attrib_pointer_fn_t)(GLuint index, GLint size,
                                                 GLenum type,
                                                 GLboolean normalized,
                                                 GLsizei stride,
                                                 const void *pointer);
typedef void (*db_gl_viewport_fn_t)(GLint x_px, GLint y_px, GLsizei width,
                                    GLsizei height);
typedef struct {
    db_gl_active_texture_fn_t active_texture;
    db_gl_attach_shader_fn_t attach_shader;
    db_gl_bind_buffer_fn_t bind_buffer;
    db_gl_bind_framebuffer_fn_t bind_framebuffer;
    db_gl_bind_texture_fn_t bind_texture;
    db_gl_bind_vertex_array_fn_t bind_vertex_array;
    db_gl_blit_framebuffer_fn_t blit_framebuffer;
    db_gl_buffer_data_fn_t buffer_data;
    db_gl_buffer_storage_fn_t buffer_storage;
    db_gl_buffer_sub_data_fn_t buffer_sub_data;
    db_gl_check_framebuffer_status_fn_t check_framebuffer_status;
    db_gl_clear_color_fn_t clear_color;
    db_gl_clear_fn_t clear;
    db_gl_color_pointer_fn_t color_pointer;
    db_gl_delete_buffers_fn_t delete_buffers;
    db_gl_delete_framebuffers_fn_t delete_framebuffers;
    db_gl_delete_program_fn_t delete_program;
    db_gl_delete_shader_fn_t delete_shader;
    db_gl_delete_textures_fn_t delete_textures;
    db_gl_delete_vertex_arrays_fn_t delete_vertex_arrays;
    db_gl_disable_client_state_fn_t disable_client_state;
    db_gl_disable_fn_t disable;
    db_gl_draw_arrays_fn_t draw_arrays;
    db_gl_enable_client_state_fn_t enable_client_state;
    db_gl_enable_fn_t enable;
    db_gl_enable_vertex_attrib_array_fn_t enable_vertex_attrib_array;
    db_gl_framebuffer_texture_2d_fn_t framebuffer_texture_2d;
    db_gl_get_error_raw_fn_t get_error;
    db_gl_gen_buffers_fn_t gen_buffers;
    db_gl_gen_framebuffers_fn_t gen_framebuffers;
    db_gl_gen_textures_fn_t gen_textures;
    db_gl_gen_vertex_arrays_fn_t gen_vertex_arrays;
    db_gl_get_buffer_sub_data_fn_t get_buffer_sub_data;
    db_gl_get_integerv_fn_t get_integerv;
    db_gl_get_program_info_log_fn_t get_program_info_log;
    db_gl_get_program_iv_fn_t get_program_iv;
    db_gl_pixel_storei_fn_t pixel_storei;
    db_gl_read_pixels_fn_t read_pixels;
    db_gl_get_shader_info_log_fn_t get_shader_info_log;
    db_gl_get_shader_iv_fn_t get_shader_iv;
    db_gl_get_string_raw_fn_t get_string;
    db_gl_get_stringi_raw_fn_t get_stringi;
    db_gl_get_uniform_location_fn_t get_uniform_location;
    db_gl_map_buffer_fn_t map_buffer;
    db_gl_map_buffer_range_fn_t map_buffer_range;
    db_gl_tex_coord_pointer_fn_t tex_coord_pointer;
    db_gl_tex_image_2d_fn_t tex_image_2d;
    db_gl_tex_parameteri_fn_t tex_parameteri;
    db_gl_tex_sub_image_2d_fn_t tex_sub_image_2d;
    db_gl_link_program_fn_t link_program;
    db_gl_shader_source_fn_t shader_source;
    db_gl_uniform_1i_fn_t uniform_1i;
    db_gl_uniform_1ui_fn_t uniform_1ui;
    db_gl_uniform_3f_fn_t uniform_3f;
    db_gl_unmap_buffer_fn_t unmap_buffer;
    db_gl_use_program_fn_t use_program;
    db_gl_vertex_attrib_pointer_fn_t vertex_attrib_pointer;
    db_gl_vertex_pointer_fn_t vertex_pointer;
    db_gl_viewport_fn_t viewport;
    db_gl_create_program_fn_t create_program;
    db_gl_create_shader_fn_t create_shader;
    db_gl_compile_shader_fn_t compile_shader;
    int loaded;
} db_gl_upload_proc_table_t;

typedef struct {
    const char *version_text;
    const char *extensions_text;
    int has_valid_version;
    int has_valid_extensions;
    int uses_indexed_extension_query;
    int is_es;
    int version_major;
    int version_minor;
} db_gl_runtime_metadata_t;

// Track a small fixed number of texture units for redundant bind elision.
#define DB_GL_TRACKED_TEXTURE_UNIT_COUNT 32U

static db_gl_upload_proc_table_t g_upload_proc_table = {0};
static db_gl_proc_resolver_fn_t g_proc_resolver = NULL;
static int g_blend_enabled_state = -1;
static int g_client_state_color_array_enabled = -1;
static int g_client_state_texcoord_array_enabled = -1;
static int g_client_state_vertex_array_enabled = -1;
static int g_cull_face_enabled_state = -1;
static int g_depth_test_enabled_state = -1;
static int g_dither_enabled_state = -1;
static unsigned int g_bound_draw_framebuffer = 0U;
static int g_bound_draw_framebuffer_valid = 0;
static unsigned int g_bound_read_framebuffer = 0U;
static int g_bound_read_framebuffer_valid = 0;
static unsigned int g_bound_vertex_array = 0U;
static int g_bound_vertex_array_valid = 0;
static unsigned int g_current_program = 0U;
static int g_current_program_valid = 0;
static unsigned int
    g_texture2d_binding_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT] = {0U};
static int g_texture2d_binding_valid_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT] =
    {0};
static unsigned int g_texture2d_enabled_state = 0U;
static int g_texture2d_enabled_state_valid = 0;
static unsigned int g_active_texture_unit = GL_TEXTURE0;
static int g_active_texture_unit_valid = 0;

// Section map:
// 1) Version/extension parsing and capability advertisement.
// 2) Proc resolver and proc table loading.
// 3) Wrapper APIs (buffer/texture/state/shader/program/pbo).
// 4) Context capability and active probe checks.
// 5) Upload path helpers and range upload execution.

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

// 1) Capability utility helpers (error clear/probe pattern/capability strings).
static void db_gl_probe_drain_errors(void) {
    if (g_upload_proc_table.get_error == NULL) {
        return;
    }
    while (g_upload_proc_table.get_error() != GL_NO_ERROR) {
    }
}

static int db_gl_probe_step_error_free(void) {
    return (g_upload_proc_table.get_error != NULL) &&
           (g_upload_proc_table.get_error() == GL_NO_ERROR);
}

static int db_gl_probe_finish(int success) {
    db_gl_probe_drain_errors();
    return (success != 0) ? 1 : 0;
}

static const uint8_t db_gl_probe_channel_high_threshold = 200U;
static const uint8_t db_gl_probe_channel_low_threshold = 80U;

static inline size_t db_gl_probe_rgba_pixel_offset(size_t width, size_t col,
                                                   size_t row) {
    return ((row * width) + col) * 4U;
}

static inline int db_gl_probe_rgb_matches(const uint8_t *pixels, size_t offset,
                                          uint8_t red, uint8_t green,
                                          uint8_t blue) {
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

static size_t db_gl_upload_probe_size_bytes(size_t bytes) {
    return (bytes < DB_GL_PROBE_PREFIX_BYTES) ? bytes
                                              : DB_GL_PROBE_PREFIX_BYTES;
}

static void db_gl_upload_probe_fill_pattern(uint8_t *pattern, size_t count) {
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

static int db_gl_runtime_has_extension(const db_gl_runtime_metadata_t *runtime,
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

static int
db_gl_runtime_has_usable_version(const db_gl_runtime_metadata_t *runtime) {
    return (runtime != NULL) && (runtime->has_valid_version != 0);
}

static int
db_gl_runtime_is_es_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es != 0);
}

static int
db_gl_runtime_is_desktop_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es == 0);
}

static int
db_gl_runtime_version_at_least(const db_gl_runtime_metadata_t *runtime,
                               int req_major, int req_minor) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           ((runtime->version_major > req_major) ||
            ((runtime->version_major == req_major) &&
             (runtime->version_minor >= req_minor)));
}

static int db_gl_runtime_supports_desktop_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_desktop_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

static int db_gl_runtime_supports_es_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_es_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

static db_gl_runtime_metadata_t db_gl_runtime_metadata_load(void) {
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

static int db_gl_extensions_advertise_buffer_storage(
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

static int
db_gl_extensions_advertise_map_buffer(const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_has_extension(runtime, "GL_OES_mapbuffer");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 1, 5, "GL_ARB_vertex_buffer_object");
}

static int db_gl_extensions_advertise_map_buffer_range(
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

static int
db_gl_extensions_advertise_pbo(const db_gl_runtime_metadata_t *runtime) {
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

static int db_gl_extensions_advertise_texture_float(
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

static int
db_gl_extensions_advertise_vbo(const db_gl_runtime_metadata_t *runtime) {
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
static void db_gl_require_upload_proc_table_loaded(const char *func_name) {
    if (g_upload_proc_table.loaded == 0) {
        db_failf("renderer_gl_common",
                 "%s requires preloaded GL proc table; call "
                 "db_gl_preload_upload_proc_table() during init",
                 func_name);
    }
}

static GLenum db_gl_get_error_value(void) {
    if (g_upload_proc_table.get_error == NULL) {
        return GL_NO_ERROR;
    }
    return g_upload_proc_table.get_error();
}

void db_gl_internal_require_upload_ready(const char *func_name) {
    db_gl_require_upload_proc_table_loaded(func_name);
}

db_gl_upload_ops_t db_gl_internal_get_upload_ops(void) {
    return (db_gl_upload_ops_t){
        .bind_buffer = g_upload_proc_table.bind_buffer,
        .buffer_data = g_upload_proc_table.buffer_data,
        .buffer_sub_data = g_upload_proc_table.buffer_sub_data,
        .map_buffer = g_upload_proc_table.map_buffer,
        .map_buffer_range = g_upload_proc_table.map_buffer_range,
        .unmap_buffer = g_upload_proc_table.unmap_buffer,
    };
}

GLenum db_gl_internal_get_error_value(void) { return db_gl_get_error_value(); }

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

#if defined(__APPLE__) || defined(__linux__)
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
    g_active_texture_unit = GL_TEXTURE0;
    g_active_texture_unit_valid = 0;
    g_blend_enabled_state = -1;
    g_client_state_color_array_enabled = -1;
    g_client_state_texcoord_array_enabled = -1;
    g_client_state_vertex_array_enabled = -1;
    g_cull_face_enabled_state = -1;
    g_depth_test_enabled_state = -1;
    g_dither_enabled_state = -1;
    g_bound_draw_framebuffer = 0U;
    g_bound_draw_framebuffer_valid = 0;
    g_bound_read_framebuffer = 0U;
    g_bound_read_framebuffer_valid = 0;
    g_bound_vertex_array = 0U;
    g_bound_vertex_array_valid = 0;
    g_current_program = 0U;
    g_current_program_valid = 0;
    g_texture2d_enabled_state = 0U;
    g_texture2d_enabled_state_valid = 0;
    for (size_t unit_index = 0U; unit_index < DB_GL_TRACKED_TEXTURE_UNIT_COUNT;
         unit_index++) {
        g_texture2d_binding_by_unit[unit_index] = 0U;
        g_texture2d_binding_valid_by_unit[unit_index] = 0;
    }
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

    g_upload_proc_table.bind_texture =
        (db_gl_bind_texture_fn_t)(db_gl_get_proc("glBindTexture"));
    g_upload_proc_table.active_texture =
        (db_gl_active_texture_fn_t)(db_gl_get_proc("glActiveTexture"));
    g_upload_proc_table.attach_shader =
        (db_gl_attach_shader_fn_t)(db_gl_get_proc("glAttachShader"));
    g_upload_proc_table.bind_framebuffer =
        (db_gl_bind_framebuffer_fn_t)(db_gl_get_proc("glBindFramebuffer"));
    g_upload_proc_table.bind_vertex_array =
        (db_gl_bind_vertex_array_fn_t)(db_gl_get_proc("glBindVertexArray"));
    g_upload_proc_table.blit_framebuffer =
        (db_gl_blit_framebuffer_fn_t)(db_gl_get_proc("glBlitFramebuffer"));
    g_upload_proc_table.clear = (db_gl_clear_fn_t)(db_gl_get_proc("glClear"));
    g_upload_proc_table.clear_color =
        (db_gl_clear_color_fn_t)(db_gl_get_proc("glClearColor"));
    g_upload_proc_table.check_framebuffer_status =
        (db_gl_check_framebuffer_status_fn_t)(db_gl_get_proc(
            "glCheckFramebufferStatus"));
    g_upload_proc_table.compile_shader =
        (db_gl_compile_shader_fn_t)(db_gl_get_proc("glCompileShader"));
    g_upload_proc_table.color_pointer =
        (db_gl_color_pointer_fn_t)(db_gl_get_proc("glColorPointer"));
    g_upload_proc_table.create_program =
        (db_gl_create_program_fn_t)(db_gl_get_proc("glCreateProgram"));
    g_upload_proc_table.create_shader =
        (db_gl_create_shader_fn_t)(db_gl_get_proc("glCreateShader"));
    g_upload_proc_table.delete_textures =
        (db_gl_delete_textures_fn_t)(db_gl_get_proc("glDeleteTextures"));
    g_upload_proc_table.delete_framebuffers =
        (db_gl_delete_framebuffers_fn_t)(db_gl_get_proc(
            "glDeleteFramebuffers"));
    g_upload_proc_table.delete_program =
        (db_gl_delete_program_fn_t)(db_gl_get_proc("glDeleteProgram"));
    g_upload_proc_table.delete_shader =
        (db_gl_delete_shader_fn_t)(db_gl_get_proc("glDeleteShader"));
    g_upload_proc_table.delete_vertex_arrays =
        (db_gl_delete_vertex_arrays_fn_t)(db_gl_get_proc(
            "glDeleteVertexArrays"));
    g_upload_proc_table.disable =
        (db_gl_disable_fn_t)(db_gl_get_proc("glDisable"));
    g_upload_proc_table.disable_client_state =
        (db_gl_disable_client_state_fn_t)(db_gl_get_proc(
            "glDisableClientState"));
    g_upload_proc_table.draw_arrays =
        (db_gl_draw_arrays_fn_t)(db_gl_get_proc("glDrawArrays"));
    g_upload_proc_table.enable =
        (db_gl_enable_fn_t)(db_gl_get_proc("glEnable"));
    g_upload_proc_table.enable_client_state =
        (db_gl_enable_client_state_fn_t)(db_gl_get_proc("glEnableClientState"));
    g_upload_proc_table.enable_vertex_attrib_array =
        (db_gl_enable_vertex_attrib_array_fn_t)(db_gl_get_proc(
            "glEnableVertexAttribArray"));
    g_upload_proc_table.framebuffer_texture_2d =
        (db_gl_framebuffer_texture_2d_fn_t)(db_gl_get_proc(
            "glFramebufferTexture2D"));
    g_upload_proc_table.gen_textures =
        (db_gl_gen_textures_fn_t)(db_gl_get_proc("glGenTextures"));
    g_upload_proc_table.gen_framebuffers =
        (db_gl_gen_framebuffers_fn_t)(db_gl_get_proc("glGenFramebuffers"));
    g_upload_proc_table.gen_vertex_arrays =
        (db_gl_gen_vertex_arrays_fn_t)(db_gl_get_proc("glGenVertexArrays"));
    g_upload_proc_table.get_error =
        (db_gl_get_error_raw_fn_t)(db_gl_get_proc("glGetError"));
    g_upload_proc_table.get_integerv =
        (db_gl_get_integerv_fn_t)(db_gl_get_proc("glGetIntegerv"));
    g_upload_proc_table.get_program_info_log =
        (db_gl_get_program_info_log_fn_t)(db_gl_get_proc(
            "glGetProgramInfoLog"));
    g_upload_proc_table.get_program_iv =
        (db_gl_get_program_iv_fn_t)(db_gl_get_proc("glGetProgramiv"));
    g_upload_proc_table.pixel_storei =
        (db_gl_pixel_storei_fn_t)(db_gl_get_proc("glPixelStorei"));
    g_upload_proc_table.read_pixels =
        (db_gl_read_pixels_fn_t)(db_gl_get_proc("glReadPixels"));
    g_upload_proc_table.get_shader_info_log =
        (db_gl_get_shader_info_log_fn_t)(db_gl_get_proc("glGetShaderInfoLog"));
    g_upload_proc_table.get_shader_iv =
        (db_gl_get_shader_iv_fn_t)(db_gl_get_proc("glGetShaderiv"));
    g_upload_proc_table.get_string =
        (db_gl_get_string_raw_fn_t)(db_gl_get_proc("glGetString"));
    g_upload_proc_table.get_stringi =
        (db_gl_get_stringi_raw_fn_t)(db_gl_get_proc("glGetStringi"));
    g_upload_proc_table.get_uniform_location =
        (db_gl_get_uniform_location_fn_t)(db_gl_get_proc(
            "glGetUniformLocation"));
    g_upload_proc_table.link_program =
        (db_gl_link_program_fn_t)(db_gl_get_proc("glLinkProgram"));
    g_upload_proc_table.shader_source =
        (db_gl_shader_source_fn_t)(db_gl_get_proc("glShaderSource"));
    g_upload_proc_table.tex_coord_pointer =
        (db_gl_tex_coord_pointer_fn_t)(db_gl_get_proc("glTexCoordPointer"));
    g_upload_proc_table.tex_image_2d =
        (db_gl_tex_image_2d_fn_t)(db_gl_get_proc("glTexImage2D"));
    g_upload_proc_table.tex_parameteri =
        (db_gl_tex_parameteri_fn_t)(db_gl_get_proc("glTexParameteri"));
    g_upload_proc_table.tex_sub_image_2d =
        (db_gl_tex_sub_image_2d_fn_t)(db_gl_get_proc("glTexSubImage2D"));
    g_upload_proc_table.uniform_1i =
        (db_gl_uniform_1i_fn_t)(db_gl_get_proc("glUniform1i"));
    g_upload_proc_table.uniform_1ui =
        (db_gl_uniform_1ui_fn_t)(db_gl_get_proc("glUniform1ui"));
    g_upload_proc_table.uniform_3f =
        (db_gl_uniform_3f_fn_t)(db_gl_get_proc("glUniform3f"));
    g_upload_proc_table.use_program =
        (db_gl_use_program_fn_t)(db_gl_get_proc("glUseProgram"));
    g_upload_proc_table.vertex_attrib_pointer =
        (db_gl_vertex_attrib_pointer_fn_t)(db_gl_get_proc(
            "glVertexAttribPointer"));
    g_upload_proc_table.vertex_pointer =
        (db_gl_vertex_pointer_fn_t)(db_gl_get_proc("glVertexPointer"));
    g_upload_proc_table.viewport =
        (db_gl_viewport_fn_t)(db_gl_get_proc("glViewport"));

    g_upload_proc_table.loaded = 1;
}

static int db_gl_context_supports_unpack_row_length_upload(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (g_upload_proc_table.pixel_storei == NULL) {
        return 0;
    }
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            &runtime, 3, 0, "GL_EXT_unpack_subimage");
    }
    // Desktop GL exposes GL_UNPACK_ROW_LENGTH in the core pixel-store API from
    // GL 1.2 onward.
    return db_gl_runtime_version_at_least(&runtime, 1, 2);
}

static void db_gl_quad_init(float *verts);
static void db_gl_set_unpack_row_length_pixels(int pixel_count);
static int db_gl_probe_texture_create_rgba16f(unsigned int *out_texture,
                                              int width, int height);

static int db_gl_probe_shadow_present_partial_upload_support_rgba8(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    if (db_gl_context_supports_unpack_row_length_upload() == 0) {
        cached_result = 0;
        return 0;
    }

    db_gl_probe_drain_errors();
    unsigned int probe_texture = 0U;
    if (db_gl_texture_create_rgba8(&probe_texture, 4, 4, NULL) == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    static const uint8_t k_base_rgba8[4U * 4U * 4U] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    static const uint8_t k_patch_rgba8[4U * 4U * 4U] = {
        0, 0, 255, 255, 0,   0,   255, 255, 0,   0,   255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 255, 0,   0,   255, 0,   255, 0,   255, 0, 0, 255, 255,
        0, 0, 255, 255, 255, 255, 0,   255, 255, 0,   255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0,   0,   255, 255, 0,   0,   255, 255, 0, 0, 255, 255,
    };
    uint8_t probe_readback[4U * 4U * 4U] = {0U};
    int viewport[4] = {0, 0, 0, 0};

    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba(0, 0, 4, 4, k_base_rgba8);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(4);
    db_gl_texture_sub_image_2d_rgba(
        1, 1, 2, 2, &k_patch_rgba8[db_gl_probe_rgba_pixel_offset(4U, 1U, 1U)]);
    db_gl_set_unpack_row_length_pixels(0);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    db_gl_get_integerv(GL_VIEWPORT, viewport);
    float probe_vertices[8] = {0.0F};
    db_gl_quad_init(probe_vertices);
    const float probe_texcoords[8] = {0.0F, 1.0F, 1.0F, 1.0F,
                                      0.0F, 0.0F, 1.0F, 0.0F};
    const float probe_colors[DB_RECT_VERTEX_COUNT * 4U] = {
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    db_gl_set_viewport_px(4, 4);
    db_gl_clear_color_rgba(0.0F, 0.0F, 0.0F, 1.0F);
    db_gl_clear_color_buffer();
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 4, 4, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);
    if (draw_ok == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    const uint8_t *pixels = probe_readback;
    const size_t center0 = db_gl_probe_rgba_pixel_offset(4U, 1U, 1U);
    const size_t center1 = db_gl_probe_rgba_pixel_offset(4U, 2U, 1U);
    const size_t center2 = db_gl_probe_rgba_pixel_offset(4U, 1U, 2U);
    const size_t center3 = db_gl_probe_rgba_pixel_offset(4U, 2U, 2U);
    cached_result = db_gl_probe_finish(
        db_gl_probe_rgb_matches(pixels, center0, 1U, 0U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center1, 0U, 1U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center2, 1U, 1U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center3, 1U, 0U, 1U));
    return cached_result;
}

static int db_gl_probe_shadow_present_partial_upload_support_rgba16f(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    if ((db_gl_context_supports_unpack_row_length_upload() == 0) ||
        (db_gl_context_probe_texture_float_present_support() == 0)) {
        cached_result = 0;
        return 0;
    }

    db_gl_probe_drain_errors();
    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 4, 4) == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    static const uint16_t k_base_rgba16f[4U * 4U * 4U] = {
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
    };
    static const uint16_t k_patch_rgba16f[4U * 4U * 4U] = {
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,
        DB_F16_ONE, DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
    };
    uint8_t probe_readback[4U * 4U * 4U] = {0U};
    int viewport[4] = {0, 0, 0, 0};

    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 4, 4, k_base_rgba16f);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(4);
    db_gl_texture_sub_image_2d_rgba16f(
        1, 1, 2, 2,
        &k_patch_rgba16f[db_gl_probe_rgba_pixel_offset(4U, 1U, 1U)]);
    db_gl_set_unpack_row_length_pixels(0);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    db_gl_get_integerv(GL_VIEWPORT, viewport);
    float probe_vertices[8] = {0.0F};
    db_gl_quad_init(probe_vertices);
    const float probe_texcoords[8] = {0.0F, 1.0F, 1.0F, 1.0F,
                                      0.0F, 0.0F, 1.0F, 0.0F};
    const float probe_colors[DB_RECT_VERTEX_COUNT * 4U] = {
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    db_gl_set_viewport_px(4, 4);
    db_gl_clear_color_rgba(0.0F, 0.0F, 0.0F, 1.0F);
    db_gl_clear_color_buffer();
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 4, 4, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);
    if (draw_ok == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    const uint8_t *pixels = probe_readback;
    const size_t center0 = db_gl_probe_rgba_pixel_offset(4U, 1U, 1U);
    const size_t center1 = db_gl_probe_rgba_pixel_offset(4U, 2U, 1U);
    const size_t center2 = db_gl_probe_rgba_pixel_offset(4U, 1U, 2U);
    const size_t center3 = db_gl_probe_rgba_pixel_offset(4U, 2U, 2U);
    cached_result = db_gl_probe_finish(
        db_gl_probe_rgb_matches(pixels, center0, 1U, 0U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center1, 0U, 1U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center2, 1U, 1U, 0U) &&
        db_gl_probe_rgb_matches(pixels, center3, 1U, 0U, 1U));
    return cached_result;
}

static int db_gl_context_supports_full_npot_texture_2d(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        return db_gl_runtime_has_extension(&runtime, "GL_OES_texture_npot");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        &runtime, 2, 0, "GL_ARB_texture_non_power_of_two");
}

static int db_gl_context_supports_shadow_present_exact_size_texture_2d(void) {
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_runtime_has_usable_version(&runtime) == 0) {
        return 0;
    }
    if (db_gl_context_supports_full_npot_texture_2d() != 0) {
        return 1;
    }
    if (db_gl_runtime_is_es_context(&runtime) != 0) {
        // ES 2.0 permits NPOT 2D textures for non-mipmapped CLAMP_TO_EDGE
        // usage. The shadow-present path only uses GL_NEAREST +
        // GL_CLAMP_TO_EDGE and never enables mipmaps.
        return db_gl_runtime_version_at_least(&runtime, 2, 0);
    }
    return 0;
}

// 3) Wrapper APIs: buffer and VBO/VAO operations.
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
    return ((g_upload_proc_table.get_error != NULL) &&
            (g_upload_proc_table.get_error() == GL_NO_ERROR))
               ? 1
               : 0;
}

// Context capability checks (metadata/procs).
static int db_gl_context_has_pbo_upload_procs(void) {
    db_gl_require_upload_proc_table_loaded(
        "db_gl_context_has_pbo_upload_procs");
    return (g_upload_proc_table.bind_buffer != NULL) &&
           (g_upload_proc_table.buffer_data != NULL) &&
           (g_upload_proc_table.buffer_sub_data != NULL) &&
           (g_upload_proc_table.gen_buffers != NULL) &&
           (g_upload_proc_table.delete_buffers != NULL);
}

static int db_gl_context_advertises_vbo(void) {
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    return db_gl_extensions_advertise_vbo(&runtime);
}

// 3) Wrapper APIs: geometry utilities and texture operations.
static void db_gl_quad_init(float *verts) {
    verts[DB_GL_QUAD_V0_X] = -1.0F;
    verts[DB_GL_QUAD_V0_Y] = -1.0F;
    verts[DB_GL_QUAD_V1_X] = 1.0F;
    verts[DB_GL_QUAD_V1_Y] = -1.0F;
    verts[DB_GL_QUAD_V2_X] = -1.0F;
    verts[DB_GL_QUAD_V2_Y] = 1.0F;
    verts[DB_GL_QUAD_V3_X] = 1.0F;
    verts[DB_GL_QUAD_V3_Y] = 1.0F;
}

static unsigned int db_gl_pbo_create_or_zero(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_create_or_zero");
    if ((g_upload_proc_table.gen_buffers == NULL) ||
        (g_upload_proc_table.delete_buffers == NULL)) {
        return 0U;
    }
    GLuint pbo = 0U;
    g_upload_proc_table.gen_buffers(1, &pbo);
    return (unsigned int)pbo;
}

static void db_gl_pbo_delete_if_valid(unsigned int pbo) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_delete_if_valid");
    if ((pbo == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_pbo = (GLuint)pbo;
    g_upload_proc_table.delete_buffers(1, &gl_pbo);
}

static void db_gl_pbo_unbind_unpack(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_unbind_unpack");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return;
    }
    g_upload_proc_table.bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0U);
}

static unsigned int db_gl_pbo_create_if_usable(int prefer_unpack_pbo) {
    db_gl_load_upload_proc_table();
    if ((prefer_unpack_pbo == 0) ||
        (db_gl_context_has_pbo_upload_procs() == 0)) {
        return 0U;
    }
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_extensions_advertise_pbo(&runtime) == 0) {
        return 0U;
    }
    return db_gl_pbo_create_or_zero();
}

void db_gl_shadow_present_init_runtime(db_gl_shadow_present_state_t *state,
                                       int prefer_unpack_pbo,
                                       int selected_content_uses_rgba16f) {
    if ((state == NULL) || (state->initialized != 0)) {
        return;
    }
    const int runtime_supports_hdr_present =
        (db_gl_context_probe_texture_float_present_support() != 0) ? 1 : 0;
    const db_gl_shadow_present_texture_format_t selected_texture_format =
        ((selected_content_uses_rgba16f != 0) &&
         (runtime_supports_hdr_present != 0))
            ? DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F
            : DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    const int supports_unpack_row_length_upload =
        (selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_probe_shadow_present_partial_upload_support_rgba16f()
            : db_gl_probe_shadow_present_partial_upload_support_rgba8();
    *state = (db_gl_shadow_present_state_t){
        .initialized = 1,
        .backing_valid = 0,
        .texture_valid = 0,
        .texture_needs_full_upload = 1,
        .runtime_supports_unpack_row_length_upload =
            (supports_unpack_row_length_upload != 0) ? 1 : 0,
        .runtime_supports_hdr_present = runtime_supports_hdr_present,
        .uses_exact_size_texture =
            (db_gl_context_supports_shadow_present_exact_size_texture_2d() != 0)
                ? 1
                : 0,
        .selected_texture_format = selected_texture_format,
        .unpack_pbo = db_gl_pbo_create_if_usable(prefer_unpack_pbo),
    };
    db_gl_quad_init(state->vertices);
    state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V0_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V1_Y] = 1.0F;
    state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    state->texcoords[DB_GL_QUAD_V3_X] = 1.0F;
    state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;
    for (size_t i = 0U; i < (size_t)DB_RECT_VERTEX_COUNT; i++) {
        const size_t base = i * 4U;
        state->colors[base + 0U] = 1.0F;
        state->colors[base + 1U] = 1.0F;
        state->colors[base + 2U] = 1.0F;
        state->colors[base + 3U] = 1.0F;
    }
}

void db_gl_shadow_present_log_decision(
    const char *backend, const char *present_name, int content_uses_rgba16f,
    int hdr_explicit_requested, const db_gl_shadow_present_state_t *state) {
    if ((backend == NULL) || (present_name == NULL) || (state == NULL)) {
        return;
    }
    const int use_pbo = (state->unpack_pbo != 0U) ? 1 : 0;
    const char *const texture_size_mode =
        (state->uses_exact_size_texture != 0) ? "exact_size" : "pow2_fallback";
    const char *const partial_upload_mode =
        (state->runtime_supports_unpack_row_length_upload != 0)
            ? "row_length"
            : "rowwise_fallback";
    if (state->selected_texture_format ==
        DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
        db_infof(backend,
                 "%s hdr=enabled reason=float texture present probe passed, "
                 "pbo=%s, hdr_explicit=%s, texture_sizing=%s, "
                 "partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no",
                 (hdr_explicit_requested != 0) ? "yes" : "no",
                 texture_size_mode, partial_upload_mode);
        return;
    }
    if (content_uses_rgba16f == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=content prefers sdr backbuffer, "
                 "pbo=%s, texture_sizing=%s, partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
                 partial_upload_mode);
        return;
    }
    if (state->runtime_supports_hdr_present == 0) {
        db_infof(backend,
                 "%s hdr=disabled reason=float texture present probe failed, "
                 "falling back to rgba8, pbo=%s, texture_sizing=%s, "
                 "partial_upload=%s",
                 present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
                 partial_upload_mode);
        return;
    }
    db_infof(backend,
             "%s hdr=disabled reason=present texture did not select rgba16f "
             "despite supported float present probe, pbo=%s, "
             "texture_sizing=%s, partial_upload=%s",
             present_name, (use_pbo != 0) ? "yes" : "no", texture_size_mode,
             partial_upload_mode);
}

void db_gl_shadow_present_shutdown(db_gl_shadow_present_state_t *state) {
    if (state == NULL) {
        return;
    }
    if (state->unpack_pbo != 0U) {
        db_gl_pbo_delete_if_valid(state->unpack_pbo);
        state->unpack_pbo = 0U;
    }
    db_gl_texture_delete_if_valid(&state->texture);
    *state = (db_gl_shadow_present_state_t){0};
}

void db_gl_shadow_present_prepare_texture(db_gl_shadow_present_state_t *state,
                                          const char *backend,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height) {
    if ((state == NULL) || (backend == NULL) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    const uint32_t target_width = (state->uses_exact_size_texture != 0)
                                      ? pixel_width
                                      : db_u32_next_pow2(pixel_width);
    const uint32_t target_height = (state->uses_exact_size_texture != 0)
                                       ? pixel_height
                                       : db_u32_next_pow2(pixel_height);
    const int content_size_changed = (state->content_width != pixel_width) ||
                                     (state->content_height != pixel_height);
    const int needs_recreate = (state->texture == 0U) ||
                               (state->texture_width < target_width) ||
                               (state->texture_height < target_height);
    if (needs_recreate == 0) {
        if (content_size_changed != 0) {
            state->content_width = pixel_width;
            state->content_height = pixel_height;
            state->texture_valid = 0;
            state->texture_needs_full_upload = 1;
        }
        return;
    }

    // Texture allocation with NULL pixels must not inherit a bound unpack PBO.
    if (state->unpack_pbo != 0U) {
        db_gl_pbo_unbind_unpack();
    }

    state->texture_width = target_width;
    state->texture_height = target_height;
    state->content_width = pixel_width;
    state->content_height = pixel_height;
    if (state->texture != 0U) {
        db_gl_texture_delete_if_valid(&state->texture);
    }
    const int created =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? db_gl_texture_create_rgba16f(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL)
            : db_gl_texture_create_rgba8(
                  &state->texture,
                  db_checked_u32_to_i32(backend, "shadow_tex_width",
                                        state->texture_width),
                  db_checked_u32_to_i32(backend, "shadow_tex_height",
                                        state->texture_height),
                  NULL);
    if (created == 0) {
        db_failf(backend, "failed to create shared shadow texture");
    }
    state->texture_valid = 0;
    state->texture_needs_full_upload = 1;
}

static void db_gl_set_unpack_row_length_pixels(int pixel_count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ROW_LENGTH, pixel_count);
    }
}

void db_gl_shadow_present_upload_damage_blocks(
    const db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *blocks, size_t block_count) {
    if ((state == NULL) || (backend == NULL) || (state->texture == 0U) ||
        (pixel_width == 0U) || (pixel_height == 0U) || (blocks == NULL) ||
        (block_count == 0U)) {
        return;
    }
    if (selected_pixels == NULL) {
        return;
    }

    const uint32_t pixel_bytes =
        (state->selected_texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
            ? (uint32_t)(sizeof(uint16_t) * 4U)
            : 4U;
    const size_t total_bytes = (size_t)db_checked_mul_u32(
        backend, "shadow_upload_total_bytes",
        db_checked_mul_u32(backend, "shadow_row_bytes", pixel_width,
                           pixel_bytes),
        pixel_height);
    if (total_bytes > (size_t)PTRDIFF_MAX) {
        db_failf(backend, "shadow_upload_total_bytes too large: %zu",
                 total_bytes);
    }
    const int use_pbo = (state->unpack_pbo != 0U) ? 1 : 0;
    const int use_unpack_row_length =
        (state->runtime_supports_unpack_row_length_upload != 0) ? 1 : 0;
    const uint32_t row_bytes = db_checked_mul_u32(backend, "shadow_row_bytes",
                                                  pixel_width, pixel_bytes);
    db_gl_texture_bind_2d(state->texture);
    db_gl_set_unpack_alignment_1();
    for (size_t i = 0U; i < block_count; i++) {
        const db_damage_block_t block = blocks[i];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        const uint32_t row_end = db_u32_min(
            pixel_height, db_checked_add_u32(backend, "shadow_row_end",
                                             block.row_start, block.row_count));
        const uint32_t col_end = db_u32_min(
            pixel_width, db_checked_add_u32(backend, "shadow_col_end",
                                            block.col_start, block.col_count));
        if ((row_end <= block.row_start) || (col_end <= block.col_start)) {
            continue;
        }
        const uint32_t row_count = row_end - block.row_start;
        const uint32_t col_count = col_end - block.col_start;
        const uint32_t block_row_bytes = db_checked_mul_u32(
            backend, "shadow_block_row_bytes", col_count, pixel_bytes);
        const size_t src_offset_bytes =
            ((size_t)block.row_start * (size_t)row_bytes) +
            ((size_t)block.col_start * (size_t)pixel_bytes);
        if (use_unpack_row_length != 0) {
            db_gl_set_unpack_row_length_pixels(db_checked_u32_to_i32(
                backend, "shadow_unpack_row_length", pixel_width));
            if (use_pbo != 0) {
                const size_t block_bytes =
                    ((size_t)(row_count - 1U) * (size_t)row_bytes) +
                    (size_t)block_row_bytes;
                db_gl_upload_buffer_target(
                    ((const uint8_t *)selected_pixels) + src_offset_bytes,
                    block_bytes, DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                    state->unpack_pbo, 0, NULL, 0, 0);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        db_gl_vbo_offset_ptr(0U));
                }
            } else {
                const void *pixels_ptr =
                    (state->selected_texture_format ==
                     DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                        ? (const void *)(((const uint16_t *)selected_pixels) +
                                         (src_offset_bytes /
                                          (sizeof(uint16_t) * 4U)))
                        : (const void *)(((const uint8_t *)selected_pixels) +
                                         src_offset_bytes);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y",
                                              block.row_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        db_checked_u32_to_i32(backend, "shadow_upload_h",
                                              row_count),
                        pixels_ptr);
                }
            }
            db_gl_set_unpack_row_length_pixels(0);
            continue;
        }
        for (uint32_t row = block.row_start; row < row_end; row++) {
            const size_t row_src_offset_bytes =
                ((size_t)row * (size_t)row_bytes) +
                ((size_t)block.col_start * (size_t)pixel_bytes);
            if (use_pbo != 0) {
                db_gl_upload_buffer_target(
                    ((const uint8_t *)selected_pixels) + row_src_offset_bytes,
                    (size_t)block_row_bytes,
                    DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER, state->unpack_pbo, 0,
                    NULL, 0, 0);
                if (state->selected_texture_format ==
                    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                    db_gl_texture_sub_image_2d_rgba16f(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                } else {
                    db_gl_texture_sub_image_2d_rgba(
                        db_checked_u32_to_i32(backend, "shadow_upload_x",
                                              block.col_start),
                        db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                        db_checked_u32_to_i32(backend, "shadow_upload_w",
                                              col_count),
                        1, db_gl_vbo_offset_ptr(0U));
                }
            } else if (state->selected_texture_format ==
                       DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F) {
                const uint16_t *pixels_ptr =
                    ((const uint16_t *)selected_pixels) +
                    (row_src_offset_bytes / (sizeof(uint16_t) * 4U));
                db_gl_texture_sub_image_2d_rgba16f(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            } else {
                const uint8_t *pixels_ptr =
                    ((const uint8_t *)selected_pixels) + row_src_offset_bytes;
                db_gl_texture_sub_image_2d_rgba(
                    db_checked_u32_to_i32(backend, "shadow_upload_x",
                                          block.col_start),
                    db_checked_u32_to_i32(backend, "shadow_upload_y", row),
                    db_checked_u32_to_i32(backend, "shadow_upload_w",
                                          col_count),
                    1, pixels_ptr);
            }
        }
    }
    if (use_unpack_row_length != 0) {
        db_gl_set_unpack_row_length_pixels(0);
    }
    if (use_pbo != 0) {
        db_gl_pbo_unbind_unpack();
    }
}

void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame) {
    if ((frame == NULL) || (frame->state == NULL) || (frame->backend == NULL) ||
        (frame->pixel_width == 0U) || (frame->pixel_height == 0U)) {
        return;
    }
    db_gl_shadow_present_prepare_texture(
        frame->state, frame->backend, frame->pixel_width, frame->pixel_height);
    if (frame->state->texture == 0U) {
        db_failf(frame->backend,
                 "shared shadow present texture is not initialized");
    }
    if (frame->selected_pixels == NULL) {
        db_failf(frame->backend, "shared shadow present pixels are missing");
    }
    if (frame->prepare_upload_target_fn != NULL) {
        frame->prepare_upload_target_fn(frame->state, frame->pixel_width,
                                        frame->pixel_height,
                                        frame->prepare_upload_target_user_data);
    }

    if ((frame->state->texture_needs_full_upload != 0) ||
        ((frame->state->texture_valid == 0) &&
         ((frame->damage_blocks == NULL) ||
          (frame->damage_block_count == 0U)))) {
        const db_damage_block_t full_block =
            db_damage_block_full(frame->pixel_height, frame->pixel_width);
        db_gl_shadow_present_upload_damage_blocks(
            frame->state, frame->backend, frame->selected_pixels,
            frame->pixel_width, frame->pixel_height, &full_block, 1U);
        frame->state->texture_valid = 1;
        frame->state->texture_needs_full_upload = 0;
    } else if ((frame->damage_blocks != NULL) &&
               (frame->damage_block_count > 0U)) {
        db_gl_shadow_present_upload_damage_blocks(
            frame->state, frame->backend, frame->selected_pixels,
            frame->pixel_width, frame->pixel_height, frame->damage_blocks,
            frame->damage_block_count);
        frame->state->texture_valid = 1;
    }

    db_gl_shadow_present_draw(frame->state, frame->pixel_width,
                              frame->pixel_height);
}

void db_gl_shadow_present_draw(const db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height) {
    if ((state == NULL) || (state->texture == 0U) || (pixel_width == 0U) ||
        (pixel_height == 0U)) {
        return;
    }
    db_gl_shadow_present_state_t *const mutable_state =
        (db_gl_shadow_present_state_t *)state;
    const float tex_u =
        (mutable_state->texture_width == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_width, mutable_state->texture_width);
    const float tex_v =
        (mutable_state->texture_height == 0U)
            ? 1.0F
            : db_u32_ratio_to_f32(pixel_height, mutable_state->texture_height);
    mutable_state->texcoords[DB_GL_QUAD_V0_X] = 0.0F;
    mutable_state->texcoords[DB_GL_QUAD_V0_Y] = tex_v;
    mutable_state->texcoords[DB_GL_QUAD_V1_X] = tex_u;
    mutable_state->texcoords[DB_GL_QUAD_V1_Y] = tex_v;
    mutable_state->texcoords[DB_GL_QUAD_V2_X] = 0.0F;
    mutable_state->texcoords[DB_GL_QUAD_V2_Y] = 0.0F;
    mutable_state->texcoords[DB_GL_QUAD_V3_X] = tex_u;
    mutable_state->texcoords[DB_GL_QUAD_V3_Y] = 0.0F;

    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, mutable_state->vertices);
    db_gl_set_color_pointer_f(4, 0, mutable_state->colors);
    db_gl_set_texcoord_pointer_2f(0, mutable_state->texcoords);
    db_gl_texture_bind_2d(mutable_state->texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
}

void db_gl_set_viewport_px(int width_px, int height_px) {
    if ((width_px <= 0) || (height_px <= 0)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.viewport != NULL) {
        g_upload_proc_table.viewport(0, 0, (GLsizei)width_px,
                                     (GLsizei)height_px);
    }
}

static void db_gl_texture_set_nearest_clamp_2d(void) {
    if (g_upload_proc_table.tex_parameteri == NULL) {
        return;
    }
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                       GL_CLAMP_TO_EDGE);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                       GL_CLAMP_TO_EDGE);
}

static int db_gl_texture_allocate_rgba_typed(unsigned int texture, int width,
                                             int height,
                                             unsigned int internal_format,
                                             unsigned int pixel_type,
                                             const void *pixels) {
    if ((texture == 0U) || (width <= 0) || (height <= 0)) {
        return 0;
    }
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.bind_texture == NULL) ||
        (g_upload_proc_table.tex_image_2d == NULL)) {
        return 0;
    }
    db_gl_probe_drain_errors();
    g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
    db_gl_texture_set_nearest_clamp_2d();
    g_upload_proc_table.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)internal_format,
                                     (GLsizei)width, (GLsizei)height, 0,
                                     GL_RGBA, (GLenum)pixel_type, pixels);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

int db_gl_texture_allocate_rgba(unsigned int texture, int width, int height,
                                unsigned int internal_format,
                                const void *pixels) {
    return db_gl_texture_allocate_rgba_typed(
        texture, width, height, internal_format, GL_UNSIGNED_BYTE, pixels);
}

static int db_gl_texture_create_rgba_typed(unsigned int *out_texture, int width,
                                           int height,
                                           unsigned int internal_format,
                                           unsigned int pixel_type,
                                           const void *pixels) {
    if (out_texture == NULL) {
        return 0;
    }
    *out_texture = 0U;
    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.gen_textures == NULL) {
        return 0;
    }
    GLuint texture = 0U;
    g_upload_proc_table.gen_textures(1, &texture);
    if (texture == 0U) {
        return 0;
    }
    if (db_gl_texture_allocate_rgba_typed((unsigned int)texture, width, height,
                                          internal_format, pixel_type,
                                          pixels) == 0) {
        if (g_upload_proc_table.delete_textures != NULL) {
            g_upload_proc_table.delete_textures(1, &texture);
        }
        return 0;
    }
    *out_texture = (unsigned int)texture;
    return 1;
}

int db_gl_texture_create_rgba(unsigned int *out_texture, int width, int height,
                              unsigned int internal_format,
                              const void *pixels) {
    return db_gl_texture_create_rgba_typed(
        out_texture, width, height, internal_format, GL_UNSIGNED_BYTE, pixels);
}

int db_gl_texture_allocate_rgba8(unsigned int texture, int width, int height,
                                 const void *pixels) {
    return db_gl_texture_allocate_rgba(texture, width, height, GL_RGBA, pixels);
}

int db_gl_texture_create_rgba8(unsigned int *out_texture, int width, int height,
                               const void *pixels) {
    return db_gl_texture_create_rgba(out_texture, width, height, GL_RGBA,
                                     pixels);
}

int db_gl_texture_allocate_rgba16f(unsigned int texture, int width, int height,
                                   const void *pixels) {
    return db_gl_texture_allocate_rgba_typed(texture, width, height, GL_RGBA16F,
                                             GL_HALF_FLOAT, pixels);
}

int db_gl_texture_create_rgba16f(unsigned int *out_texture, int width,
                                 int height, const void *pixels) {
    return db_gl_texture_create_rgba_typed(out_texture, width, height,
                                           GL_RGBA16F, GL_HALF_FLOAT, pixels);
}

void db_gl_texture_delete_if_valid(unsigned int *texture) {
    if ((texture == NULL) || (*texture == 0U)) {
        return;
    }
    const GLuint gl_texture = (GLuint)(*texture);
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.delete_textures != NULL) {
        g_upload_proc_table.delete_textures(1, &gl_texture);
    }
    *texture = 0U;
}

void db_gl_texture_bind_2d(unsigned int texture) {
    db_gl_load_upload_proc_table();
    if ((g_active_texture_unit_valid != 0) &&
        (g_active_texture_unit >= GL_TEXTURE0) &&
        (g_active_texture_unit <
         (GL_TEXTURE0 + DB_GL_TRACKED_TEXTURE_UNIT_COUNT))) {
        const size_t unit_index = (size_t)(g_active_texture_unit - GL_TEXTURE0);
        if ((g_texture2d_binding_valid_by_unit[unit_index] != 0) &&
            (g_texture2d_binding_by_unit[unit_index] == texture)) {
            return;
        }
        if (g_upload_proc_table.bind_texture != NULL) {
            g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
            g_texture2d_binding_by_unit[unit_index] = texture;
            g_texture2d_binding_valid_by_unit[unit_index] = 1;
        }
        return;
    }
    if (g_upload_proc_table.bind_texture != NULL) {
        g_upload_proc_table.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
    }
}

void db_gl_texture_sub_image_2d_rgba(int x_px, int y_px, int width, int height,
                                     const void *pixels) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_sub_image_2d != NULL) {
        g_upload_proc_table.tex_sub_image_2d(GL_TEXTURE_2D, 0, x_px, y_px,
                                             (GLsizei)width, (GLsizei)height,
                                             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

void db_gl_texture_sub_image_2d_rgba16f(int x_px, int y_px, int width,
                                        int height, const void *pixels) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_sub_image_2d != NULL) {
        g_upload_proc_table.tex_sub_image_2d(GL_TEXTURE_2D, 0, x_px, y_px,
                                             (GLsizei)width, (GLsizei)height,
                                             GL_RGBA, GL_HALF_FLOAT, pixels);
    }
}

// 3) Wrapper APIs: fixed-function state, client arrays, and readback.
void db_gl_clear_color_rgba(float red, float green, float blue, float alpha) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.clear_color != NULL) {
        g_upload_proc_table.clear_color(red, green, blue, alpha);
    }
}

void db_gl_clear_color_buffer(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.clear != NULL) {
        g_upload_proc_table.clear(GL_COLOR_BUFFER_BIT);
    }
}

void db_gl_set_blend_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_blend_enabled_state == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(GL_BLEND);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(GL_BLEND);
    }
    g_blend_enabled_state = normalized_enabled;
}

void db_gl_set_cull_face_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_cull_face_enabled_state == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(GL_CULL_FACE);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(GL_CULL_FACE);
    }
    g_cull_face_enabled_state = normalized_enabled;
}

void db_gl_set_depth_test_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_depth_test_enabled_state == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(GL_DEPTH_TEST);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(GL_DEPTH_TEST);
    }
    g_depth_test_enabled_state = normalized_enabled;
}

void db_gl_set_dither_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_dither_enabled_state == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(GL_DITHER);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(GL_DITHER);
    }
    g_dither_enabled_state = normalized_enabled;
}

void db_gl_set_pack_alignment_1(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_PACK_ALIGNMENT, 1);
    }
}

void db_gl_set_unpack_alignment_1(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
    }
}

void db_gl_read_pixels_rgba8(int x_px, int y_px, int width, int height,
                             void *pixels) {
    if ((width <= 0) || (height <= 0) || (pixels == NULL)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.read_pixels != NULL) {
        g_upload_proc_table.read_pixels(x_px, y_px, (GLsizei)width,
                                        (GLsizei)height, GL_RGBA,
                                        GL_UNSIGNED_BYTE, pixels);
    }
}

void db_gl_read_pixels_rgba16f(int x_px, int y_px, int width, int height,
                               void *pixels) {
    if ((width <= 0) || (height <= 0) || (pixels == NULL)) {
        return;
    }
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.read_pixels != NULL) {
        g_upload_proc_table.read_pixels(x_px, y_px, (GLsizei)width,
                                        (GLsizei)height, GL_RGBA, GL_HALF_FLOAT,
                                        pixels);
    }
}

void db_gl_set_texture_2d_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if ((g_texture2d_enabled_state_valid != 0) &&
        ((int)g_texture2d_enabled_state == normalized_enabled)) {
        return;
    }
    if ((normalized_enabled != 0) && (g_upload_proc_table.enable != NULL)) {
        g_upload_proc_table.enable(GL_TEXTURE_2D);
    }
    if ((normalized_enabled == 0) && (g_upload_proc_table.disable != NULL)) {
        g_upload_proc_table.disable(GL_TEXTURE_2D);
    }
    g_texture2d_enabled_state = (unsigned int)normalized_enabled;
    g_texture2d_enabled_state_valid = 1;
}

void db_gl_set_client_state_vertex_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_client_state_vertex_array_enabled == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) &&
        (g_upload_proc_table.enable_client_state != NULL)) {
        g_upload_proc_table.enable_client_state(GL_VERTEX_ARRAY);
    }
    if ((normalized_enabled == 0) &&
        (g_upload_proc_table.disable_client_state != NULL)) {
        g_upload_proc_table.disable_client_state(GL_VERTEX_ARRAY);
    }
    g_client_state_vertex_array_enabled = normalized_enabled;
}

void db_gl_set_client_state_color_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_client_state_color_array_enabled == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) &&
        (g_upload_proc_table.enable_client_state != NULL)) {
        g_upload_proc_table.enable_client_state(GL_COLOR_ARRAY);
    }
    if ((normalized_enabled == 0) &&
        (g_upload_proc_table.disable_client_state != NULL)) {
        g_upload_proc_table.disable_client_state(GL_COLOR_ARRAY);
    }
    g_client_state_color_array_enabled = normalized_enabled;
}

void db_gl_set_client_state_texcoord_array_enabled(int enabled) {
    db_gl_load_upload_proc_table();
    const int normalized_enabled = (enabled != 0) ? 1 : 0;
    if (g_client_state_texcoord_array_enabled == normalized_enabled) {
        return;
    }
    if ((normalized_enabled != 0) &&
        (g_upload_proc_table.enable_client_state != NULL)) {
        g_upload_proc_table.enable_client_state(GL_TEXTURE_COORD_ARRAY);
    }
    if ((normalized_enabled == 0) &&
        (g_upload_proc_table.disable_client_state != NULL)) {
        g_upload_proc_table.disable_client_state(GL_TEXTURE_COORD_ARRAY);
    }
    g_client_state_texcoord_array_enabled = normalized_enabled;
}

void db_gl_set_vertex_pointer_2f(int stride_bytes, const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_pointer != NULL) {
        g_upload_proc_table.vertex_pointer(2, GL_FLOAT, (GLsizei)stride_bytes,
                                           pointer);
    }
}

void db_gl_set_color_pointer_f(int component_count, int stride_bytes,
                               const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.color_pointer != NULL) {
        g_upload_proc_table.color_pointer(component_count, GL_FLOAT,
                                          (GLsizei)stride_bytes, pointer);
    }
}

void db_gl_set_texcoord_pointer_2f(int stride_bytes, const void *pointer) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.tex_coord_pointer != NULL) {
        g_upload_proc_table.tex_coord_pointer(2, GL_FLOAT,
                                              (GLsizei)stride_bytes, pointer);
    }
}

void db_gl_draw_arrays_triangles(int first, int count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.draw_arrays != NULL) {
        g_upload_proc_table.draw_arrays(GL_TRIANGLES, first, (GLsizei)count);
    }
}

void db_gl_draw_arrays_triangle_strip(int first, int count) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.draw_arrays != NULL) {
        g_upload_proc_table.draw_arrays(GL_TRIANGLE_STRIP, first,
                                        (GLsizei)count);
    }
}

// 3) Wrapper APIs: shader/program and modern pipeline operations.
void db_gl_active_texture(unsigned int texture_unit) {
    db_gl_load_upload_proc_table();
    if ((g_active_texture_unit_valid != 0) &&
        (g_active_texture_unit == texture_unit)) {
        return;
    }
    if (g_upload_proc_table.active_texture != NULL) {
        g_upload_proc_table.active_texture((GLenum)texture_unit);
        g_active_texture_unit = texture_unit;
        g_active_texture_unit_valid = 1;
    }
}

void db_gl_attach_shader(unsigned int program, unsigned int shader) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.attach_shader != NULL) {
        g_upload_proc_table.attach_shader((GLuint)program, (GLuint)shader);
    }
}

void db_gl_bind_framebuffer(unsigned int target, unsigned int framebuffer) {
    db_gl_load_upload_proc_table();
    const int target_is_framebuffer = ((GLenum)target == GL_FRAMEBUFFER);
    const int target_is_read = ((GLenum)target == GL_READ_FRAMEBUFFER);
    const int target_is_draw = ((GLenum)target == GL_DRAW_FRAMEBUFFER);
    if (target_is_framebuffer != 0) {
        if ((g_bound_read_framebuffer_valid != 0) &&
            (g_bound_draw_framebuffer_valid != 0) &&
            (g_bound_read_framebuffer == framebuffer) &&
            (g_bound_draw_framebuffer == framebuffer)) {
            return;
        }
    } else if (target_is_read != 0) {
        if ((g_bound_read_framebuffer_valid != 0) &&
            (g_bound_read_framebuffer == framebuffer)) {
            return;
        }
    } else if (target_is_draw != 0) {
        if ((g_bound_draw_framebuffer_valid != 0) &&
            (g_bound_draw_framebuffer == framebuffer)) {
            return;
        }
    }
    if (g_upload_proc_table.bind_framebuffer != NULL) {
        g_upload_proc_table.bind_framebuffer((GLenum)target,
                                             (GLuint)framebuffer);
        if (target_is_framebuffer != 0) {
            g_bound_read_framebuffer = framebuffer;
            g_bound_read_framebuffer_valid = 1;
            g_bound_draw_framebuffer = framebuffer;
            g_bound_draw_framebuffer_valid = 1;
        } else if (target_is_read != 0) {
            g_bound_read_framebuffer = framebuffer;
            g_bound_read_framebuffer_valid = 1;
        } else if (target_is_draw != 0) {
            g_bound_draw_framebuffer = framebuffer;
            g_bound_draw_framebuffer_valid = 1;
        }
    }
}

void db_gl_bind_vertex_array(unsigned int vao) {
    db_gl_load_upload_proc_table();
    if ((g_bound_vertex_array_valid != 0) && (g_bound_vertex_array == vao)) {
        return;
    }
    if (g_upload_proc_table.bind_vertex_array != NULL) {
        g_upload_proc_table.bind_vertex_array((GLuint)vao);
        g_bound_vertex_array = vao;
        g_bound_vertex_array_valid = 1;
    }
}

void db_gl_blit_framebuffer(int src_x0, int src_y0, int src_x1, int src_y1,
                            int dst_x0, int dst_y0, int dst_x1, int dst_y1,
                            unsigned int mask, unsigned int filter) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.blit_framebuffer != NULL) {
        g_upload_proc_table.blit_framebuffer(src_x0, src_y0, src_x1, src_y1,
                                             dst_x0, dst_y0, dst_x1, dst_y1,
                                             (GLbitfield)mask, (GLenum)filter);
    }
}

unsigned int db_gl_check_framebuffer_status(unsigned int target) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.check_framebuffer_status == NULL) {
        return 0U;
    }
    return (unsigned int)g_upload_proc_table.check_framebuffer_status(
        (GLenum)target);
}

void db_gl_compile_shader(unsigned int shader) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.compile_shader != NULL) {
        g_upload_proc_table.compile_shader((GLuint)shader);
    }
}

unsigned int db_gl_create_program(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.create_program == NULL) {
        return 0U;
    }
    return (unsigned int)g_upload_proc_table.create_program();
}

unsigned int db_gl_create_shader(unsigned int shader_type) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.create_shader == NULL) {
        return 0U;
    }
    return (unsigned int)g_upload_proc_table.create_shader((GLenum)shader_type);
}

void db_gl_delete_framebuffers(int count, const unsigned int *framebuffers) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_framebuffers == NULL) || (count <= 0) ||
        (framebuffers == NULL)) {
        return;
    }
    g_upload_proc_table.delete_framebuffers((GLsizei)count,
                                            (const GLuint *)framebuffers);
}

void db_gl_delete_program(unsigned int program) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_program != NULL) && (program != 0U)) {
        g_upload_proc_table.delete_program((GLuint)program);
    }
}

void db_gl_delete_shader(unsigned int shader) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_shader != NULL) && (shader != 0U)) {
        g_upload_proc_table.delete_shader((GLuint)shader);
    }
}

void db_gl_delete_vertex_arrays(int count, const unsigned int *arrays) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_vertex_arrays == NULL) || (count <= 0) ||
        (arrays == NULL)) {
        return;
    }
    g_upload_proc_table.delete_vertex_arrays((GLsizei)count,
                                             (const GLuint *)arrays);
}

void db_gl_enable_vertex_attrib_array(unsigned int index) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.enable_vertex_attrib_array != NULL) {
        g_upload_proc_table.enable_vertex_attrib_array((GLuint)index);
    }
}

void db_gl_framebuffer_texture_2d(unsigned int target, unsigned int attachment,
                                  unsigned int textarget, unsigned int texture,
                                  int level) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.framebuffer_texture_2d != NULL) {
        g_upload_proc_table.framebuffer_texture_2d(
            (GLenum)target, (GLenum)attachment, (GLenum)textarget,
            (GLuint)texture, (GLint)level);
    }
}

void db_gl_gen_framebuffers(int count, unsigned int *framebuffers) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_framebuffers == NULL) || (count <= 0) ||
        (framebuffers == NULL)) {
        return;
    }
    g_upload_proc_table.gen_framebuffers((GLsizei)count,
                                         (GLuint *)framebuffers);
}

void db_gl_gen_vertex_arrays(int count, unsigned int *arrays) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_vertex_arrays == NULL) || (count <= 0) ||
        (arrays == NULL)) {
        return;
    }
    g_upload_proc_table.gen_vertex_arrays((GLsizei)count, (GLuint *)arrays);
}

void db_gl_get_integerv(unsigned int pname, int *value) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_integerv != NULL) && (value != NULL)) {
        g_upload_proc_table.get_integerv((GLenum)pname, (GLint *)value);
    }
}

void db_gl_get_program_info_log(unsigned int program, int buf_size, int *length,
                                char *log) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_program_info_log != NULL) && (buf_size > 0) &&
        (log != NULL)) {
        g_upload_proc_table.get_program_info_log(
            (GLuint)program, (GLsizei)buf_size, (GLsizei *)length, log);
    }
}

void db_gl_get_program_iv(unsigned int program, unsigned int pname,
                          int *value) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_program_iv != NULL) && (value != NULL)) {
        g_upload_proc_table.get_program_iv((GLuint)program, (GLenum)pname,
                                           (GLint *)value);
    }
}

void db_gl_get_shader_info_log(unsigned int shader, int buf_size, int *length,
                               char *log) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_shader_info_log != NULL) && (buf_size > 0) &&
        (log != NULL)) {
        g_upload_proc_table.get_shader_info_log(
            (GLuint)shader, (GLsizei)buf_size, (GLsizei *)length, log);
    }
}

void db_gl_get_shader_iv(unsigned int shader, unsigned int pname, int *value) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_shader_iv != NULL) && (value != NULL)) {
        g_upload_proc_table.get_shader_iv((GLuint)shader, (GLenum)pname,
                                          (GLint *)value);
    }
}

int db_gl_get_uniform_location(unsigned int program, const char *name) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_uniform_location == NULL) || (name == NULL)) {
        return -1;
    }
    return (int)g_upload_proc_table.get_uniform_location((GLuint)program, name);
}

void db_gl_link_program(unsigned int program) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.link_program != NULL) {
        g_upload_proc_table.link_program((GLuint)program);
    }
}

void db_gl_shader_source_single(unsigned int shader, const char *source) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.shader_source != NULL) && (source != NULL)) {
        g_upload_proc_table.shader_source((GLuint)shader, 1, &source, NULL);
    }
}

void db_gl_uniform1i(int location, int value) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.uniform_1i != NULL) {
        g_upload_proc_table.uniform_1i((GLint)location, (GLint)value);
    }
}

void db_gl_uniform1ui(int location, unsigned int value) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.uniform_1ui != NULL) {
        g_upload_proc_table.uniform_1ui((GLint)location, (GLuint)value);
    }
}

void db_gl_uniform3f(int location, float x_val, float y_val, float z_val) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.uniform_3f != NULL) {
        g_upload_proc_table.uniform_3f((GLint)location, x_val, y_val, z_val);
    }
}

void db_gl_uniform3fv3(int location, const float *xyz) {
    if (xyz == NULL) {
        return;
    }
    db_gl_uniform3f(location, xyz[0], xyz[1], xyz[2]);
}

void db_gl_use_program(unsigned int program) {
    db_gl_load_upload_proc_table();
    if ((g_current_program_valid != 0) && (g_current_program == program)) {
        return;
    }
    if (g_upload_proc_table.use_program != NULL) {
        g_upload_proc_table.use_program((GLuint)program);
        g_current_program = program;
        g_current_program_valid = 1;
    }
}

void db_gl_vertex_attrib_pointer_2f(unsigned int index, int stride_bytes,
                                    size_t byte_offset) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_attrib_pointer != NULL) {
        g_upload_proc_table.vertex_attrib_pointer(
            (GLuint)index, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride_bytes,
            db_gl_vbo_offset_ptr(byte_offset));
    }
}

void db_gl_vertex_attrib_pointer_3f(unsigned int index, int stride_bytes,
                                    size_t byte_offset) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_attrib_pointer != NULL) {
        g_upload_proc_table.vertex_attrib_pointer(
            (GLuint)index, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride_bytes,
            db_gl_vbo_offset_ptr(byte_offset));
    }
}

unsigned int db_gl_get_error_code(void) {
    db_gl_load_upload_proc_table();
    return (unsigned int)db_gl_get_error_value();
}

const char *db_gl_get_version_string(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.get_string == NULL) {
        return NULL;
    }
    return (const char *)g_upload_proc_table.get_string(GL_VERSION);
}

const char *db_gl_get_renderer_string(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.get_string == NULL) {
        return NULL;
    }
    return (const char *)g_upload_proc_table.get_string(GL_RENDERER);
}

const char *db_gl_get_extensions_string(void) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.get_string == NULL) {
        return NULL;
    }
    return (const char *)g_upload_proc_table.get_string(GL_EXTENSIONS);
}

// 3) Wrapper APIs: query strings and proc preload.

void db_gl_preload_upload_proc_table(void) { db_gl_load_upload_proc_table(); }

// Context probe checks (active runtime verification).
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
    db_gl_probe_drain_errors();
    g_upload_proc_table.get_buffer_sub_data(GL_ARRAY_BUFFER, 0,
                                            (GLsizeiptr)expected_size, actual);

    return db_gl_probe_finish(db_gl_probe_step_error_free() &&
                              (memcmp(expected, actual, expected_size) == 0));
}

static int db_gl_probe_texture_create_rgba16f(unsigned int *out_texture,
                                              int width, int height) {
    if (out_texture == NULL) {
        return 0;
    }
    *out_texture = 0U;
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_textures == NULL) ||
        (g_upload_proc_table.bind_texture == NULL) ||
        (g_upload_proc_table.tex_parameteri == NULL) ||
        (g_upload_proc_table.tex_image_2d == NULL)) {
        return 0;
    }

    db_gl_probe_drain_errors();

    GLuint texture = 0U;
    g_upload_proc_table.gen_textures(1, &texture);
    if (texture == 0U) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.bind_texture(GL_TEXTURE_2D, texture);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                       GL_NEAREST);
    g_upload_proc_table.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)GL_RGBA16F,
                                     (GLsizei)width, (GLsizei)height, 0,
                                     GL_RGBA, GL_HALF_FLOAT, NULL);
    if (db_gl_probe_step_error_free() == 0) {
        if (g_upload_proc_table.delete_textures != NULL) {
            g_upload_proc_table.delete_textures(1, &texture);
        }
        return db_gl_probe_finish(0);
    }

    *out_texture = (unsigned int)texture;
    return db_gl_probe_finish(1);
}

int db_gl_context_probe_texture_float_support(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_extensions_advertise_texture_float(&runtime) == 0) {
        cached_result = 0;
        return 0;
    }

    // Probe: create/upload/delete a tiny RGBA16F texture and require clean
    // error state throughout.
    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 2, 2) == 0) {
        db_gl_probe_drain_errors();
        cached_result = 0;
        return 0;
    }

    static const uint16_t k_probe_rgba16f[4U * 4U] = {
        DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, DB_F16_ONE,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 2, 2, k_probe_rgba16f);
    const int upload_ok = db_gl_probe_step_error_free();
    db_gl_texture_delete_if_valid(&probe_texture);
    db_gl_texture_bind_2d(0U);
    cached_result = db_gl_probe_finish(upload_ok);
    return cached_result;
}

int db_gl_context_probe_texture_float_present_support(void) {
    static int cached_result = -1;
    if (cached_result >= 0) {
        return cached_result;
    }
    if (db_gl_context_probe_texture_float_support() == 0) {
        cached_result = 0;
        return 0;
    }

    db_gl_probe_drain_errors();

    unsigned int probe_texture = 0U;
    if (db_gl_probe_texture_create_rgba16f(&probe_texture, 2, 2) == 0) {
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    static const uint16_t k_probe_rgba16f[4U * 4U] = {
        DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, 0x0000U,    DB_F16_ONE,
        0x0000U,    DB_F16_ONE, 0x0000U,    0x0000U,    DB_F16_ONE, DB_F16_ONE,
        DB_F16_ONE, DB_F16_ONE, DB_F16_ONE, DB_F16_ONE,
    };
    db_gl_texture_bind_2d(probe_texture);
    db_gl_texture_sub_image_2d_rgba16f(0, 0, 2, 2, k_probe_rgba16f);
    if (db_gl_probe_step_error_free() == 0) {
        db_gl_texture_delete_if_valid(&probe_texture);
        db_gl_texture_bind_2d(0U);
        cached_result = db_gl_probe_finish(0);
        return cached_result;
    }

    int viewport[4] = {0, 0, 0, 0};
    db_gl_get_integerv(GL_VIEWPORT, viewport);
    float probe_vertices[8] = {0.0F};
    db_gl_quad_init(probe_vertices);
    const float probe_texcoords[8] = {0.0F, 1.0F, 1.0F, 1.0F,
                                      0.0F, 0.0F, 1.0F, 0.0F};
    const float probe_colors[DB_RECT_VERTEX_COUNT * 4U] = {
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    uint8_t probe_readback[2U * 2U * 4U] = {0U};

    db_gl_set_viewport_px(2, 2);
    db_gl_clear_color_rgba(0.0F, 0.0F, 0.0F, 1.0F);
    db_gl_clear_color_buffer();
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
    db_gl_set_vertex_pointer_2f(0, probe_vertices);
    db_gl_set_color_pointer_f(4, 0, probe_colors);
    db_gl_set_texcoord_pointer_2f(0, probe_texcoords);
    db_gl_texture_bind_2d(probe_texture);
    db_gl_draw_arrays_triangle_strip(0, 4);
    db_gl_read_pixels_rgba8(0, 0, 2, 2, probe_readback);
    const int draw_ok = db_gl_probe_step_error_free();

    db_gl_texture_bind_2d(0U);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_viewport_px(viewport[2], viewport[3]);
    db_gl_texture_delete_if_valid(&probe_texture);

    if (draw_ok == 0) {
        cached_result = db_gl_probe_finish(0);
        return 0;
    }

    const uint8_t *const p0 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 0U, 0U)];
    const uint8_t *const p1 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 1U, 0U)];
    const uint8_t *const p2 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 0U, 1U)];
    const uint8_t *const p3 =
        &probe_readback[db_gl_probe_rgba_pixel_offset(2U, 1U, 1U)];
    cached_result =
        db_gl_probe_finish((db_gl_probe_rgb_matches(p0, 0U, 0U, 0U, 1U) &&
                            db_gl_probe_rgb_matches(p1, 0U, 1U, 1U, 1U) &&
                            db_gl_probe_rgb_matches(p2, 0U, 1U, 0U, 0U) &&
                            db_gl_probe_rgb_matches(p3, 0U, 0U, 1U, 0U))
                               ? 1
                               : 0);
    return cached_result;
}

static int db_gl_context_probe_persistent_upload(size_t bytes,
                                                 const float *initial_vertices,
                                                 void **mapped_out) {
    if ((g_upload_proc_table.buffer_storage == NULL) ||
        (g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL)) {
        return 0;
    }
    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if ((probe_size == 0U) || (initial_vertices == NULL)) {
        return 0;
    }
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    db_gl_probe_drain_errors();
    g_upload_proc_table.buffer_storage(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL,
                                       storage_flags);
    if (db_gl_probe_step_error_free() == 0) {
        return db_gl_probe_finish(0);
    }

    void *mapped = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, storage_flags);
    if ((mapped == NULL) || (db_gl_probe_step_error_free() == 0)) {
        if (mapped != NULL) {
            (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        }
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(mapped, initial_vertices, probe_size);
    if (!db_gl_verify_buffer_prefix((const uint8_t *)initial_vertices,
                                    probe_size)) {
        (void)g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER);
        return db_gl_probe_finish(0);
    }

    *mapped_out = mapped;
    return db_gl_probe_finish(1);
}

static int db_gl_context_probe_map_range_upload(size_t bytes,
                                                const float *initial_vertices) {
    if ((g_upload_proc_table.map_buffer_range == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_upload_probe_fill_pattern(pattern, probe_size);

    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer_range(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
            GL_MAP_UNSYNCHRONIZED_BIT);
    if ((dst == NULL) || (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

static int
db_gl_context_probe_map_buffer_upload(size_t bytes,
                                      const float *initial_vertices) {
    if ((g_upload_proc_table.map_buffer == NULL) ||
        (g_upload_proc_table.unmap_buffer == NULL) ||
        (g_upload_proc_table.buffer_sub_data == NULL)) {
        return 0;
    }

    const size_t probe_size = db_gl_upload_probe_size_bytes(bytes);
    if (probe_size == 0U) {
        return 0;
    }

    uint8_t pattern[DB_GL_PROBE_PREFIX_BYTES] = {0};
    db_gl_upload_probe_fill_pattern(pattern, probe_size);

    db_gl_probe_drain_errors();
    void *dst = g_upload_proc_table.map_buffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if ((dst == NULL) || (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    db_copy_bytes(dst, pattern, probe_size);
    if ((g_upload_proc_table.unmap_buffer(GL_ARRAY_BUFFER) != GL_TRUE) ||
        (db_gl_probe_step_error_free() == 0)) {
        return db_gl_probe_finish(0);
    }

    if (!db_gl_verify_buffer_prefix(pattern, probe_size)) {
        return db_gl_probe_finish(0);
    }

    g_upload_proc_table.buffer_sub_data(
        GL_ARRAY_BUFFER, 0, (GLsizeiptr)probe_size, initial_vertices);
    return db_gl_probe_finish(db_gl_probe_step_error_free());
}

void db_gl_context_probe_upload_capabilities(size_t bytes,
                                             const float *initial_vertices,
                                             db_gl_upload_probe_result_t *out) {
    if (out == NULL) {
        db_failf("renderer_gl_common",
                 "db_gl_context_probe_upload_capabilities: output is null");
    }

    *out = (db_gl_upload_probe_result_t){0};
    if (db_gl_context_advertises_vbo() == 0) {
        return;
    }

    db_gl_require_upload_proc_table_loaded(
        "db_gl_context_probe_upload_capabilities");

    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();

    if (db_gl_extensions_advertise_buffer_storage(&runtime) &&
        db_gl_extensions_advertise_map_buffer_range(&runtime) &&
        db_gl_context_probe_persistent_upload(bytes, initial_vertices,
                                              &out->persistent_mapped_ptr)) {
        out->use_persistent_upload = 1;
        return;
    }

    if (g_upload_proc_table.buffer_data == NULL) {
        return;
    }
    // Intentionally pass NULL: this is a storage/orphan allocation step for
    // the probe buffer, not the data-path validation itself.
    g_upload_proc_table.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, NULL,
                                    GL_DYNAMIC_DRAW);
    if (db_gl_probe_step_error_free() == 0) {
        (void)db_gl_probe_finish(0);
        return;
    }

    if (db_gl_extensions_advertise_map_buffer_range(&runtime) &&
        db_gl_context_probe_map_range_upload(bytes, initial_vertices)) {
        out->use_map_range_upload = 1;
        return;
    }

    if (db_gl_extensions_advertise_map_buffer(&runtime) &&
        db_gl_context_probe_map_buffer_upload(bytes, initial_vertices)) {
        out->use_map_buffer_upload = 1;
    }
}

// 5) Upload path helpers and range upload execution.
// Implemented in renderer_gl_upload.c.
