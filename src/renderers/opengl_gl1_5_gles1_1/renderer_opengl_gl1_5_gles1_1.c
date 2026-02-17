#include "renderer_opengl_gl1_5_gles1_1.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_BAND_VERT_FLOATS))
#define POS_OFFSET_FLOATS 0U
#define MODE_VBO "vbo"
#define MODE_CLIENT_ARRAY "client_array"
#define BASE10 10

typedef struct {
    int use_vbo;
    GLuint vbo;
    float *vertices;
    uint32_t work_unit_count;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    int snake_clearing_phase;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
} renderer_state_t;

static renderer_state_t g_state = {0};

static int has_gl15_vbo_support(void) {
    const char *version_text = (const char *)glGetString(GL_VERSION);
    if (version_text == NULL) {
        return 0;
    }

    char *parse_end = NULL;
    long major_l = strtol(version_text, &parse_end, BASE10);
    if ((parse_end == version_text) || (*parse_end != '.')) {
        return 0;
    }
    const char *minor_start = parse_end + 1;
    long minor_l = strtol(minor_start, &parse_end, BASE10);
    if (parse_end == minor_start) {
        return 0;
    }

    int major = (int)major_l;
    int minor = (int)minor_l;
    return (major > 1) || ((major == 1) && (minor >= 5));
}

static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)((const char *)NULL + byte_offset);
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
    g_state.snake_prev_start = 0U;
    g_state.snake_prev_count = 0U;
    g_state.snake_clearing_phase = 0;
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

static void db_render_snake_grid_step(void) {
    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(g_state.work_unit_count);
    const uint32_t batch_size = db_snake_grid_step_batch_size(
        g_state.snake_cursor, g_state.work_unit_count, tiles_per_step);
    float target_r = 0.0F;
    float target_g = 0.0F;
    float target_b = 0.0F;
    db_snake_grid_target_color_rgb(g_state.snake_clearing_phase, &target_r,
                                   &target_g, &target_b);

    for (uint32_t update_index = 0; update_index < g_state.snake_prev_count;
         update_index++) {
        const uint32_t tile_step = g_state.snake_prev_start + update_index;
        const uint32_t tile_index =
            db_snake_grid_tile_index_from_step(tile_step);
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_BAND_TRI_VERTS_PER_BAND *
                                         DB_BAND_VERT_FLOATS;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             target_r, target_g, target_b);
        if (g_state.use_vbo) {
            glBufferSubData(
                GL_ARRAY_BUFFER, (GLintptr)(tile_float_offset * sizeof(float)),
                (GLsizeiptr)((size_t)DB_BAND_TRI_VERTS_PER_BAND *
                             (size_t)DB_BAND_VERT_FLOATS * sizeof(float)),
                unit);
        }
    }

    for (uint32_t update_index = 0; update_index < batch_size; update_index++) {
        const uint32_t tile_step = g_state.snake_cursor + update_index;
        const uint32_t tile_index =
            db_snake_grid_tile_index_from_step(tile_step);

        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_snake_grid_window_color_rgb(update_index, batch_size,
                                       g_state.snake_clearing_phase, &color_r,
                                       &color_g, &color_b);

        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_BAND_TRI_VERTS_PER_BAND *
                                         DB_BAND_VERT_FLOATS;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             color_r, color_g, color_b);

        if (g_state.use_vbo) {
            glBufferSubData(
                GL_ARRAY_BUFFER, (GLintptr)(tile_float_offset * sizeof(float)),
                (GLsizeiptr)((size_t)DB_BAND_TRI_VERTS_PER_BAND *
                             (size_t)DB_BAND_VERT_FLOATS * sizeof(float)),
                unit);
        }
    }

    const int phase_completed =
        (g_state.snake_cursor + batch_size) >= g_state.work_unit_count;
    if (phase_completed) {
        for (uint32_t update_index = 0; update_index < batch_size; update_index++) {
            const uint32_t tile_step = g_state.snake_cursor + update_index;
            const uint32_t tile_index =
                db_snake_grid_tile_index_from_step(tile_step);
            const size_t tile_float_offset = (size_t)tile_index *
                                             DB_BAND_TRI_VERTS_PER_BAND *
                                             DB_BAND_VERT_FLOATS;
            float *unit = &g_state.vertices[tile_float_offset];
            db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                                 target_r, target_g, target_b);
            if (g_state.use_vbo) {
                glBufferSubData(
                    GL_ARRAY_BUFFER, (GLintptr)(tile_float_offset * sizeof(float)),
                    (GLsizeiptr)((size_t)DB_BAND_TRI_VERTS_PER_BAND *
                                 (size_t)DB_BAND_VERT_FLOATS * sizeof(float)),
                    unit);
            }
        }
    }

    g_state.snake_prev_start = g_state.snake_cursor;
    g_state.snake_prev_count = phase_completed ? 0U : batch_size;
    g_state.snake_cursor += batch_size;
    if (g_state.snake_cursor >= g_state.work_unit_count) {
        g_state.snake_cursor = 0U;
        g_state.snake_clearing_phase = !g_state.snake_clearing_phase;
    }
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (!db_init_vertices_for_mode()) {
        db_failf(BACKEND_NAME, "failed to allocate benchmark vertex buffers");
    }

    g_state.use_vbo = has_gl15_vbo_support();
    g_state.vbo = 0U;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    if (g_state.use_vbo) {
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)((size_t)g_state.draw_vertex_count *
                                      DB_BAND_VERT_FLOATS * sizeof(float)),
                         g_state.vertices, GL_STREAM_DRAW);
            glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                            vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
            glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                           vbo_offset_ptr(sizeof(float) * DB_BAND_POS_FLOATS));
            db_infof(BACKEND_NAME, "using capability mode: %s",
                     db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            return;
        }
    }

    g_state.use_vbo = 0;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    db_infof(BACKEND_NAME, "using capability mode: %s",
             db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s) {
    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb(g_state.vertices, g_state.work_unit_count,
                                      time_s);
    } else {
        db_render_snake_grid_step();
    }

    if (g_state.use_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        if (g_state.pattern == DB_PATTERN_BANDS) {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)((size_t)g_state.draw_vertex_count *
                                         DB_BAND_VERT_FLOATS * sizeof(float)),
                            g_state.vertices);
        }
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                        vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
        glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                       vbo_offset_ptr(sizeof(float) * DB_BAND_POS_FLOATS));
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES, &g_state.vertices[0]);
        glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                       &g_state.vertices[DB_BAND_POS_FLOATS]);
    }

    glDrawArrays(GL_TRIANGLES, 0, g_state.draw_vertex_count);
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vbo != 0U) {
        glDeleteBuffers(1, &g_state.vbo);
        g_state.vbo = 0U;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    free(g_state.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    return g_state.use_vbo ? MODE_VBO : MODE_CLIENT_ARRAY;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return (g_state.work_unit_count != 0U) ? g_state.work_unit_count
                                           : BENCH_BANDS;
}
