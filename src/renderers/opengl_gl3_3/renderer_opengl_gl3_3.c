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
#define DB_RENDER_MODE_GRADIENT_SWEEP ((int)DB_PATTERN_GRADIENT_SWEEP)
#define DB_RENDER_MODE_BANDS ((int)DB_PATTERN_BANDS)
#define DB_RENDER_MODE_SNAKE_GRID ((int)DB_PATTERN_SNAKE_GRID)
#define DB_RENDER_MODE_GRADIENT_FILL ((int)DB_PATTERN_GRADIENT_FILL)
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
    GLint u_grid_base_color;
    GLint u_grid_target_color;
    int uniform_grid_clearing_phase_cache;
    int uniform_grid_phase_completed_cache;
    uint32_t uniform_grid_cursor_cache;
    uint32_t uniform_grid_batch_size_cache;
    uint32_t uniform_gradient_head_row_cache;
    float *vertices;
    uint32_t work_unit_count;
    uint32_t grid_cursor;
    uint32_t grid_batch_size;
    int grid_phase_completed;
    int grid_clearing_phase;
    uint32_t gradient_head_row;
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

static void db_set_uniform1f_u32_if_changed(GLint location, uint32_t *cache,
                                            uint32_t value) {
    if ((location >= 0) && (*cache != value)) {
        glUniform1f(location, (float)value);
        *cache = value;
    }
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
        failf("Shader compile failed (%u): %.*s", (unsigned)shader_type,
              (int)msg_len, log_msg);
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
        failf("Program link failed: %.*s", (int)msg_len, log_msg);
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
    g_state.u_grid_base_color =
        glGetUniformLocation(g_state.program, "u_grid_base_color");
    g_state.u_grid_target_color =
        glGetUniformLocation(g_state.program, "u_grid_target_color");
    g_state.uniform_grid_clearing_phase_cache = -1;
    g_state.uniform_grid_phase_completed_cache = -1;
    g_state.uniform_grid_cursor_cache = UINT32_MAX;
    g_state.uniform_grid_batch_size_cache = UINT32_MAX;
    g_state.uniform_gradient_head_row_cache = UINT32_MAX;

    if (g_state.u_render_mode >= 0) {
        int render_mode = DB_RENDER_MODE_BANDS;
        if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
            render_mode = DB_RENDER_MODE_GRADIENT_SWEEP;
        } else if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
            render_mode = DB_RENDER_MODE_SNAKE_GRID;
        } else if (g_state.pattern == DB_PATTERN_GRADIENT_FILL) {
            render_mode = DB_RENDER_MODE_GRADIENT_FILL;
        }
        glUniform1i(g_state.u_render_mode, render_mode);
    }
    glUniform3f(g_state.u_grid_base_color, BENCH_GRID_PHASE0_R,
                BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    glUniform3f(g_state.u_grid_target_color, BENCH_GRID_PHASE1_R,
                BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B);
    glUniform1f(g_state.u_grid_cols, (float)db_grid_cols_effective());
    glUniform1f(g_state.u_grid_rows, (float)db_grid_rows_effective());
    glUniform1f(g_state.u_gradient_window_rows,
                (float)db_gradient_sweep_window_rows_effective());
    glUniform1f(g_state.u_gradient_fill_window_rows,
                (float)db_gradient_fill_window_rows_effective());
    db_set_uniform1f_u32_if_changed(g_state.u_gradient_head_row,
                                    &g_state.uniform_gradient_head_row_cache,
                                    g_state.gradient_head_row);
    db_set_uniform1i_if_changed(g_state.u_grid_clearing_phase,
                                &g_state.uniform_grid_clearing_phase_cache,
                                g_state.grid_clearing_phase);
}

void db_renderer_opengl_gl3_3_render_frame(double time_s) {
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
        db_set_uniform1f_u32_if_changed(g_state.u_grid_cursor,
                                        &g_state.uniform_grid_cursor_cache,
                                        grid_plan.active_cursor);
        db_set_uniform1f_u32_if_changed(g_state.u_grid_batch_size,
                                        &g_state.uniform_grid_batch_size_cache,
                                        g_state.grid_batch_size);
    } else {
        const uint32_t rows = db_grid_rows_effective();
        if (rows > 0U) {
            uint32_t next_head = g_state.gradient_head_row + 1U;
            int next_phase = g_state.grid_clearing_phase;
            if (next_head >= rows) {
                next_head = 0U;
                next_phase = !next_phase;
            }
            g_state.gradient_head_row = next_head;
            g_state.grid_clearing_phase = next_phase;
            db_set_uniform1f_u32_if_changed(
                g_state.u_gradient_head_row,
                &g_state.uniform_gradient_head_row_cache, next_head);
            db_set_uniform1i_if_changed(
                g_state.u_grid_clearing_phase,
                &g_state.uniform_grid_clearing_phase_cache, next_phase);
        }
    }

    glDrawArrays(GL_TRIANGLES, 0, g_state.draw_vertex_count);
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    if (g_state.persistent_mapped_ptr != NULL) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        glUnmapBuffer(GL_ARRAY_BUFFER);
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
