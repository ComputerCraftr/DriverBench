#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define DB_ALPHA_U8 255U
#define DB_CAP_MODE_CPU_OFFSCREEN_BO "cpu_offscreen_bo"
#define DB_COLOR_SHIFT_A 24U
#define DB_COLOR_SHIFT_B 16U
#define DB_COLOR_SHIFT_G 8U
#define DB_COLOR_SHIFT_R 0U
#define DB_ROUND_HALF_UP_F 0.5F
#define DB_U8_MAX_F 255.0F

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels_rgba8;
} db_cpu_bo_t;

typedef struct {
    db_cpu_bo_t bos[2];
    db_dirty_row_range_t damage_rows[2];
    size_t damage_row_count;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    uint64_t state_hash;
    uint64_t frame_index;
    int history_mode;
    int history_read_index;
    int initialized;
    db_benchmark_runtime_init_t runtime;
} db_cpu_renderer_state_t;

static db_cpu_renderer_state_t g_state = {0};

static void db_cpu_set_full_damage(const db_cpu_bo_t *bo) {
    if ((bo == NULL) || (bo->height == 0U)) {
        g_state.damage_row_count = 0U;
        return;
    }
    g_state.damage_rows[0] = (db_dirty_row_range_t){
        .row_start = 0U,
        .row_count = bo->height,
    };
    g_state.damage_rows[1] = (db_dirty_row_range_t){0U, 0U};
    g_state.damage_row_count = 1U;
}

static void
db_cpu_set_damage_from_gradient_plan(const db_gradient_damage_plan_t *plan,
                                     uint32_t rows) {
    db_dirty_row_range_t ranges[2] = {{0U, 0U}, {0U, 0U}};
    const size_t count = db_gradient_collect_dirty_ranges(plan, ranges);
    g_state.damage_row_count = 0U;
    for (size_t i = 0U; (i < count) && (g_state.damage_row_count < 2U); i++) {
        if ((ranges[i].row_count == 0U) || (ranges[i].row_start >= rows)) {
            continue;
        }
        const uint32_t clamped_count =
            db_u32_min(ranges[i].row_count, rows - ranges[i].row_start);
        if (clamped_count == 0U) {
            continue;
        }
        g_state.damage_rows[g_state.damage_row_count++] =
            (db_dirty_row_range_t){
                .row_start = ranges[i].row_start,
                .row_count = clamped_count,
            };
    }
    if (g_state.damage_row_count == 0U) {
        db_cpu_set_full_damage(&g_state.bos[g_state.history_read_index]);
    }
}

static void db_cpu_set_damage_from_spans(const db_snake_col_span_t *spans,
                                         size_t span_count, uint32_t rows) {
    uint32_t row_min = rows;
    uint32_t row_max_exclusive = 0U;
    for (size_t i = 0U; i < span_count; i++) {
        if (spans[i].col_end <= spans[i].col_start) {
            continue;
        }
        if (spans[i].row >= rows) {
            continue;
        }
        row_min = db_u32_min(row_min, spans[i].row);
        row_max_exclusive = db_u32_max(row_max_exclusive, spans[i].row + 1U);
    }
    if ((row_max_exclusive <= row_min) || (row_min >= rows)) {
        g_state.damage_row_count = 0U;
        return;
    }
    g_state.damage_rows[0] = (db_dirty_row_range_t){
        .row_start = row_min,
        .row_count = row_max_exclusive - row_min,
    };
    g_state.damage_rows[1] = (db_dirty_row_range_t){0U, 0U};
    g_state.damage_row_count = 1U;
}

static uint32_t db_channel_to_u8(float value) {
    float clamped = value;
    if (clamped < 0.0F) {
        clamped = 0.0F;
    } else if (clamped > 1.0F) {
        clamped = 1.0F;
    }
    return (uint32_t)((clamped * DB_U8_MAX_F) + DB_ROUND_HALF_UP_F);
}

static uint32_t db_pack_rgb(float red, float green, float blue) {
    const uint32_t red_u8 = db_channel_to_u8(red);
    const uint32_t green_u8 = db_channel_to_u8(green);
    const uint32_t blue_u8 = db_channel_to_u8(blue);
    return (DB_ALPHA_U8 << DB_COLOR_SHIFT_A) | (blue_u8 << DB_COLOR_SHIFT_B) |
           (green_u8 << DB_COLOR_SHIFT_G) | (red_u8 << DB_COLOR_SHIFT_R);
}

static void db_unpack_rgb(uint32_t rgba, float *out_red, float *out_green,
                          float *out_blue) {
    *out_red = (float)((rgba >> DB_COLOR_SHIFT_R) & 255U) / DB_U8_MAX_F;
    *out_green = (float)((rgba >> DB_COLOR_SHIFT_G) & 255U) / DB_U8_MAX_F;
    *out_blue = (float)((rgba >> DB_COLOR_SHIFT_B) & 255U) / DB_U8_MAX_F;
}

static void db_bo_fill_solid(db_cpu_bo_t *bo, uint32_t rgba) {
    const uint64_t pixel_count = (uint64_t)bo->width * (uint64_t)bo->height;
    for (uint64_t idx = 0U; idx < pixel_count; idx++) {
        bo->pixels_rgba8[idx] = rgba;
    }
}

static void db_bo_copy(db_cpu_bo_t *dst, const db_cpu_bo_t *src) {
    const size_t pixel_count =
        (size_t)((uint64_t)dst->width * (uint64_t)dst->height);
    db_copy_u32_buffer(dst->pixels_rgba8, src->pixels_rgba8, pixel_count);
}

static size_t db_grid_index(uint32_t row, uint32_t col, uint32_t cols) {
    return ((size_t)row * (size_t)cols) + (size_t)col;
}

static void db_render_bands(db_cpu_bo_t *bo, double time_s) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        float band_red = 0.0F;
        float band_green = 0.0F;
        float band_blue = 0.0F;
        db_band_color_rgb(band, BENCH_BANDS, time_s, &band_red, &band_green,
                          &band_blue);
        const uint32_t color = db_pack_rgb(band_red, band_green, band_blue);
        const uint32_t x0 = (band * cols) / BENCH_BANDS;
        const uint32_t x1 = ((band + 1U) * cols) / BENCH_BANDS;
        for (uint32_t row = 0U; row < rows; row++) {
            const size_t row_base = (size_t)row * cols;
            for (uint32_t col = x0; col < x1; col++) {
                bo->pixels_rgba8[row_base + col] = color;
            }
        }
    }
}

static void
db_render_snake_step(db_cpu_bo_t *write_bo, const db_cpu_bo_t *read_bo,
                     const db_snake_plan_t *plan,
                     const db_snake_region_t *region, int apply_shape_clip,
                     const db_snake_shape_cache_t *shape_cache_ptr,
                     float target_red, float target_green, float target_blue,
                     int full_fill_on_phase_completed) {
    if ((plan == NULL) || (region == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }

    const uint32_t cols = write_bo->width;
    const uint32_t rows = write_bo->height;
    const uint32_t target_rgba =
        db_pack_rgb(target_red, target_green, target_blue);
    const db_snake_shape_cache_t *active_shape_cache =
        (apply_shape_clip != 0) ? shape_cache_ptr : NULL;
    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_bo_fill_solid(write_bo, target_rgba);
        return;
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
        const uint32_t row = tile_index / cols;
        const uint32_t col = tile_index % cols;
        if ((row >= rows) || (col >= cols)) {
            continue;
        }
        if (active_shape_cache != NULL) {
            const int inside = db_snake_shape_cache_contains_tile(
                active_shape_cache, row, col);
            if (inside == 0) {
                continue;
            }
        }
        write_bo->pixels_rgba8[db_grid_index(row, col, cols)] = target_rgba;
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
        const uint32_t row = tile_index / cols;
        const uint32_t col = tile_index % cols;
        if ((row >= rows) || (col >= cols)) {
            continue;
        }
        if (active_shape_cache != NULL) {
            const int inside = db_snake_shape_cache_contains_tile(
                active_shape_cache, row, col);
            if (inside == 0) {
                continue;
            }
        }
        const size_t idx = db_grid_index(row, col, cols);
        float prior_red = 0.0F;
        float prior_green = 0.0F;
        float prior_blue = 0.0F;
        db_unpack_rgb(read_bo->pixels_rgba8[idx], &prior_red, &prior_green,
                      &prior_blue);
        const float blend =
            db_window_blend_factor(update_index, plan->batch_size);
        float out_red = 0.0F;
        float out_green = 0.0F;
        float out_blue = 0.0F;
        db_blend_rgb(prior_red, prior_green, prior_blue, target_red,
                     target_green, target_blue, blend, &out_red, &out_green,
                     &out_blue);
        write_bo->pixels_rgba8[idx] = db_pack_rgb(out_red, out_green, out_blue);
    }
}

static void db_render_gradient(db_cpu_bo_t *bo, uint32_t head_row,
                               int direction_down, uint32_t cycle_index) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    for (uint32_t row = 0U; row < rows; row++) {
        float row_red = 0.0F;
        float row_green = 0.0F;
        float row_blue = 0.0F;
        db_gradient_row_color_rgb(row, head_row, direction_down, cycle_index,
                                  &row_red, &row_green, &row_blue);
        const uint32_t rgba = db_pack_rgb(row_red, row_green, row_blue);
        const size_t row_base = (size_t)row * cols;
        for (uint32_t col = 0U; col < cols; col++) {
            bo->pixels_rgba8[row_base + col] = rgba;
        }
    }
}

void db_renderer_cpu_renderer_init(void) {
    if (g_state.initialized != 0) {
        return;
    }

    db_benchmark_runtime_init_t init_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &init_state)) {
        db_failf(BACKEND_NAME, "cpu renderer init failed");
    }

    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint64_t pixel_count = (uint64_t)grid_cols * (uint64_t)grid_rows;
    if ((pixel_count == 0U) ||
        (pixel_count > ((uint64_t)SIZE_MAX / sizeof(uint32_t)))) {
        db_failf(BACKEND_NAME, "invalid offscreen BO size: %ux%u", grid_cols,
                 grid_rows);
    }

    db_cpu_bo_t bos[2] = {
        {.width = grid_cols,
         .height = grid_rows,
         .pixels_rgba8 =
             (uint32_t *)calloc((size_t)pixel_count, sizeof(uint32_t))},
        {.width = grid_cols,
         .height = grid_rows,
         .pixels_rgba8 =
             (uint32_t *)calloc((size_t)pixel_count, sizeof(uint32_t))},
    };
    if ((bos[0].pixels_rgba8 == NULL) || (bos[1].pixels_rgba8 == NULL)) {
        free(bos[0].pixels_rgba8);
        free(bos[1].pixels_rgba8);
        db_failf(BACKEND_NAME, "failed to allocate offscreen BOs");
    }

    const uint32_t phase0 = db_pack_rgb(
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    db_bo_fill_solid(&bos[0], phase0);
    db_bo_fill_solid(&bos[1], phase0);
    db_snake_shape_row_bounds_t *snake_row_bounds = NULL;
    size_t snake_row_bounds_capacity = 0U;
    if (init_state.pattern == DB_PATTERN_SNAKE_SHAPES) {
        if ((size_t)grid_rows >
            (SIZE_MAX / sizeof(db_snake_shape_row_bounds_t))) {
            free(bos[0].pixels_rgba8);
            free(bos[1].pixels_rgba8);
            db_failf(BACKEND_NAME, "invalid snake row-bounds size: %u",
                     grid_rows);
        }
        snake_row_bounds = (db_snake_shape_row_bounds_t *)calloc(
            (size_t)grid_rows, sizeof(*snake_row_bounds));
        if (snake_row_bounds == NULL) {
            free(bos[0].pixels_rgba8);
            free(bos[1].pixels_rgba8);
            db_failf(BACKEND_NAME,
                     "failed to allocate snake row-bounds scratch");
        }
        snake_row_bounds_capacity = (size_t)grid_rows;
    }

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.runtime = init_state;
    g_state.bos[0] = bos[0];
    g_state.bos[1] = bos[1];
    g_state.history_mode = db_pattern_uses_history_texture(init_state.pattern);
    g_state.history_read_index = 0;
    g_state.runtime.snake_shape_index = 0U;
    g_state.snake_row_bounds = snake_row_bounds;
    g_state.snake_row_bounds_capacity = snake_row_bounds_capacity;
}

void db_renderer_cpu_renderer_render_frame(double time_s) {
    if (g_state.initialized == 0) {
        return;
    }

    int write_index = 0;
    if (g_state.history_mode != 0) {
        write_index = (g_state.history_read_index == 0) ? 1 : 0;
        db_bo_copy(&g_state.bos[write_index],
                   &g_state.bos[g_state.history_read_index]);
    }

    db_cpu_bo_t *write_bo = &g_state.bos[write_index];
    const db_cpu_bo_t *read_bo = &g_state.bos[g_state.history_read_index];

    g_state.damage_row_count = 0U;
    if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        db_render_bands(write_bo, time_s);
        db_cpu_set_full_damage(write_bo);
    } else if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const int is_shapes =
            (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake_shape_index, g_state.runtime.snake_cursor,
            g_state.runtime.snake_prev_start, g_state.runtime.snake_prev_count,
            g_state.runtime.mode_phase_flag, g_state.runtime.bench_speed_step);
        const db_snake_plan_t plan = db_snake_plan_next_step(&request);
        const db_snake_step_target_t target = db_snake_step_target_from_plan(
            is_grid, g_state.runtime.pattern_seed, &plan);
        const db_snake_shape_kind_t shape_kind =
            (is_shapes != 0) ? target.shape_kind : DB_SNAKE_SHAPE_RECT;
        db_snake_shape_cache_t shape_cache = {0};
        const db_snake_shape_cache_t *shape_cache_ptr = NULL;
        if (is_shapes != 0) {
            if ((g_state.snake_row_bounds != NULL) &&
                (g_state.snake_row_bounds_capacity > 0U) &&
                (db_snake_shape_cache_init_from_index(
                     &shape_cache, g_state.snake_row_bounds,
                     g_state.snake_row_bounds_capacity,
                     g_state.runtime.pattern_seed, plan.active_shape_index,
                     DB_PALETTE_SALT, &target.region, shape_kind) != 0)) {
                shape_cache_ptr = &shape_cache;
            }
        }
        if (target.has_next_mode_phase_flag != 0) {
            g_state.runtime.mode_phase_flag = target.next_mode_phase_flag;
        }
        if (target.has_next_shape_index != 0) {
            g_state.runtime.snake_shape_index = target.next_shape_index;
        }
        db_render_snake_step(write_bo, read_bo, &plan, &target.region,
                             is_shapes, shape_cache_ptr, target.target_r,
                             target.target_g, target.target_b,
                             target.full_fill_on_phase_completed);
        if ((target.full_fill_on_phase_completed != 0) &&
            (plan.phase_completed != 0)) {
            db_cpu_set_full_damage(write_bo);
        } else {
            const size_t max_spans =
                (size_t)plan.prev_count + (size_t)plan.batch_size;
            db_snake_col_span_t spans[BENCH_SNAKE_PHASE_WINDOW_TILES * 2U];
            if (max_spans <= ((size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * 2U)) {
                const size_t span_count = db_snake_collect_damage_spans(
                    spans, max_spans, &target.region, plan.prev_start,
                    plan.prev_count, plan.active_cursor, plan.batch_size,
                    shape_cache_ptr);
                db_cpu_set_damage_from_spans(spans, span_count,
                                             write_bo->height);
            } else {
                db_cpu_set_full_damage(write_bo);
            }
        }
        g_state.runtime.snake_cursor = plan.next_cursor;
        g_state.runtime.snake_prev_start = plan.next_prev_start;
        g_state.runtime.snake_prev_count = plan.next_prev_count;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_step_t gradient_step = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient_head_row,
            g_state.runtime.mode_phase_flag, g_state.runtime.gradient_cycle,
            g_state.runtime.bench_speed_step);
        const db_gradient_damage_plan_t *plan = &gradient_step.plan;
        db_render_gradient(write_bo, plan->render_head_row,
                           gradient_step.render_direction_down,
                           plan->render_cycle_index);
        db_cpu_set_damage_from_gradient_plan(plan, write_bo->height);
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_step);
    } else {
        db_cpu_set_full_damage(write_bo);
    }

    if (g_state.history_mode != 0) {
        g_state.history_read_index = write_index;
    }
    g_state.state_hash =
        db_benchmark_runtime_state_hash(&g_state.runtime, g_state.frame_index,
                                        write_bo->width, write_bo->height);
    g_state.frame_index++;
}

const uint32_t *db_renderer_cpu_renderer_pixels_rgba8(uint32_t *out_width,
                                                      uint32_t *out_height) {
    if (g_state.initialized == 0) {
        return NULL;
    }
    if (out_width != NULL) {
        *out_width = g_state.bos[g_state.history_read_index].width;
    }
    if (out_height != NULL) {
        *out_height = g_state.bos[g_state.history_read_index].height;
    }
    return g_state.bos[g_state.history_read_index].pixels_rgba8;
}

uint32_t db_renderer_cpu_renderer_work_unit_count(void) {
    if (g_state.initialized == 0) {
        return 0U;
    }
    return g_state.runtime.work_unit_count;
}

const char *db_renderer_cpu_renderer_capability_mode(void) {
    return DB_CAP_MODE_CPU_OFFSCREEN_BO;
}

uint64_t db_renderer_cpu_renderer_state_hash(void) {
    return g_state.state_hash;
}

const db_dirty_row_range_t *
db_renderer_cpu_renderer_damage_rows(size_t *out_count) {
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (g_state.initialized == 0) {
        return NULL;
    }
    const size_t count = db_u32_min((uint32_t)g_state.damage_row_count, 2U);
    if (out_count != NULL) {
        *out_count = count;
    }
    return g_state.damage_rows;
}

void db_renderer_cpu_renderer_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    free(g_state.snake_row_bounds);
    free(g_state.bos[0].pixels_rgba8);
    free(g_state.bos[1].pixels_rgba8);
    g_state = (db_cpu_renderer_state_t){0};
}
