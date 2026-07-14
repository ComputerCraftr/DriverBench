#include "../core/db_core.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"
#include <stddef.h>
#include <stdint.h>

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

void db_gl_delete_framebuffers(uint32_t count,
                               const unsigned int *framebuffers) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_framebuffers == NULL) || (count == 0U) ||
        (framebuffers == NULL)) {
        return;
    }
    g_upload_proc_table.delete_framebuffers(
        db_checked_u32_to_int("db_gl_delete_framebuffers", "count", count),
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

void db_gl_delete_vertex_arrays(uint32_t count, const unsigned int *arrays) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.delete_vertex_arrays == NULL) || (count == 0U) ||
        (arrays == NULL)) {
        return;
    }
    g_upload_proc_table.delete_vertex_arrays(
        db_checked_u32_to_int("db_gl_delete_vertex_arrays", "count", count),
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

void db_gl_gen_framebuffers(uint32_t count, unsigned int *framebuffers) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_framebuffers == NULL) || (count == 0U) ||
        (framebuffers == NULL)) {
        return;
    }
    g_upload_proc_table.gen_framebuffers(
        db_checked_u32_to_int("db_gl_gen_framebuffers", "count", count),
        (GLuint *)framebuffers);
}

void db_gl_gen_vertex_arrays(uint32_t count, unsigned int *arrays) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.gen_vertex_arrays == NULL) || (count == 0U) ||
        (arrays == NULL)) {
        return;
    }
    g_upload_proc_table.gen_vertex_arrays(
        db_checked_u32_to_int("db_gl_gen_vertex_arrays", "count", count),
        (GLuint *)arrays);
}

void db_gl_get_integerv(unsigned int pname, int *value) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_integerv != NULL) && (value != NULL)) {
        g_upload_proc_table.get_integerv((GLenum)pname, (GLint *)value);
    }
}

void db_gl_get_program_info_log(unsigned int program, size_t buf_size,
                                int *length, char *log) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_program_info_log != NULL) && (buf_size > 0U) &&
        (log != NULL)) {
        g_upload_proc_table.get_program_info_log(
            (GLuint)program,
            db_checked_size_to_i32("db_gl_get_program_info_log", "buf_size",
                                   buf_size),
            (GLsizei *)length, log);
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

void db_gl_get_shader_info_log(unsigned int shader, size_t buf_size,
                               int *length, char *log) {
    db_gl_load_upload_proc_table();
    if ((g_upload_proc_table.get_shader_info_log != NULL) && (buf_size > 0U) &&
        (log != NULL)) {
        g_upload_proc_table.get_shader_info_log(
            (GLuint)shader,
            db_checked_size_to_i32("db_gl_get_shader_info_log", "buf_size",
                                   buf_size),
            (GLsizei *)length, log);
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

void db_gl_vertex_attrib_pointer_4f(unsigned int index, int stride_bytes,
                                    size_t byte_offset) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_attrib_pointer != NULL) {
        g_upload_proc_table.vertex_attrib_pointer(
            (GLuint)index, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride_bytes,
            db_gl_vbo_offset_ptr(byte_offset));
    }
}

int db_gl_vertex_attrib_divisor(unsigned int index, unsigned int divisor) {
    db_gl_load_upload_proc_table();
    if (g_upload_proc_table.vertex_attrib_divisor == NULL) {
        return 0;
    }
    g_upload_proc_table.vertex_attrib_divisor((GLuint)index, (GLuint)divisor);
    return 1;
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
