#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
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

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels_rgba8;
} db_cpu_bo_t;

typedef struct {
    db_cpu_bo_t bo;
    db_dirty_row_range_t damage_rows[2];
    size_t damage_row_count;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    uint64_t state_hash;
    uint32_t frame_index;
    int initialized;
    db_benchmark_runtime_init_t runtime;
} db_cpu_renderer_state_t;

typedef struct {
    db_cpu_bo_t *bo;
    uint32_t cols;
} db_cpu_gradient_row_apply_ctx_t;

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
        db_cpu_set_full_damage(&g_state.bo);
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

static uint32_t db_pack_rgb(float red, float green, float blue) {
    const uint32_t red_u8 = (uint32_t)db_float01_to_u8_clamped(red);
    const uint32_t green_u8 = (uint32_t)db_float01_to_u8_clamped(green);
    const uint32_t blue_u8 = (uint32_t)db_float01_to_u8_clamped(blue);
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

static size_t db_grid_index(uint32_t row, uint32_t col, uint32_t cols) {
    return ((size_t)row * (size_t)cols) + (size_t)col;
}

static void db_cpu_apply_gradient_row_color(uint32_t row, float row_red,
                                            float row_green, float row_blue,
                                            void *user_data) {
    db_cpu_gradient_row_apply_ctx_t *ctx =
        (db_cpu_gradient_row_apply_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->bo == NULL)) {
        return;
    }
    const uint32_t rgba = db_pack_rgb(row_red, row_green, row_blue);
    const size_t row_base = (size_t)row * ctx->cols;
    for (uint32_t col = 0U; col < ctx->cols; col++) {
        ctx->bo->pixels_rgba8[row_base + col] = rgba;
    }
}

static void db_render_bands(db_cpu_bo_t *bo, uint32_t frame_index) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        float band_red = 0.0F;
        float band_green = 0.0F;
        float band_blue = 0.0F;
        db_band_color_rgb(band, BENCH_BANDS, frame_index, &band_red,
                          &band_green, &band_blue);
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

static void db_render_snake_step(db_cpu_bo_t *bo, const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 const db_snake_shape_cache_t *shape_cache_ptr,
                                 float target_red, float target_green,
                                 float target_blue,
                                 int full_fill_on_phase_completed) {
    if ((plan == NULL) || (region == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }

    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    const uint32_t target_rgba =
        db_pack_rgb(target_red, target_green, target_blue);

    // Snapshot prior colors for the active blend window BEFORE we mutate the
    // buffer. This mirrors the GL1.5 path and is required for determinism.
    float prior_rgb[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0F};

    const uint32_t max_batch = BENCH_SNAKE_PHASE_WINDOW_TILES;
    const uint32_t batch_limit =
        (plan->batch_size <= max_batch) ? plan->batch_size : max_batch;

    for (uint32_t update_index = 0U; update_index < batch_limit;
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
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }
        const size_t idx = db_grid_index(row, col, cols);
        float pr = 0.0F;
        float pg = 0.0F;
        float pb = 0.0F;
        db_unpack_rgb(bo->pixels_rgba8[idx], &pr, &pg, &pb);
        const size_t base = (size_t)update_index * 3U;
        prior_rgb[base + 0U] = pr;
        prior_rgb[base + 1U] = pg;
        prior_rgb[base + 2U] = pb;
    }

    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_bo_fill_solid(bo, target_rgba);
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
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }
        bo->pixels_rgba8[db_grid_index(row, col, cols)] = target_rgba;
    }

    // Blend the active window using the snapshotted prior colors.
    for (uint32_t update_index = 0U; update_index < batch_limit;
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
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }

        const size_t prior_base = (size_t)update_index * 3U;
        const float prior_red = prior_rgb[prior_base + 0U];
        const float prior_green = prior_rgb[prior_base + 1U];
        const float prior_blue = prior_rgb[prior_base + 2U];

        const float blend =
            db_window_blend_factor(update_index, plan->batch_size);
        float out_red = 0.0F;
        float out_green = 0.0F;
        float out_blue = 0.0F;
        db_blend_rgb(prior_red, prior_green, prior_blue, target_red,
                     target_green, target_blue, blend, &out_red, &out_green,
                     &out_blue);

        bo->pixels_rgba8[db_grid_index(row, col, cols)] =
            db_pack_rgb(out_red, out_green, out_blue);
    }
}

static void db_render_gradient(db_cpu_bo_t *bo, uint32_t head_row,
                               int direction_down, uint32_t cycle_index) {
    db_cpu_gradient_row_apply_ctx_t apply_ctx = {
        .bo = bo,
        .cols = bo->width,
    };
    db_for_each_gradient_row_color(0U, bo->height, head_row, direction_down,
                                   cycle_index, db_cpu_apply_gradient_row_color,
                                   &apply_ctx);
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

    db_cpu_bo_t bo = {
        .width = grid_cols,
        .height = grid_rows,
        .pixels_rgba8 = (uint32_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "pixels_rgba8", (size_t)pixel_count,
            sizeof(uint32_t)),
    };
    if (bo.pixels_rgba8 == NULL) {
        db_failf(BACKEND_NAME, "failed to allocate offscreen BO");
    }

    const uint32_t phase0 = db_pack_rgb(
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    db_bo_fill_solid(&bo, phase0);
    db_snake_shape_row_bounds_t *snake_row_bounds = NULL;
    size_t snake_row_bounds_capacity = 0U;
    if (init_state.pattern == DB_PATTERN_SNAKE_SHAPES) {
        snake_row_bounds =
            (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_row_bounds", (size_t)grid_rows,
                sizeof(*snake_row_bounds));
        snake_row_bounds_capacity = (size_t)grid_rows;
    }

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.runtime = init_state;
    g_state.bo = bo;
    g_state.runtime.snake.shape_index = 0U;
    g_state.snake_row_bounds = snake_row_bounds;
    g_state.snake_row_bounds_capacity = snake_row_bounds_capacity;
}

void db_renderer_cpu_renderer_render_frame(uint32_t frame_index) {
    if (g_state.initialized == 0) {
        return;
    }

    db_cpu_bo_t *write_bo = &g_state.bo;

    g_state.damage_row_count = 0U;
    if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        db_render_bands(write_bo, frame_index);
        db_cpu_set_full_damage(write_bo);
    } else if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const int is_shapes =
            (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake.shape_index, g_state.runtime.snake.cursor,
            g_state.runtime.snake.prev_start, g_state.runtime.snake.prev_count,
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
                     DB_U32_SALT_PALETTE, &target.region, shape_kind) != 0)) {
                shape_cache_ptr = &shape_cache;
            }
        }
        if (target.has_next_mode_phase_flag != 0) {
            g_state.runtime.mode_phase_flag = target.next_mode_phase_flag;
        }
        if (target.has_next_shape_index != 0) {
            g_state.runtime.snake.shape_index = target.next_shape_index;
        }
        db_render_snake_step(write_bo, &plan, &target.region, shape_cache_ptr,
                             target.target_r, target.target_g, target.target_b,
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
        g_state.runtime.snake.cursor = plan.next_cursor;
        g_state.runtime.snake.prev_start = plan.next_prev_start;
        g_state.runtime.snake.prev_count = plan.next_prev_count;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_damage_plan_t plan = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient.head_row,
            g_state.runtime.mode_phase_flag,
            g_state.runtime.gradient.cycle_index,
            g_state.runtime.bench_speed_step);
        db_render_gradient(write_bo, plan.render_state.head_row,
                           plan.render_state.direction_down,
                           plan.render_state.cycle_index);
        db_cpu_set_damage_from_gradient_plan(&plan, write_bo->height);
        db_gradient_apply_step_to_runtime(&g_state.runtime, &plan);
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
        *out_width = g_state.bo.width;
    }
    if (out_height != NULL) {
        *out_height = g_state.bo.height;
    }
    return g_state.bo.pixels_rgba8;
}

uint32_t db_renderer_cpu_renderer_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, g_state.initialized);
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
    free(g_state.bo.pixels_rgba8);
    g_state = (db_cpu_renderer_state_t){0};
}
