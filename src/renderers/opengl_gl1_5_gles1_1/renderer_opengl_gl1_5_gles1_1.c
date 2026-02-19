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
#include <OpenGL/gltypes.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define POS_OFFSET_FLOATS 0U
#define DB_CAP_MODE_OPENGL_VBO_MAP_RANGE "opengl_vbo_map_range"
#define DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER "opengl_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_VBO "opengl_vbo"
#define DB_CAP_MODE_OPENGL_CLIENT_ARRAY "opengl_client_array"
#define DB_ES_COLOR_CHANNELS 4U
#define DB_MAX_SNAKE_UPLOAD_RANGES                                             \
    ((size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U)
#define DB_MAX_GRADIENT_UPLOAD_RANGES 64U
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    int use_map_range_upload;
    int use_map_buffer_upload;
    int is_es_context;
    GLuint vbo;
    float *vertices;
    float *colors_rgba;
    uint32_t work_unit_count;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t rect_snake_rect_index;
    uint32_t rect_snake_seed;
    int rect_snake_reset_pending;
    int snake_clearing_phase;
    uint32_t gradient_head_row;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
} renderer_state_t;

static renderer_state_t g_state = {0};

// NOLINTBEGIN(performance-no-int-to-ptr)
static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static int db_init_es_color_array(void) {
    const size_t color_count =
        (size_t)g_state.draw_vertex_count * DB_ES_COLOR_CHANNELS;
    g_state.colors_rgba = (float *)calloc(color_count, sizeof(float));
    return g_state.colors_rgba != NULL;
}

static void db_update_es_color_array_full(void) {
    if ((g_state.colors_rgba == NULL) || (g_state.vertices == NULL)) {
        return;
    }
    for (uint32_t vertex_index = 0U;
         vertex_index < (uint32_t)g_state.draw_vertex_count; vertex_index++) {
        const size_t src = (size_t)vertex_index * DB_VERTEX_FLOAT_STRIDE;
        const size_t dst = (size_t)vertex_index * DB_ES_COLOR_CHANNELS;
        g_state.colors_rgba[dst] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT];
        g_state.colors_rgba[dst + 1U] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        g_state.colors_rgba[dst + 2U] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        g_state.colors_rgba[dst + 3U] = 1.0F;
    }
}

static void db_update_es_color_array_tile(uint32_t tile_index) {
    if ((g_state.colors_rgba == NULL) || (g_state.vertices == NULL)) {
        return;
    }

    const size_t first_vertex = (size_t)tile_index * DB_RECT_VERTEX_COUNT;
    for (uint32_t vertex = 0U; vertex < DB_RECT_VERTEX_COUNT; vertex++) {
        const size_t vertex_index = first_vertex + vertex;
        const size_t src = vertex_index * DB_VERTEX_FLOAT_STRIDE;
        const size_t dst = vertex_index * DB_ES_COLOR_CHANNELS;
        g_state.colors_rgba[dst] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT];
        g_state.colors_rgba[dst + 1U] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        g_state.colors_rgba[dst + 2U] =
            g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        g_state.colors_rgba[dst + 3U] = 1.0F;
    }
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
    g_state.snake_cursor = init_state.snake_cursor;
    g_state.snake_prev_start = init_state.snake_prev_start;
    g_state.snake_prev_count = init_state.snake_prev_count;
    g_state.rect_snake_seed = init_state.rect_snake_seed;
    g_state.snake_clearing_phase = init_state.snake_clearing_phase;
    g_state.gradient_head_row = init_state.gradient_head_row;
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
        const uint32_t tile_index = db_grid_tile_index_from_step(tile_step);
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, target_r, target_g,
                             target_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }

    for (uint32_t update_index = 0; update_index < plan->batch_size;
         update_index++) {
        const uint32_t tile_step = plan->active_cursor + update_index;
        const uint32_t tile_index = db_grid_tile_index_from_step(tile_step);

        float target_r = 0.0F;
        float target_g = 0.0F;
        float target_b = 0.0F;
        db_snake_grid_target_color_rgb(plan->clearing_phase, &target_r,
                                       &target_g, &target_b);
        const float blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);

        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }

    g_state.snake_prev_start = plan->next_prev_start;
    g_state.snake_prev_count = plan->next_prev_count;
    g_state.snake_cursor = plan->next_cursor;
    g_state.snake_clearing_phase = plan->next_clearing_phase;
}

static void db_fill_grid_all_rgb(float color_r, float color_g, float color_b) {
    for (uint32_t tile_index = 0U; tile_index < g_state.work_unit_count;
         tile_index++) {
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, color_r, color_g,
                             color_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }
}

static void db_blend_grid_row_to_color(uint32_t row, float target_r,
                                       float target_g, float target_b,
                                       float blend_factor) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t cols = db_grid_cols_effective();
    if ((rows == 0U) || (cols == 0U) || (blend_factor <= 0.0F)) {
        return;
    }
    if (blend_factor > 1.0F) {
        blend_factor = 1.0F;
    }

    const uint32_t row_wrapped = row % rows;
    for (uint32_t col = 0U; col < cols; col++) {
        const uint32_t tile_index = (row_wrapped * cols) + col;
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }
}

static int db_render_rect_snake_step(const db_rect_snake_plan_t *plan) {
    if (plan == NULL) {
        return 0;
    }

    int full_repaint = 0;
    if (g_state.rect_snake_reset_pending != 0) {
        db_fill_grid_all_rgb(BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                             BENCH_GRID_PHASE0_B);
        g_state.rect_snake_reset_pending = 0;
        full_repaint = 1;
    }

    const db_rect_snake_rect_t rect = db_rect_snake_rect_from_index(
        g_state.rect_snake_seed, plan->active_rect_index);
    if ((rect.width == 0U) || (rect.height == 0U)) {
        return full_repaint;
    }

    for (uint32_t update_index = 0U; update_index < g_state.snake_prev_count;
         update_index++) {
        const uint32_t step = g_state.snake_prev_start + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(&rect, step);
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, rect.color_r,
                             rect.color_g, rect.color_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(&rect, step);
        const float blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, rect.color_r, rect.color_g,
                     rect.color_b, blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_tile(tile_index);
        }
    }

    g_state.snake_prev_start = plan->active_cursor;
    g_state.snake_prev_count =
        (plan->rect_completed != 0) ? 0U : plan->batch_size;
    g_state.snake_cursor = plan->next_cursor;
    g_state.rect_snake_rect_index = plan->next_rect_index;
    if (plan->wrapped != 0) {
        g_state.rect_snake_reset_pending = 1;
        g_state.snake_prev_count = 0U;
    }
    return full_repaint;
}

static size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

static void
db_append_gradient_row_ranges(db_gl_upload_range_t *ranges, size_t max_ranges,
                              size_t *inout_range_count, uint32_t row_start,
                              uint32_t row_count, size_t row_bytes) {
    const uint32_t rows = db_grid_rows_effective();
    if ((ranges == NULL) || (inout_range_count == NULL) || (rows == 0U) ||
        (row_count == 0U) || (row_bytes == 0U)) {
        return;
    }

    for (uint32_t i = 0U; i < row_count; i++) {
        if (*inout_range_count >= max_ranges) {
            failf("gradient upload range overflow (max=%zu)", max_ranges);
        }
        const uint32_t row = row_start + i;
        if (row >= rows) {
            break;
        }
        const size_t row_offset = (size_t)row * row_bytes;
        ranges[*inout_range_count] = (db_gl_upload_range_t){
            .dst_offset_bytes = row_offset,
            .src_offset_bytes = row_offset,
            .size_bytes = row_bytes,
        };
        (*inout_range_count)++;
    }
}

static void db_append_snake_step_ranges(
    db_gl_upload_range_t *ranges, size_t max_ranges, size_t *inout_range_count,
    uint32_t step_start, uint32_t step_count, size_t dst_tile_bytes,
    size_t src_tile_bytes, uint32_t rect_x, uint32_t rect_y,
    uint32_t rect_width, uint32_t rect_height) {
    if ((ranges == NULL) || (inout_range_count == NULL) || (step_count == 0U) ||
        (dst_tile_bytes == 0U) || (src_tile_bytes == 0U)) {
        return;
    }

    const uint32_t cols = db_grid_cols_effective();
    db_snake_col_span_t spans[DB_MAX_SNAKE_UPLOAD_RANGES];
    size_t span_count = 0U;
    db_snake_append_step_spans_for_rect(spans, DB_MAX_SNAKE_UPLOAD_RANGES,
                                        &span_count, rect_x, rect_y, rect_width,
                                        rect_height, step_start, step_count);
    for (size_t i = 0U; i < span_count; i++) {
        const uint32_t row = spans[i].row;
        const uint32_t col_start = spans[i].col_start;
        const uint32_t col_end = spans[i].col_end;
        const uint32_t chunk_steps = col_end - col_start;
        const uint32_t first_tile = (row * cols) + col_start;
        const size_t dst_byte_offset = (size_t)first_tile * dst_tile_bytes;
        const size_t src_byte_offset = (size_t)first_tile * src_tile_bytes;
        const size_t byte_count = (size_t)chunk_steps * dst_tile_bytes;
        if (*inout_range_count >= max_ranges) {
            failf("snake upload range overflow (max=%zu)", max_ranges);
        }
        ranges[*inout_range_count] = (db_gl_upload_range_t){
            .dst_offset_bytes = dst_byte_offset,
            .src_offset_bytes = src_byte_offset,
            .size_bytes = byte_count,
        };
        (*inout_range_count)++;
    }
}

static void
db_upload_vbo_source(const float *source, size_t total_bytes, size_t tile_bytes,
                     const db_snake_damage_plan_t *snake_plan,
                     const db_rect_snake_plan_t *rect_plan,
                     uint32_t rect_prev_start, uint32_t rect_prev_count,
                     int rect_full_repaint, uint32_t gradient_row_start,
                     uint32_t gradient_row_count) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
        db_gl_upload_range_t ranges[DB_MAX_SNAKE_UPLOAD_RANGES];
        size_t range_count = 0U;
        db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                    &range_count, snake_plan->prev_start,
                                    snake_plan->prev_count, tile_bytes,
                                    tile_bytes, 0U, 0U, cols, rows);
        db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                    &range_count, snake_plan->active_cursor,
                                    snake_plan->batch_size, tile_bytes,
                                    tile_bytes, 0U, 0U, cols, rows);
        db_gl_upload_ranges(source, total_bytes, 0, NULL,
                            g_state.use_map_range_upload,
                            g_state.use_map_buffer_upload, ranges, range_count);
    } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
        if ((rect_full_repaint != 0) || (rect_plan == NULL)) {
            db_gl_upload_buffer(source, total_bytes, 0, NULL,
                                g_state.use_map_range_upload,
                                g_state.use_map_buffer_upload);
            return;
        }

        const db_rect_snake_rect_t rect = db_rect_snake_rect_from_index(
            g_state.rect_snake_seed, rect_plan->active_rect_index);
        if ((rect.width == 0U) || (rect.height == 0U)) {
            return;
        }

        db_gl_upload_range_t ranges[DB_MAX_SNAKE_UPLOAD_RANGES];
        size_t range_count = 0U;
        db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                    &range_count, rect_prev_start,
                                    rect_prev_count, tile_bytes, tile_bytes,
                                    rect.x, rect.y, rect.width, rect.height);
        db_append_snake_step_ranges(
            ranges, DB_MAX_SNAKE_UPLOAD_RANGES, &range_count,
            rect_plan->active_cursor, rect_plan->batch_size, tile_bytes,
            tile_bytes, rect.x, rect.y, rect.width, rect.height);
        if (range_count == 0U) {
            return;
        }
        db_gl_upload_ranges(source, total_bytes, 0, NULL,
                            g_state.use_map_range_upload,
                            g_state.use_map_buffer_upload, ranges, range_count);
    } else if ((g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.pattern == DB_PATTERN_GRADIENT_FILL)) {
        if (gradient_row_count > DB_MAX_GRADIENT_UPLOAD_RANGES) {
            db_gl_upload_buffer(source, total_bytes, 0, NULL,
                                g_state.use_map_range_upload,
                                g_state.use_map_buffer_upload);
            return;
        }
        db_gl_upload_range_t ranges[DB_MAX_GRADIENT_UPLOAD_RANGES];
        size_t range_count = 0U;
        const size_t row_bytes = (size_t)cols * tile_bytes;
        db_append_gradient_row_ranges(ranges, DB_MAX_GRADIENT_UPLOAD_RANGES,
                                      &range_count, gradient_row_start,
                                      gradient_row_count, row_bytes);
        db_gl_upload_ranges(source, total_bytes, 0, NULL,
                            g_state.use_map_range_upload,
                            g_state.use_map_buffer_upload, ranges, range_count);
    } else {
        db_gl_upload_buffer(source, total_bytes, 0, NULL,
                            g_state.use_map_range_upload,
                            g_state.use_map_buffer_upload);
    }
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const uint32_t rows = db_grid_rows_effective();

    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_upload_probe_result_t probe_result = {0};
    const size_t vbo_bytes = (size_t)g_state.draw_vertex_count *
                             DB_VERTEX_FLOAT_STRIDE * sizeof(float);
    const size_t es_color_bytes = (size_t)g_state.draw_vertex_count *
                                  sizeof(float) * DB_ES_COLOR_CHANNELS;

    g_state.use_map_range_upload = 0;
    g_state.use_map_buffer_upload = 0;
    g_state.is_es_context =
        db_gl_is_es_context((const char *)glGetString(GL_VERSION));
    g_state.vbo = 0U;
    g_state.colors_rgba = NULL;

    if (g_state.is_es_context != 0) {
        if (!db_init_es_color_array()) {
            failf("failed to allocate GLES color array");
        }
    }
    if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
        db_gradient_sweep_set_rows_color(g_state.vertices,
                                         g_state.gradient_head_row, 0U, rows);
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_FILL) {
        db_gradient_fill_set_rows_color(g_state.vertices,
                                        g_state.gradient_head_row,
                                        g_state.snake_clearing_phase, 0U, rows);
    }
    if (g_state.is_es_context != 0) {
        db_update_es_color_array_full();
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    if (db_gl_has_vbo_support() != 0) {
        const float *probe_source = (g_state.is_es_context != 0)
                                        ? g_state.colors_rgba
                                        : g_state.vertices;
        const size_t probe_bytes =
            (g_state.is_es_context != 0) ? es_color_bytes : vbo_bytes;
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            db_gl_probe_upload_capabilities(probe_bytes, probe_source, 0,
                                            &probe_result);
            g_state.use_map_range_upload =
                (probe_result.use_map_range_upload != 0);
            g_state.use_map_buffer_upload =
                (probe_result.use_map_buffer_upload != 0);
            if (g_state.is_es_context == 0) {
                glVertexPointer(
                    2, GL_FLOAT, STRIDE_BYTES,
                    vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
                glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                               vbo_offset_ptr(sizeof(float) *
                                              DB_VERTEX_POSITION_FLOAT_COUNT));
            }
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
    db_rect_snake_plan_t rect_snake_plan = {0};
    uint32_t rect_prev_start = 0U;
    uint32_t rect_prev_count = 0U;
    int rect_full_repaint = 0;
    uint32_t gradient_row_start = 0U;
    uint32_t gradient_row_count = 0U;
    const uint32_t rows = db_grid_rows_effective();

    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb(g_state.vertices, g_state.work_unit_count,
                                      time_s);
        if (g_state.is_es_context != 0) {
            db_update_es_color_array_full();
        }
    } else if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
        snake_plan = db_snake_grid_plan_next_step(
            g_state.snake_cursor, g_state.snake_prev_start,
            g_state.snake_prev_count, g_state.snake_clearing_phase,
            g_state.work_unit_count);
        db_render_snake_grid_step(&snake_plan);
    } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
        rect_prev_start = g_state.snake_prev_start;
        rect_prev_count = g_state.snake_prev_count;
        rect_snake_plan = db_rect_snake_plan_next_step(
            g_state.rect_snake_seed, g_state.rect_snake_rect_index,
            g_state.snake_cursor);
        rect_full_repaint = db_render_rect_snake_step(&rect_snake_plan);
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
        const db_gradient_sweep_damage_plan_t plan =
            db_gradient_sweep_plan_next_frame(g_state.gradient_head_row);
        gradient_row_start = plan.dirty_row_start;
        gradient_row_count = plan.dirty_row_count;
        if ((rows > 0U) && (gradient_row_count > 0U)) {
            for (uint32_t i = 0U; i < gradient_row_count; i++) {
                const uint32_t row = gradient_row_start + i;
                if (row >= rows) {
                    break;
                }
                float row_r = 0.0F;
                float row_g = 0.0F;
                float row_b = 0.0F;
                db_gradient_sweep_row_color_rgb(row, plan.render_head_row,
                                                &row_r, &row_g, &row_b);
                db_blend_grid_row_to_color(row, row_r, row_g, row_b, 1.0F);
            }
        }
        g_state.gradient_head_row = plan.next_head_row;
    } else {
        const db_gradient_fill_damage_plan_t plan =
            db_gradient_fill_plan_next_frame(g_state.gradient_head_row,
                                             g_state.snake_clearing_phase);
        gradient_row_start = plan.dirty_row_start;
        gradient_row_count = plan.dirty_row_count;
        if ((rows > 0U) && (gradient_row_count > 0U)) {
            for (uint32_t i = 0U; i < gradient_row_count; i++) {
                const uint32_t row = gradient_row_start + i;
                if (row >= rows) {
                    break;
                }
                float row_r = 0.0F;
                float row_g = 0.0F;
                float row_b = 0.0F;
                db_gradient_fill_row_color_rgb(row, plan.render_head_row,
                                               plan.render_clearing_phase,
                                               &row_r, &row_g, &row_b);
                db_blend_grid_row_to_color(row, row_r, row_g, row_b, 1.0F);
            }
        }
        g_state.gradient_head_row = plan.next_head_row;
        g_state.snake_clearing_phase = plan.next_clearing_phase;
    }

    if (g_state.vbo != 0U) {
        const float *upload_source = (g_state.is_es_context != 0)
                                         ? g_state.colors_rgba
                                         : g_state.vertices;
        const size_t upload_bytes =
            (g_state.is_es_context != 0)
                ? ((size_t)g_state.draw_vertex_count * sizeof(float) *
                   DB_ES_COLOR_CHANNELS)
                : ((size_t)g_state.draw_vertex_count * DB_VERTEX_FLOAT_STRIDE *
                   sizeof(float));
        const size_t upload_tile_bytes =
            (g_state.is_es_context != 0)
                ? db_rect_tile_bytes(DB_ES_COLOR_CHANNELS)
                : db_rect_tile_bytes(DB_VERTEX_FLOAT_STRIDE);

        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        db_upload_vbo_source(upload_source, upload_bytes, upload_tile_bytes,
                             &snake_plan, &rect_snake_plan, rect_prev_start,
                             rect_prev_count, rect_full_repaint,
                             gradient_row_start, gradient_row_count);

        if (g_state.is_es_context != 0) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glVertexPointer(2, GL_FLOAT, STRIDE_BYTES, &g_state.vertices[0]);
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            glColorPointer((GLint)DB_ES_COLOR_CHANNELS, GL_FLOAT,
                           (GLsizei)(sizeof(float) * DB_ES_COLOR_CHANNELS),
                           vbo_offset_ptr(0U));
        } else {
            glVertexPointer(2, GL_FLOAT, STRIDE_BYTES,
                            vbo_offset_ptr(sizeof(float) * POS_OFFSET_FLOATS));
            glColorPointer(
                3, GL_FLOAT, STRIDE_BYTES,
                vbo_offset_ptr(sizeof(float) * DB_VERTEX_POSITION_FLOAT_COUNT));
        }
    } else {
        glVertexPointer(2, GL_FLOAT, STRIDE_BYTES, &g_state.vertices[0]);
        if (g_state.is_es_context != 0) {
            glColorPointer((GLint)DB_ES_COLOR_CHANNELS, GL_FLOAT,
                           (GLsizei)(sizeof(float) * DB_ES_COLOR_CHANNELS),
                           g_state.colors_rgba);
        } else {
            glColorPointer(3, GL_FLOAT, STRIDE_BYTES,
                           &g_state.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
        }
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
    free(g_state.colors_rgba);
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
