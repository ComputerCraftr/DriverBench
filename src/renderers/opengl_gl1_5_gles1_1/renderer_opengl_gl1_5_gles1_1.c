#include "renderer_opengl_gl1_5_gles1_1.h"
#include "renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderer_opengl_gl1_5_gles1_1_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_api.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_viewport_common.h"

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define DB_GL1_GRADIENT_DIRTY_RANGE_CAP 2U
#define DB_GL1_GRADIENT_REPLAY_ROW_CAP 4U
#define ES_STRIDE_BYTES ((int)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((int)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    db_gl_upload_range_t *curr_upload_ranges;
    db_gl_upload_range_t *prev_upload_ranges;
    size_t prev_upload_count;
} db_gl1_snake_replay_t;

typedef struct {
    // Runtime mode and benchmark state
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;

    // Vertex and viewport state
    db_gl_vertex_init_t vertex;
    db_gl_viewport_cache_t viewport;
    int is_es_context;

    // History/replay state
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_gl1_snake_replay_t snake_replay;
    db_history_snake_scratch_t snake_scratch;
    db_history_snake_backbuffer_state_t snake_backbuffer_state;
    int backbuffer_valid;

    // GL objects and caches
    db_gl_buffer_cache_t buffers;
    db_gl_compact_vbo_state_t compact_vbo;

    // CPU shadow state for compact snake upload path
    float *snake_color_state;
    size_t snake_color_capacity;
    float *gradient_row_y_ndc;
    uint32_t gradient_row_y_ndc_rows;
    int gradient_row_y_ndc_viewport_h;
    int bands_x_cache_viewport_w;
    uint32_t bands_x_cache_cols;
    uint32_t bands_x_cache_count;
    int bands_x_cache_px[BENCH_BANDS + 1U];
    float bands_x_cache_ndc[BENCH_BANDS + 1U];

    // Vertex attribute cache flags
    int client_arrays_configured;
    int vbo_arrays_configured;
} renderer_state_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_region_t target_region;
    double target_r;
    double target_g;
    double target_b;
    db_gl_upload_range_t *preview_ranges;
    size_t preview_capacity;
    size_t preview_count;
    int force_full_upload;
    int force_replay_full;
} db_gl1_snake_frame_state_t;

static renderer_state_t g_state = {0};

static int db_gl1_has_snake_color_state(void) {
    return (g_state.snake_color_state != NULL) &&
           (g_state.snake_color_capacity >=
            ((size_t)g_state.runtime.work_unit_count *
             DB_GL_COLOR_COMPONENT_COUNT));
}

static float *db_gl1_snake_tile_color_ptr(uint32_t tile_index) {
    if (db_gl1_has_snake_color_state() == 0) {
        return NULL;
    }
    if ((size_t)tile_index >= (size_t)g_state.runtime.work_unit_count) {
        return NULL;
    }
    return &g_state.snake_color_state[(size_t)tile_index *
                                      DB_GL_COLOR_COMPONENT_COUNT];
}

static void db_gl1_init_snake_color_state_from_vertices(void) {
    if ((db_gl1_has_snake_color_state() == 0) ||
        (g_state.vertex.vertices == NULL)) {
        return;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    for (uint32_t tile_index = 0U; tile_index < g_state.runtime.work_unit_count;
         tile_index++) {
        const size_t base =
            (size_t)tile_index * (size_t)DB_RECT_VERTEX_COUNT * stride;
        const float *unit = &g_state.vertex.vertices[base];
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color == NULL) {
            continue;
        }
        tile_color[DB_GL_COLOR_R_INDEX] =
            unit[DB_VERTEX_POSITION_FLOAT_COUNT + DB_GL_COLOR_R_INDEX];
        tile_color[DB_GL_COLOR_G_INDEX] =
            unit[DB_VERTEX_POSITION_FLOAT_COUNT + DB_GL_COLOR_G_INDEX];
        tile_color[DB_GL_COLOR_B_INDEX] =
            unit[DB_VERTEX_POSITION_FLOAT_COUNT + DB_GL_COLOR_B_INDEX];
    }
}

static void db_gl1_refresh_tile_positions_for_viewport(int viewport_w,
                                                       int viewport_h) {
    if ((viewport_w <= 0) || (viewport_h <= 0) ||
        (g_state.vertex.vertices == NULL)) {
        return;
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t tile_index = 0U; tile_index < g_state.vertex.work_unit_count;
         tile_index++) {
        const uint32_t row = tile_index / cols;
        const uint32_t col = tile_index % cols;
        const int x0_px =
            (int)(((uint64_t)col * (uint64_t)viewport_w) / (uint64_t)cols);
        int x1_px = (int)((((uint64_t)col + 1U) * (uint64_t)viewport_w) /
                          (uint64_t)cols);
        if ((col + 1U) == cols) {
            x1_px = viewport_w;
        }

        const int y_top_px =
            (int)(((uint64_t)row * (uint64_t)viewport_h) / (uint64_t)rows);
        int y_bottom_px = (int)((((uint64_t)row + 1U) * (uint64_t)viewport_h) /
                                (uint64_t)rows);
        if ((row + 1U) == rows) {
            y_bottom_px = viewport_h;
        }
        const int y0_px = viewport_h - y_bottom_px;
        const int y1_px = viewport_h - y_top_px;

        const float x0 = db_gl1_ndc_from_pixel_coord(x0_px, viewport_w);
        const float x1 = db_gl1_ndc_from_pixel_coord(x1_px, viewport_w);
        const float y0 = db_gl1_ndc_from_pixel_coord(y0_px, viewport_h);
        const float y1 = db_gl1_ndc_from_pixel_coord(y1_px, viewport_h);

        const size_t base = (size_t)tile_index * DB_RECT_VERTEX_COUNT *
                            g_state.vertex.vertex_stride;
        db_fill_rect_unit_pos(&g_state.vertex.vertices[base], x0, y0, x1, y1,
                              g_state.vertex.vertex_stride);
    }

    if (g_state.buffers.vbo != 0U) {
        const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
        const db_gl_upload_range_t full_range =
            db_gl_upload_full_range(upload_bytes);
        db_gl_upload_vbo_damage_ranges(g_state.vertex.vertices, upload_bytes,
                                       &g_state.vertex.upload, &full_range, 1U);
    }
}

static void db_gl1_refresh_gradient_row_ndc_cache(int viewport_h) {
    if ((viewport_h <= 0) || (g_state.gradient_row_y_ndc == NULL)) {
        return;
    }
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return;
    }
    if ((g_state.gradient_row_y_ndc_rows == rows) &&
        (g_state.gradient_row_y_ndc_viewport_h == viewport_h)) {
        return;
    }
    for (uint32_t row = 0U; row <= rows; row++) {
        const int y_top_px =
            (int)(((uint64_t)row * (uint64_t)viewport_h) / (uint64_t)rows);
        const int y_px = viewport_h - y_top_px;
        g_state.gradient_row_y_ndc[row] =
            db_gl1_ndc_from_pixel_coord(y_px, viewport_h);
    }
    g_state.gradient_row_y_ndc_rows = rows;
    g_state.gradient_row_y_ndc_viewport_h = viewport_h;
}

static void db_gl1_refresh_bands_x_cache(uint32_t cols, uint32_t band_count,
                                         int viewport_w) {
    if ((viewport_w <= 0) || (cols == 0U) || (band_count == 0U) ||
        (band_count > BENCH_BANDS)) {
        return;
    }
    if ((g_state.bands_x_cache_viewport_w == viewport_w) &&
        (g_state.bands_x_cache_cols == cols) &&
        (g_state.bands_x_cache_count == band_count)) {
        return;
    }
    for (uint32_t band = 0U; band <= band_count; band++) {
        const uint32_t tile_x = (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                                           (uint64_t)band_count);
        int x_px =
            (int)(((uint64_t)tile_x * (uint64_t)viewport_w) / (uint64_t)cols);
        if (band == band_count) {
            x_px = viewport_w;
        }
        if (x_px < 0) {
            x_px = 0;
        }
        if (x_px > viewport_w) {
            x_px = viewport_w;
        }
        g_state.bands_x_cache_px[band] = x_px;
        g_state.bands_x_cache_ndc[band] =
            db_gl1_ndc_from_pixel_coord(x_px, viewport_w);
    }
    g_state.bands_x_cache_viewport_w = viewport_w;
    g_state.bands_x_cache_cols = cols;
    g_state.bands_x_cache_count = band_count;
}

static void db_gl1_seed_backbuffer_clear_cb(const float rgba[4],
                                            void *user_data) {
    (void)user_data;
    if (rgba == NULL) {
        return;
    }
    db_gl_clear_color_rgb(rgba[0], rgba[1], rgba[2]);
    db_gl_clear_color_buffer();
}

static void db_gl1_refresh_capability_mode(void) {
    const char *draw_mode = db_gl_capability_mode_draw_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode,
        g_state.runtime_flags.is_snake_history_texture, 0);
    const char *upload_mode = db_gl_capability_mode_upload_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode,
        db_gl_capability_mode_upload_from_probe(
            (g_state.buffers.vbo != 0U) ? 1 : 0, &g_state.vertex.upload));
    db_gl_capability_mode_compose(
        g_state.capability_mode, sizeof(g_state.capability_mode), draw_mode,
        upload_mode, db_runtime_backbuffer_replay_enabled(&g_state.runtime));
}

static int db_init_vertices_for_mode(size_t vertex_stride) {
    db_benchmark_runtime_init_t runtime_state = {0};
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &runtime_state)) {
        return 0;
    }
    if (!db_init_vertices_for_runtime_common_with_stride(
            BACKEND_NAME, &init_state, &runtime_state, vertex_stride)) {
        return 0;
    }

    g_state.vertex = init_state;
    g_state.runtime = runtime_state;
    return 1;
}

static void db_render_snake_step(const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 uint32_t shape_kind, uint32_t pattern_seed,
                                 uint32_t shape_index, double target_r,
                                 double target_g, double target_b,
                                 int force_full_fill_on_phase_complete) {
    if ((plan == NULL) || (region == NULL)) {
        return;
    }
    if (plan->batch_size > BENCH_SNAKE_PHASE_WINDOW_TILES) {
        failf("snake batch size %u exceeds BENCH_SNAKE_PHASE_WINDOW_TILES=%u",
              plan->batch_size, BENCH_SNAKE_PHASE_WINDOW_TILES);
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }
    if ((force_full_fill_on_phase_complete != 0) &&
        (plan->phase_completed != 0)) {
        float target_r_f = 0.0F;
        float target_g_f = 0.0F;
        float target_b_f = 0.0F;
        db_rgb_f64_to_f32_triplet(target_r, target_g, target_b, &target_r_f,
                                  &target_g_f, &target_b_f);
        if (db_gl1_has_snake_color_state() != 0) {
            for (uint32_t tile_index = 0U;
                 tile_index < g_state.runtime.work_unit_count; tile_index++) {
                float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
                if (tile_color == NULL) {
                    continue;
                }
                tile_color[0] = target_r_f;
                tile_color[1] = target_g_f;
                tile_color[2] = target_b_f;
            }
        } else {
            db_fill_grid_all_rgb_stride(
                g_state.vertex.vertices, g_state.runtime.work_unit_count,
                g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                target_r_f, target_g_f, target_b_f);
        }
        return;
    }
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (g_state.runtime_flags.is_snake_shapes != 0) {
        if ((g_state.snake_scratch.row_bounds != NULL) &&
            (db_snake_shape_cache_init_from_index(
                 &shape_cache, g_state.snake_scratch.row_bounds,
                 g_state.snake_scratch.row_bounds_capacity, pattern_seed,
                 shape_index, DB_U32_SALT_PALETTE, region,
                 (db_snake_shape_kind_t)shape_kind) != 0)) {
            shape_cache_ptr = &shape_cache;
        }
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    float target_r_f = 0.0F;
    float target_g_f = 0.0F;
    float target_b_f = 0.0F;
    db_rgb_f64_to_f32_triplet(target_r, target_g, target_b, &target_r_f,
                              &target_g_f, &target_b_f);
    const uint32_t batch_limit =
        db_snake_plan_active_batch_limit(plan, BENCH_SNAKE_PHASE_WINDOW_TILES);
    double prior_rgb_local[BENCH_SNAKE_PHASE_WINDOW_TILES *
                           DB_GL_COLOR_COMPONENT_COUNT] = {0.0};
    uint32_t active_tile_indices_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    uint8_t active_tile_valid_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    double *prior_rgb = prior_rgb_local;
    uint32_t *active_tile_indices = active_tile_indices_local;
    uint8_t *active_tile_valid = active_tile_valid_local;
    if ((g_state.snake_scratch.active_tile_indices != NULL) &&
        (g_state.snake_scratch.active_tile_valid != NULL) &&
        (g_state.snake_scratch.active_prior_rgb != NULL) &&
        (g_state.snake_scratch.active_tile_capacity >= batch_limit)) {
        active_tile_indices = g_state.snake_scratch.active_tile_indices;
        active_tile_valid = g_state.snake_scratch.active_tile_valid;
        prior_rgb = g_state.snake_scratch.active_prior_rgb;
    }
    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        active_tile_valid[update_index] = 0U;
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_active_tile(plan, region, shape_cache_ptr,
                                              update_index, cols, rows,
                                              &tile) == 0) {
            continue;
        }
        active_tile_indices[update_index] = tile.tile_index;
        active_tile_valid[update_index] = 1U;
        const size_t prior_base =
            (size_t)update_index * DB_GL_COLOR_COMPONENT_COUNT;
        const uint32_t tile_index = active_tile_indices[update_index];
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color != NULL) {
            prior_rgb[prior_base + DB_GL_COLOR_R_INDEX] =
                tile_color[DB_GL_COLOR_R_INDEX];
            prior_rgb[prior_base + DB_GL_COLOR_G_INDEX] =
                tile_color[DB_GL_COLOR_G_INDEX];
            prior_rgb[prior_base + DB_GL_COLOR_B_INDEX] =
                tile_color[DB_GL_COLOR_B_INDEX];
        } else {
            float *unit = &g_state.vertex.vertices[tile_float_offset];
            prior_rgb[prior_base + DB_GL_COLOR_R_INDEX] =
                unit[DB_GL_COLOR_R_OFFSET];
            prior_rgb[prior_base + DB_GL_COLOR_G_INDEX] =
                unit[DB_GL_COLOR_G_OFFSET];
            prior_rgb[prior_base + DB_GL_COLOR_B_INDEX] =
                unit[DB_GL_COLOR_B_OFFSET];
        }
    }

    for (uint32_t prev_offset = 0U; prev_offset < plan->prev_count;
         prev_offset++) {
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_prev_tile(plan, region, shape_cache_ptr,
                                            prev_offset, cols, rows,
                                            &tile) == 0) {
            continue;
        }
        const uint32_t tile_index = tile.tile_index;
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color != NULL) {
            tile_color[0] = target_r_f;
            tile_color[1] = target_g_f;
            tile_color[2] = target_b_f;
        } else {
            float *unit = &g_state.vertex.vertices[tile_float_offset];
            db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                                 DB_VERTEX_POSITION_FLOAT_COUNT, target_r_f,
                                 target_g_f, target_b_f);
        }
    }

    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        if (active_tile_valid[update_index] == 0U) {
            continue;
        }
        const uint32_t tile_index = active_tile_indices[update_index];
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        const double blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);

        const size_t prior_base =
            (size_t)update_index * DB_GL_COLOR_COMPONENT_COUNT;
        const double prior_r = prior_rgb[prior_base];
        const double prior_g = prior_rgb[prior_base + 1U];
        const double prior_b = prior_rgb[prior_base + 2U];
        double out_r_value = 0.0;
        double out_g_value = 0.0;
        double out_b_value = 0.0;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r_value, &out_g_value, &out_b_value);
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_rgb_f64_to_f32_triplet(out_r_value, out_g_value, out_b_value, &out_r,
                                  &out_g, &out_b);
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color != NULL) {
            tile_color[0] = out_r;
            tile_color[1] = out_g;
            tile_color[2] = out_b;
        } else {
            float *unit = &g_state.vertex.vertices[tile_float_offset];
            db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                                 DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                                 out_b);
        }
    }
}

static size_t db_gl1_build_gradient_compact_row_vertices(
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    uint32_t head_row, int direction_down, uint32_t cycle_index, int viewport_w,
    int viewport_h, float *dst_vertices, size_t dst_float_capacity) {
    if ((dirty_ranges == NULL) || (dirty_count == 0U) || (viewport_w <= 0) ||
        (viewport_h <= 0) || (dst_vertices == NULL) ||
        (dst_float_capacity == 0U)) {
        return 0U;
    }
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return 0U;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if (rect_float_count == 0U) {
        return 0U;
    }
    const size_t rect_capacity = dst_float_capacity / rect_float_count;
    if (rect_capacity == 0U) {
        return 0U;
    }
    const float x0 = db_gl1_ndc_from_pixel_coord(0, viewport_w);
    const float x1 = db_gl1_ndc_from_pixel_coord(viewport_w, viewport_w);
    const int can_use_cached_rows =
        (g_state.gradient_row_y_ndc != NULL) &&
        (g_state.gradient_row_y_ndc_rows == rows) &&
        (g_state.gradient_row_y_ndc_viewport_h == viewport_h);

    size_t rect_count = 0U;
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count_raw = dirty_ranges[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= rows)) {
            continue;
        }
        const uint32_t row_end =
            db_u32_min(rows, db_checked_add_u32(BACKEND_NAME, "row_end",
                                                row_start, row_count_raw));
        uint32_t row = row_start;
        while (row < row_end) {
            if (rect_count >= rect_capacity) {
                return rect_count;
            }

            double row_r = 0.0;
            double row_g = 0.0;
            double row_b = 0.0;
            db_gradient_row_color_rgb(row, head_row, direction_down,
                                      cycle_index, &row_r, &row_g, &row_b);
            float row_r_f = 0.0F;
            float row_g_f = 0.0F;
            float row_b_f = 0.0F;
            db_rgb_f64_to_f32_triplet(row_r, row_g, row_b, &row_r_f, &row_g_f,
                                      &row_b_f);
            const uint32_t row_r_bits = db_f32_to_bits_u32(row_r_f);
            const uint32_t row_g_bits = db_f32_to_bits_u32(row_g_f);
            const uint32_t row_b_bits = db_f32_to_bits_u32(row_b_f);

            uint32_t run_end = row + 1U;
            while (run_end < row_end) {
                double next_r = 0.0;
                double next_g = 0.0;
                double next_b = 0.0;
                db_gradient_row_color_rgb(run_end, head_row, direction_down,
                                          cycle_index, &next_r, &next_g,
                                          &next_b);
                float next_r_f = 0.0F;
                float next_g_f = 0.0F;
                float next_b_f = 0.0F;
                db_rgb_f64_to_f32_triplet(next_r, next_g, next_b, &next_r_f,
                                          &next_g_f, &next_b_f);
                if ((db_f32_to_bits_u32(next_r_f) != row_r_bits) ||
                    (db_f32_to_bits_u32(next_g_f) != row_g_bits) ||
                    (db_f32_to_bits_u32(next_b_f) != row_b_bits)) {
                    break;
                }
                run_end++;
            }
            const float y0 =
                can_use_cached_rows
                    ? g_state.gradient_row_y_ndc[run_end]
                    : db_gl1_ndc_from_pixel_coord(
                          viewport_h -
                              (int)(((uint64_t)run_end * (uint64_t)viewport_h) /
                                    (uint64_t)rows),
                          viewport_h);
            const float y1 =
                can_use_cached_rows
                    ? g_state.gradient_row_y_ndc[row]
                    : db_gl1_ndc_from_pixel_coord(
                          viewport_h -
                              (int)(((uint64_t)row * (uint64_t)viewport_h) /
                                    (uint64_t)rows),
                          viewport_h);
            const size_t base = rect_count * rect_float_count;
            float *const unit = &dst_vertices[base];
            db_fill_rect_unit_pos(unit, x0, y0, x1, y1, stride);
            db_set_rect_unit_rgb(unit, stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                                 row_r_f, row_g_f, row_b_f);
            if (g_state.is_es_context != 0) {
                db_set_rect_unit_alpha(unit, stride, DB_GL_COLOR_A_OFFSET,
                                       1.0F);
            }
            rect_count++;
            row = run_end;
        }
    }
    return rect_count;
}

static void db_gl1_configure_client_arrays_if_needed(void) {
    if (g_state.client_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(0U,
                                         &g_state.buffers.bound_array_buffer);

    const int client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int client_color_components = (g_state.is_es_context != 0)
                                            ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                            : DB_VERTEX_COLOR_FLOAT_COUNT;

    db_gl_set_vertex_pointer_2f(client_stride, &g_state.vertex.vertices[0]);
    db_gl_set_color_pointer_f(
        client_color_components, client_stride,
        &g_state.vertex.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);

    g_state.client_arrays_configured = 1;
    g_state.vbo_arrays_configured = 0;
}

static void db_gl1_configure_vbo_arrays_if_needed(void) {
    if (g_state.buffers.vbo == 0U || g_state.vbo_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(g_state.buffers.vbo,
                                         &g_state.buffers.bound_array_buffer);

    const int vbo_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int vbo_color_components = (g_state.is_es_context != 0)
                                         ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                         : DB_VERTEX_COLOR_FLOAT_COUNT;

    db_gl_set_vertex_pointer_2f(vbo_stride, db_gl_vbo_offset_ptr(0U));
    db_gl_set_color_pointer_f(
        vbo_color_components, vbo_stride,
        db_gl_vbo_offset_ptr(sizeof(float) * DB_VERTEX_POSITION_FLOAT_COUNT));

    g_state.vbo_arrays_configured = 1;
    g_state.client_arrays_configured = 0;
}

static void db_gl1_invalidate_array_pointer_cache(void) {
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
}

static void db_gl1_draw_compact_scratch_vbo(const char *first_vertex_label,
                                            size_t draw_vertex_count) {
    if ((first_vertex_label == NULL) || (draw_vertex_count == 0U)) {
        return;
    }
    db_gl1_configure_vbo_arrays_if_needed();
    db_gl_draw_arrays_triangles(
        db_checked_u32_to_i32(
            BACKEND_NAME, first_vertex_label,
            db_checked_size_to_u32(BACKEND_NAME, first_vertex_label,
                                   g_state.compact_vbo.first_vertex)),
        db_gl_draw_vertex_count_i32(BACKEND_NAME, draw_vertex_count));
}

static void db_gl1_draw_compact_scratch_client(size_t draw_vertex_count) {
    if (draw_vertex_count == 0U) {
        return;
    }
    db_gl1_configure_client_arrays_if_needed();
    const int client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int client_color_components = (g_state.is_es_context != 0)
                                            ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                            : DB_VERTEX_COLOR_FLOAT_COUNT;
    db_gl_set_vertex_pointer_2f(client_stride,
                                &g_state.compact_vbo.scratch_vertices[0]);
    db_gl_set_color_pointer_f(
        client_color_components, client_stride,
        &g_state.compact_vbo.scratch_vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
    db_gl_draw_arrays_triangles(
        0, db_gl_draw_vertex_count_i32(BACKEND_NAME, draw_vertex_count));
    db_gl1_invalidate_array_pointer_cache();
}

static int db_gl1_draw_compact_ranges_once(const db_gl_upload_range_t *ranges,
                                           size_t range_count,
                                           size_t upload_bytes) {
    if ((ranges == NULL) || (range_count == 0U) ||
        (g_state.buffers.vbo == 0U) ||
        (g_state.compact_vbo.scratch_vertices == NULL) ||
        (g_state.compact_vbo.vbo_capacity_bytes == 0U)) {
        return 0;
    }
    size_t compact_bytes = 0U;
    if (db_gl_compact_copy_ranges_from_vertices(
            ranges, range_count, g_state.vertex.vertices, upload_bytes,
            g_state.vertex.vertex_stride, &g_state.compact_vbo,
            &compact_bytes) == 0) {
        return 0;
    }
    if (db_gl_upload_compact_prepared(
            &g_state.compact_vbo, &g_state.vertex.upload, compact_bytes) == 0) {
        return 0;
    }
    const size_t bytes_per_vertex =
        g_state.vertex.vertex_stride * sizeof(float);
    db_gl1_draw_compact_scratch_vbo("compact_first_vertex",
                                    compact_bytes / bytes_per_vertex);
    return 1;
}

static int
db_gl1_draw_compact_client_ranges_once(const db_gl_upload_range_t *ranges,
                                       size_t range_count,
                                       size_t upload_bytes) {
    if ((ranges == NULL) || (range_count == 0U) ||
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        return 0;
    }
    size_t compact_bytes = 0U;
    if (db_gl_compact_copy_ranges_from_vertices(
            ranges, range_count, g_state.vertex.vertices, upload_bytes,
            g_state.vertex.vertex_stride, &g_state.compact_vbo,
            &compact_bytes) == 0) {
        return 0;
    }

    db_gl1_draw_compact_scratch_client(
        compact_bytes / (g_state.vertex.vertex_stride * sizeof(float)));
    return 1;
}

static int db_gl1_draw_compact_ranges_from_snake_colors_once(
    const db_gl_upload_range_t *ranges, size_t range_count) {
    if ((ranges == NULL) || (range_count == 0U) ||
        (g_state.compact_vbo.scratch_vertices == NULL) ||
        (g_state.compact_vbo.vbo_capacity_bytes == 0U) ||
        (g_state.vertex.vertices == NULL) ||
        (db_gl1_has_snake_color_state() == 0)) {
        return 0;
    }

    const size_t stride = g_state.vertex.vertex_stride;
    const size_t bytes_per_vertex = stride * sizeof(float);
    const size_t tile_bytes = (size_t)DB_RECT_VERTEX_COUNT * bytes_per_vertex;
    const size_t tile_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if ((bytes_per_vertex == 0U) || (tile_bytes == 0U) ||
        (tile_float_count == 0U)) {
        return 0;
    }

    const size_t total_tiles = (size_t)g_state.runtime.work_unit_count;
    const size_t scratch_tile_capacity =
        g_state.compact_vbo.scratch_float_capacity / tile_float_count;
    if (scratch_tile_capacity == 0U) {
        return 0;
    }
    size_t out_index = 0U;
    for (size_t range_index = 0U; range_index < range_count; range_index++) {
        const db_gl_upload_range_t *range = &ranges[range_index];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % tile_bytes) != 0U) ||
            ((range->size_bytes % tile_bytes) != 0U)) {
            continue;
        }
        const size_t first_tile = range->src_offset_bytes / tile_bytes;
        const size_t range_tiles = range->size_bytes / tile_bytes;
        if ((first_tile > total_tiles) ||
            (range_tiles > (total_tiles - first_tile))) {
            return 0;
        }
        if ((out_index > scratch_tile_capacity) ||
            (range_tiles > (scratch_tile_capacity - out_index))) {
            return 0;
        }
        for (size_t tile_offset = 0U; tile_offset < range_tiles;
             tile_offset++) {
            const size_t tile_index = first_tile + tile_offset;
            const size_t src_base = tile_index * tile_float_count;
            const size_t dst_base = out_index * tile_float_count;
            float *const dst_unit =
                &g_state.compact_vbo.scratch_vertices[dst_base];
            const float *tile_color =
                &g_state.snake_color_state[tile_index *
                                           DB_GL_COLOR_COMPONENT_COUNT];
            db_copy_bytes(dst_unit, &g_state.vertex.vertices[src_base],
                          tile_bytes);
            db_set_rect_unit_rgb(dst_unit, stride,
                                 DB_VERTEX_POSITION_FLOAT_COUNT, tile_color[0],
                                 tile_color[1], tile_color[2]);
            if (g_state.is_es_context != 0) {
                db_set_rect_unit_alpha(dst_unit, stride, DB_GL_COLOR_A_OFFSET,
                                       1.0F);
            }
            out_index++;
        }
    }
    if (out_index == 0U) {
        return 0;
    }

    const size_t compact_vertex_count =
        out_index * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = compact_vertex_count * bytes_per_vertex;
    if ((compact_bytes == 0U) ||
        (compact_bytes > g_state.compact_vbo.vbo_capacity_bytes)) {
        return 0;
    }

    if ((g_state.buffers.vbo != 0U) &&
        (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                       &g_state.vertex.upload,
                                       compact_bytes) != 0)) {
        db_gl1_draw_compact_scratch_vbo("snake_compact_first_vertex",
                                        compact_vertex_count);
        return 1;
    }

    db_gl1_draw_compact_scratch_client(compact_vertex_count);
    return 1;
}

static void db_gl1_draw_ranges_best_effort(const db_gl_upload_range_t *ranges,
                                           size_t range_count,
                                           size_t upload_bytes) {
    if ((ranges == NULL) || (range_count == 0U)) {
        return;
    }
    if (db_gl1_draw_compact_ranges_from_snake_colors_once(ranges,
                                                          range_count) != 0) {
        return;
    }
    if (db_gl1_draw_compact_ranges_once(ranges, range_count, upload_bytes) !=
        0) {
        return;
    }
    if ((g_state.buffers.vbo == 0U) &&
        (db_gl1_draw_compact_client_ranges_once(ranges, range_count,
                                                upload_bytes) != 0)) {
        return;
    }
    if (g_state.buffers.vbo != 0U) {
        db_gl_upload_vbo_damage_ranges(g_state.vertex.vertices, upload_bytes,
                                       &g_state.vertex.upload, ranges,
                                       range_count);
        db_gl1_configure_vbo_arrays_if_needed();
    } else {
        db_gl1_configure_client_arrays_if_needed();
    }
    db_gl_draw_dirty_ranges_common(BACKEND_NAME, g_state.vertex.vertex_stride,
                                   g_state.vertex.draw_vertex_count, ranges,
                                   range_count);
}

static void
db_gl1_draw_gradient_dirty_rows_mesh(const db_dirty_row_range_t *dirty_ranges,
                                     size_t dirty_count, uint32_t head_row,
                                     int direction_down, uint32_t cycle_index,
                                     int viewport_w, int viewport_h) {
    if ((viewport_w <= 0) || (viewport_h <= 0) ||
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        return;
    }

    const size_t dirty_row_total =
        db_gl1_gradient_dirty_row_total(dirty_ranges, dirty_count);
    if (dirty_row_total == 0U) {
        return;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    const size_t needed_float_count = dirty_row_total * rect_float_count;
    if (needed_float_count > g_state.compact_vbo.scratch_float_capacity) {
        return;
    }
    const size_t rect_count = db_gl1_build_gradient_compact_row_vertices(
        dirty_ranges, dirty_count, head_row, direction_down, cycle_index,
        viewport_w, viewport_h, g_state.compact_vbo.scratch_vertices,
        g_state.compact_vbo.scratch_float_capacity);
    if (rect_count == 0U) {
        return;
    }
    const size_t draw_vertex_count = rect_count * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = draw_vertex_count * stride * sizeof(float);

    if (g_state.buffers.vbo != 0U) {
        if (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                          &g_state.vertex.upload,
                                          compact_bytes) == 0) {
            return;
        }
        db_gl1_draw_compact_scratch_vbo("gradient_compact_first_vertex",
                                        draw_vertex_count);
    } else {
        db_gl1_draw_compact_scratch_client(draw_vertex_count);
    }
}

static void db_gl1_draw_bands_compact(uint32_t cols, uint32_t band_count,
                                      uint32_t frame_index, int viewport_w,
                                      int viewport_h) {
    if ((cols == 0U) || (band_count == 0U) || (viewport_w <= 0) ||
        (viewport_h <= 0) || (g_state.compact_vbo.scratch_vertices == NULL)) {
        return;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if ((rect_float_count == 0U) ||
        ((size_t)band_count >
         (g_state.compact_vbo.scratch_float_capacity / rect_float_count))) {
        return;
    }
    db_gl1_refresh_bands_x_cache(cols, band_count, viewport_w);
    const int use_cached_x = (band_count <= BENCH_BANDS) &&
                             (g_state.bands_x_cache_viewport_w == viewport_w) &&
                             (g_state.bands_x_cache_cols == cols) &&
                             (g_state.bands_x_cache_count == band_count);
    const float y0_ndc = db_gl1_ndc_from_pixel_coord(0, viewport_h);
    const float y1_ndc = db_gl1_ndc_from_pixel_coord(viewport_h, viewport_h);

    size_t rect_count = 0U;
    for (uint32_t band = 0U; band < band_count; band++) {
        int x0 = 0;
        int x1 = 0;
        float x0_ndc = 0.0F;
        float x1_ndc = 0.0F;
        if (use_cached_x != 0) {
            x0 = g_state.bands_x_cache_px[band];
            x1 = g_state.bands_x_cache_px[band + 1U];
            x0_ndc = g_state.bands_x_cache_ndc[band];
            x1_ndc = g_state.bands_x_cache_ndc[band + 1U];
        } else {
            const uint32_t tile_x0 =
                (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                           (uint64_t)band_count);
            const uint32_t tile_x1 =
                (uint32_t)(((uint64_t)(band + 1U) * (uint64_t)cols) /
                           (uint64_t)band_count);
            x0 = (int)(((uint64_t)tile_x0 * (uint64_t)viewport_w) /
                       (uint64_t)cols);
            x1 = (int)(((uint64_t)tile_x1 * (uint64_t)viewport_w) /
                       (uint64_t)cols);
            if (band + 1U == band_count) {
                x1 = viewport_w;
            }
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > viewport_w) {
                x1 = viewport_w;
            }
            x0_ndc = db_gl1_ndc_from_pixel_coord(x0, viewport_w);
            x1_ndc = db_gl1_ndc_from_pixel_coord(x1, viewport_w);
        }
        const int rect_w = x1 - x0;
        if (rect_w <= 0) {
            continue;
        }

        double color_r = 0.0;
        double color_g = 0.0;
        double color_b = 0.0;
        db_band_color_rgb(band, band_count, frame_index, &color_r, &color_g,
                          &color_b);
        const size_t base = rect_count * rect_float_count;
        float *const unit = &g_state.compact_vbo.scratch_vertices[base];
        float color_r_f = 0.0F;
        float color_g_f = 0.0F;
        float color_b_f = 0.0F;
        db_rgb_f64_to_f32_triplet(color_r, color_g, color_b, &color_r_f,
                                  &color_g_f, &color_b_f);
        db_fill_rect_unit_pos(unit, x0_ndc, y0_ndc, x1_ndc, y1_ndc, stride);
        db_set_rect_unit_rgb(unit, stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                             color_r_f, color_g_f, color_b_f);
        if (g_state.is_es_context != 0) {
            db_set_rect_unit_alpha(unit, stride, DB_GL_COLOR_A_OFFSET, 1.0F);
        }
        rect_count++;
    }
    if (rect_count == 0U) {
        return;
    }

    const size_t draw_vertex_count = rect_count * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = draw_vertex_count * stride * sizeof(float);
    if ((g_state.buffers.vbo != 0U) &&
        (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                       &g_state.vertex.upload,
                                       compact_bytes) != 0)) {
        db_gl1_draw_compact_scratch_vbo("bands_compact_first_vertex",
                                        draw_vertex_count);
        return;
    }
    db_gl1_draw_compact_scratch_client(draw_vertex_count);
}

static size_t
db_collect_gl1_damage_ranges(const db_snake_plan_t *plan, int force_full_upload,
                             const db_dirty_row_range_t *gradient_dirty_ranges,
                             size_t gradient_dirty_count,
                             db_gl_upload_range_t *range_storage,
                             size_t range_capacity) {
    const db_gl1_damage_collect_ctx_t collect_ctx = {
        .backend_name = BACKEND_NAME,
        .pattern = g_state.runtime.pattern,
        .cols = db_grid_cols_effective(),
        .rows = db_grid_rows_effective(),
        .upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                        g_state.vertex.vertex_stride * sizeof(float),
        .upload_tile_bytes = db_rect_tile_bytes(g_state.vertex.vertex_stride),
        .force_full_upload = force_full_upload,
        .snake_plan = plan,
        .pattern_seed = g_state.runtime.pattern_seed,
        .snake_scratch = &g_state.snake_scratch,
        .damage_row_ranges = gradient_dirty_ranges,
        .damage_row_count = gradient_dirty_count,
        .default_history_range_storage =
            g_state.snake_replay.curr_upload_ranges,
        .gradient_dirty_range_cap = DB_GL1_GRADIENT_DIRTY_RANGE_CAP,
        .is_gradient_pattern = g_state.runtime_flags.is_gradient,
        .is_snake_history_texture_pattern =
            g_state.runtime_flags.is_snake_history_texture,
    };
    return db_gl1_collect_pattern_damage_ranges(&collect_ctx, range_storage,
                                                range_capacity);
}

static size_t db_gl1_build_snake_upload_ranges(const db_snake_plan_t *plan,
                                               int force_full_upload,
                                               db_gl_upload_range_t *ranges,
                                               size_t range_capacity) {
    return db_collect_gl1_damage_ranges(plan, force_full_upload, NULL, 0U,
                                        ranges, range_capacity);
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);

    g_state.is_es_context = db_gl_is_es_context(db_gl_get_version_string());
    g_state.vertex.vertex_stride = (g_state.is_es_context != 0)
                                       ? DB_ES_VERTEX_FLOAT_STRIDE
                                       : DB_VERTEX_FLOAT_STRIDE;

    if (!db_init_vertices_for_mode(g_state.vertex.vertex_stride)) {
        failf("failed to allocate benchmark vertex buffers");
    }
    g_state.snake_replay.curr_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_count = 0U;
    g_state.snake_scratch.spans = NULL;
    g_state.snake_scratch.row_bounds = NULL;
    g_state.snake_scratch.row_bounds_capacity = 0U;
    g_state.snake_scratch.span_capacity = 0U;
    g_state.snake_color_state = NULL;
    g_state.snake_color_capacity = 0U;
    g_state.gradient_row_y_ndc = NULL;
    g_state.gradient_row_y_ndc_rows = 0U;
    g_state.gradient_row_y_ndc_viewport_h = 0;
    g_state.bands_x_cache_viewport_w = 0;
    g_state.bands_x_cache_cols = 0U;
    g_state.bands_x_cache_count = 0U;
    db_history_snake_backbuffer_state_reset(&g_state.snake_backbuffer_state, 0);
    g_state.runtime_flags = db_history_runtime_mode_flags(&g_state.runtime);
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_history_snake_active_cache_init(&g_state.snake_scratch, BACKEND_NAME,
                                           BENCH_SNAKE_PHASE_WINDOW_TILES,
                                           DB_GL_COLOR_COMPONENT_COUNT);
        g_state.snake_color_capacity = (size_t)g_state.runtime.work_unit_count *
                                       DB_GL_COLOR_COMPONENT_COUNT;
        g_state.snake_color_state = (float *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_color_state", g_state.snake_color_capacity,
            sizeof(float));
        db_gl1_init_snake_color_state_from_vertices();
        const size_t scratch_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        g_state.snake_replay.curr_upload_ranges =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_upload_ranges", scratch_capacity,
                sizeof(*g_state.snake_replay.curr_upload_ranges));
        g_state.snake_replay.prev_upload_ranges =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_prev_upload_ranges", scratch_capacity,
                sizeof(*g_state.snake_replay.prev_upload_ranges));
        g_state.snake_scratch.spans =
            (db_snake_col_span_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_spans", scratch_capacity,
                sizeof(*g_state.snake_scratch.spans));
        if (g_state.runtime_flags.is_snake_shapes != 0) {
            g_state.snake_scratch.row_bounds =
                (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                    sizeof(*g_state.snake_scratch.row_bounds));
            g_state.snake_scratch.row_bounds_capacity =
                (size_t)db_grid_rows_effective();
        }
        g_state.snake_scratch.span_capacity = scratch_capacity;
    }
    g_state.gradient_row_y_ndc = (float *)db_alloc_array_or_fail(
        BACKEND_NAME, "gradient_row_y_ndc",
        (size_t)db_grid_rows_effective() + 1U, sizeof(float));

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    g_state.buffers.vbo = 0U;
    g_state.backbuffer_valid = 0;
    db_history_gradient_replay_state_reset(&g_state.gradient_prev_frame);

    g_state.capability_mode[0] = '\0';
    db_gl1_refresh_capability_mode();

    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    const size_t full_mesh_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);

    if (db_gl_context_advertises_vbo() != 0) {
        const size_t probe_bytes = full_mesh_bytes;
        const size_t total_vbo_bytes =
            db_gl_compact_vbo_total_bytes(probe_bytes);
        if (total_vbo_bytes == 0U) {
            failf("GL1 VBO size overflow");
        }
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.buffers.vbo = vbo_u32;
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached(
                    g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_vbo_init_data(total_vbo_bytes, NULL, GL_DYNAMIC_DRAW) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            db_gl_context_probe_upload_capabilities(
                total_vbo_bytes, g_state.vertex.vertices, &probe_result);
            g_state.vertex.upload = probe_result;
        }
        if (g_state.buffers.vbo != 0U) {
            const db_gl_upload_range_t full_base_range = {
                .src_offset_bytes = 0U,
                .dst_offset_bytes = 0U,
                .size_bytes = probe_bytes,
            };
            db_gl_upload_vbo_damage_ranges(g_state.vertex.vertices, probe_bytes,
                                           &g_state.vertex.upload,
                                           &full_base_range, 1U);
            db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                           probe_bytes,
                                           g_state.vertex.vertex_stride);
            g_state.vbo_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            db_log_renderer_capability_mode(
                BACKEND_NAME,
                db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            return;
        }
    }

    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                       full_mesh_bytes,
                                       g_state.vertex.vertex_stride);
    }

    db_gl1_configure_client_arrays_if_needed();
    db_gl1_refresh_capability_mode();
    db_log_renderer_capability_mode(
        BACKEND_NAME, db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

static void
db_gl1_render_snake_draw_pass(const db_gl1_snake_frame_state_t *snake_frame,
                              int dirty_backbuffer_mode, int viewport_w,
                              int viewport_h) {
    (void)dirty_backbuffer_mode;
    (void)viewport_w;
    (void)viewport_h;
    if (snake_frame == NULL) {
        return;
    }
    db_gl_upload_range_t *range_storage = snake_frame->preview_ranges;
    size_t draw_range_count = snake_frame->preview_count;
    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    const int allow_empty_dirty_draw = dirty_backbuffer_mode;
    const int must_force_full_draw =
        (g_state.runtime.backbuffer_draw_full != 0);
    if ((must_force_full_draw != 0) ||
        ((draw_range_count == 0U) && (allow_empty_dirty_draw == 0))) {
        range_storage[0] = db_gl_upload_full_range(upload_bytes);
        draw_range_count = 1U;
    }
    const int draw_is_full_mesh = (draw_range_count == 1U) &&
                                  (range_storage[0].src_offset_bytes == 0U) &&
                                  (range_storage[0].size_bytes == upload_bytes);

    if (draw_range_count == 0U) {
        // No damage this frame; preserve backbuffer contents.
    } else {
        db_gl_upload_range_t *mesh_ranges = range_storage;
        const size_t mesh_range_count = draw_range_count;
        db_gl1_draw_ranges_best_effort(mesh_ranges, mesh_range_count,
                                       upload_bytes);
        if (mesh_range_count > 0U) {
            db_history_record_mesh_draw_stats(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              draw_is_full_mesh, 1U);
        }
    }
    const db_gl_upload_range_t force_replay_full_range =
        db_gl_upload_full_range(upload_bytes);
    const db_gl_upload_range_t *persist_draw_ranges = range_storage;
    size_t persist_draw_count = draw_range_count;
    if (snake_frame->force_replay_full != 0) {
        persist_draw_ranges = &force_replay_full_range;
        persist_draw_count = 1U;
    }
    if ((persist_draw_count > 0U) &&
        (g_state.snake_replay.prev_upload_ranges != NULL) &&
        (persist_draw_count <= g_state.snake_scratch.span_capacity)) {
        g_state.snake_replay.prev_upload_count =
            db_gl_copy_upload_ranges(persist_draw_ranges, persist_draw_count,
                                     g_state.snake_replay.prev_upload_ranges,
                                     g_state.snake_scratch.span_capacity);
    } else if (persist_draw_count > 0U) {
        g_state.snake_replay.prev_upload_count = 0U;
    }
    // Keep previous replay ranges across zero-damage frames so the next
    // backbuffer can still be brought forward in double-buffered
    // preserved-backbuffer mode.
    g_state.snake_backbuffer_state.backbuffer_valid = 1;
}

static void db_gl1_prepare_snake_frame_state(db_gl1_snake_frame_state_t *state,
                                             int is_double_buffered,
                                             int dirty_backbuffer_mode,
                                             int has_viewport) {
    if (state == NULL) {
        return;
    }

    const db_history_snake_step_eval_t eval =
        db_history_eval_snake_step_from_runtime(&g_state.runtime);
    state->plan = eval.plan;
    db_snake_step_target_t target = eval.target;
    const db_snake_shape_kind_t shape_kind = eval.shape_kind;
    state->target_region = target.region;
    state->target_r = target.target_r;
    state->target_g = target.target_g;
    state->target_b = target.target_b;

    const db_history_snake_backbuffer_action_t history_action =
        db_history_eval_snake_backbuffer_action_io(
            dirty_backbuffer_mode, is_double_buffered,
            &g_state.snake_backbuffer_state.seed_frames_remaining,
            &g_state.snake_backbuffer_state.resync_frames_remaining,
            &g_state.snake_backbuffer_state.initial_seed_done,
            &g_state.snake_backbuffer_state.backbuffer_valid);

    if (db_history_run_seed_clear_if_needed(
            history_action.should_seed_now, &g_state.runtime,
            db_gl1_seed_backbuffer_clear_cb, NULL) != 0) {
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, 1U);
        g_state.snake_replay.prev_upload_count = 0U;
    }
    if (history_action.should_force_full_upload != 0) {
        state->force_full_upload = 1;
    }

    if ((g_state.snake_replay.curr_upload_ranges != NULL) &&
        (g_state.snake_scratch.span_capacity > 0U)) {
        state->preview_ranges = g_state.snake_replay.curr_upload_ranges;
        state->preview_capacity = g_state.snake_scratch.span_capacity;
    }
    state->preview_count = db_gl1_build_snake_upload_ranges(
        &state->plan, state->force_full_upload, state->preview_ranges,
        state->preview_capacity);
    if ((g_state.runtime_flags.is_snake_grid != 0) &&
        (state->force_full_upload != 0) && dirty_backbuffer_mode &&
        has_viewport && (g_state.snake_scratch.spans != NULL)) {
        // Grid fast-path on invalid backbuffer: clear to the pre-step base
        // phase, then redraw the full non-base set (settled + active). If the
        // span set exceeds capacity, keep the normal full upload.
        const size_t needed_spans =
            db_snake_plan_span_capacity_needed(&state->plan);
        if (needed_spans <= state->preview_capacity) {
            double base_r = 0.0;
            double base_g = 0.0;
            double base_b = 0.0;
            const int base_phase = (state->plan.phase_flag == 0) ? 1 : 0;
            db_grid_target_color_rgb(base_phase, &base_r, &base_g, &base_b);
            float base_r_f = 0.0F;
            float base_g_f = 0.0F;
            float base_b_f = 0.0F;
            db_rgb_f64_to_f32_triplet(base_r, base_g, base_b, &base_r_f,
                                      &base_g_f, &base_b_f);
            db_gl_clear_color_rgb(base_r_f, base_g_f, base_b_f);
            db_gl_clear_color_buffer();
            db_history_record_draw_stats_for_work(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames, 1, 0, 1U);

            const size_t replay_span_count =
                db_snake_collect_damage_spans_for_plan(
                    g_state.snake_scratch.spans, state->preview_capacity,
                    &state->target_region, &state->plan, NULL);
            const uint32_t cols = db_grid_cols_effective();
            const size_t tile_bytes =
                db_rect_tile_bytes(g_state.vertex.vertex_stride);
            state->preview_count = db_gl_collect_span_upload_ranges(
                cols, tile_bytes, tile_bytes, g_state.snake_scratch.spans,
                replay_span_count, state->preview_ranges,
                state->preview_capacity);
            state->force_replay_full = 1;
        }
    }
    const int can_replay_snake = db_history_can_replay_previous_damage(
        is_double_buffered, dirty_backbuffer_mode,
        g_state.snake_backbuffer_state.backbuffer_valid,
        g_state.snake_replay.prev_upload_count);
    if (can_replay_snake != 0) {
        // Replay the full previously-drawn damage set onto the alternate
        // backbuffer. Do not cap to BENCH_SNAKE_PHASE_WINDOW_TILES here:
        // high speed can produce far more ranges.
        size_t replay_draw_count = g_state.snake_replay.prev_upload_count;
        DB_LOG_CAPACITY_EXCEEDED_ONCE(
            BACKEND_NAME, "snake_replay_prev_upload_ranges", replay_draw_count,
            g_state.snake_scratch.span_capacity);
        if (replay_draw_count > g_state.snake_scratch.span_capacity) {
            replay_draw_count = g_state.snake_scratch.span_capacity;
        }
        if (replay_draw_count > 0U) {
            const size_t full_upload_bytes =
                (size_t)g_state.vertex.draw_vertex_count *
                g_state.vertex.vertex_stride * sizeof(float);
            db_gl1_draw_ranges_best_effort(
                g_state.snake_replay.prev_upload_ranges, replay_draw_count,
                full_upload_bytes);
        }
    }

    db_render_snake_step(
        &state->plan, &target.region, shape_kind, g_state.runtime.pattern_seed,
        state->plan.active_shape_index, target.target_r, target.target_g,
        target.target_b, target.force_full_fill_on_phase_complete);
    db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
}

static void db_gl1_render_gradient_frame(int viewport_w, int viewport_h,
                                         int is_double_buffered) {
    const db_gradient_damage_plan_t gradient_plan =
        db_history_eval_gradient_step_from_runtime(&g_state.runtime);

    // Render MUST use the plan's render_* state. The plan's next_* state is
    // only applied to the runtime AFTER we draw, matching CPU renderer
    // semantics.
    const uint32_t gradient_render_head_row =
        gradient_plan.render_state.head_row;
    const int gradient_render_direction_down =
        gradient_plan.render_state.direction_down;
    const uint32_t gradient_render_cycle_index =
        gradient_plan.render_state.cycle_index;

    db_dirty_row_range_t gradient_dirty_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] =
        {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
    const size_t gradient_dirty_count =
        db_gradient_collect_dirty_ranges(&gradient_plan, gradient_dirty_ranges);

    // When bench_speed_step > 1, the head can advance by multiple rows. Plan
    // dirty ranges cover the new sweep window, but traversed rows can be
    // skipped unless we repaint them as solid per-row colors.
    db_dirty_row_range_t skipped_ranges[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
        {0U, 0U},
        {0U, 0U},
    };
    size_t skipped_count = 0U;

    const uint32_t rows = db_grid_rows_effective();
    const uint32_t render_head = gradient_plan.render_state.head_row;
    const uint32_t next_head = gradient_plan.next_state.head_row;
    if (rows > 0U) {
        uint32_t delta = 0U;
        if (gradient_render_direction_down != 0) {
            // Moving down => head increases modulo rows.
            delta = (next_head + rows - render_head) % rows;
        } else {
            // Moving up => head decreases modulo rows.
            delta = (render_head + rows - next_head) % rows;
        }

        // If head changed but delta==0, we wrapped a full cycle.
        // Conservatively repaint everything.
        if ((next_head != render_head) && (delta == 0U)) {
            delta = rows;
        }

        if (delta > 1U) {
            const uint32_t skipped_rows = delta - 1U;
            if (gradient_render_direction_down != 0) {
                // Downwards: skipped rows are render+1 .. render+skipped.
                const uint32_t start_row = (render_head + 1U) % rows;
                skipped_ranges[0].row_start = start_row;
                skipped_ranges[0].row_count =
                    db_u32_min(rows - start_row, skipped_rows);
                skipped_ranges[1].row_start = 0U;
                skipped_ranges[1].row_count =
                    skipped_rows - skipped_ranges[0].row_count;
                skipped_count = (skipped_ranges[1].row_count > 0U) ? 2U : 1U;
            } else if (render_head >= skipped_rows) {
                // No wrap past 0: [render-skipped .. render-1]
                skipped_ranges[0].row_start = render_head - skipped_rows;
                skipped_ranges[0].row_count = skipped_rows;
                skipped_count = (skipped_rows > 0U) ? 1U : 0U;
            } else {
                // Wraps past 0: [0 .. render-1] and [rows-underflow .. rows-1]
                const uint32_t underflow = skipped_rows - render_head;
                skipped_ranges[0].row_start = 0U;
                skipped_ranges[0].row_count = render_head;
                skipped_ranges[1].row_start = rows - underflow;
                skipped_ranges[1].row_count = underflow;
                skipped_count = 0U;
                if (skipped_ranges[0].row_count > 0U) {
                    skipped_count++;
                }
                if (skipped_ranges[1].row_count > 0U) {
                    skipped_count++;
                }
            }
        }
    }

    // Gradient patterns: damage-only updates directly on the default
    // framebuffer/backbuffer.
    if ((viewport_w > 0) && (viewport_h > 0) && (rows > 0U)) {
        if (g_state.runtime.backbuffer_draw_full != 0) {
            db_dirty_row_range_t full_range[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
                {0U, 0U},
                {0U, 0U},
            };
            const size_t full_count =
                db_history_set_full_row_ranges(rows, full_range, 2U);
            db_gl1_draw_gradient_dirty_rows_mesh(
                full_range, full_count, gradient_render_head_row,
                gradient_render_direction_down, gradient_render_cycle_index,
                viewport_w, viewport_h);
            g_state.backbuffer_valid = 1;
            const db_gradient_state_t render_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };
            db_history_gradient_replay_state_store(&g_state.gradient_prev_frame,
                                                   full_range, full_count,
                                                   &render_state);
            db_history_record_draw_stats_for_work(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames, 1, 0, 1U);
        } else {
            db_dirty_row_range_t
                curr_draw_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
                    {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
            size_t curr_draw_count = db_gradient_build_curr_draw_ranges(
                skipped_ranges, skipped_count, gradient_dirty_ranges,
                gradient_dirty_count, curr_draw_ranges,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            const int needs_full_seed = db_history_should_seed_full_on_invalid(
                g_state.backbuffer_valid);
            const db_dirty_row_range_t *persist_ranges = curr_draw_ranges;
            size_t persist_count = curr_draw_count;
            db_gradient_state_t persist_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };

            if (needs_full_seed != 0) {
                (void)db_history_apply_full_seed_rows_if_needed(
                    &g_state.backbuffer_valid, rows, curr_draw_ranges,
                    DB_GL1_GRADIENT_REPLAY_ROW_CAP, &curr_draw_count);
                db_gl1_draw_gradient_dirty_rows_mesh(
                    curr_draw_ranges, curr_draw_count, gradient_render_head_row,
                    gradient_render_direction_down, gradient_render_cycle_index,
                    viewport_w, viewport_h);
                persist_count = curr_draw_count;
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 1, 0, 1U);
            } else {
                const int has_replay =
                    (is_double_buffered != 0) &&
                    (g_state.gradient_prev_frame.draw_count > 0U);
                const int has_current = (curr_draw_count > 0U);
                db_dirty_row_range_t
                    replay_draw_ranges[DB_GRADIENT_DRAW_RANGE_WORK_CAP] = {
                        {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U},
                        {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
                const db_dirty_row_range_t *replay_draw_ptr =
                    g_state.gradient_prev_frame.draw_rows;
                size_t replay_draw_count =
                    g_state.gradient_prev_frame.draw_count;
                if ((has_replay != 0) && (has_current != 0)) {
                    replay_draw_count = db_gradient_subtract_replay_ranges(
                        g_state.gradient_prev_frame.draw_rows,
                        g_state.gradient_prev_frame.draw_count,
                        curr_draw_ranges, curr_draw_count, replay_draw_ranges,
                        DB_GRADIENT_DRAW_RANGE_WORK_CAP);
                    replay_draw_ptr = replay_draw_ranges;
                }
                const int has_replay_draw = (replay_draw_count > 0U);
                if ((g_state.runtime_flags.is_gradient_sweep != 0) &&
                    (has_replay != 0) && (has_current == 0)) {
                    // At bottom bounce, sweep can be replay-only for one frame.
                    persist_ranges = g_state.gradient_prev_frame.draw_rows;
                    persist_count = g_state.gradient_prev_frame.draw_count;
                    persist_state = g_state.gradient_prev_frame.state;
                }

                if (has_replay_draw != 0) {
                    db_gl1_draw_gradient_dirty_rows_mesh(
                        replay_draw_ptr, replay_draw_count,
                        g_state.gradient_prev_frame.state.head_row,
                        g_state.gradient_prev_frame.state.direction_down,
                        g_state.gradient_prev_frame.state.cycle_index,
                        viewport_w, viewport_h);
                }
                if (has_current != 0) {
                    db_gl1_draw_gradient_dirty_rows_mesh(
                        curr_draw_ranges, curr_draw_count,
                        gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index, viewport_w, viewport_h);
                }
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 0, 1,
                    ((has_replay_draw != 0) || (has_current != 0)) ? 1U : 0U);
            }

            // Persist the damage we drew this frame for next double-buffer
            // replay. `curr_draw_ranges` and replay-diff ranges are already
            // generated in normalized order, so avoid a second normalize pass.
            size_t persist_count_limited = persist_count;
            const db_dirty_row_range_t *persist_ranges_ptr = persist_ranges;
            DB_LOG_CAPACITY_EXCEEDED_ONCE(
                BACKEND_NAME, "gradient_replay_persist_rows", persist_count,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            if (persist_count_limited > DB_GL1_GRADIENT_REPLAY_ROW_CAP) {
                persist_count_limited = DB_GL1_GRADIENT_REPLAY_ROW_CAP;
            }
            db_history_gradient_replay_state_store(
                &g_state.gradient_prev_frame, persist_ranges_ptr,
                persist_count_limited, &persist_state);
        }
    }

    // Apply step AFTER rendering (CPU renderer ordering).
    db_history_apply_gradient_step_to_runtime(&g_state.runtime, &gradient_plan);
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(uint32_t frame_index,
                                                   int viewport_width_px,
                                                   int viewport_height_px,
                                                   int double_buffered) {
    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(BACKEND_NAME, &viewport_width_px,
                                           &viewport_height_px,
                                           &g_state.viewport.last_viewport_w,
                                           &g_state.viewport.last_viewport_h);

    if (viewport_state.viewport_changed != 0) {
        // Keep GL viewport in sync with drawable pixels (HiDPI-safe)
        // without querying GL state.
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
        db_gl1_refresh_tile_positions_for_viewport(viewport_width_px,
                                                   viewport_height_px);
        db_gl1_refresh_gradient_row_ndc_cache(viewport_height_px);
        db_history_invalidate_snake_backbuffer_on_resize(
            double_buffered, &g_state.backbuffer_valid,
            &g_state.snake_replay.prev_upload_count,
            &g_state.snake_backbuffer_state);
    }

    // Preserved-backbuffer assumption for snake paths: dirty draws operate
    // directly on the default framebuffer.

    // Use a known viewport size without querying GL state.
    // Pixel-space ops must use drawable pixels; tile math stays on grid
    // cols/rows.
    const int viewport_w = viewport_state.viewport_width_px;
    const int viewport_h = viewport_state.viewport_height_px;
    const int has_viewport = viewport_state.has_viewport;
    const int is_double_buffered = (double_buffered != 0);
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl1_snake_frame_state_t snake_frame = {0};
        db_gl_upload_range_t
            snake_preview_local[BENCH_SNAKE_PHASE_WINDOW_TILES];
        snake_frame.preview_ranges = snake_preview_local;
        snake_frame.preview_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
        db_gl1_prepare_snake_frame_state(
            &snake_frame, is_double_buffered,
            g_state.runtime_flags.uses_dirty_backbuffer_mode, has_viewport);
        db_gl1_render_snake_draw_pass(
            &snake_frame, g_state.runtime_flags.uses_dirty_backbuffer_mode,
            viewport_w, viewport_h);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        db_gl1_render_gradient_frame(viewport_w, viewport_h,
                                     is_double_buffered);
    } else if (g_state.runtime_flags.is_bands != 0) {
        db_gl1_draw_bands_compact(db_grid_cols_effective(), BENCH_BANDS,
                                  frame_index, viewport_w, viewport_h);
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, BENCH_BANDS);
    }
    db_history_finalize_frame(&g_state.frame, &g_state.runtime,
                              db_grid_cols_effective(),
                              db_grid_rows_effective());
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached(
            g_state.buffers.vbo, &g_state.buffers.bound_array_buffer);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.buffers.vbo != 0U) {
        db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
        g_state.buffers.vbo = 0U;
    }
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    free(g_state.snake_replay.curr_upload_ranges);
    free(g_state.snake_replay.prev_upload_ranges);
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.snake_scratch.spans);
    free(g_state.snake_scratch.row_bounds);
    free(g_state.gradient_row_y_ndc);
    free(g_state.snake_color_state);
    db_gl_compact_vbo_free(&g_state.compact_vbo);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    if (g_state.capability_mode[0] == '\0') {
        db_gl1_refresh_capability_mode();
    }
    return g_state.capability_mode;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, 1);
}

uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void) {
    return g_state.frame.state_hash;
}

void db_renderer_opengl_gl1_5_gles1_1_draw_stats(uint64_t *full_draw_frames,
                                                 uint64_t *dirty_draw_frames) {
    db_history_copy_draw_stats(&g_state.frame, full_draw_frames,
                               dirty_draw_frames);
}
