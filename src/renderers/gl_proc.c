#include "core/db_log.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void db_gl_check_error_at(const char *file, int line, const char *func) {
    if (g_upload_proc_table.get_error == NULL) {
        return;
    }
    db_gl_error_trace_t trace = {0};
    (void)db_gl_error_trace_drain(&trace, "api", "gl", func);
    for (size_t index = 0U; index < trace.count; index++) {
        DB_RUNTIME_ERROR("gl_error", "%s:%d (%s) GL error: 0x%04X", file, line,
                         func, trace.records[index].error_code);
    }
}

void db_gl_require_upload_proc_table_loaded(const char *func_name) {
    if (g_upload_proc_table.loaded == 0) {
        DB_RUNTIME_FAIL("renderer_gl_common",
                        "%s requires preloaded GL proc table; call "
                        "db_gl_load_upload_proc_table() during init",
                        func_name);
    }
}

uint32_t db_gl_get_error_value(void) {
    if (g_upload_proc_table.get_error == NULL) {
        return (uint32_t)GL_NO_ERROR;
    }
    db_gl_error_trace_t trace = {0};
    (void)db_gl_error_trace_drain(&trace, "api", "gl", "get_error_value");
    return (trace.count > 0U) ? trace.records[0].error_code
                              : (uint32_t)GL_NO_ERROR;
}

db_gl_generic_proc_t db_gl_get_proc(const char *name) {
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
    void *const symbol = dlsym(RTLD_DEFAULT, name);
    db_gl_generic_proc_t dlsym_proc = NULL;
    static_assert(sizeof(dlsym_proc) == sizeof(symbol),
                  "dlsym pointer representation must fit a GL procedure");
    memcpy((void *)&dlsym_proc, (const void *)&symbol, sizeof(dlsym_proc));
    if (dlsym_proc != NULL) {
        return dlsym_proc;
    }
#endif

    return NULL;
}

void db_gl_set_proc_resolver(db_gl_proc_resolver_fn_t resolver) {
    if ((g_upload_proc_table.loaded != 0) && (g_proc_resolver != resolver)) {
        DB_RUNTIME_FAIL(
            "renderer_gl_common",
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

void db_gl_load_upload_proc_table(void) {
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
    g_upload_proc_table.draw_arrays_instanced =
        (db_gl_draw_arrays_instanced_fn_t)(db_gl_get_proc(
            "glDrawArraysInstanced"));
    g_upload_proc_table.enable =
        (db_gl_enable_fn_t)(db_gl_get_proc("glEnable"));
    g_upload_proc_table.enable_client_state =
        (db_gl_enable_client_state_fn_t)(db_gl_get_proc("glEnableClientState"));
    g_upload_proc_table.enable_vertex_attrib_array =
        (db_gl_enable_vertex_attrib_array_fn_t)(db_gl_get_proc(
            "glEnableVertexAttribArray"));
    g_upload_proc_table.fence_sync =
        (db_gl_fence_sync_fn_t)(db_gl_get_proc("glFenceSync"));
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
    g_upload_proc_table.client_wait_sync =
        (db_gl_client_wait_sync_fn_t)(db_gl_get_proc("glClientWaitSync"));
    g_upload_proc_table.delete_sync =
        (db_gl_delete_sync_fn_t)(db_gl_get_proc("glDeleteSync"));
    g_upload_proc_table.shader_source =
        (db_gl_shader_source_fn_t)(db_gl_get_proc("glShaderSource"));
    g_upload_proc_table.tex_coord_pointer =
        (db_gl_tex_coord_pointer_fn_t)(db_gl_get_proc("glTexCoordPointer"));
    g_upload_proc_table.tex_image_2d =
        (db_gl_tex_image_2d_fn_t)(db_gl_get_proc("glTexImage2D"));
    g_upload_proc_table.tex_parameteri =
        (db_gl_tex_parameteri_fn_t)(db_gl_get_proc("glTexParameteri"));
    g_upload_proc_table.tex_envi =
        (db_gl_tex_envi_fn_t)(db_gl_get_proc("glTexEnvi"));
    g_upload_proc_table.tex_buffer =
        (db_gl_tex_buffer_fn_t)(db_gl_get_proc("glTexBuffer"));
    if (g_upload_proc_table.tex_buffer == NULL) {
        g_upload_proc_table.tex_buffer =
            (db_gl_tex_buffer_fn_t)(db_gl_get_proc("glTexBufferARB"));
    }
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
    g_upload_proc_table.vertex_attrib_i_pointer =
        (db_gl_vertex_attrib_i_pointer_fn_t)(db_gl_get_proc(
            "glVertexAttribIPointer"));
    g_upload_proc_table.vertex_attrib_divisor =
        (db_gl_vertex_attrib_divisor_fn_t)(db_gl_get_proc(
            "glVertexAttribDivisor"));
    g_upload_proc_table.vertex_pointer =
        (db_gl_vertex_pointer_fn_t)(db_gl_get_proc("glVertexPointer"));
    g_upload_proc_table.viewport =
        (db_gl_viewport_fn_t)(db_gl_get_proc("glViewport"));
    g_upload_proc_table.scissor =
        (db_gl_scissor_fn_t)(db_gl_get_proc("glScissor"));

    g_upload_proc_table.loaded = 1;
}
