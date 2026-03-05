#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H

#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "../config/benchmark_config.h"
#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../core/db_numeric.h"

#define DB_RECT_VERTEX_COUNT 6U
#define DB_VERTEX_POSITION_FLOAT_COUNT 2U
#define DB_VERTEX_COLOR_FLOAT_COUNT 3U
#define DB_VERTEX_FLOAT_STRIDE                                                 \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_VERTEX_COLOR_FLOAT_COUNT)
#define DB_ES_VERTEX_COLOR_FLOAT_COUNT 4U
#define DB_ES_VERTEX_FLOAT_STRIDE                                              \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_ES_VERTEX_COLOR_FLOAT_COUNT)
#define DB_BENCH_COMMON_BACKEND "renderer_benchmark_common"
#define DB_BENCHMARK_MODE_BANDS "bands"
#define DB_BENCHMARK_MODE_SNAKE_GRID "snake_grid"
#define DB_BENCHMARK_MODE_GRADIENT_SWEEP "gradient_sweep"
#define DB_BENCHMARK_MODE_GRADIENT_FILL "gradient_fill"
#define DB_BENCHMARK_MODE_SNAKE_RECT "snake_rect"
#define DB_BENCHMARK_MODE_SNAKE_SHAPES "snake_shapes"
#define DB_BENCH_SPEED_STEP_MAX 1024U
#define DB_COLOR_CHANNEL_BIAS 0.20
#define DB_COLOR_CHANNEL_SCALE 0.75
#define DB_GRADIENT_WINDOW_ROWS 32U
#define DB_GRADIENT_DRAW_RANGE_WORK_CAP 8U
#define DB_PALETTE_SALT_BASE_STEP DB_U32_GOLDEN_RATIO

typedef enum {
    DB_PATTERN_GRADIENT_SWEEP = 0,
    DB_PATTERN_BANDS = 1,
    DB_PATTERN_SNAKE_GRID = 2,
    DB_PATTERN_GRADIENT_FILL = 3,
    DB_PATTERN_SNAKE_RECT = 4,
    DB_PATTERN_SNAKE_SHAPES = 5,
} db_pattern_t;

typedef struct {
    uint32_t head_row;
    uint32_t cycle_index;
    int direction_down;
} db_gradient_state_t;

typedef struct {
    db_gradient_state_t render_state;
    db_gradient_state_t next_state;
    uint32_t dirty_row_start;
    uint32_t dirty_row_count;
    uint32_t dirty_row_start_second;
    uint32_t dirty_row_count_second;
} db_gradient_damage_plan_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
} db_dirty_row_range_t;

typedef struct {
    db_dirty_row_range_t draw_rows[4];
    size_t draw_count;
    db_gradient_state_t state;
} db_gradient_backbuffer_replay_state_t;

typedef struct {
    uint32_t shape_index;
    uint32_t cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int grid_phase_flag;
    int phase_completed;
} db_snake_state_t;

typedef struct {
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    db_snake_state_t snake;
    db_gradient_state_t gradient;
    uint32_t bench_speed_step;
    uint32_t pattern_seed;
    int backbuffer_draw_full;
} db_benchmark_runtime_init_t;

typedef void (*db_gradient_row_color_apply_fn_t)(uint32_t row, double row_r,
                                                 double row_g, double row_b,
                                                 void *user_data);

static inline uint64_t db_benchmark_runtime_state_hash_cross_renderer(
    const db_benchmark_runtime_init_t *runtime, uint32_t frame_index,
    uint32_t render_width, uint32_t render_height) {
    if (runtime == NULL) {
        return 0U;
    }
    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = db_fnv1a64_mix_u64(hash, frame_index);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->pattern);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->work_unit_count);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.shape_index);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.cursor);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.prev_start);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.prev_count);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.batch_size);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.phase_completed);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake.grid_phase_flag);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->gradient.direction_down);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->gradient.head_row);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->gradient.cycle_index);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->pattern_seed);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)render_width);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)render_height);
    return hash;
}

static inline uint32_t db_grid_rows_effective(void) {
    return (uint32_t)BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_grid_cols_effective(void) {
    return (uint32_t)BENCH_WINDOW_WIDTH_PX;
}

static inline int
db_parse_benchmark_pattern_from_runtime(db_pattern_t *out_pattern) {
    const char *mode = db_runtime_option_get(DB_RUNTIME_OPT_BENCHMARK_MODE);
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
    if (strcmp(mode, DB_BENCHMARK_MODE_SNAKE_RECT) == 0) {
        *out_pattern = DB_PATTERN_SNAKE_RECT;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_SNAKE_SHAPES) == 0) {
        *out_pattern = DB_PATTERN_SNAKE_SHAPES;
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
    case DB_PATTERN_SNAKE_RECT:
        return DB_BENCHMARK_MODE_SNAKE_RECT;
    case DB_PATTERN_SNAKE_SHAPES:
        return DB_BENCHMARK_MODE_SNAKE_SHAPES;
    default:
        return "unknown";
    }
}

static inline uint32_t db_gradient_window_rows_effective(void) {
    const uint32_t rows =
        db_u32_min(db_grid_rows_effective(), DB_GRADIENT_WINDOW_ROWS);
    if (rows == 0U) {
        return 1U;
    }
    return rows;
}

static inline void db_benchmark_seed_background_color_rgb(
    const db_benchmark_runtime_init_t *runtime, double *out_r, double *out_g,
    double *out_b) {
    if ((out_r == NULL) || (out_g == NULL) || (out_b == NULL)) {
        return;
    }

    if ((runtime != NULL) && (runtime->pattern == DB_PATTERN_SNAKE_GRID)) {
        // Snake grid alternates between two stable full-grid phases. Seed to
        // the current base phase so the first dirty update composes correctly.
        if (runtime->snake.grid_phase_flag == 0) {
            *out_r = BENCH_GRID_PHASE0_R;
            *out_g = BENCH_GRID_PHASE0_G;
            *out_b = BENCH_GRID_PHASE0_B;
        } else {
            *out_r = BENCH_GRID_PHASE1_R;
            *out_g = BENCH_GRID_PHASE1_G;
            *out_b = BENCH_GRID_PHASE1_B;
        }
        return;
    }

    // Keep non-snake seed behavior stable.
    *out_r = BENCH_GRID_PHASE0_R;
    *out_g = BENCH_GRID_PHASE0_G;
    *out_b = BENCH_GRID_PHASE0_B;
}

static inline uint32_t db_pattern_seed_from_time(void) {
    const time_t now = time(NULL);
    if (now == (time_t)-1) {
        db_failf(DB_BENCH_COMMON_BACKEND, "time() failed for random seed");
    }
    const uint32_t raw = db_fold_u64_to_u32((uint64_t)now);
    const uint32_t salted = raw ^ DB_U32_GOLDEN_RATIO;
    return db_mix_u32(salted);
}

static inline uint32_t db_benchmark_cycle_from_seed(uint32_t seed,
                                                    uint32_t salt) {
    return db_mix_u32(seed ^ salt);
}

static inline uint32_t
db_benchmark_random_seed_from_runtime_or_time(const char *backend_name) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_RANDOM_SEED);
    if ((value == NULL) || (value[0] == '\0')) {
        return db_pattern_seed_from_time();
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 0);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        db_failf(backend_name, "Invalid %s='%s'", DB_RUNTIME_OPT_RANDOM_SEED,
                 value);
    }
    return (uint32_t)parsed;
}

static inline uint32_t
db_benchmark_speed_step_from_runtime(const char *backend_name) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_BENCH_SPEED);
    if ((value == NULL) || (value[0] == '\0')) {
        return 1U;
    }
    char *end = NULL;
    const double parsed = strtod(value, &end);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        !isfinite(parsed) || (parsed <= 0.0)) {
        db_failf(backend_name, "Invalid %s='%s' (expected: > 0)",
                 DB_RUNTIME_OPT_BENCH_SPEED, value);
    }
    double rounded_up = ceil(parsed);
    if (rounded_up < 1.0) {
        rounded_up = 1.0;
    }
    if (rounded_up > (double)DB_BENCH_SPEED_STEP_MAX) {
        db_failf(backend_name,
                 "Invalid %s='%.9g' (max effective per-frame step: %u)",
                 DB_RUNTIME_OPT_BENCH_SPEED, parsed, DB_BENCH_SPEED_STEP_MAX);
    }
    return (uint32_t)rounded_up;
}

static inline void db_log_benchmark_mode(const char *backend_name,
                                         db_pattern_t pattern,
                                         uint32_t pattern_seed,
                                         uint32_t bench_speed_step) {
    if ((pattern == DB_PATTERN_SNAKE_RECT) ||
        (pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const char *shape_desc =
            (pattern == DB_PATTERN_SNAKE_SHAPES)
                ? "shapes (rectangles/circles/diamonds/triangles/trapezoids)"
                : "rectangles";
        db_infof(backend_name,
                 "benchmark mode: %s (seed=%u, deterministic PRNG random "
                 "%s, S-snake draw, speed_step=%u)",
                 db_pattern_mode_name(pattern), pattern_seed, shape_desc,
                 bench_speed_step);
        return;
    }
    if (pattern == DB_PATTERN_SNAKE_GRID) {
        db_infof(backend_name,
                 "benchmark mode: %s (%ux%u tiles, deterministic snake "
                 "sweep, speed_step=%u)",
                 db_pattern_mode_name(pattern), db_grid_rows_effective(),
                 db_grid_cols_effective(), bench_speed_step);
        return;
    }
    if ((pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (pattern == DB_PATTERN_GRADIENT_FILL)) {
        db_infof(backend_name,
                 "benchmark mode: %s (seed=%u, top-down random palette "
                 "gradient over %ux%u tiles, %u-row transition, speed_step=%u)",
                 db_pattern_mode_name(pattern), pattern_seed,
                 db_grid_rows_effective(), db_grid_cols_effective(),
                 db_gradient_window_rows_effective(), bench_speed_step);
        return;
    }
    db_infof(backend_name,
             "benchmark mode: %s (%u vertical bands, speed_step=%u)",
             db_pattern_mode_name(pattern), BENCH_BANDS, bench_speed_step);
}

static inline uint32_t db_pattern_work_unit_count(db_pattern_t pattern) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t cols = db_grid_cols_effective();
    (void)pattern;
    const uint64_t count = (uint64_t)rows * cols;
    if ((count == 0U) || (count > UINT32_MAX)) {
        return 0U;
    }
    return (uint32_t)count;
}

static inline uint32_t
db_runtime_work_unit_count(const db_benchmark_runtime_init_t *runtime,
                           int is_initialized) {
    if ((is_initialized == 0) || (runtime == NULL)) {
        return 0U;
    }
    if (runtime->work_unit_count != 0U) {
        return runtime->work_unit_count;
    }
    return db_pattern_work_unit_count(DB_PATTERN_GRADIENT_SWEEP);
}

static inline int
db_init_benchmark_runtime_common(const char *backend_name,
                                 db_benchmark_runtime_init_t *out_state) {
    db_pattern_t requested = DB_PATTERN_GRADIENT_SWEEP;
    if (!db_parse_benchmark_pattern_from_runtime(&requested)) {
        const char *mode = db_runtime_option_get(DB_RUNTIME_OPT_BENCHMARK_MODE);
        db_failf(backend_name, "Invalid %s='%s' (expected: %s|%s|%s|%s|%s|%s)",
                 DB_RUNTIME_OPT_BENCHMARK_MODE, (mode != NULL) ? mode : "",
                 DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_BENCHMARK_MODE_BANDS,
                 DB_BENCHMARK_MODE_SNAKE_GRID, DB_BENCHMARK_MODE_GRADIENT_FILL,
                 DB_BENCHMARK_MODE_SNAKE_RECT, DB_BENCHMARK_MODE_SNAKE_SHAPES);
    }

    *out_state = (db_benchmark_runtime_init_t){0};
    out_state->pattern = requested;
    out_state->work_unit_count = db_pattern_work_unit_count(requested);
    if (out_state->work_unit_count == 0U) {
        db_failf(backend_name, "Invalid work-unit geometry for mode '%s'",
                 db_pattern_mode_name(requested));
    }
    const uint64_t draw_vertex_count_u64 =
        (uint64_t)out_state->work_unit_count * DB_RECT_VERTEX_COUNT;
    if (draw_vertex_count_u64 > UINT32_MAX) {
        db_failf(backend_name, "draw vertex count overflow for mode '%s'",
                 db_pattern_mode_name(requested));
    }
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        backend_name, "draw_vertex_count", draw_vertex_count_u64);
    out_state->bench_speed_step =
        db_benchmark_speed_step_from_runtime(backend_name);
    const char *backbuffer_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE);
    out_state->backbuffer_draw_full =
        (backbuffer_mode != NULL) && (strcmp(backbuffer_mode, "full") == 0);

    if ((requested != DB_PATTERN_BANDS) &&
        (requested != DB_PATTERN_SNAKE_GRID)) {
        out_state->pattern_seed =
            db_benchmark_random_seed_from_runtime_or_time(backend_name);
    }
    if ((requested == DB_PATTERN_GRADIENT_SWEEP) ||
        (requested == DB_PATTERN_GRADIENT_FILL)) {
        out_state->gradient.cycle_index = db_benchmark_cycle_from_seed(
            out_state->pattern_seed, DB_U32_SALT_PALETTE);
        out_state->gradient.head_row = 0U;
        out_state->gradient.direction_down = 0;
        if (requested == DB_PATTERN_GRADIENT_SWEEP) {
            // Start sweep at top-offscreen hold: head=0 while moving up. The
            // first planner step keeps render at head=0 and flips to down.
            out_state->gradient.head_row = 0U;
            out_state->gradient.direction_down = 0;
            // Delay first visible palette swap by one step so top-offscreen
            // start does not advance past the seed phase immediately.
            out_state->gradient.cycle_index =
                db_u32_wrapping_sub(out_state->gradient.cycle_index, 1U);
        } else if (requested == DB_PATTERN_GRADIENT_FILL) {
            // Start fill so first rendered frame is top-offscreen (head=0) by
            // beginning from the single-frame bottom hold.
            out_state->gradient.head_row = db_checked_add_u32(
                DB_BENCH_COMMON_BACKEND, "gradient_init_head_max",
                db_grid_rows_effective(), db_gradient_window_rows_effective());
            out_state->gradient.direction_down = 1;
            // Delay first visible palette swap by one step so top-offscreen
            // start does not advance past the seed phase immediately.
            out_state->gradient.cycle_index =
                db_u32_wrapping_sub(out_state->gradient.cycle_index, 1U);
        }
    }
    if ((requested == DB_PATTERN_SNAKE_GRID) ||
        (requested == DB_PATTERN_SNAKE_RECT) ||
        (requested == DB_PATTERN_SNAKE_SHAPES)) {
        out_state->snake.cursor = UINT32_MAX;
        out_state->snake.grid_phase_flag = 0;
    }

    db_log_benchmark_mode(backend_name, requested, out_state->pattern_seed,
                          out_state->bench_speed_step);
    return 1;
}

static inline uint32_t db_grid_tile_index_from_step(uint32_t step) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t row = step / cols;
    const uint32_t col_step = step % cols;
    const uint32_t col = ((row & 1U) == 0U) ? col_step : (cols - 1U - col_step);
    return (row * cols) + col;
}

static inline void
db_rect_pixels_to_ndc_bounds(int x0_px, int y0_px, int x1_px, int y1_px,
                             int viewport_w_px, int viewport_h_px,
                             float *x0_ndc_out, float *y0_ndc_out,
                             float *x1_ndc_out, float *y1_ndc_out) {
    const double inv_w = 1.0 / (double)viewport_w_px;
    const double inv_h = 1.0 / (double)viewport_h_px;

    *x0_ndc_out = db_double_to_f32((2.0 * (double)x0_px * inv_w) - 1.0);
    *x1_ndc_out = db_double_to_f32((2.0 * (double)x1_px * inv_w) - 1.0);
    *y0_ndc_out = db_double_to_f32((2.0 * (double)y0_px * inv_h) - 1.0);
    *y1_ndc_out = db_double_to_f32((2.0 * (double)y1_px * inv_h) - 1.0);
}

static inline void db_grid_tile_bounds_ndc(uint32_t tile_index, float *x0,
                                           float *y0, float *x1, float *y1) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    const double inv_cols = 1.0 / (double)cols;
    const double inv_rows = 1.0 / (double)rows;

    *x0 = db_double_to_f32((2.0 * (double)col * inv_cols) - 1.0);
    *x1 = db_double_to_f32((2.0 * (double)(col + 1U) * inv_cols) - 1.0);
    *y1 = db_double_to_f32(1.0 - (2.0 * (double)row * inv_rows));
    *y0 = db_double_to_f32(1.0 - (2.0 * (double)(row + 1U) * inv_rows));
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

static inline void
db_set_rect_tile_range_rgb(float *vertices, uint32_t first_tile_index,
                           uint32_t tile_count, size_t stride_floats,
                           size_t color_offset_floats, float color_r,
                           float color_g, float color_b) {
    if ((vertices == NULL) || (tile_count == 0U)) {
        return;
    }
    const size_t first_tile_offset =
        (size_t)first_tile_index * DB_RECT_VERTEX_COUNT * stride_floats;
    float *unit = &vertices[first_tile_offset];
    for (uint32_t tile = 0U; tile < tile_count; tile++) {
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
        unit += (size_t)DB_RECT_VERTEX_COUNT * stride_floats;
    }
}

static inline void db_set_rect_unit_alpha(float *unit, size_t stride_floats,
                                          size_t alpha_offset_floats,
                                          float alpha_value) {
    if (unit == NULL) {
        return;
    }
    for (uint32_t vertex_index = 0U; vertex_index < DB_RECT_VERTEX_COUNT;
         vertex_index++) {
        const size_t base = (size_t)vertex_index * stride_floats;
        unit[base + alpha_offset_floats] = alpha_value;
    }
}

static inline void
db_fill_grid_all_rgb_stride(float *vertices, uint32_t tile_count,
                            size_t stride_floats, size_t color_offset_floats,
                            float color_r, float color_g, float color_b) {
    db_set_rect_tile_range_rgb(vertices, 0U, tile_count, stride_floats,
                               color_offset_floats, color_r, color_g, color_b);
}

static inline void db_band_color_rgb(uint32_t band_index, uint32_t band_count,
                                     uint32_t frame_index, double *out_r,
                                     double *out_g, double *out_b) {
    const double band_value = (double)band_index;
    const double frame_value = (double)frame_index;
    const double pulse_value =
        BENCH_PULSE_BASE +
        (BENCH_PULSE_AMP * sin((frame_value * BENCH_PULSE_FREQ) +
                               (band_value * BENCH_PULSE_PHASE)));
    const double color_r_value =
        pulse_value * (BENCH_COLOR_R_BASE +
                       (BENCH_COLOR_R_SCALE * band_value / (double)band_count));
    *out_r = color_r_value;
    *out_g = pulse_value * BENCH_COLOR_G_SCALE;
    *out_b = 1.0 - color_r_value;
}

static inline double db_color_channel(uint32_t seed) {
    const double normalized = (double)(seed & 255U) / 255.0;
    return DB_COLOR_CHANNEL_BIAS + (normalized * DB_COLOR_CHANNEL_SCALE);
}

static inline void db_palette_cycle_color_rgb(uint32_t cycle_index,
                                              double *out_r, double *out_g,
                                              double *out_b) {
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_PALETTE_SALT_BASE_STEP) ^ DB_U32_SALT_PALETTE);
    *out_r = db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R));
    *out_g = db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G));
    *out_b = db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B));
}

static inline db_gradient_damage_plan_t
db_gradient_plan_next_frame(uint32_t head_row, int direction_down,
                            uint32_t cycle_index, int restart_at_top_only,
                            uint32_t head_step) {
    db_gradient_damage_plan_t plan = {0};
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return plan;
    }

    const uint32_t window_rows = db_gradient_window_rows_effective();
    const uint32_t max_head = db_checked_add_u32(
        DB_BENCH_COMMON_BACKEND, "gradient_max_head", rows, window_rows);
    const uint32_t prev_head = head_row;
    int next_direction_down = 1;
    if (restart_at_top_only == 0) {
        next_direction_down = (direction_down != 0) ? 1 : 0;
    }
    uint32_t next_cycle = cycle_index;
    uint32_t next_head = head_row;
    uint32_t wrap_count = 0U;
    const uint32_t step_count = db_u32_max(head_step, 1U);
    for (uint32_t step = 0U; step < step_count; step++) {
        if (restart_at_top_only != 0) {
            if (next_head >= max_head) {
                next_head = 0U;
                wrap_count = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_wrap_count_next",
                                                wrap_count, 1U);
                next_cycle = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_palette_cycle_next",
                                                next_cycle, 1U);
            } else {
                next_head =
                    db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                       "gradient_head_next", next_head, 1U);
            }
        } else {
            if (next_direction_down != 0) {
                // Sweep: one frame at bottom-offscreen (head=max_head), then
                // reverse immediately on the next tick.
                if (next_head >= max_head) {
                    next_direction_down = 0;
                    next_head = max_head;
                    next_cycle = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                    "gradient_cycle_next",
                                                    next_cycle, 1U);
                } else {
                    next_head =
                        db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                           "gradient_head_next", next_head, 1U);
                }
            } else {
                // Sweep: one frame at top-offscreen (head=0), then reverse
                // immediately on the next tick.
                if (next_head == 0U) {
                    next_direction_down = 1;
                    next_head = 0U;
                    next_cycle = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                    "gradient_cycle_next",
                                                    next_cycle, 1U);
                } else {
                    next_head =
                        db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                           "gradient_head_prev", next_head, 1U);
                }
            }
        }
    }

    const uint32_t prev_head_start =
        db_u32_saturating_sub(prev_head, window_rows);
    const uint32_t next_head_start =
        db_u32_saturating_sub(next_head, window_rows);
    const uint32_t prev_head_end =
        db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_prev_head_end",
                           prev_head_start, window_rows);
    const uint32_t next_head_end =
        db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_next_head_end",
                           next_head_start, window_rows);
    uint32_t traversed_dirty_start = prev_head_start;
    uint32_t traversed_dirty_end = db_u32_min(prev_head_end, rows);
    if (traversed_dirty_end < traversed_dirty_start) {
        traversed_dirty_end = traversed_dirty_start;
    }
    {
        uint32_t sample_head = prev_head;
        int sample_direction_down = 1;
        if (restart_at_top_only == 0) {
            sample_direction_down = (direction_down != 0) ? 1 : 0;
        }
        for (uint32_t step = 0U; step < step_count; step++) {
            if (restart_at_top_only != 0) {
                if (sample_head >= max_head) {
                    sample_head = 0U;
                } else {
                    sample_head = db_checked_add_u32(
                        DB_BENCH_COMMON_BACKEND, "gradient_sample_head_next",
                        sample_head, 1U);
                }
            } else {
                if (sample_direction_down != 0) {
                    if (sample_head >= max_head) {
                        sample_direction_down = 0;
                        sample_head = max_head;
                    } else {
                        sample_head = db_checked_add_u32(
                            DB_BENCH_COMMON_BACKEND, "gradient_sample_head_inc",
                            sample_head, 1U);
                    }
                } else {
                    if (sample_head == 0U) {
                        sample_direction_down = 1;
                        sample_head = 0U;
                    } else {
                        sample_head = db_checked_sub_u32(
                            DB_BENCH_COMMON_BACKEND, "gradient_sample_head_dec",
                            sample_head, 1U);
                    }
                }
            }
            const uint32_t sample_start =
                db_u32_saturating_sub(sample_head, window_rows);
            const uint32_t sample_end =
                db_u32_min(db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                              "gradient_sample_head_end",
                                              sample_start, window_rows),
                           rows);
            traversed_dirty_start =
                db_u32_min(traversed_dirty_start, sample_start);
            traversed_dirty_end = db_u32_max(traversed_dirty_end, sample_end);
        }
    }

    uint32_t cycle_advance = 0U;
    if (next_cycle >= cycle_index) {
        cycle_advance = db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                           "gradient_cycle_advance", next_cycle,
                                           cycle_index);
    } else {
        cycle_advance = UINT32_MAX;
    }

    if (cycle_advance > 1U) {
        plan.dirty_row_start = 0U;
        plan.dirty_row_count = rows;
        plan.dirty_row_start_second = 0U;
        plan.dirty_row_count_second = 0U;
    } else if ((next_cycle != cycle_index) && (restart_at_top_only != 0)) {
        const uint32_t expected_next_cycle =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                               "gradient_expected_next_cycle", cycle_index, 1U);
        // For fill-mode wrap, source advances to previous target color and the
        // head teleports to top. In that case, only bottom and top ranges are
        // dirty rather than the whole frame.
        if ((next_cycle == expected_next_cycle) && (wrap_count == 1U)) {
            const uint32_t dirty0_start = prev_head_start;
            const uint32_t dirty0_end = rows;
            const uint32_t dirty1_start = 0U;
            const uint32_t dirty1_end = db_u32_min(next_head_end, rows);
            const int overlap =
                (dirty1_end >= dirty0_start) && (dirty0_end > dirty1_start);
            if (overlap != 0) {
                plan.dirty_row_start = 0U;
                plan.dirty_row_count = rows;
                plan.dirty_row_start_second = 0U;
                plan.dirty_row_count_second = 0U;
            } else {
                if (dirty0_end > dirty0_start) {
                    plan.dirty_row_start = dirty0_start;
                    plan.dirty_row_count = db_checked_sub_u32(
                        DB_BENCH_COMMON_BACKEND, "gradient_dirty_bottom_count",
                        dirty0_end, dirty0_start);
                }
                if (dirty1_end > dirty1_start) {
                    plan.dirty_row_start_second = dirty1_start;
                    plan.dirty_row_count_second = db_checked_sub_u32(
                        DB_BENCH_COMMON_BACKEND, "gradient_dirty_top_count",
                        dirty1_end, dirty1_start);
                }
            }
        } else {
            plan.dirty_row_start = 0U;
            plan.dirty_row_count = rows;
            plan.dirty_row_start_second = 0U;
            plan.dirty_row_count_second = 0U;
        }
    } else {
        if (traversed_dirty_end > traversed_dirty_start) {
            plan.dirty_row_start = traversed_dirty_start;
            plan.dirty_row_count = db_checked_sub_u32(
                DB_BENCH_COMMON_BACKEND, "gradient_dirty_row_count",
                traversed_dirty_end, traversed_dirty_start);
            plan.dirty_row_start_second = 0U;
            plan.dirty_row_count_second = 0U;
        }
    }

    plan.render_state.head_row = next_head;
    plan.render_state.direction_down = next_direction_down;
    plan.render_state.cycle_index = next_cycle;
    plan.next_state.head_row = next_head;
    plan.next_state.direction_down = next_direction_down;
    plan.next_state.cycle_index = next_cycle;
    return plan;
}

static inline db_gradient_damage_plan_t
db_gradient_step_from_runtime(db_pattern_t pattern, uint32_t head_row,
                              int direction_down, uint32_t cycle_index,
                              uint32_t head_step) {
    const int is_sweep = (pattern == DB_PATTERN_GRADIENT_SWEEP);
    return db_gradient_plan_next_frame(head_row, is_sweep ? direction_down : 1,
                                       cycle_index, is_sweep ? 0 : 1,
                                       head_step);
}

static inline size_t
db_gradient_collect_dirty_ranges(const db_gradient_damage_plan_t *plan,
                                 db_dirty_row_range_t out_ranges[2]) {
    if ((plan == NULL) || (out_ranges == NULL)) {
        return 0U;
    }
    size_t count = 0U;
    if (plan->dirty_row_count > 0U) {
        out_ranges[count++] = (db_dirty_row_range_t){
            .row_start = plan->dirty_row_start,
            .row_count = plan->dirty_row_count,
        };
    }
    if (plan->dirty_row_count_second > 0U) {
        out_ranges[count++] = (db_dirty_row_range_t){
            .row_start = plan->dirty_row_start_second,
            .row_count = plan->dirty_row_count_second,
        };
    }
    return count;
}

static inline size_t db_gradient_collect_dirty_ranges_clamped(
    const db_gradient_damage_plan_t *plan, uint32_t max_rows,
    db_dirty_row_range_t *out_ranges, size_t out_capacity) {
    if ((plan == NULL) || (out_ranges == NULL) || (out_capacity == 0U) ||
        (max_rows == 0U)) {
        return 0U;
    }
    db_dirty_row_range_t raw[2] = {{0U, 0U}, {0U, 0U}};
    const size_t raw_count = db_gradient_collect_dirty_ranges(plan, raw);
    size_t out_count = 0U;
    for (size_t index = 0U; (index < raw_count) && (out_count < out_capacity);
         index++) {
        const db_dirty_row_range_t range = raw[index];
        if ((range.row_count == 0U) || (range.row_start >= max_rows)) {
            continue;
        }
        const uint32_t clamped_end =
            db_u32_min(max_rows, range.row_start + range.row_count);
        if (clamped_end <= range.row_start) {
            continue;
        }
        out_ranges[out_count++] = (db_dirty_row_range_t){
            .row_start = range.row_start,
            .row_count = clamped_end - range.row_start,
        };
    }
    return out_count;
}

static inline size_t db_append_nonzero_row_ranges(
    const db_dirty_row_range_t *ranges, size_t range_count,
    db_dirty_row_range_t *out_ranges, size_t out_capacity, size_t out_count) {
    if ((ranges == NULL) || (out_ranges == NULL) || (out_capacity == 0U)) {
        return out_count;
    }
    if (out_count >= out_capacity) {
        return out_count;
    }

    const size_t copy_capacity = out_capacity - out_count;
    DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                  "append_nonzero_row_ranges", range_count,
                                  copy_capacity);
    const size_t copy_limit =
        (range_count < copy_capacity) ? range_count : copy_capacity;
    if (copy_limit == 0U) {
        return out_count;
    }

    // Fast path: dense nonzero prefix can be copied directly.
    // Typical after normalization/coalescing.
    size_t index = 0U;
    while ((index < copy_limit) && (ranges[index].row_count != 0U)) {
        index++;
    }
    if (index == copy_limit) {
        db_copy_bytes(out_ranges + out_count, ranges,
                      copy_limit * sizeof(db_dirty_row_range_t));
        return out_count + copy_limit;
    }
    if (index > 0U) {
        db_copy_bytes(out_ranges + out_count, ranges,
                      index * sizeof(db_dirty_row_range_t));
        out_count += index;
    }

    for (; index < copy_limit; index++) {
        if (ranges[index].row_count == 0U) {
            continue;
        }
        out_ranges[out_count++] = ranges[index];
    }
    return out_count;
}

static inline size_t db_gradient_build_curr_draw_ranges(
    const db_dirty_row_range_t *skipped_ranges, size_t skipped_count,
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_dirty_row_range_t *out_ranges, size_t out_capacity) {
    if ((out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }
    // Fast path: only dirty ranges contributed this frame.
    // Avoids an unnecessary append pass over skipped ranges.
    if ((skipped_count == 0U) && (dirty_ranges != NULL) && (dirty_count > 0U) &&
        (dirty_count <= out_capacity)) {
        return db_append_nonzero_row_ranges(dirty_ranges, dirty_count,
                                            out_ranges, out_capacity, 0U);
    }
    // Fast path: only skipped/replay ranges contributed this frame.
    // Avoids an unnecessary append pass over current dirty ranges.
    if ((dirty_count == 0U) && (skipped_ranges != NULL) &&
        (skipped_count > 0U) && (skipped_count <= out_capacity)) {
        return db_append_nonzero_row_ranges(skipped_ranges, skipped_count,
                                            out_ranges, out_capacity, 0U);
    }

    size_t out_count = db_append_nonzero_row_ranges(
        skipped_ranges, skipped_count, out_ranges, out_capacity, 0U);
    out_count = db_append_nonzero_row_ranges(
        dirty_ranges, dirty_count, out_ranges, out_capacity, out_count);
    return out_count;
}

static inline size_t
db_gradient_row_range_lower_bound(const db_dirty_row_range_t *ranges,
                                  size_t sorted_count, uint32_t row_start) {
    if ((ranges == NULL) || (sorted_count == 0U)) {
        return 0U;
    }
    if (row_start <= ranges[0].row_start) {
        return 0U;
    }

    size_t hi = 1U;
    while ((hi < sorted_count) && (ranges[hi].row_start < row_start)) {
        hi <<= 1U;
    }
    size_t lo = hi >> 1U;
    if (hi > sorted_count) {
        hi = sorted_count;
    }
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) >> 1U);
        if (ranges[mid].row_start < row_start) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static inline size_t db_gradient_normalize_row_ranges(
    const db_dirty_row_range_t *source_ranges, size_t source_count,
    db_dirty_row_range_t *out_ranges, size_t out_capacity) {
    if ((source_ranges == NULL) || (out_ranges == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }

    DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                  "gradient_normalize_row_ranges", source_count,
                                  out_capacity);
    size_t out_count = 0U;
    for (size_t index = 0U;
         (index < source_count) && (out_count < out_capacity); index++) {
        const db_dirty_row_range_t candidate = source_ranges[index];
        if (candidate.row_count == 0U) {
            continue;
        }
        if ((out_count == 0U) ||
            (candidate.row_start >= out_ranges[out_count - 1U].row_start)) {
            out_ranges[out_count++] = candidate;
            continue;
        }
        const size_t insert_index = db_gradient_row_range_lower_bound(
            out_ranges, out_count, candidate.row_start);
        db_move_bytes(out_ranges + insert_index + 1U, out_ranges + insert_index,
                      (out_count - insert_index) *
                          sizeof(db_dirty_row_range_t));
        out_ranges[insert_index] = candidate;
        out_count++;
    }

    if (out_count <= 1U) {
        return out_count;
    }

    size_t merged_count = 0U;
    for (size_t index = 0U; index < out_count; index++) {
        const db_dirty_row_range_t current = out_ranges[index];
        if (merged_count == 0U) {
            out_ranges[merged_count++] = current;
            continue;
        }
        db_dirty_row_range_t *tail = &out_ranges[merged_count - 1U];
        const uint32_t tail_end =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_tail_end",
                               tail->row_start, tail->row_count);
        const uint32_t current_end =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_current_end",
                               current.row_start, current.row_count);
        if (current.row_start <= tail_end) {
            if (current_end > tail_end) {
                tail->row_count = db_checked_sub_u32(
                    DB_BENCH_COMMON_BACKEND, "gradient_merged_count",
                    current_end, tail->row_start);
            }
            continue;
        }
        out_ranges[merged_count++] = current;
    }
    return merged_count;
}

static inline size_t db_gradient_subtract_replay_ranges(
    const db_dirty_row_range_t *base_ranges, size_t base_count,
    const db_dirty_row_range_t *cut_ranges, size_t cut_count,
    db_dirty_row_range_t *out_ranges, size_t out_capacity) {
    if ((base_ranges == NULL) || (base_count == 0U) || (out_ranges == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }
    db_dirty_row_range_t normalized_base[DB_GRADIENT_DRAW_RANGE_WORK_CAP] = {
        {0U, 0U}};
    DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                  "gradient_subtract_replay.base_normalize",
                                  base_count, DB_GRADIENT_DRAW_RANGE_WORK_CAP);
    const size_t normalized_base_count = db_gradient_normalize_row_ranges(
        base_ranges, base_count, normalized_base,
        DB_GRADIENT_DRAW_RANGE_WORK_CAP);
    if (normalized_base_count == 0U) {
        return 0U;
    }

    db_dirty_row_range_t normalized_cut[DB_GRADIENT_DRAW_RANGE_WORK_CAP] = {
        {0U, 0U}};
    DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                  "gradient_subtract_replay.cut_normalize",
                                  cut_count, DB_GRADIENT_DRAW_RANGE_WORK_CAP);
    const size_t normalized_cut_count = db_gradient_normalize_row_ranges(
        cut_ranges, cut_count, normalized_cut, DB_GRADIENT_DRAW_RANGE_WORK_CAP);
    if (normalized_cut_count == 0U) {
        // Fast path: normalized cut set is empty, so subtraction is a copy.
        return db_append_nonzero_row_ranges(normalized_base,
                                            normalized_base_count, out_ranges,
                                            out_capacity, 0U);
    }

    // Main path: linear two-pointer subtraction over normalized ranges.
    size_t out_count = 0U;
    size_t cut_index = 0U;
    for (size_t base_index = 0U;
         (base_index < normalized_base_count) && (out_count < out_capacity);
         base_index++) {
        const db_dirty_row_range_t base = normalized_base[base_index];
        const uint32_t base_start = base.row_start;
        const uint32_t base_end =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_base_end",
                               base_start, base.row_count);
        uint32_t current_start = base_start;

        while (cut_index < normalized_cut_count) {
            const db_dirty_row_range_t cut = normalized_cut[cut_index];
            const uint32_t cut_start = cut.row_start;
            const uint32_t cut_end =
                db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_cut_end",
                                   cut_start, cut.row_count);

            if (cut_end <= current_start) {
                cut_index++;
                continue;
            }
            if (cut_start >= base_end) {
                break;
            }
            if ((current_start < cut_start) && (out_count < out_capacity)) {
                out_ranges[out_count++] = (db_dirty_row_range_t){
                    .row_start = current_start,
                    .row_count = db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                                    "gradient_left_count",
                                                    cut_start, current_start),
                };
            }
            if (cut_end >= base_end) {
                current_start = base_end;
                break;
            }
            current_start = cut_end;
            cut_index++;
        }
        if ((current_start < base_end) && (out_count < out_capacity)) {
            out_ranges[out_count++] = (db_dirty_row_range_t){
                .row_start = current_start,
                .row_count = db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_right_count",
                                                base_end, current_start),
            };
        }
    }
    return out_count;
}

static inline void
db_gradient_apply_step_to_runtime(db_benchmark_runtime_init_t *runtime,
                                  const db_gradient_damage_plan_t *plan) {
    if ((runtime == NULL) || (plan == NULL)) {
        return;
    }
    runtime->gradient.head_row = plan->next_state.head_row;
    runtime->gradient.cycle_index = plan->next_state.cycle_index;
    runtime->gradient.direction_down = plan->next_state.direction_down;
}

static inline void
db_gradient_row_color_rgb(uint32_t row_index, uint32_t head_row,
                          int direction_down, uint32_t cycle_index,
                          double *out_r, double *out_g, double *out_b) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_window_rows_effective();
    double source_r = 0.0;
    double source_g = 0.0;
    double source_b = 0.0;
    double target_r = 0.0;
    double target_g = 0.0;
    double target_b = 0.0;
    db_palette_cycle_color_rgb(cycle_index, &source_r, &source_g, &source_b);
    db_palette_cycle_color_rgb(cycle_index + 1U, &target_r, &target_g,
                               &target_b);
    if ((rows == 0U) || (window_rows == 0U)) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }

    const uint32_t row = row_index % rows;
    const int64_t head_start_i64 = (int64_t)head_row - (int64_t)window_rows;
    const int64_t head_end_i64 = head_start_i64 + (int64_t)window_rows;
    const int64_t row_i64 = (int64_t)row;
    if (row_i64 < head_start_i64) {
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
    if (row_i64 >= head_end_i64) {
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
    const uint64_t delta_u64 = (uint64_t)(row_i64 - head_start_i64);
    const uint32_t delta = db_checked_u64_to_u32(DB_BENCH_COMMON_BACKEND,
                                                 "gradient_delta", delta_u64);

    double blend = 1.0;
    if (window_rows > 1U) {
        const double blend_t = (double)delta / (double)(window_rows - 1U);
        blend = (direction_down != 0) ? (1.0 - blend_t) : blend_t;
    }
    *out_r = source_r + ((target_r - source_r) * blend);
    *out_g = source_g + ((target_g - source_g) * blend);
    *out_b = source_b + ((target_b - source_b) * blend);
}

static inline void db_for_each_gradient_row_color(
    uint32_t row_start, uint32_t row_count, uint32_t head_row,
    int direction_down, uint32_t cycle_index,
    db_gradient_row_color_apply_fn_t apply_row_color, void *user_data) {
    const uint32_t rows = db_grid_rows_effective();
    if ((rows == 0U) || (row_count == 0U) || (apply_row_color == NULL)) {
        return;
    }
    for (uint32_t i = 0U; i < row_count; i++) {
        const uint32_t row = row_start + i;
        if (row >= rows) {
            break;
        }
        double row_r = 0.0;
        double row_g = 0.0;
        double row_b = 0.0;
        db_gradient_row_color_rgb(row, head_row, direction_down, cycle_index,
                                  &row_r, &row_g, &row_b);
        apply_row_color(row, row_r, row_g, row_b, user_data);
    }
}

#endif
