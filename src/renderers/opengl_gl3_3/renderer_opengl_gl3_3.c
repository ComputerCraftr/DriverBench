#include "renderer_opengl_gl3_3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
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

#if !defined(OPENGL_GL3_3_VERT_SHADER_PATH) ||                                 \
    !defined(OPENGL_GL3_3_FRAG_SHADER_PATH)
#error "OpenGL GL3.3 shader paths must be provided by the build system."
#endif

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define SHADER_LOG_MSG_CAPACITY 1024
#define ATTR_POSITION_LOC 0U
#define ATTR_COLOR_LOC 1U
#define ATTR_POSITION_COMPONENTS 2
#define ATTR_COLOR_COMPONENTS 3
#define DB_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT "opengl_shader_vbo_persistent"
#define DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE "opengl_shader_vbo_map_range"
#define DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER "opengl_shader_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_SHADER_VBO "opengl_shader_vbo"
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint program;
    GLint u_render_mode;
    GLint u_grid_clearing_phase;
    GLint u_grid_phase_completed;
    GLint u_grid_cursor;
    GLint u_grid_batch_size;
    GLint u_grid_cols;
    GLint u_grid_rows;
    GLint u_gradient_head_row;
    GLint u_gradient_window_rows;
    GLint u_gradient_fill_window_rows;
    GLint u_gradient_fill_cycle;
    GLint u_rect_seed;
    GLint u_grid_base_color;
    GLint u_grid_target_color;
    GLint u_history_tex;
    int uniform_grid_clearing_phase_cache;
    int uniform_grid_phase_completed_cache;
    int uniform_grid_cursor_cache;
    int uniform_grid_batch_size_cache;
    int uniform_gradient_head_row_cache;
    int uniform_gradient_fill_cycle_cache;
    float *vertices;
    uint32_t work_unit_count;
    uint32_t grid_cursor;
    uint32_t grid_batch_size;
    int grid_phase_completed;
    int grid_clearing_phase;
    uint32_t gradient_head_row;
    int gradient_sweep_direction_down;
    uint32_t gradient_sweep_cycle;
    uint32_t gradient_fill_cycle;
    uint32_t rect_snake_rect_index;
    uint32_t rect_snake_seed;
    GLuint fallback_tex;
    GLuint history_tex[2];
    GLuint history_fbo[2];
    int history_width;
    int history_height;
    int history_initialized;
    int history_read_index;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
    size_t vbo_bytes;
    int use_map_range_upload;
    int use_map_buffer_upload;
    int use_persistent_upload;
    void *persistent_mapped_ptr;
} renderer_state_t;

static renderer_state_t g_state = {0};

// NOLINTBEGIN(performance-no-int-to-ptr)
static const void *vbo_offset_ptr(size_t byte_offset) {
    return (const void *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static void db_set_uniform1i_if_changed(GLint location, int *cache, int value) {
    if ((location >= 0) && (*cache != value)) {
        glUniform1i(location, value);
        *cache = value;
    }
}

static void db_set_uniform1i_u32_if_changed(GLint location, int *cache,
                                            uint32_t value) {
    const int as_i32 = db_checked_u32_to_i32(BACKEND_NAME, "gl_uniform", value);
    if ((location >= 0) && (*cache != as_i32)) {
        glUniform1i(location, as_i32);
        *cache = as_i32;
    }
}

static void db_gl3_destroy_history_targets(void) {
    for (size_t i = 0U; i < 2U; i++) {
        if (g_state.history_fbo[i] != 0U) {
            glDeleteFramebuffers(1, &g_state.history_fbo[i]);
            g_state.history_fbo[i] = 0U;
        }
        if (g_state.history_tex[i] != 0U) {
            glDeleteTextures(1, &g_state.history_tex[i]);
            g_state.history_tex[i] = 0U;
        }
    }
    g_state.history_width = 0;
    g_state.history_height = 0;
    g_state.history_initialized = 0;
    g_state.history_read_index = -1;
}

static void db_gl3_create_fallback_texture(void) {
    if (g_state.fallback_tex != 0U) {
        return;
    }
    static const unsigned char fallback_rgba[4] = {
        (unsigned char)(BENCH_GRID_PHASE0_R * 255.0F),
        (unsigned char)(BENCH_GRID_PHASE0_G * 255.0F),
        (unsigned char)(BENCH_GRID_PHASE0_B * 255.0F), 255U};
    glGenTextures(1, &g_state.fallback_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_state.fallback_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 fallback_rgba);
}

static void db_gl3_ensure_history_targets(void) {
    if (db_pattern_uses_history_texture(g_state.pattern) == 0) {
        return;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if ((viewport[2] <= 0) || (viewport[3] <= 0)) {
        return;
    }
    GLint prev_read_fbo = 0;
    GLint prev_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    if ((g_state.history_width == viewport[2]) &&
        (g_state.history_height == viewport[3]) &&
        (g_state.history_tex[0] != 0U) && (g_state.history_tex[1] != 0U) &&
        (g_state.history_fbo[0] != 0U) && (g_state.history_fbo[1] != 0U) &&
        (g_state.history_read_index >= 0)) {
        return;
    }

    const int old_width = g_state.history_width;
    const int old_height = g_state.history_height;
    const int old_read_index = g_state.history_read_index;
    const int old_initialized = g_state.history_initialized;
    const GLuint old_tex0 = g_state.history_tex[0];
    const GLuint old_tex1 = g_state.history_tex[1];
    const GLuint old_fbo0 = g_state.history_fbo[0];
    const GLuint old_fbo1 = g_state.history_fbo[1];

    GLuint new_tex[2] = {0U, 0U};
    GLuint new_fbo[2] = {0U, 0U};
    for (size_t i = 0U; i < 2U; i++) {
        glGenTextures(1, &new_tex[i]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, new_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewport[2], viewport[3], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glGenFramebuffers(1, &new_fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, new_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, new_tex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            failf("history framebuffer incomplete");
        }
    }

    int copied_history = 0;
    if ((old_initialized != 0) && (old_width > 0) && (old_height > 0) &&
        ((old_read_index == 0) || (old_read_index == 1))) {
        const GLuint old_read_tex = (old_read_index == 0) ? old_tex0 : old_tex1;
        if (old_read_tex != 0U) {
            GLuint read_fbo = 0U;
            glGenFramebuffers(1, &read_fbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, old_read_tex, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE) {
                copied_history = 1;
                for (size_t i = 0U; i < 2U; i++) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, new_fbo[i]);
                    glBlitFramebuffer(0, 0, old_width, old_height, 0, 0,
                                      viewport[2], viewport[3],
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    if (glGetError() != GL_NO_ERROR) {
                        copied_history = 0;
                        break;
                    }
                }
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
            if (read_fbo != 0U) {
                glDeleteFramebuffers(1, &read_fbo);
            }
        }
    }

    if (copied_history == 0) {
        for (size_t i = 0U; i < 2U; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, new_fbo[i]);
            glClearColor(BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                         BENCH_GRID_PHASE0_B, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    if (old_fbo0 != 0U) {
        glDeleteFramebuffers(1, &old_fbo0);
    }
    if (old_fbo1 != 0U) {
        glDeleteFramebuffers(1, &old_fbo1);
    }
    if (old_tex0 != 0U) {
        glDeleteTextures(1, &old_tex0);
    }
    if (old_tex1 != 0U) {
        glDeleteTextures(1, &old_tex1);
    }

    g_state.history_tex[0] = new_tex[0];
    g_state.history_tex[1] = new_tex[1];
    g_state.history_fbo[0] = new_fbo[0];
    g_state.history_fbo[1] = new_fbo[1];
    g_state.history_width = viewport[2];
    g_state.history_height = viewport[3];
    g_state.history_initialized = 1;
    g_state.history_read_index = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
}

static GLuint compile_shader(GLenum shader_type, const char *source) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        GLsizei msg_len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log_msg), &msg_len, log_msg);
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "shader_log_msg_len", msg_len);
        failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
              msg_len_i32, log_msg);
    }
    return shader;
}

static GLuint build_program_from_files(const char *vert_shader_path,
                                       const char *frag_shader_path) {
    char *vert_src = db_read_text_file_or_fail(BACKEND_NAME, vert_shader_path);
    char *frag_src = db_read_text_file_or_fail(BACKEND_NAME, frag_shader_path);

    GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    free(vert_src);
    free(frag_src);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log_msg[SHADER_LOG_MSG_CAPACITY];
        GLsizei msg_len = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(log_msg), &msg_len,
                            log_msg);
        const int msg_len_i32 =
            db_checked_int_to_i32(BACKEND_NAME, "program_log_msg_len", msg_len);
        failf("Program link failed: %.*s", msg_len_i32, log_msg);
    }
    return program;
}

static int db_init_vertices_for_mode(void) {
    db_pattern_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_mode_common(BACKEND_NAME, &init_state)) {
        return 0;
    }

    g_state.vertices = init_state.vertices;
    g_state.pattern = init_state.pattern;
    g_state.work_unit_count = init_state.work_unit_count;
    g_state.draw_vertex_count = (GLsizei)init_state.draw_vertex_count;
    g_state.grid_cursor = init_state.snake_cursor;
    g_state.grid_batch_size = init_state.snake_batch_size;
    g_state.grid_phase_completed = init_state.snake_phase_completed;
    g_state.grid_clearing_phase = init_state.snake_clearing_phase;
    g_state.gradient_head_row = init_state.gradient_head_row;
    g_state.gradient_sweep_direction_down =
        init_state.gradient_sweep_direction_down;
    g_state.gradient_sweep_cycle = init_state.gradient_sweep_cycle;
    g_state.gradient_fill_cycle = init_state.gradient_fill_cycle;
    g_state.rect_snake_seed = init_state.rect_snake_seed;
    return 1;
}

void db_renderer_opengl_gl3_3_init(void) {
    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    glGenVertexArrays(1, &g_state.vao);
    glGenBuffers(1, &g_state.vbo);
    glBindVertexArray(g_state.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
    g_state.vbo_bytes = (size_t)g_state.draw_vertex_count *
                        DB_VERTEX_FLOAT_STRIDE * sizeof(float);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_state.vbo_bytes,
                 g_state.vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(ATTR_POSITION_LOC);
    glVertexAttribPointer(
        ATTR_POSITION_LOC, ATTR_POSITION_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)), (const void *)0);
    glEnableVertexAttribArray(ATTR_COLOR_LOC);
    glVertexAttribPointer(
        ATTR_COLOR_LOC, ATTR_COLOR_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_VERTEX_FLOAT_STRIDE * sizeof(float)),
        vbo_offset_ptr(DB_VERTEX_POSITION_FLOAT_COUNT * sizeof(float)));

    g_state.use_map_range_upload = 0;
    g_state.use_map_buffer_upload = 0;
    g_state.use_persistent_upload = 0;
    g_state.persistent_mapped_ptr = NULL;
    db_gl_upload_probe_result_t probe_result = {0};
    db_gl_probe_upload_capabilities(g_state.vbo_bytes, g_state.vertices, 1,
                                    &probe_result);
    g_state.use_map_range_upload = (probe_result.use_map_range_upload != 0);
    g_state.use_map_buffer_upload = (probe_result.use_map_buffer_upload != 0);
    g_state.use_persistent_upload = (probe_result.use_persistent_upload != 0);
    g_state.persistent_mapped_ptr = probe_result.persistent_mapped_ptr;
    infof("using capability mode: %s",
          db_renderer_opengl_gl3_3_capability_mode());

    g_state.program = build_program_from_files(OPENGL_GL3_3_VERT_SHADER_PATH,
                                               OPENGL_GL3_3_FRAG_SHADER_PATH);
    glUseProgram(g_state.program);
    g_state.u_render_mode =
        glGetUniformLocation(g_state.program, "u_render_mode");
    g_state.u_grid_clearing_phase =
        glGetUniformLocation(g_state.program, "u_grid_clearing_phase");
    g_state.u_grid_phase_completed =
        glGetUniformLocation(g_state.program, "u_grid_phase_completed");
    g_state.u_grid_cursor =
        glGetUniformLocation(g_state.program, "u_grid_cursor");
    g_state.u_grid_batch_size =
        glGetUniformLocation(g_state.program, "u_grid_batch_size");
    g_state.u_grid_cols = glGetUniformLocation(g_state.program, "u_grid_cols");
    g_state.u_grid_rows = glGetUniformLocation(g_state.program, "u_grid_rows");
    g_state.u_gradient_head_row =
        glGetUniformLocation(g_state.program, "u_gradient_head_row");
    g_state.u_gradient_window_rows =
        glGetUniformLocation(g_state.program, "u_gradient_window_rows");
    g_state.u_gradient_fill_window_rows =
        glGetUniformLocation(g_state.program, "u_gradient_fill_window_rows");
    g_state.u_gradient_fill_cycle =
        glGetUniformLocation(g_state.program, "u_gradient_fill_cycle");
    g_state.u_rect_seed = glGetUniformLocation(g_state.program, "u_rect_seed");
    g_state.u_grid_base_color =
        glGetUniformLocation(g_state.program, "u_grid_base_color");
    g_state.u_grid_target_color =
        glGetUniformLocation(g_state.program, "u_grid_target_color");
    g_state.u_history_tex =
        glGetUniformLocation(g_state.program, "u_history_tex");
    g_state.uniform_grid_clearing_phase_cache = -1;
    g_state.uniform_grid_phase_completed_cache = -1;
    g_state.uniform_grid_cursor_cache = -1;
    g_state.uniform_grid_batch_size_cache = -1;
    g_state.uniform_gradient_head_row_cache = -1;
    g_state.uniform_gradient_fill_cycle_cache = -1;

    if (g_state.u_render_mode >= 0) {
        int render_mode = DB_RENDER_MODE_BANDS;
        if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
            render_mode = DB_RENDER_MODE_GRADIENT_SWEEP;
        } else if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
            render_mode = DB_RENDER_MODE_SNAKE_GRID;
        } else if (g_state.pattern == DB_PATTERN_GRADIENT_FILL) {
            render_mode = DB_RENDER_MODE_GRADIENT_FILL;
        } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
            render_mode = DB_RENDER_MODE_RECT_SNAKE;
        }
        glUniform1i(g_state.u_render_mode, render_mode);
    }
    glUniform3f(g_state.u_grid_base_color, BENCH_GRID_PHASE0_R,
                BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    glUniform3f(g_state.u_grid_target_color, BENCH_GRID_PHASE1_R,
                BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B);
    glUniform1i(g_state.u_grid_cols,
                db_checked_u32_to_i32(BACKEND_NAME, "u_grid_cols",
                                      db_grid_cols_effective()));
    glUniform1i(g_state.u_grid_rows,
                db_checked_u32_to_i32(BACKEND_NAME, "u_grid_rows",
                                      db_grid_rows_effective()));
    glUniform1i(
        g_state.u_gradient_window_rows,
        db_checked_u32_to_i32(BACKEND_NAME, "u_gradient_window_rows",
                              db_gradient_sweep_window_rows_effective()));
    glUniform1i(
        g_state.u_gradient_fill_window_rows,
        db_checked_u32_to_i32(BACKEND_NAME, "u_gradient_fill_window_rows",
                              db_gradient_fill_window_rows_effective()));
    glUniform1i(g_state.u_rect_seed,
                db_checked_u32_to_i32(BACKEND_NAME, "u_rect_seed",
                                      g_state.rect_snake_seed));
    if (g_state.u_history_tex >= 0) {
        glUniform1i(g_state.u_history_tex, 0);
    }
    db_gl3_create_fallback_texture();
    if (g_state.fallback_tex != 0U) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_state.fallback_tex);
    }
    db_set_uniform1i_u32_if_changed(g_state.u_gradient_head_row,
                                    &g_state.uniform_gradient_head_row_cache,
                                    g_state.gradient_head_row);
    db_set_uniform1i_u32_if_changed(
        g_state.u_gradient_fill_cycle,
        &g_state.uniform_gradient_fill_cycle_cache,
        (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP)
            ? g_state.gradient_sweep_cycle
            : g_state.gradient_fill_cycle);
    db_set_uniform1i_if_changed(g_state.u_grid_clearing_phase,
                                &g_state.uniform_grid_clearing_phase_cache,
                                g_state.grid_clearing_phase);
}

void db_renderer_opengl_gl3_3_render_frame(double time_s) {
    db_gl3_ensure_history_targets();

    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb(g_state.vertices, g_state.work_unit_count,
                                      time_s);
        db_gl_upload_buffer(
            g_state.vertices, g_state.vbo_bytes, g_state.use_persistent_upload,
            g_state.persistent_mapped_ptr, g_state.use_map_range_upload,
            g_state.use_map_buffer_upload);
    } else if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
        const db_snake_damage_plan_t grid_plan = db_snake_grid_plan_next_step(
            g_state.grid_cursor, 0U, 0U, g_state.grid_clearing_phase,
            g_state.work_unit_count);
        db_set_uniform1i_if_changed(g_state.u_grid_clearing_phase,
                                    &g_state.uniform_grid_clearing_phase_cache,
                                    grid_plan.clearing_phase);
        g_state.grid_batch_size = grid_plan.batch_size;
        g_state.grid_phase_completed = grid_plan.phase_completed;
        g_state.grid_cursor = grid_plan.next_cursor;
        g_state.grid_clearing_phase = grid_plan.next_clearing_phase;
        db_set_uniform1i_if_changed(g_state.u_grid_phase_completed,
                                    &g_state.uniform_grid_phase_completed_cache,
                                    g_state.grid_phase_completed);
        db_set_uniform1i_u32_if_changed(g_state.u_grid_cursor,
                                        &g_state.uniform_grid_cursor_cache,
                                        grid_plan.active_cursor);
        db_set_uniform1i_u32_if_changed(g_state.u_grid_batch_size,
                                        &g_state.uniform_grid_batch_size_cache,
                                        g_state.grid_batch_size);
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
        const db_gradient_sweep_damage_plan_t plan =
            db_gradient_sweep_plan_next_frame(
                g_state.gradient_head_row,
                g_state.gradient_sweep_direction_down,
                g_state.gradient_sweep_cycle);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.gradient_sweep_direction_down = plan.next_direction_down;
        g_state.gradient_sweep_cycle = plan.next_cycle_index;
        db_set_uniform1i_u32_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache, plan.render_head_row);
        db_set_uniform1i_if_changed(g_state.u_grid_clearing_phase,
                                    &g_state.uniform_grid_clearing_phase_cache,
                                    plan.render_direction_down);
        db_set_uniform1i_u32_if_changed(
            g_state.u_gradient_fill_cycle,
            &g_state.uniform_gradient_fill_cycle_cache,
            plan.render_cycle_index);
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_FILL) {
        const db_gradient_fill_damage_plan_t plan =
            db_gradient_fill_plan_next_frame(g_state.gradient_head_row,
                                             g_state.gradient_fill_cycle);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.gradient_fill_cycle = plan.next_cycle_index;
        db_set_uniform1i_u32_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache, plan.render_head_row);
        db_set_uniform1i_u32_if_changed(
            g_state.u_gradient_fill_cycle,
            &g_state.uniform_gradient_fill_cycle_cache,
            plan.render_cycle_index);
    } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
        const db_rect_snake_plan_t plan = db_rect_snake_plan_next_step(
            g_state.rect_snake_seed, g_state.rect_snake_rect_index,
            g_state.grid_cursor);
        db_set_uniform1i_u32_if_changed(
            g_state.u_gradient_head_row,
            &g_state.uniform_gradient_head_row_cache, plan.active_rect_index);
        db_set_uniform1i_u32_if_changed(g_state.u_grid_cursor,
                                        &g_state.uniform_grid_cursor_cache,
                                        plan.active_cursor);
        db_set_uniform1i_u32_if_changed(g_state.u_grid_batch_size,
                                        &g_state.uniform_grid_batch_size_cache,
                                        plan.batch_size);
        db_set_uniform1i_if_changed(g_state.u_grid_phase_completed,
                                    &g_state.uniform_grid_phase_completed_cache,
                                    plan.rect_completed);
        g_state.grid_cursor = plan.next_cursor;
        g_state.rect_snake_rect_index = plan.next_rect_index;
    }
    if (db_pattern_uses_history_texture(g_state.pattern) == 0) {
        if (g_state.fallback_tex != 0U) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_state.fallback_tex);
        }
        glDrawArrays(GL_TRIANGLES, 0, g_state.draw_vertex_count);
        return;
    }
    if ((g_state.history_read_index < 0) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        return;
    }

    const int read_index = g_state.history_read_index;
    const int write_index = (read_index == 0) ? 1 : 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex[read_index]);

    GLint prev_read_fbo = 0;
    GLint prev_draw_fbo = 0;
    GLint prev_viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, g_state.history_fbo[write_index]);
    glViewport(0, 0, g_state.history_width, g_state.history_height);
    glDrawArrays(GL_TRIANGLES, 0, g_state.draw_vertex_count);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_state.history_fbo[write_index]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, g_state.history_width, g_state.history_height, 0, 0,
                      g_state.history_width, g_state.history_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2],
               prev_viewport[3]);
    g_state.history_read_index = write_index;
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    if (g_state.persistent_mapped_ptr != NULL) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    db_gl3_destroy_history_targets();
    if (g_state.fallback_tex != 0U) {
        glDeleteTextures(1, &g_state.fallback_tex);
        g_state.fallback_tex = 0U;
    }
    glDeleteProgram(g_state.program);
    glDeleteBuffers(1, &g_state.vbo);
    glDeleteVertexArrays(1, &g_state.vao);
    free(g_state.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl3_3_capability_mode(void) {
    if (g_state.use_persistent_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT;
    }
    if (g_state.use_map_range_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE;
    }
    if (g_state.use_map_buffer_upload != 0) {
        return DB_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER;
    }
    return DB_CAP_MODE_OPENGL_SHADER_VBO;
}

uint32_t db_renderer_opengl_gl3_3_work_unit_count(void) {
    return (g_state.work_unit_count != 0U) ? g_state.work_unit_count
                                           : BENCH_BANDS;
}
