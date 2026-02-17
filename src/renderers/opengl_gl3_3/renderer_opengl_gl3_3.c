#include "renderer_opengl_gl3_3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"

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

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define SHADER_LOG_MSG_CAPACITY 1024
#define ATTR_POSITION_LOC 0U
#define ATTR_COLOR_LOC 1U
#define ATTR_POSITION_COMPONENTS 2
#define ATTR_COLOR_COMPONENTS 3
#define BUFFER_OFFSET_BYTES(byte_count)                                        \
    ((const void *)((const char *)NULL + (byte_count)))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define CAPABILITY_MODE "shader_vbo"
#define RENDER_MODE_BANDS 0
#define RENDER_MODE_SNAKE_GRID 1

typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint program;
    GLint u_render_mode;
    GLint u_snake_clearing_phase;
    GLint u_snake_phase_completed;
    GLint u_snake_cursor;
    GLint u_snake_batch_size;
    GLint u_snake_cols;
    GLint u_snake_base_color;
    GLint u_snake_target_color;
    int uniform_snake_clearing_phase_cache;
    int uniform_snake_phase_completed_cache;
    uint32_t uniform_snake_cursor_cache;
    uint32_t uniform_snake_batch_size_cache;
    float *vertices;
    uint32_t work_unit_count;
    uint32_t snake_cursor;
    uint32_t snake_batch_size;
    int snake_phase_completed;
    int snake_clearing_phase;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
    const char *capability_mode;
} ShaderRendererState;

static ShaderRendererState g_state = {0};

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

static int db_init_band_vertices(void) {
    const size_t vertex_count =
        (size_t)BENCH_BANDS * DB_BAND_TRI_VERTS_PER_BAND;
    const size_t float_count = vertex_count * DB_BAND_VERT_FLOATS;

    g_state.vertices = (float *)calloc(float_count, sizeof(float));
    if (g_state.vertices == NULL) {
        return 0;
    }

    g_state.pattern = DB_PATTERN_BANDS;
    g_state.work_unit_count = BENCH_BANDS;
    g_state.draw_vertex_count = (GLsizei)vertex_count;
    g_state.capability_mode = CAPABILITY_MODE;
    return 1;
}

static int db_init_snake_grid_vertices(void) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(DB_PATTERN_SNAKE_GRID);
    if (tile_count_u64 == 0U || tile_count_u64 > UINT32_MAX) {
        return 0;
    }

    const uint64_t vertex_count_u64 =
        tile_count_u64 * DB_BAND_TRI_VERTS_PER_BAND;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * DB_BAND_VERT_FLOATS;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = (uint32_t)tile_count_u64;

    g_state.vertices = (float *)calloc(float_count, sizeof(float));
    if (g_state.vertices == NULL) {
        return 0;
    }

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_snake_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);

        const size_t base = (size_t)tile_index * DB_BAND_TRI_VERTS_PER_BAND *
                            DB_BAND_VERT_FLOATS;
        float *unit = &g_state.vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, DB_BAND_VERT_FLOATS);
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                             BENCH_GRID_PHASE0_B);
    }

    g_state.pattern = DB_PATTERN_SNAKE_GRID;
    g_state.work_unit_count = tile_count;
    g_state.draw_vertex_count = (GLsizei)vertex_count_u64;
    g_state.snake_cursor = 0U;
    g_state.snake_batch_size = 0U;
    g_state.snake_phase_completed = 0;
    g_state.snake_clearing_phase = 0;
    g_state.capability_mode = CAPABILITY_MODE;
    return 1;
}

static int db_init_vertices_for_mode(void) {
    db_pattern_t requested = DB_PATTERN_BANDS;
    if (!db_parse_benchmark_pattern_from_env(&requested)) {
        const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
        db_infof(BACKEND_NAME, "Invalid %s='%s'; using '%s'",
                 DB_BENCHMARK_MODE_ENV, (mode != NULL) ? mode : "",
                 DB_BENCHMARK_MODE_BANDS);
    }
    if ((requested == DB_PATTERN_SNAKE_GRID) && db_init_snake_grid_vertices()) {
        db_infof(BACKEND_NAME,
                 "benchmark mode: %s (%ux%u tiles, deterministic snake sweep)",
                 DB_BENCHMARK_MODE_SNAKE_GRID, db_snake_grid_rows_effective(),
                 db_snake_grid_cols_effective());
        return 1;
    }

    if (requested == DB_PATTERN_SNAKE_GRID) {
        db_infof(
            BACKEND_NAME,
            "snake_grid initialization failed; falling back to bands mode");
    }

    if (!db_init_band_vertices()) {
        return 0;
    }

    db_infof(BACKEND_NAME, "benchmark mode: %s (%u vertical bands)",
             DB_BENCHMARK_MODE_BANDS, BENCH_BANDS);
    return 1;
}

static void db_advance_snake_grid_state(void) {
    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(g_state.work_unit_count);
    g_state.snake_batch_size = db_snake_grid_step_batch_size(
        g_state.snake_cursor, g_state.work_unit_count, tiles_per_step);
    g_state.snake_phase_completed =
        ((g_state.snake_cursor + g_state.snake_batch_size) >=
         g_state.work_unit_count);
    g_state.snake_cursor += g_state.snake_batch_size;
    if (g_state.snake_cursor >= g_state.work_unit_count) {
        g_state.snake_cursor = 0U;
        g_state.snake_clearing_phase = !g_state.snake_clearing_phase;
    }
}

void db_renderer_opengl_gl3_3_init(const char *vert_shader_path,
                                   const char *frag_shader_path) {
    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    glGenVertexArrays(1, &g_state.vao);
    glGenBuffers(1, &g_state.vbo);
    glBindVertexArray(g_state.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)g_state.draw_vertex_count *
                              DB_BAND_VERT_FLOATS * sizeof(float)),
                 g_state.vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(ATTR_POSITION_LOC);
    glVertexAttribPointer(
        ATTR_POSITION_LOC, ATTR_POSITION_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_BAND_VERT_FLOATS * sizeof(float)), (const void *)0);
    glEnableVertexAttribArray(ATTR_COLOR_LOC);
    glVertexAttribPointer(
        ATTR_COLOR_LOC, ATTR_COLOR_COMPONENTS, GL_FLOAT, GL_FALSE,
        (GLsizei)(DB_BAND_VERT_FLOATS * sizeof(float)),
        BUFFER_OFFSET_BYTES(DB_BAND_POS_FLOATS * sizeof(float)));

    g_state.program =
        build_program_from_files(vert_shader_path, frag_shader_path);
    glUseProgram(g_state.program);
    g_state.u_render_mode = glGetUniformLocation(g_state.program, "u_render_mode");
    g_state.u_snake_clearing_phase =
        glGetUniformLocation(g_state.program, "u_snake_clearing_phase");
    g_state.u_snake_phase_completed =
        glGetUniformLocation(g_state.program, "u_snake_phase_completed");
    g_state.u_snake_cursor = glGetUniformLocation(g_state.program, "u_snake_cursor");
    g_state.u_snake_batch_size =
        glGetUniformLocation(g_state.program, "u_snake_batch_size");
    g_state.u_snake_cols = glGetUniformLocation(g_state.program, "u_snake_cols");
    g_state.u_snake_base_color =
        glGetUniformLocation(g_state.program, "u_snake_base_color");
    g_state.u_snake_target_color =
        glGetUniformLocation(g_state.program, "u_snake_target_color");
    g_state.uniform_snake_clearing_phase_cache = -1;
    g_state.uniform_snake_phase_completed_cache = -1;
    g_state.uniform_snake_cursor_cache = UINT32_MAX;
    g_state.uniform_snake_batch_size_cache = UINT32_MAX;

    if (g_state.u_render_mode >= 0) {
        const int render_mode =
            (g_state.pattern == DB_PATTERN_SNAKE_GRID) ? RENDER_MODE_SNAKE_GRID
                                                       : RENDER_MODE_BANDS;
        glUniform1i(g_state.u_render_mode, render_mode);
    }
    glUniform3f(g_state.u_snake_base_color, BENCH_GRID_PHASE0_R,
                BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    glUniform3f(g_state.u_snake_target_color, BENCH_GRID_PHASE1_R,
                BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B);
    glUniform1f(g_state.u_snake_cols, (float)db_snake_grid_cols_effective());
}

void db_renderer_opengl_gl3_3_render_frame(double time_s) {
    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb(g_state.vertices, g_state.work_unit_count,
                                      time_s);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)((size_t)g_state.draw_vertex_count *
                                     DB_BAND_VERT_FLOATS * sizeof(float)),
                        g_state.vertices);
    } else {
        const uint32_t active_cursor = g_state.snake_cursor;
        db_set_uniform1i_if_changed(g_state.u_snake_clearing_phase,
                                    &g_state.uniform_snake_clearing_phase_cache,
                                    g_state.snake_clearing_phase);
        db_advance_snake_grid_state();
        db_set_uniform1i_if_changed(g_state.u_snake_phase_completed,
                                    &g_state.uniform_snake_phase_completed_cache,
                                    g_state.snake_phase_completed);
        db_set_uniform1f_u32_if_changed(g_state.u_snake_cursor,
                                        &g_state.uniform_snake_cursor_cache,
                                        active_cursor);
        db_set_uniform1f_u32_if_changed(g_state.u_snake_batch_size,
                                        &g_state.uniform_snake_batch_size_cache,
                                        g_state.snake_batch_size);
    }

    glDrawArrays(GL_TRIANGLES, 0, g_state.draw_vertex_count);
}

void db_renderer_opengl_gl3_3_shutdown(void) {
    glDeleteProgram(g_state.program);
    glDeleteBuffers(1, &g_state.vbo);
    glDeleteVertexArrays(1, &g_state.vao);
    free(g_state.vertices);
    g_state = (ShaderRendererState){0};
}

const char *db_renderer_opengl_gl3_3_capability_mode(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : CAPABILITY_MODE;
}

uint32_t db_renderer_opengl_gl3_3_work_unit_count(void) {
    return (g_state.work_unit_count != 0U) ? g_state.work_unit_count
                                           : BENCH_BANDS;
}
