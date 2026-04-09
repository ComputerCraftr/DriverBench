#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_probe_internal.h"
#include "renderer_gl_proc_runtime_internal.h"
#include <stddef.h>

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

int db_gl_texture_create_rgba8(unsigned int *out_texture, int width, int height,
                               const void *pixels) {
    return db_gl_texture_create_rgba(out_texture, width, height, GL_RGBA,
                                     pixels);
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
    if ((g_unpack_alignment_state_valid != 0) &&
        (g_unpack_alignment_state == 1)) {
        return;
    }
    if (g_upload_proc_table.pixel_storei != NULL) {
        g_upload_proc_table.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
        g_unpack_alignment_state = 1;
        g_unpack_alignment_state_valid = 1;
    }
}

void db_gl_prepare_textured_present_state(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_blend_enabled(0);
    db_gl_set_dither_enabled(0);
    db_gl_set_texture_2d_enabled(1);
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(0);
    (void)db_gl_bind_array_buffer_cached(0U, NULL);
    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    db_gl_set_client_state_texcoord_array_enabled(1);
}

void db_gl_finish_textured_present_state(void) {
    db_gl_texture_bind_2d(0U);
    db_gl_set_unpack_row_length_pixels(0);
    db_gl_set_texture_2d_enabled(0);
    db_gl_set_client_state_texcoord_array_enabled(0);
}

void db_gl_upload_state_reset_unpack(void) {
    db_gl_set_unpack_alignment_1();
    db_gl_set_unpack_row_length_pixels(0);
    (void)db_gl_bind_unpack_buffer_cached(0U, NULL);
}

void db_gl_upload_state_reset_present_arrays(void) {
    (void)db_gl_bind_array_buffer_cached(0U, NULL);
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
