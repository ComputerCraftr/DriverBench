#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H

#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "../core/db_core.h"
#include "../displays/bench_config.h"

#define DB_RECT_VERTEX_COUNT 6U
#define DB_VERTEX_POSITION_FLOAT_COUNT 2U
#define DB_VERTEX_COLOR_FLOAT_COUNT 3U
#define DB_VERTEX_FLOAT_STRIDE                                                 \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_VERTEX_COLOR_FLOAT_COUNT)
#define DB_ES_VERTEX_COLOR_FLOAT_COUNT 4U
#define DB_ES_VERTEX_FLOAT_STRIDE                                              \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_ES_VERTEX_COLOR_FLOAT_COUNT)
#define DB_BENCHMARK_MODE_ENV "DRIVERBENCH_BENCHMARK_MODE"
#define DB_RANDOM_SEED_ENV "DRIVERBENCH_RANDOM_SEED"
#define DB_BENCH_COMMON_BACKEND "renderer_benchmark_common"
#define DB_BENCHMARK_MODE_BANDS "bands"
#define DB_BENCHMARK_MODE_SNAKE_GRID "snake_grid"
#define DB_BENCHMARK_MODE_GRADIENT_SWEEP "gradient_sweep"
#define DB_BENCHMARK_MODE_GRADIENT_FILL "gradient_fill"
#define DB_BENCHMARK_MODE_RECT_SNAKE "rect_snake"
#define DB_GRADIENT_SWEEP_WINDOW_ROWS 32U
#define DB_GRADIENT_FILL_WINDOW_ROWS 32U
#define DB_HASH_MIX_SHIFT_A 16U
#define DB_HASH_MIX_SHIFT_B 15U
#define DB_HASH_MIX_MUL_A 0x7FEB352DU
#define DB_HASH_MIX_MUL_B 0x846CA68BU
#define DB_SEED_COMMON_GOLDEN_RATIO 0x9E3779B9U
#define DB_SEED_COMMON_COLOR_R 0x27D4EB2FU
#define DB_SEED_COMMON_COLOR_G 0x165667B1U
#define DB_SEED_COMMON_COLOR_B 0x85EBCA77U
#define DB_RECT_SNAKE_SEED_STEP 0x85EBCA77U
#define DB_RECT_SNAKE_SEED_RECT_W 0xA511E9B3U
#define DB_RECT_SNAKE_SEED_RECT_H 0x63D83595U
#define DB_RECT_SNAKE_SEED_RECT_X DB_SEED_COMMON_GOLDEN_RATIO
#define DB_RECT_SNAKE_SEED_RECT_Y 0xC2B2AE35U
#define DB_RECT_SNAKE_MIN_DIM_THRESHOLD 16U
#define DB_RECT_SNAKE_MIN_DIM_LARGE 8U
#define DB_RECT_SNAKE_MIN_DIM_SMALL 1U
#define DB_RECT_SNAKE_MAX_DIM_DIVISOR 3U
#define DB_RECT_SNAKE_COLOR_BIAS 0.25F
#define DB_RECT_SNAKE_COLOR_SCALE 0.70F
#define DB_RECT_SNAKE_SEED_I32_MASK 0x7FFFFFFFU
#define DB_GRADIENT_FILL_COLOR_BIAS 0.20F
#define DB_GRADIENT_FILL_COLOR_SCALE 0.75F
#define DB_GRADIENT_FILL_SEED_BASE_STEP DB_SEED_COMMON_GOLDEN_RATIO
#define DB_GRADIENT_FILL_SEED_PHASE_A 0xA511E9B3U
#define DB_GRADIENT_FILL_SEED_PHASE_B 0x63D83595U

typedef enum {
    DB_PATTERN_GRADIENT_SWEEP = 0,
    DB_PATTERN_BANDS = 1,
    DB_PATTERN_SNAKE_GRID = 2,
    DB_PATTERN_GRADIENT_FILL = 3,
    DB_PATTERN_RECT_SNAKE = 4,
} db_pattern_t;

typedef enum {
    DB_RENDER_MODE_GRADIENT_SWEEP = 0,
    DB_RENDER_MODE_BANDS = 1,
    DB_RENDER_MODE_SNAKE_GRID = 2,
    DB_RENDER_MODE_GRADIENT_FILL = 3,
    DB_RENDER_MODE_RECT_SNAKE = 4,
} db_render_mode_t;

typedef struct {
    uint32_t active_cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int clearing_phase;
    int phase_completed;
    uint32_t next_cursor;
    uint32_t next_prev_start;
    uint32_t next_prev_count;
    int next_clearing_phase;
} db_snake_damage_plan_t;

typedef struct {
    uint32_t render_head_row;
    int render_direction_down;
    uint32_t render_cycle_index;
    uint32_t next_head_row;
    int next_direction_down;
    uint32_t next_cycle_index;
    uint32_t dirty_row_start;
    uint32_t dirty_row_count;
} db_gradient_sweep_damage_plan_t;

typedef struct {
    uint32_t render_head_row;
    uint32_t render_cycle_index;
    uint32_t next_head_row;
    uint32_t next_cycle_index;
    uint32_t dirty_row_start;
    uint32_t dirty_row_count;
} db_gradient_fill_damage_plan_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float color_r;
    float color_g;
    float color_b;
} db_rect_snake_rect_t;

typedef struct {
    uint32_t active_rect_index;
    uint32_t active_cursor;
    uint32_t batch_size;
    uint32_t rect_tile_count;
    int rect_completed;
    uint32_t next_rect_index;
    uint32_t next_cursor;
    int wrapped;
} db_rect_snake_plan_t;

typedef struct {
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
} db_snake_col_span_t;

typedef struct {
    float *vertices;
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t snake_batch_size;
    int snake_phase_completed;
    int snake_clearing_phase;
    uint32_t gradient_head_row;
    int gradient_sweep_direction_down;
    uint32_t gradient_sweep_cycle;
    uint32_t gradient_fill_cycle;
    uint32_t random_seed;
    uint32_t pattern_seed;
} db_pattern_vertex_init_t;

static inline uint32_t db_grid_rows_effective(void) {
    return (uint32_t)BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_grid_cols_effective(void) {
    return (uint32_t)BENCH_WINDOW_WIDTH_PX;
}

static inline int
db_parse_benchmark_pattern_from_env(db_pattern_t *out_pattern) {
    const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
    if ((mode == NULL) ||
        (strcmp(mode, DB_BENCHMARK_MODE_GRADIENT_SWEEP) == 0)) {
        *out_pattern = DB_PATTERN_GRADIENT_SWEEP;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_BANDS) == 0) {
        *out_pattern = DB_PATTERN_BANDS;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_SNAKE_GRID) == 0) {
        *out_pattern = DB_PATTERN_SNAKE_GRID;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_GRADIENT_FILL) == 0) {
        *out_pattern = DB_PATTERN_GRADIENT_FILL;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_RECT_SNAKE) == 0) {
        *out_pattern = DB_PATTERN_RECT_SNAKE;
        return 1;
    }
    *out_pattern = DB_PATTERN_GRADIENT_SWEEP;
    return 0;
}

static inline const char *db_pattern_mode_name(db_pattern_t pattern) {
    switch (pattern) {
    case DB_PATTERN_GRADIENT_SWEEP:
        return DB_BENCHMARK_MODE_GRADIENT_SWEEP;
    case DB_PATTERN_BANDS:
        return DB_BENCHMARK_MODE_BANDS;
    case DB_PATTERN_SNAKE_GRID:
        return DB_BENCHMARK_MODE_SNAKE_GRID;
    case DB_PATTERN_GRADIENT_FILL:
        return DB_BENCHMARK_MODE_GRADIENT_FILL;
    case DB_PATTERN_RECT_SNAKE:
        return DB_BENCHMARK_MODE_RECT_SNAKE;
    default:
        return "unknown";
    }
}

static inline int db_pattern_uses_history_texture(db_pattern_t pattern) {
    return (pattern == DB_PATTERN_SNAKE_GRID) ||
           (pattern == DB_PATTERN_RECT_SNAKE);
}

static inline uint32_t db_pattern_work_unit_count(db_pattern_t pattern) {
    if ((pattern == DB_PATTERN_SNAKE_GRID) ||
        (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (pattern == DB_PATTERN_GRADIENT_FILL) ||
        (pattern == DB_PATTERN_RECT_SNAKE)) {
        const uint32_t rows = db_grid_rows_effective();
        const uint32_t cols = db_grid_cols_effective();
        const uint64_t count = (uint64_t)rows * cols;
        if ((count == 0U) || (count > UINT32_MAX)) {
            return 0U;
        }
        return (uint32_t)count;
    }
    return BENCH_BANDS;
}

static inline uint32_t db_grid_tile_index_from_step(uint32_t step) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t row = step / cols;
    const uint32_t col_step = step % cols;
    const uint32_t col = ((row & 1U) == 0U) ? col_step : (cols - 1U - col_step);
    return (row * cols) + col;
}

static inline void db_grid_tile_bounds_ndc(uint32_t tile_index, float *x0,
                                           float *y0, float *x1, float *y1) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    const float inv_cols = 1.0F / (float)cols;
    const float inv_rows = 1.0F / (float)rows;

    *x0 = (2.0F * (float)col * inv_cols) - 1.0F;
    *x1 = (2.0F * (float)(col + 1U) * inv_cols) - 1.0F;
    *y1 = 1.0F - (2.0F * (float)row * inv_rows);
    *y0 = 1.0F - (2.0F * (float)(row + 1U) * inv_rows);
}

static inline void db_fill_rect_unit_pos(float *unit_base, float x0, float y0,
                                         float x1, float y1,
                                         size_t stride_floats) {
    // Triangle 1
    unit_base[0] = x0;
    unit_base[1] = y0;
    unit_base[stride_floats] = x1;
    unit_base[stride_floats + 1U] = y0;
    unit_base[2U * stride_floats] = x1;
    unit_base[(2U * stride_floats) + 1U] = y1;

    // Triangle 2
    unit_base[3U * stride_floats] = x0;
    unit_base[(3U * stride_floats) + 1U] = y0;
    unit_base[4U * stride_floats] = x1;
    unit_base[(4U * stride_floats) + 1U] = y1;
    unit_base[5U * stride_floats] = x0;
    unit_base[(5U * stride_floats) + 1U] = y1;
}

static inline void db_set_rect_unit_rgb(float *unit_base, size_t stride_floats,
                                        size_t color_offset_floats,
                                        float color_r, float color_g,
                                        float color_b) {
    float *color = unit_base + color_offset_floats;
    for (uint32_t v = 0; v < DB_RECT_VERTEX_COUNT; v++) {
        color[0] = color_r;
        color[1] = color_g;
        color[2] = color_b;
        color += stride_floats;
    }
}

static inline void db_band_color_rgb(uint32_t band_index, uint32_t band_count,
                                     double time_s, float *out_r, float *out_g,
                                     float *out_b) {
    const float band_f = (float)band_index;
    const float pulse =
        BENCH_PULSE_BASE_F +
        (BENCH_PULSE_AMP_F * sinf((float)((time_s * BENCH_PULSE_FREQ_F) +
                                          (band_f * BENCH_PULSE_PHASE_F))));
    const float color_r =
        pulse * (BENCH_COLOR_R_BASE_F +
                 (BENCH_COLOR_R_SCALE_F * band_f / (float)band_count));
    *out_r = color_r;
    *out_g = pulse * BENCH_COLOR_G_SCALE_F;
    *out_b = 1.0F - color_r;
}

static inline uint32_t db_snake_grid_tiles_per_step(uint32_t work_unit_count) {
    if (work_unit_count == 0U) {
        return 1U;
    }
    uint32_t tiles_per_step = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if (tiles_per_step == 0U) {
        tiles_per_step = 1U;
    }
    if (tiles_per_step > work_unit_count) {
        tiles_per_step = work_unit_count;
    }
    return tiles_per_step;
}

static inline uint32_t db_mix_u32(uint32_t value) {
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    value *= DB_HASH_MIX_MUL_A;
    value ^= value >> DB_HASH_MIX_SHIFT_B;
    value *= DB_HASH_MIX_MUL_B;
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    return value;
}

static inline uint32_t db_u32_range(uint32_t seed, uint32_t min_value,
                                    uint32_t max_value) {
    if (max_value <= min_value) {
        return min_value;
    }
    return min_value + (seed % (max_value - min_value + 1U));
}

static inline float db_rect_snake_color_channel(uint32_t seed) {
    const float normalized = (float)(seed & 255U) / 255.0F;
    return DB_RECT_SNAKE_COLOR_BIAS + (normalized * DB_RECT_SNAKE_COLOR_SCALE);
}

static inline float db_palette_color_channel(uint32_t seed) {
    const float normalized = (float)(seed & 255U) / 255.0F;
    return DB_GRADIENT_FILL_COLOR_BIAS +
           (normalized * DB_GRADIENT_FILL_COLOR_SCALE);
}

static inline void db_palette_cycle_color_rgb(uint32_t cycle_index,
                                              float *out_r, float *out_g,
                                              float *out_b) {
    const uint32_t phase_seed = DB_GRADIENT_FILL_SEED_PHASE_A;
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_GRADIENT_FILL_SEED_BASE_STEP) ^ phase_seed);
    *out_r = db_palette_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_R));
    *out_g = db_palette_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_G));
    *out_b = db_palette_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_B));
}

static inline uint32_t db_pattern_seed_from_time(void) {
    const time_t now = time(NULL);
    if (now == (time_t)-1) {
        db_failf(DB_BENCH_COMMON_BACKEND, "time() failed for random seed");
    }
    const uint32_t raw = db_fold_u64_to_u32((uint64_t)now);
    const uint32_t salted = raw ^ DB_SEED_COMMON_GOLDEN_RATIO;
    return db_mix_u32(salted) & DB_RECT_SNAKE_SEED_I32_MASK;
}

static inline uint32_t db_benchmark_cycle_from_seed(uint32_t seed,
                                                    uint32_t salt) {
    return db_mix_u32(seed ^ salt);
}

static inline uint32_t
db_benchmark_random_seed_from_env_or_time(const char *backend_name) {
    const char *value = getenv(DB_RANDOM_SEED_ENV);
    if ((value == NULL) || (value[0] == '\0')) {
        return db_pattern_seed_from_time();
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 0);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        db_failf(backend_name, "Invalid %s='%s'", DB_RANDOM_SEED_ENV, value);
    }
    return (uint32_t)parsed;
}

static inline db_rect_snake_rect_t
db_rect_snake_rect_from_index(uint32_t seed, uint32_t rect_index) {
    db_rect_snake_rect_t rect = {0};
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t cols = db_grid_cols_effective();
    if ((rows == 0U) || (cols == 0U)) {
        return rect;
    }

    const uint32_t seed_base =
        db_mix_u32(seed + (rect_index * DB_RECT_SNAKE_SEED_STEP) + 1U);
    const uint32_t min_w = (cols >= DB_RECT_SNAKE_MIN_DIM_THRESHOLD)
                               ? DB_RECT_SNAKE_MIN_DIM_LARGE
                               : DB_RECT_SNAKE_MIN_DIM_SMALL;
    const uint32_t min_h = (rows >= DB_RECT_SNAKE_MIN_DIM_THRESHOLD)
                               ? DB_RECT_SNAKE_MIN_DIM_LARGE
                               : DB_RECT_SNAKE_MIN_DIM_SMALL;
    const uint32_t max_w = db_u32_max(
        min_w, db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "rect_max_w",
                                  cols / DB_RECT_SNAKE_MAX_DIM_DIVISOR, min_w));
    const uint32_t max_h = db_u32_max(
        min_h, db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "rect_max_h",
                                  rows / DB_RECT_SNAKE_MAX_DIM_DIVISOR, min_h));
    rect.width = db_u32_range(db_mix_u32(seed_base ^ DB_RECT_SNAKE_SEED_RECT_W),
                              min_w, db_u32_min(max_w, cols));
    rect.height =
        db_u32_range(db_mix_u32(seed_base ^ DB_RECT_SNAKE_SEED_RECT_H), min_h,
                     db_u32_min(max_h, rows));
    const uint32_t max_x = db_u32_saturating_sub(cols, rect.width);
    const uint32_t max_y = db_u32_saturating_sub(rows, rect.height);
    rect.x = db_u32_range(db_mix_u32(seed_base ^ DB_RECT_SNAKE_SEED_RECT_X), 0U,
                          max_x);
    rect.y = db_u32_range(db_mix_u32(seed_base ^ DB_RECT_SNAKE_SEED_RECT_Y), 0U,
                          max_y);
    rect.color_r = db_rect_snake_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_R));
    rect.color_g = db_rect_snake_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_G));
    rect.color_b = db_rect_snake_color_channel(
        db_mix_u32(seed_base ^ DB_SEED_COMMON_COLOR_B));
    return rect;
}

static inline uint32_t
db_rect_snake_tile_index_from_step(const db_rect_snake_rect_t *rect,
                                   uint32_t step) {
    if ((rect == NULL) || (rect->width == 0U) || (rect->height == 0U)) {
        return 0U;
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t local_row = step / rect->width;
    const uint32_t local_col_step = step % rect->width;
    const uint32_t local_col = ((local_row & 1U) == 0U)
                                   ? local_col_step
                                   : ((rect->width - 1U) - local_col_step);
    return ((rect->y + local_row) * cols) + (rect->x + local_col);
}

static inline void db_snake_append_step_spans_for_rect(
    db_snake_col_span_t *spans, size_t max_spans, size_t *inout_span_count,
    uint32_t rect_x, uint32_t rect_y, uint32_t rect_cols, uint32_t rect_rows,
    uint32_t step_start, uint32_t step_count) {
    if ((spans == NULL) || (inout_span_count == NULL) || (rect_cols == 0U) ||
        (rect_rows == 0U) || (step_count == 0U)) {
        return;
    }

    uint32_t remaining = step_count;
    uint32_t step_cursor = step_start;
    while (remaining > 0U) {
        const uint32_t local_row = step_cursor / rect_cols;
        if (local_row >= rect_rows) {
            return;
        }
        const uint32_t local_col_step = step_cursor % rect_cols;
        const uint32_t steps_left_in_row = rect_cols - local_col_step;
        const uint32_t chunk_steps = db_u32_min(remaining, steps_left_in_row);
        uint32_t first_local_col = 0U;
        if ((local_row & 1U) == 0U) {
            first_local_col = local_col_step;
        } else {
            first_local_col =
                (rect_cols - 1U) - (local_col_step + chunk_steps - 1U);
        }

        if (*inout_span_count >= max_spans) {
            return;
        }
        spans[*inout_span_count] = (db_snake_col_span_t){
            .row = rect_y + local_row,
            .col_start = rect_x + first_local_col,
            .col_end = rect_x + first_local_col + chunk_steps,
        };
        (*inout_span_count)++;
        step_cursor += chunk_steps;
        remaining -= chunk_steps;
    }
}

static inline db_rect_snake_plan_t
db_rect_snake_plan_next_step(uint32_t seed, uint32_t rect_index,
                             uint32_t rect_cursor) {
    db_rect_snake_plan_t plan = {0};
    const uint32_t rect_index_modulus = (uint32_t)INT32_MAX;
    plan.active_rect_index = rect_index % rect_index_modulus;
    plan.active_cursor = rect_cursor;
    const db_rect_snake_rect_t rect =
        db_rect_snake_rect_from_index(seed, plan.active_rect_index);
    const uint32_t rect_tile_count = rect.width * rect.height;
    plan.rect_tile_count = rect_tile_count;
    if (rect_tile_count == 0U) {
        return plan;
    }

    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(rect_tile_count);
    plan.batch_size = tiles_per_step;
    plan.rect_completed = (plan.active_cursor >= rect_tile_count) ? 1 : 0;
    plan.next_rect_index = plan.active_rect_index;
    plan.wrapped = 0;
    if (plan.rect_completed != 0) {
        plan.next_cursor = 0U;
        plan.next_rect_index = plan.active_rect_index + 1U;
        if (plan.next_rect_index >= rect_index_modulus) {
            plan.next_rect_index = 0U;
            plan.wrapped = 1;
        }
    } else {
        plan.next_cursor = plan.active_cursor + 1U;
        if (plan.next_cursor > rect_tile_count) {
            plan.next_cursor = rect_tile_count;
        }
    }
    return plan;
}

static inline uint32_t db_snake_grid_step_batch_size(uint32_t cursor,
                                                     uint32_t work_unit_count,
                                                     uint32_t tiles_per_step) {
    if ((work_unit_count == 0U) || (cursor >= work_unit_count)) {
        return 0U;
    }
    const uint32_t remaining = work_unit_count - cursor;
    return db_u32_min(tiles_per_step, remaining);
}

static inline db_snake_damage_plan_t
db_snake_grid_plan_next_step(uint32_t snake_cursor, uint32_t snake_prev_start,
                             uint32_t snake_prev_count, int clearing_phase,
                             uint32_t work_unit_count) {
    db_snake_damage_plan_t plan = {0};
    plan.active_cursor = snake_cursor;
    plan.prev_start = snake_prev_start;
    plan.prev_count = snake_prev_count;
    plan.clearing_phase = clearing_phase;

    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(work_unit_count);
    plan.batch_size = tiles_per_step;
    plan.phase_completed = (snake_cursor >= work_unit_count) ? 1 : 0;

    plan.next_prev_start = snake_cursor;
    plan.next_prev_count = plan.phase_completed ? 0U : plan.batch_size;
    plan.next_clearing_phase = clearing_phase;
    if (plan.phase_completed != 0) {
        plan.next_cursor = 0U;
        plan.next_clearing_phase = !clearing_phase;
    } else {
        plan.next_cursor = snake_cursor + 1U;
        if (plan.next_cursor > work_unit_count) {
            plan.next_cursor = work_unit_count;
        }
    }
    return plan;
}

static inline float db_window_blend_factor(uint32_t window_index,
                                           uint32_t window_size) {
    const uint32_t span = db_u32_max(window_size, 1U);
    if (span <= 1U) {
        return 1.0F;
    }
    return (float)((span - 1U) - window_index) / (float)(span - 1U);
}

static inline void db_grid_target_color_rgb(int clearing_phase, float *out_r,
                                            float *out_g, float *out_b) {
    if (clearing_phase != 0) {
        *out_r = BENCH_GRID_PHASE0_R;
        *out_g = BENCH_GRID_PHASE0_G;
        *out_b = BENCH_GRID_PHASE0_B;
        return;
    }
    *out_r = BENCH_GRID_PHASE1_R;
    *out_g = BENCH_GRID_PHASE1_G;
    *out_b = BENCH_GRID_PHASE1_B;
}

static inline void db_blend_rgb(float prior_r, float prior_g, float prior_b,
                                float target_r, float target_g, float target_b,
                                float blend_factor, float *out_r, float *out_g,
                                float *out_b) {
    if (blend_factor <= 0.0F) {
        *out_r = prior_r;
        *out_g = prior_g;
        *out_b = prior_b;
        return;
    }
    if (blend_factor >= 1.0F) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }
    *out_r = prior_r + ((target_r - prior_r) * blend_factor);
    *out_g = prior_g + ((target_g - prior_g) * blend_factor);
    *out_b = prior_b + ((target_b - prior_b) * blend_factor);
}

static inline void db_snake_grid_target_color_rgb(int clearing_phase,
                                                  float *out_r, float *out_g,
                                                  float *out_b) {
    db_grid_target_color_rgb(clearing_phase, out_r, out_g, out_b);
}

static inline void db_snake_grid_window_color_rgb(uint32_t window_index,
                                                  uint32_t window_size,
                                                  int clearing_phase,
                                                  float *out_r, float *out_g,
                                                  float *out_b) {
    const float blend_factor =
        db_window_blend_factor(window_index, window_size);
    const float from_r =
        clearing_phase ? BENCH_GRID_PHASE1_R : BENCH_GRID_PHASE0_R;
    const float from_g =
        clearing_phase ? BENCH_GRID_PHASE1_G : BENCH_GRID_PHASE0_G;
    const float from_b =
        clearing_phase ? BENCH_GRID_PHASE1_B : BENCH_GRID_PHASE0_B;
    float to_r = 0.0F;
    float to_g = 0.0F;
    float to_b = 0.0F;
    db_grid_target_color_rgb(clearing_phase, &to_r, &to_g, &to_b);
    db_blend_rgb(from_r, from_g, from_b, to_r, to_g, to_b, blend_factor, out_r,
                 out_g, out_b);
}

static inline uint32_t db_gradient_sweep_window_rows_effective(void) {
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return 1U;
    }
    return db_u32_min(rows, DB_GRADIENT_SWEEP_WINDOW_ROWS);
}

static inline db_gradient_sweep_damage_plan_t
db_gradient_sweep_plan_next_frame(uint32_t head_row, int direction_down,
                                  uint32_t cycle_index) {
    db_gradient_sweep_damage_plan_t plan = {0};
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return plan;
    }

    const uint32_t window_rows = db_gradient_sweep_window_rows_effective();
    const uint32_t max_head = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_sweep_max_head", rows, window_rows);
    const uint32_t prev_head = head_row;
    int next_direction_down = (direction_down != 0) ? 1 : 0;
    uint32_t next_cycle = cycle_index;
    uint32_t next_head = head_row;
    if (next_direction_down != 0) {
        if (head_row >= max_head) {
            next_direction_down = 0;
            next_head = db_u32_saturating_sub(max_head, 1U);
            next_cycle =
                db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                   "gradient_sweep_cycle_next", next_cycle, 1U);
        } else {
            next_head =
                db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                   "gradient_sweep_head_next", head_row, 1U);
        }
    } else {
        if (head_row == 0U) {
            next_direction_down = 1;
            next_head = db_u32_min(max_head, 1U);
            next_cycle =
                db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                   "gradient_sweep_cycle_next", next_cycle, 1U);
        } else {
            next_head =
                db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                   "gradient_sweep_head_prev", head_row, 1U);
        }
    }

    const uint32_t prev_head_start =
        db_u32_saturating_sub(prev_head, window_rows);
    const uint32_t next_head_start =
        db_u32_saturating_sub(next_head, window_rows);
    const uint32_t prev_head_end = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_sweep_prev_head_end",
        prev_head_start, window_rows);
    const uint32_t next_head_end = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_sweep_next_head_end",
        next_head_start, window_rows);

    const uint32_t dirty_start = db_u32_min(prev_head_start, next_head_start);
    const uint32_t dirty_end =
        db_u32_min(db_u32_max(prev_head_end, next_head_end), rows);
    if (dirty_end > dirty_start) {
        plan.dirty_row_start = dirty_start;
        plan.dirty_row_count = db_checked_sub_u32(
            DB_BENCH_COMMON_BACKEND, "gradient_sweep_dirty_row_count",
            dirty_end, dirty_start);
    }

    plan.render_head_row = next_head;
    plan.render_direction_down = next_direction_down;
    plan.render_cycle_index = next_cycle;
    plan.next_head_row = next_head;
    plan.next_direction_down = next_direction_down;
    plan.next_cycle_index = next_cycle;
    return plan;
}

static inline void
db_gradient_sweep_row_color_rgb(uint32_t row_index, uint32_t head_row,
                                int direction_down, uint32_t cycle_index,
                                float *out_r, float *out_g, float *out_b) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_sweep_window_rows_effective();
    float source_r = 0.0F;
    float source_g = 0.0F;
    float source_b = 0.0F;
    float target_r = 0.0F;
    float target_g = 0.0F;
    float target_b = 0.0F;
    db_palette_cycle_color_rgb(cycle_index, &source_r, &source_g, &source_b);
    db_palette_cycle_color_rgb(cycle_index + 1U, &target_r, &target_g,
                               &target_b);
    if ((rows == 0U) || (window_rows == 0U)) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }

    const uint32_t head_start = db_u32_saturating_sub(head_row, window_rows);
    const uint32_t head_end =
        db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_sweep_head_end",
                           head_start, window_rows);
    if (row_index < head_start) {
        if (direction_down != 0) {
            *out_r = target_r;
            *out_g = target_g;
            *out_b = target_b;
        } else {
            *out_r = source_r;
            *out_g = source_g;
            *out_b = source_b;
        }
        return;
    }
    if (row_index >= head_end) {
        if (direction_down != 0) {
            *out_r = source_r;
            *out_g = source_g;
            *out_b = source_b;
        } else {
            *out_r = target_r;
            *out_g = target_g;
            *out_b = target_b;
        }
        return;
    }
    const uint32_t delta = row_index - head_start;

    float blend = 1.0F;
    if (window_rows > 1U) {
        const float blend_t = (float)delta / (float)(window_rows - 1U);
        blend = (direction_down != 0) ? (1.0F - blend_t) : blend_t;
    }

    db_blend_rgb(source_r, source_g, source_b, target_r, target_g, target_b,
                 blend, out_r, out_g, out_b);
}

static inline void db_gradient_sweep_set_row_color(float *vertices,
                                                   uint32_t row_index,
                                                   uint32_t head_row,
                                                   int direction_down,
                                                   uint32_t cycle_index) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((vertices == NULL) || (cols == 0U) || (rows == 0U)) {
        return;
    }

    float color_r = 0.0F;
    float color_g = 0.0F;
    float color_b = 0.0F;
    db_gradient_sweep_row_color_rgb(row_index % rows, head_row, direction_down,
                                    cycle_index, &color_r, &color_g, &color_b);

    const uint32_t row = row_index % rows;
    const uint32_t first_tile = row * cols;
    for (uint32_t col = 0U; col < cols; col++) {
        const size_t base = (size_t)(first_tile + col) * DB_RECT_VERTEX_COUNT *
                            DB_VERTEX_FLOAT_STRIDE;
        db_set_rect_unit_rgb(&vertices[base], DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, color_r, color_g,
                             color_b);
    }
}

static inline void
db_gradient_sweep_set_rows_color(float *vertices, uint32_t head_row,
                                 int direction_down, uint32_t cycle_index,
                                 uint32_t row_start, uint32_t row_count) {
    const uint32_t rows = db_grid_rows_effective();
    if ((vertices == NULL) || (rows == 0U) || (row_count == 0U)) {
        return;
    }
    for (uint32_t i = 0U; i < row_count; i++) {
        const uint32_t row = (row_start + i) % rows;
        db_gradient_sweep_set_row_color(vertices, row, head_row, direction_down,
                                        cycle_index);
    }
}

static inline uint32_t db_gradient_fill_window_rows_effective(void) {
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return 1U;
    }
    return db_u32_min(rows, DB_GRADIENT_FILL_WINDOW_ROWS);
}

static inline db_gradient_fill_damage_plan_t
db_gradient_fill_plan_next_frame(uint32_t head_row, uint32_t cycle_index) {
    db_gradient_fill_damage_plan_t plan = {0};
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return plan;
    }

    const uint32_t window_rows = db_gradient_fill_window_rows_effective();
    const uint32_t max_head = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_fill_max_head", rows, window_rows);
    uint32_t next_head = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_fill_head_next", head_row, 1U);
    uint32_t next_cycle = cycle_index;
    if (next_head > max_head) {
        next_head = 0U;
        next_cycle =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                               "gradient_fill_cycle_next", next_cycle, 1U);
        plan.dirty_row_start = 0U;
        plan.dirty_row_count = rows;
    } else {
        const uint32_t dirty_span =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                               "gradient_fill_dirty_span", window_rows, 1U);
        const uint32_t dirty_start_unclamped =
            db_u32_saturating_sub(next_head, dirty_span);
        const uint32_t dirty_end = db_u32_min(next_head, rows);
        if (dirty_end > dirty_start_unclamped) {
            plan.dirty_row_start = dirty_start_unclamped;
            plan.dirty_row_count = dirty_end - dirty_start_unclamped;
        }
    }

    plan.render_head_row = next_head;
    plan.render_cycle_index = next_cycle;
    plan.next_head_row = next_head;
    plan.next_cycle_index = next_cycle;
    return plan;
}

static inline uint32_t db_gradient_fill_solid_rows(uint32_t head_row) {
    const uint32_t window_rows = db_gradient_fill_window_rows_effective();
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t solid_rows = db_u32_saturating_sub(head_row, window_rows);
    return db_u32_min(solid_rows, rows);
}

static inline void db_gradient_fill_row_color_rgb(uint32_t row_index,
                                                  uint32_t head_row,
                                                  uint32_t cycle_index,
                                                  float *out_r, float *out_g,
                                                  float *out_b) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_fill_window_rows_effective();
    if ((rows == 0U) || (window_rows == 0U)) {
        *out_r = BENCH_GRID_PHASE0_R;
        *out_g = BENCH_GRID_PHASE0_G;
        *out_b = BENCH_GRID_PHASE0_B;
        return;
    }

    const uint32_t head = head_row;
    const uint32_t row = row_index % rows;
    float source_r = 0.0F;
    float source_g = 0.0F;
    float source_b = 0.0F;
    float target_r = 0.0F;
    float target_g = 0.0F;
    float target_b = 0.0F;
    db_palette_cycle_color_rgb(cycle_index, &source_r, &source_g, &source_b);
    db_palette_cycle_color_rgb(cycle_index + 1U, &target_r, &target_g,
                               &target_b);
    if (row >= head) {
        *out_r = source_r;
        *out_g = source_g;
        *out_b = source_b;
        return;
    }

    const uint32_t delta = head - row;
    if (delta >= window_rows) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }

    if (window_rows <= 1U) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }

    const float blend = (float)delta / (float)(window_rows - 1U);
    db_blend_rgb(source_r, source_g, source_b, target_r, target_g, target_b,
                 blend, out_r, out_g, out_b);
}

static inline void db_gradient_fill_set_row_color(float *vertices,
                                                  uint32_t row_index,
                                                  uint32_t head_row,
                                                  uint32_t cycle_index) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((vertices == NULL) || (cols == 0U) || (rows == 0U)) {
        return;
    }

    float color_r = 0.0F;
    float color_g = 0.0F;
    float color_b = 0.0F;
    db_gradient_fill_row_color_rgb(row_index % rows, head_row, cycle_index,
                                   &color_r, &color_g, &color_b);

    const uint32_t row = row_index % rows;
    const uint32_t first_tile = row * cols;
    for (uint32_t col = 0U; col < cols; col++) {
        const size_t base = (size_t)(first_tile + col) * DB_RECT_VERTEX_COUNT *
                            DB_VERTEX_FLOAT_STRIDE;
        db_set_rect_unit_rgb(&vertices[base], DB_VERTEX_FLOAT_STRIDE,
                             DB_VERTEX_POSITION_FLOAT_COUNT, color_r, color_g,
                             color_b);
    }
}

static inline void db_gradient_fill_set_rows_color(float *vertices,
                                                   uint32_t head_row,
                                                   uint32_t cycle_index,
                                                   uint32_t row_start,
                                                   uint32_t row_count) {
    const uint32_t rows = db_grid_rows_effective();
    if ((vertices == NULL) || (rows == 0U) || (row_count == 0U)) {
        return;
    }
    for (uint32_t i = 0U; i < row_count; i++) {
        const uint32_t row = (row_start + i) % rows;
        db_gradient_fill_set_row_color(vertices, row, head_row, cycle_index);
    }
}

static inline int
db_init_band_vertices_common(db_pattern_vertex_init_t *out_state) {
    const size_t vertex_count = (size_t)BENCH_BANDS * DB_RECT_VERTEX_COUNT;
    const size_t float_count = vertex_count * DB_VERTEX_FLOAT_STRIDE;

    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    *out_state = (db_pattern_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->pattern = DB_PATTERN_BANDS;
    out_state->work_unit_count = BENCH_BANDS;
    out_state->draw_vertex_count = db_checked_size_to_u32(
        DB_BENCH_COMMON_BACKEND, "bands_draw_vertex_count", vertex_count);
    return 1;
}

static inline int
db_init_grid_vertices_common(db_pattern_vertex_init_t *out_state) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(DB_PATTERN_SNAKE_GRID);
    if ((tile_count_u64 == 0U) || (tile_count_u64 > UINT32_MAX)) {
        return 0;
    }

    const uint64_t vertex_count_u64 = tile_count_u64 * DB_RECT_VERTEX_COUNT;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * DB_VERTEX_FLOAT_STRIDE;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_tile_count", tile_count_u64);
    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);
        const size_t base =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * DB_VERTEX_FLOAT_STRIDE;
        float *unit = &vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, DB_VERTEX_FLOAT_STRIDE);
        db_set_rect_unit_rgb(
            unit, DB_VERTEX_FLOAT_STRIDE, DB_VERTEX_POSITION_FLOAT_COUNT,
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    }

    *out_state = (db_pattern_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->pattern = DB_PATTERN_SNAKE_GRID;
    out_state->work_unit_count = tile_count;
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_draw_vertex_count", vertex_count_u64);
    out_state->gradient_head_row = db_gradient_sweep_window_rows_effective();
    out_state->gradient_sweep_direction_down = 1;
    out_state->gradient_sweep_cycle = 0U;
    out_state->gradient_fill_cycle = 0U;
    out_state->random_seed = 0U;
    return 1;
}

static inline int
db_init_vertices_for_mode_common(const char *backend_name,
                                 db_pattern_vertex_init_t *out_state) {
    db_pattern_t requested = DB_PATTERN_GRADIENT_SWEEP;
    if (!db_parse_benchmark_pattern_from_env(&requested)) {
        const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
        db_failf(backend_name, "Invalid %s='%s' (expected: %s|%s|%s|%s|%s)",
                 DB_BENCHMARK_MODE_ENV, (mode != NULL) ? mode : "",
                 DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_BENCHMARK_MODE_BANDS,
                 DB_BENCHMARK_MODE_SNAKE_GRID, DB_BENCHMARK_MODE_GRADIENT_FILL,
                 DB_BENCHMARK_MODE_RECT_SNAKE);
    }
    if ((requested == DB_PATTERN_SNAKE_GRID) ||
        (requested == DB_PATTERN_GRADIENT_SWEEP) ||
        (requested == DB_PATTERN_GRADIENT_FILL) ||
        (requested == DB_PATTERN_RECT_SNAKE)) {
        if (!db_init_grid_vertices_common(out_state)) {
            db_failf(backend_name, "benchmark mode '%s' initialization failed",
                     db_pattern_mode_name(requested));
        }
        out_state->pattern = requested;
        out_state->random_seed =
            db_benchmark_random_seed_from_env_or_time(backend_name);
        out_state->pattern_seed = out_state->random_seed;
        out_state->gradient_sweep_cycle = db_benchmark_cycle_from_seed(
            out_state->random_seed, DB_GRADIENT_FILL_SEED_PHASE_A);
        out_state->gradient_fill_cycle = db_benchmark_cycle_from_seed(
            out_state->random_seed, DB_GRADIENT_FILL_SEED_PHASE_B);
        if (requested == DB_PATTERN_GRADIENT_SWEEP) {
            out_state->gradient_head_row =
                db_gradient_sweep_window_rows_effective();
            out_state->gradient_sweep_direction_down = 1;
        } else {
            out_state->gradient_head_row = 0U;
        }
        if (requested == DB_PATTERN_GRADIENT_SWEEP) {
            db_infof(backend_name,
                     "benchmark mode: %s (%u-row random palette sweep over "
                     "%ux%u tiles)",
                     DB_BENCHMARK_MODE_GRADIENT_SWEEP,
                     db_gradient_sweep_window_rows_effective(),
                     db_grid_rows_effective(), db_grid_cols_effective());
        } else if (requested == DB_PATTERN_GRADIENT_FILL) {
            db_infof(backend_name,
                     "benchmark mode: %s (top-down random palette sweep over "
                     "%ux%u tiles, %u-row transition)",
                     DB_BENCHMARK_MODE_GRADIENT_FILL, db_grid_rows_effective(),
                     db_grid_cols_effective(),
                     db_gradient_fill_window_rows_effective());
        } else if (requested == DB_PATTERN_RECT_SNAKE) {
            db_infof(backend_name,
                     "benchmark mode: %s (seed=%u, deterministic PRNG random "
                     "rectangles, S-snake draw)",
                     DB_BENCHMARK_MODE_RECT_SNAKE, out_state->pattern_seed);
        } else {
            db_infof(
                backend_name,
                "benchmark mode: %s (%ux%u tiles, deterministic snake sweep)",
                DB_BENCHMARK_MODE_SNAKE_GRID, db_grid_rows_effective(),
                db_grid_cols_effective());
        }
        return 1;
    }

    if (!db_init_band_vertices_common(out_state)) {
        db_failf(backend_name, "benchmark mode '%s' initialization failed",
                 db_pattern_mode_name(requested));
    }
    db_infof(backend_name, "benchmark mode: %s (%u vertical bands)",
             DB_BENCHMARK_MODE_BANDS, BENCH_BANDS);
    return 1;
}

static inline void
db_fill_band_vertices_pos_rgb_stride(float *out_vertices, uint32_t band_count,
                                     double time_s, size_t stride_floats,
                                     size_t color_offset_floats) {
    const float inv_band_count = 1.0F / (float)band_count;
    const float band_x_scale = 2.0F * inv_band_count;
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        const float band_f = (float)band_index;
        const float x0 = (band_f * band_x_scale) - 1.0F;
        const float x1 = x0 + band_x_scale;
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_fill_rect_unit_pos(unit, x0, -1.0F, x1, 1.0F, stride_floats);
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
    }
}

static inline void
db_update_band_vertices_rgb_stride(float *out_vertices, uint32_t band_count,
                                   double time_s, size_t stride_floats,
                                   size_t color_offset_floats) {
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t band_base =
            (size_t)band_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &out_vertices[band_base];
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
    }
}

static inline void db_fill_band_vertices_pos_rgb(float *out_vertices,
                                                 uint32_t band_count,
                                                 double time_s) {
    db_fill_band_vertices_pos_rgb_stride(out_vertices, band_count, time_s,
                                         DB_VERTEX_FLOAT_STRIDE,
                                         DB_VERTEX_POSITION_FLOAT_COUNT);
}

#endif
