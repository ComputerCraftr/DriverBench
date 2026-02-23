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
#define ES_STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define DB_CAP_MODE_OPENGL_VBO_MAP_RANGE "opengl_vbo_map_range"
#define DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER "opengl_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_VBO "opengl_vbo"
#define DB_CAP_MODE_OPENGL_CLIENT_ARRAY "opengl_client_array"
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
    uint32_t work_unit_count;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t rect_snake_rect_index;
    uint32_t pattern_seed;
    int rect_snake_reset_pending;
    int snake_clearing_phase;
    uint32_t gradient_head_row;
    uint32_t gradient_cycle;
    int gradient_sweep_direction_down;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
    size_t vertex_stride;
} renderer_state_t;

static renderer_state_t g_state = {0};

static void db_fill_grid_all_rgb(float color_r, float color_g, float color_b,
                                 size_t stride_floats);

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
    g_state.draw_vertex_count = (GLsizei)db_checked_u32_to_i32(
        BACKEND_NAME, "draw_vertex_count", init_state.draw_vertex_count);
    g_state.snake_cursor = init_state.snake_cursor;
    g_state.snake_prev_start = init_state.snake_prev_start;
    g_state.snake_prev_count = init_state.snake_prev_count;
    g_state.pattern_seed = init_state.pattern_seed;
    g_state.snake_clearing_phase = init_state.snake_clearing_phase;
    g_state.gradient_head_row = init_state.gradient_head_row;
    g_state.gradient_sweep_direction_down =
        init_state.gradient_sweep_direction_down;
    g_state.gradient_cycle = init_state.gradient_sweep_cycle;
    if ((g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (g_state.pattern == DB_PATTERN_GRADIENT_FILL)) {
        float source_r = 0.0F;
        float source_g = 0.0F;
        float source_b = 0.0F;
        db_palette_cycle_color_rgb(g_state.gradient_cycle, &source_r, &source_g,
                                   &source_b);
        db_fill_grid_all_rgb(source_r, source_g, source_b,
                             DB_VERTEX_FLOAT_STRIDE);
    }
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
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, target_r, target_g,
                             target_b);
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
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
    }

    g_state.snake_prev_start = plan->next_prev_start;
    g_state.snake_prev_count = plan->next_prev_count;
    g_state.snake_cursor = plan->next_cursor;
    g_state.snake_clearing_phase = plan->next_clearing_phase;
}

static void db_fill_grid_all_rgb(float color_r, float color_g, float color_b,
                                 size_t stride_floats) {
    for (uint32_t tile_index = 0U; tile_index < g_state.work_unit_count;
         tile_index++) {
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, stride_floats,
                             DB_VERTEX_POSITION_FLOAT_COUNT, color_r, color_g,
                             color_b);
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
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
    }
}

static void db_apply_gradient_dirty_rows_sweep(uint32_t row_start,
                                               uint32_t row_count,
                                               uint32_t head_row,
                                               int direction_down,
                                               uint32_t cycle_index) {
    const uint32_t rows = db_grid_rows_effective();
    if ((rows == 0U) || (row_count == 0U)) {
        return;
    }
    for (uint32_t i = 0U; i < row_count; i++) {
        const uint32_t row = row_start + i;
        if (row >= rows) {
            break;
        }
        float row_r = 0.0F;
        float row_g = 0.0F;
        float row_b = 0.0F;
        db_gradient_sweep_row_color_rgb(row, head_row, direction_down,
                                        cycle_index, &row_r, &row_g, &row_b);
        db_blend_grid_row_to_color(row, row_r, row_g, row_b, 1.0F);
    }
}

static void db_apply_gradient_dirty_rows_fill(uint32_t row_start,
                                              uint32_t row_count,
                                              uint32_t head_row,
                                              uint32_t cycle_index) {
    db_apply_gradient_dirty_rows_sweep(row_start, row_count, head_row, 1,
                                       cycle_index);
}

static int db_render_rect_snake_step(const db_rect_snake_plan_t *plan) {
    if (plan == NULL) {
        return 0;
    }

    int full_repaint = 0;
    if (g_state.rect_snake_reset_pending != 0) {
        db_fill_grid_all_rgb(BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                             BENCH_GRID_PHASE0_B, g_state.vertex_stride);
        g_state.rect_snake_reset_pending = 0;
        full_repaint = 1;
    }

    const db_rect_snake_rect_t rect = db_rect_snake_rect_from_index(
        g_state.pattern_seed, plan->active_rect_index);
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
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, rect.color_r,
                             rect.color_g, rect.color_b);
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
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, rect.color_r, rect.color_g,
                     rect.color_b, blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
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
            g_state.pattern_seed, rect_plan->active_rect_index);
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

    if (!db_init_vertices_for_mode()) {
        failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.use_map_range_upload = 0;
    g_state.use_map_buffer_upload = 0;
    g_state.is_es_context =
        db_gl_is_es_context((const char *)glGetString(GL_VERSION));
    g_state.vbo = 0U;

    if (g_state.is_es_context != 0) {
        g_state.vertex_stride = DB_ES_VERTEX_FLOAT_STRIDE;
        const size_t vertex_count = (size_t)g_state.draw_vertex_count;
        const size_t es_float_count = vertex_count * DB_ES_VERTEX_FLOAT_STRIDE;
        float *es_vertices = (float *)calloc(es_float_count, sizeof(float));
        if (es_vertices == NULL) {
            failf("failed to allocate GLES vertex array");
        }
        for (uint32_t v = 0U; v < (uint32_t)g_state.draw_vertex_count; v++) {
            const size_t src = (size_t)v * DB_VERTEX_FLOAT_STRIDE;
            const size_t dst = (size_t)v * DB_ES_VERTEX_FLOAT_STRIDE;
            es_vertices[dst] = g_state.vertices[src];
            es_vertices[dst + 1U] = g_state.vertices[src + 1U];
            es_vertices[dst + 2U] =
                g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT];
            es_vertices[dst + 3U] =
                g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
            es_vertices[dst + 4U] =
                g_state.vertices[src + DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
            es_vertices[dst + 5U] = 1.0F;
        }
        free(g_state.vertices);
        g_state.vertices = es_vertices;
    } else {
        g_state.vertex_stride = DB_VERTEX_FLOAT_STRIDE;
    }

    if (g_state.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb_stride(
            g_state.vertices, g_state.work_unit_count, 0.0,
            g_state.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    if (g_state.is_es_context != 0) {
        glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT,
                        ES_STRIDE_BYTES, &g_state.vertices[0]);
        glColorPointer(DB_ES_VERTEX_COLOR_FLOAT_COUNT, GL_FLOAT,
                       ES_STRIDE_BYTES,
                       &g_state.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
    } else {
        glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT, STRIDE_BYTES,
                        &g_state.vertices[0]);
        glColorPointer(DB_VERTEX_COLOR_FLOAT_COUNT, GL_FLOAT, STRIDE_BYTES,
                       &g_state.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
    }

    if (db_gl_has_vbo_support() != 0) {
        const size_t probe_bytes = (size_t)g_state.draw_vertex_count *
                                   g_state.vertex_stride * sizeof(float);
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
            db_gl_probe_upload_capabilities(probe_bytes, g_state.vertices, 0,
                                            &probe_result);
            g_state.use_map_range_upload =
                (probe_result.use_map_range_upload != 0);
            g_state.use_map_buffer_upload =
                (probe_result.use_map_buffer_upload != 0);
            if (g_state.is_es_context == 0) {
                glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT,
                                STRIDE_BYTES, vbo_offset_ptr(0));
                glColorPointer(DB_VERTEX_COLOR_FLOAT_COUNT, GL_FLOAT,
                               STRIDE_BYTES,
                               vbo_offset_ptr(sizeof(float) *
                                              DB_VERTEX_POSITION_FLOAT_COUNT));
            } else {
                glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT,
                                ES_STRIDE_BYTES, vbo_offset_ptr(0));
                glColorPointer(DB_ES_VERTEX_COLOR_FLOAT_COUNT, GL_FLOAT,
                               ES_STRIDE_BYTES,
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

    if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
        snake_plan = db_snake_grid_plan_next_step(
            g_state.snake_cursor, g_state.snake_prev_start,
            g_state.snake_prev_count, g_state.snake_clearing_phase,
            g_state.work_unit_count);
        db_render_snake_grid_step(&snake_plan);
    } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
        rect_prev_start = g_state.snake_prev_start;
        rect_prev_count = g_state.snake_prev_count;
        rect_snake_plan = db_rect_snake_plan_next_step(
            g_state.pattern_seed, g_state.rect_snake_rect_index,
            g_state.snake_cursor);
        rect_full_repaint = db_render_rect_snake_step(&rect_snake_plan);
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
        const db_gradient_sweep_damage_plan_t plan =
            db_gradient_sweep_plan_next_frame(
                g_state.gradient_head_row,
                g_state.gradient_sweep_direction_down, g_state.gradient_cycle);
        gradient_row_start = plan.dirty_row_start;
        gradient_row_count = plan.dirty_row_count;
        db_apply_gradient_dirty_rows_sweep(
            gradient_row_start, gradient_row_count, plan.render_head_row,
            plan.render_direction_down, plan.render_cycle_index);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.gradient_sweep_direction_down = plan.next_direction_down;
        g_state.gradient_cycle = plan.next_cycle_index;
    } else if (g_state.pattern == DB_PATTERN_GRADIENT_FILL) {
        const db_gradient_sweep_damage_plan_t plan =
            db_gradient_fill_plan_next_frame(g_state.gradient_head_row,
                                             g_state.gradient_cycle);
        gradient_row_start = plan.dirty_row_start;
        gradient_row_count = plan.dirty_row_count;
        db_apply_gradient_dirty_rows_fill(
            gradient_row_start, gradient_row_count, plan.render_head_row,
            plan.render_cycle_index);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.gradient_cycle = plan.next_cycle_index;
    } else {
        db_update_band_vertices_rgb_stride(
            g_state.vertices, g_state.work_unit_count, time_s,
            g_state.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
    }

    if (g_state.vbo != 0U) {
        const size_t upload_bytes = (size_t)g_state.draw_vertex_count *
                                    g_state.vertex_stride * sizeof(float);
        const size_t upload_tile_bytes =
            db_rect_tile_bytes(g_state.vertex_stride);

        db_upload_vbo_source(g_state.vertices, upload_bytes, upload_tile_bytes,
                             &snake_plan, &rect_snake_plan, rect_prev_start,
                             rect_prev_count, rect_full_repaint,
                             gradient_row_start, gradient_row_count);
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
