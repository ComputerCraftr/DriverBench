#include "renderer_opengl_gl1_5_gles1_1.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"

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
#define DB_CAP_MODE_OPENGL_VBO_MAP_RANGE "opengl_vbo_map_range"
#define DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER "opengl_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_VBO "opengl_vbo"
#define DB_CAP_MODE_OPENGL_CLIENT_ARRAY "opengl_client_array"
#define DB_MAX_SNAKE_UPLOAD_RANGES                                             \
    ((size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U)
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    int use_map_range_upload;
    int use_map_buffer_upload;
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

// NOLINTBEGIN(performance-no-int-to-ptr)
static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static int db_init_vertices_for_mode(void) {
    db_pattern_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_mode_common(BACKEND_NAME, &init_state)) {
        return 0;
    }

    g_state.vertices = init_state.vertices;
    g_state.pattern = init_state.pattern;
    g_state.work_unit_count = init_state.work_unit_count;
    g_state.draw_vertex_count = (GLsizei)init_state.draw_vertex_count;
    g_state.snake_cursor = init_state.snake_cursor;
    g_state.snake_prev_start = init_state.snake_prev_start;
    g_state.snake_prev_count = init_state.snake_prev_count;
    g_state.snake_clearing_phase = init_state.snake_clearing_phase;
    return 1;
}

static void db_render_snake_grid_step(const db_snake_damage_plan_t *plan) {
    if (plan == NULL) {
        return;
    }
    float target_r = 0.0F;
    float target_g = 0.0F;
    float target_b = 0.0F;
    db_snake_grid_target_color_rgb(plan->clearing_phase, &target_r, &target_g,
                                   &target_b);

    for (uint32_t update_index = 0; update_index < plan->prev_count;
         update_index++) {
        const uint32_t tile_step = plan->prev_start + update_index;
        const uint32_t tile_index =
            db_snake_grid_tile_index_from_step(tile_step);
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_BAND_TRI_VERTS_PER_BAND *
                                         DB_BAND_VERT_FLOATS;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             target_r, target_g, target_b);
    }

    for (uint32_t update_index = 0; update_index < plan->batch_size;
         update_index++) {
        const uint32_t tile_step = plan->active_cursor + update_index;
        const uint32_t tile_index =
            db_snake_grid_tile_index_from_step(tile_step);

        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_snake_grid_window_color_rgb(update_index, plan->batch_size,
                                       plan->clearing_phase, &color_r, &color_g,
                                       &color_b);

        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_BAND_TRI_VERTS_PER_BAND *
                                         DB_BAND_VERT_FLOATS;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             color_r, color_g, color_b);
    }

    if (plan->phase_completed != 0) {
        for (uint32_t update_index = 0; update_index < plan->batch_size;
             update_index++) {
            const uint32_t tile_step = plan->active_cursor + update_index;
            const uint32_t tile_index =
                db_snake_grid_tile_index_from_step(tile_step);
            const size_t tile_float_offset = (size_t)tile_index *
                                             DB_BAND_TRI_VERTS_PER_BAND *
                                             DB_BAND_VERT_FLOATS;
            float *unit = &g_state.vertices[tile_float_offset];
            db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                                 target_r, target_g, target_b);
        }
    }

    g_state.snake_prev_start = plan->next_prev_start;
    g_state.snake_prev_count = plan->next_prev_count;
    g_state.snake_cursor = plan->next_cursor;
    g_state.snake_clearing_phase = plan->next_clearing_phase;
}

static size_t db_snake_tile_bytes(void) {
    return (size_t)DB_BAND_TRI_VERTS_PER_BAND * DB_BAND_VERT_FLOATS *
           sizeof(float);
}

static void
db_append_snake_step_ranges(db_gl_upload_range_t *ranges, size_t max_ranges,
                            size_t *inout_range_count, uint32_t step_start,
                            uint32_t step_count, size_t tile_bytes) {
    if ((ranges == NULL) || (inout_range_count == NULL) || (step_count == 0U) ||
        (tile_bytes == 0U)) {
        return;
    }

    const uint32_t cols = db_snake_grid_cols_effective();
    uint32_t remaining = step_count;
    uint32_t step_cursor = step_start;

    while (remaining > 0U) {
        const uint32_t row = step_cursor / cols;
        const uint32_t col_step = step_cursor % cols;
        const uint32_t steps_left_in_row = cols - col_step;
        const uint32_t chunk_steps =
            (remaining < steps_left_in_row) ? remaining : steps_left_in_row;

        uint32_t first_col = 0U;
        if ((row & 1U) == 0U) {
            first_col = col_step;
        } else {
            first_col = (cols - 1U) - (col_step + chunk_steps - 1U);
        }

        const uint32_t first_tile = (row * cols) + first_col;
        const size_t float_offset = (size_t)first_tile *
                                    DB_BAND_TRI_VERTS_PER_BAND *
                                    DB_BAND_VERT_FLOATS;
        const size_t byte_offset = (size_t)first_tile * tile_bytes;
        const size_t byte_count = (size_t)chunk_steps * tile_bytes;
        if (*inout_range_count >= max_ranges) {
            failf("snake upload range overflow (max=%zu)", max_ranges);
        }
        ranges[*inout_range_count] = (db_gl_upload_range_t){
            .dst_offset_bytes = byte_offset,
            .src_offset_bytes = float_offset * sizeof(float),
            .size_bytes = byte_count,
        };
        (*inout_range_count)++;

        step_cursor += chunk_steps;
        remaining -= chunk_steps;
    }
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_upload_probe_result_t probe_result = {0};
    const size_t vbo_bytes =
        (size_t)g_state.draw_vertex_count * DB_BAND_VERT_FLOATS * sizeof(float);

    g_state.use_map_range_upload = 0;
    g_state.use_map_buffer_upload = 0;
    g_state.vbo = 0U;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    if (db_gl_has_vbo_support() != 0) {
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            db_gl_probe_upload_capabilities(vbo_bytes, g_state.vertices, 0,
                                            &probe_result);
            g_state.use_map_range_upload =
                (probe_result.use_map_range_upload != 0);
            g_state.use_map_buffer_upload =
                (probe_result.use_map_buffer_upload != 0);
            glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                            vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
            glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                           vbo_offset_ptr(sizeof(float) * DB_BAND_POS_FLOATS));
            infof("using capability mode: %s",
                  db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            return;
        }
    }

    infof("using capability mode: %s",
          db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s) {
    db_snake_damage_plan_t snake_plan = {0};

    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb(g_state.vertices, g_state.work_unit_count,
                                      time_s);
    } else {
        snake_plan = db_snake_grid_plan_next_step(
            g_state.snake_cursor, g_state.snake_prev_start,
            g_state.snake_prev_count, g_state.snake_clearing_phase,
            g_state.work_unit_count);
        db_render_snake_grid_step(&snake_plan);
    }

    if (g_state.vbo != 0U) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        const size_t vbo_bytes = (size_t)g_state.draw_vertex_count *
                                 DB_BAND_VERT_FLOATS * sizeof(float);
        if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
            const size_t tile_bytes = db_snake_tile_bytes();
            db_gl_upload_range_t ranges[DB_MAX_SNAKE_UPLOAD_RANGES];
            size_t range_count = 0U;
            db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                        &range_count, snake_plan.prev_start,
                                        snake_plan.prev_count, tile_bytes);
            db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                        &range_count, snake_plan.active_cursor,
                                        snake_plan.batch_size, tile_bytes);
            db_gl_upload_ranges(g_state.vertices, vbo_bytes, 0, NULL,
                                g_state.use_map_range_upload,
                                g_state.use_map_buffer_upload, ranges,
                                range_count);
        } else {
            db_gl_upload_buffer(g_state.vertices, vbo_bytes, 0, NULL,
                                g_state.use_map_range_upload,
                                g_state.use_map_buffer_upload);
        }
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                        vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
        glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                       vbo_offset_ptr(sizeof(float) * DB_BAND_POS_FLOATS));
    } else {
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
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    free(g_state.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    if (g_state.vbo == 0U) {
        return DB_CAP_MODE_OPENGL_CLIENT_ARRAY;
    }
    if (g_state.use_map_range_upload != 0) {
        return DB_CAP_MODE_OPENGL_VBO_MAP_RANGE;
    }
    if (g_state.use_map_buffer_upload != 0) {
        return DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER;
    }
    return DB_CAP_MODE_OPENGL_VBO;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return (g_state.work_unit_count != 0U) ? g_state.work_unit_count
                                           : BENCH_BANDS;
}
