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
    uint32_t snake_rect_index;
    uint32_t pattern_seed;
    int snake_reset_pending;
    int mode_phase_flag;
    uint32_t gradient_head_row;
    uint32_t gradient_cycle;
    db_pattern_t pattern;
    GLsizei draw_vertex_count;
    size_t vertex_stride;
} renderer_state_t;

static renderer_state_t g_state = {0};

// NOLINTBEGIN(performance-no-int-to-ptr)
static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static int db_init_vertices_for_mode(size_t vertex_stride) {
    db_pattern_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_mode_common_with_stride(BACKEND_NAME, &init_state,
                                                      vertex_stride)) {
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
    g_state.mode_phase_flag = init_state.mode_phase_flag;
    g_state.gradient_head_row = init_state.gradient_head_row;
    g_state.gradient_cycle = init_state.gradient_cycle;
    g_state.vertex_stride = init_state.vertex_stride;
    return 1;
}

static void db_render_snake_step(const db_snake_plan_t *plan,
                                 const db_rect_snake_rect_t *rect,
                                 float target_r, float target_g, float target_b,
                                 int full_fill_on_phase_completed) {
    if ((plan == NULL) || (rect == NULL)) {
        return;
    }
    if (plan->batch_size > BENCH_SNAKE_PHASE_WINDOW_TILES) {
        failf("snake batch size %u exceeds BENCH_SNAKE_PHASE_WINDOW_TILES=%u",
              plan->batch_size, BENCH_SNAKE_PHASE_WINDOW_TILES);
    }
    if ((rect->width == 0U) || (rect->height == 0U)) {
        return;
    }
    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_fill_grid_all_rgb_stride(
            g_state.vertices, g_state.work_unit_count, g_state.vertex_stride,
            DB_VERTEX_POSITION_FLOAT_COUNT, target_r, target_g, target_b);
        return;
    }
    float prior_rgb[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0F};
    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const size_t prior_base = (size_t)update_index * 3U;
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(rect, step);
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        prior_rgb[prior_base] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        prior_rgb[prior_base + 1U] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        prior_rgb[prior_base + 2U] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(rect, step);
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, target_r, target_g,
                             target_b);
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(rect, step);
        const float blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);

        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * g_state.vertex_stride;
        float *unit = &g_state.vertices[tile_float_offset];
        const size_t prior_base = (size_t)update_index * 3U;
        const float prior_r = prior_rgb[prior_base];
        const float prior_g = prior_rgb[prior_base + 1U];
        const float prior_b = prior_rgb[prior_base + 2U];
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

static void db_apply_gradient_dirty_rows(uint32_t row_start, uint32_t row_count,
                                         uint32_t head_row, int direction_down,
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
        db_gradient_row_color_rgb(row, head_row, direction_down, cycle_index,
                                  &row_r, &row_g, &row_b);
        db_blend_grid_row_to_color(row, row_r, row_g, row_b, 1.0F);
    }
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
                     const db_snake_plan_t *plan, uint32_t rect_prev_start,
                     uint32_t rect_prev_count, int rect_full_repaint,
                     uint32_t gradient_row_start, uint32_t gradient_row_count) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((g_state.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.pattern == DB_PATTERN_RECT_SNAKE)) {
        const int is_grid = (g_state.pattern == DB_PATTERN_SNAKE_GRID);
        if ((is_grid == 0) && ((rect_full_repaint != 0) || (plan == NULL))) {
            db_gl_upload_buffer(source, total_bytes, 0, NULL,
                                g_state.use_map_range_upload,
                                g_state.use_map_buffer_upload);
            return;
        }
        const db_rect_snake_rect_t rect =
            (is_grid != 0) ? (db_rect_snake_rect_t){
                                 .x = 0U,
                                 .y = 0U,
                                 .width = cols,
                                 .height = rows,
                                 .color_r = 0.0F,
                                 .color_g = 0.0F,
                                 .color_b = 0.0F,
                             }
                           : db_rect_snake_rect_from_index(
                                 g_state.pattern_seed, plan->active_rect_index);
        if ((rect.width == 0U) || (rect.height == 0U)) {
            return;
        }
        db_gl_upload_range_t ranges[DB_MAX_SNAKE_UPLOAD_RANGES];
        size_t range_count = 0U;
        db_append_snake_step_ranges(
            ranges, DB_MAX_SNAKE_UPLOAD_RANGES, &range_count,
            (is_grid != 0) ? plan->prev_start : rect_prev_start,
            (is_grid != 0) ? plan->prev_count : rect_prev_count, tile_bytes,
            tile_bytes, rect.x, rect.y, rect.width, rect.height);
        db_append_snake_step_ranges(ranges, DB_MAX_SNAKE_UPLOAD_RANGES,
                                    &range_count, plan->active_cursor,
                                    plan->batch_size, tile_bytes, tile_bytes,
                                    rect.x, rect.y, rect.width, rect.height);
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

    g_state.is_es_context =
        db_gl_is_es_context((const char *)glGetString(GL_VERSION));
    g_state.vertex_stride = (g_state.is_es_context != 0)
                                ? DB_ES_VERTEX_FLOAT_STRIDE
                                : DB_VERTEX_FLOAT_STRIDE;

    if (!db_init_vertices_for_mode(g_state.vertex_stride)) {
        failf("failed to allocate benchmark vertex buffers");
    }

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.use_map_range_upload = 0;
    g_state.use_map_buffer_upload = 0;
    g_state.vbo = 0U;

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
    db_snake_plan_t plan = {0};
    uint32_t rect_prev_start = 0U;
    uint32_t rect_prev_count = 0U;
    int rect_full_repaint = 0;
    uint32_t gradient_row_start = 0U;
    uint32_t gradient_row_count = 0U;

    if ((g_state.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.pattern == DB_PATTERN_RECT_SNAKE)) {
        const int is_grid = (g_state.pattern == DB_PATTERN_SNAKE_GRID);
        if (is_grid == 0) {
            rect_prev_start = g_state.snake_prev_start;
            rect_prev_count = g_state.snake_prev_count;
        }
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.pattern_seed, g_state.snake_rect_index,
            g_state.snake_cursor, g_state.snake_prev_start,
            g_state.snake_prev_count, g_state.mode_phase_flag);
        plan = db_snake_plan_next_step(&request);
        db_rect_snake_rect_t rect = {0};
        float target_r = 0.0F;
        float target_g = 0.0F;
        float target_b = 0.0F;
        int full_fill_on_phase_completed = 0;
        if (is_grid != 0) {
            rect = (db_rect_snake_rect_t){
                .x = 0U,
                .y = 0U,
                .width = db_grid_cols_effective(),
                .height = db_grid_rows_effective(),
                .color_r = 0.0F,
                .color_g = 0.0F,
                .color_b = 0.0F,
            };
            db_grid_target_color_rgb(plan.clearing_phase, &target_r, &target_g,
                                     &target_b);
            full_fill_on_phase_completed = 1;
            g_state.mode_phase_flag = plan.next_clearing_phase;
        } else {
            if (g_state.snake_reset_pending != 0) {
                db_fill_grid_all_rgb_stride(
                    g_state.vertices, g_state.work_unit_count,
                    g_state.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                    BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                    BENCH_GRID_PHASE0_B);
                g_state.snake_reset_pending = 0;
                rect_full_repaint = 1;
            }
            rect = db_rect_snake_rect_from_index(g_state.pattern_seed,
                                                 plan.active_rect_index);
            target_r = rect.color_r;
            target_g = rect.color_g;
            target_b = rect.color_b;
            g_state.snake_rect_index = plan.next_rect_index;
            if (plan.wrapped != 0) {
                g_state.snake_reset_pending = 1;
                g_state.snake_prev_count = 0U;
            }
        }
        db_render_snake_step(&plan, &rect, target_r, target_g, target_b,
                             full_fill_on_phase_completed);
        g_state.snake_prev_start = plan.next_prev_start;
        g_state.snake_prev_count = plan.next_prev_count;
        g_state.snake_cursor = plan.next_cursor;
    } else if ((g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const int is_sweep = (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP);
        const db_gradient_damage_plan_t plan = db_gradient_plan_next_frame(
            g_state.gradient_head_row, is_sweep ? g_state.mode_phase_flag : 1,
            g_state.gradient_cycle, is_sweep ? 0 : 1);
        gradient_row_start = plan.dirty_row_start;
        gradient_row_count = plan.dirty_row_count;
        db_apply_gradient_dirty_rows(
            gradient_row_start, gradient_row_count, plan.render_head_row,
            is_sweep ? plan.render_direction_down : 1, plan.render_cycle_index);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.mode_phase_flag = plan.next_direction_down;
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
                             &plan, rect_prev_start, rect_prev_count,
                             rect_full_repaint, gradient_row_start,
                             gradient_row_count);
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
