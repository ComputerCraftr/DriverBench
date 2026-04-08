#ifndef DRIVERBENCH_RENDERER_GL_PROC_RUNTIME_INTERNAL_H
#define DRIVERBENCH_RENDERER_GL_PROC_RUNTIME_INTERNAL_H

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
#include "renderer_benchmark_gradient.h"

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

#define DB_GL_TRACKED_TEXTURE_UNIT_COUNT 32U

extern db_gl_upload_proc_table_t g_upload_proc_table;
extern db_gl_proc_resolver_fn_t g_proc_resolver;
extern int g_blend_enabled_state;
extern int g_client_state_color_array_enabled;
extern int g_client_state_texcoord_array_enabled;
extern int g_client_state_vertex_array_enabled;
extern int g_cull_face_enabled_state;
extern int g_depth_test_enabled_state;
extern int g_dither_enabled_state;
extern unsigned int g_bound_draw_framebuffer;
extern int g_bound_draw_framebuffer_valid;
extern unsigned int g_bound_read_framebuffer;
extern int g_bound_read_framebuffer_valid;
extern unsigned int g_bound_vertex_array;
extern int g_bound_vertex_array_valid;
extern unsigned int g_current_program;
extern int g_current_program_valid;
extern unsigned int
    g_texture2d_binding_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT];
extern int g_texture2d_binding_valid_by_unit[DB_GL_TRACKED_TEXTURE_UNIT_COUNT];
extern unsigned int g_texture2d_enabled_state;
extern int g_texture2d_enabled_state_valid;
extern unsigned int g_active_texture_unit;
extern int g_active_texture_unit_valid;

void db_gl_require_upload_proc_table_loaded(const char *func_name);
GLenum db_gl_get_error_value(void);
db_gl_generic_proc_t db_gl_get_proc(const char *name);
void db_gl_load_upload_proc_table(void);
int db_gl_runtime_has_extension(const db_gl_runtime_metadata_t *runtime,
                                const char *extension_name);
int db_gl_runtime_has_usable_version(const db_gl_runtime_metadata_t *runtime);
int db_gl_runtime_is_es_context(const db_gl_runtime_metadata_t *runtime);
int db_gl_runtime_version_at_least(const db_gl_runtime_metadata_t *runtime,
                                   int req_major, int req_minor);
int db_gl_runtime_supports_desktop_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name);
int db_gl_runtime_supports_es_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name);
db_gl_runtime_metadata_t db_gl_runtime_metadata_load(void);
int db_gl_extensions_advertise_buffer_storage(
    const db_gl_runtime_metadata_t *runtime);
int db_gl_extensions_advertise_map_buffer(
    const db_gl_runtime_metadata_t *runtime);
int db_gl_extensions_advertise_map_buffer_range(
    const db_gl_runtime_metadata_t *runtime);
int db_gl_extensions_advertise_pbo(const db_gl_runtime_metadata_t *runtime);
int db_gl_extensions_advertise_texture_float(
    const db_gl_runtime_metadata_t *runtime);
int db_gl_extensions_advertise_vbo(const db_gl_runtime_metadata_t *runtime);
int db_gl_context_has_pbo_upload_procs(void);
int db_gl_context_advertises_vbo(void);
void db_gl_quad_init(float *verts);
unsigned int db_gl_pbo_create_or_zero(void);
void db_gl_pbo_delete_if_valid(unsigned int pbo);
void db_gl_pbo_unbind_unpack(void);
unsigned int db_gl_pbo_create_if_usable(int prefer_unpack_pbo);

#endif
