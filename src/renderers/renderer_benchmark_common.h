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
#include "../core/db_core.h"
#include "../core/db_hash.h"

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
#define DB_COLOR_CHANNEL_BIAS 0.20F
#define DB_COLOR_CHANNEL_SCALE 0.75F
#define DB_GRADIENT_WINDOW_ROWS 32U
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
    uint32_t render_head_row;
    int render_direction_down;
    uint32_t render_cycle_index;
    uint32_t next_head_row;
    int next_direction_down;
    uint32_t next_cycle_index;
    uint32_t dirty_row_start;
    uint32_t dirty_row_count;
    uint32_t dirty_row_start_second;
    uint32_t dirty_row_count_second;
} db_gradient_damage_plan_t;

typedef struct {
    db_gradient_damage_plan_t plan;
    int render_direction_down;
    int next_mode_phase_flag;
} db_gradient_step_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
} db_dirty_row_range_t;

typedef struct {
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    uint32_t snake_shape_index;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t snake_batch_size;
    int snake_phase_completed;
    int mode_phase_flag;
    uint32_t gradient_head_row;
    uint32_t gradient_cycle;
    uint32_t bench_speed_step;
    uint32_t random_seed;
    uint32_t pattern_seed;
} db_benchmark_runtime_init_t;

static inline uint64_t
db_benchmark_runtime_state_hash(const db_benchmark_runtime_init_t *runtime,
                                uint64_t frame_index, uint32_t render_width,
                                uint32_t render_height) {
    if (runtime == NULL) {
        return 0U;
    }
    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = db_fnv1a64_mix_u64(hash, frame_index);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->pattern);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->work_unit_count);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->draw_vertex_count);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_shape_index);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_cursor);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_prev_start);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_prev_count);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_batch_size);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->snake_phase_completed);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->mode_phase_flag);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->gradient_head_row);
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->gradient_cycle);
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

static inline int db_pattern_uses_history_texture(db_pattern_t pattern) {
    return (pattern == DB_PATTERN_SNAKE_GRID) ||
           (pattern == DB_PATTERN_SNAKE_RECT) ||
           (pattern == DB_PATTERN_SNAKE_SHAPES);
}

static inline uint32_t db_pattern_work_unit_count(db_pattern_t pattern) {
    if ((pattern == DB_PATTERN_SNAKE_GRID) ||
        (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (pattern == DB_PATTERN_GRADIENT_FILL) ||
        (pattern == DB_PATTERN_SNAKE_RECT) ||
        (pattern == DB_PATTERN_SNAKE_SHAPES)) {
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

    if (requested != DB_PATTERN_BANDS) {
        out_state->random_seed =
            db_benchmark_random_seed_from_runtime_or_time(backend_name);
        out_state->pattern_seed = out_state->random_seed;
        out_state->gradient_cycle = db_benchmark_cycle_from_seed(
            out_state->random_seed, DB_U32_SALT_PALETTE);
        out_state->gradient_head_row = 0U;
        out_state->mode_phase_flag =
            ((requested == DB_PATTERN_GRADIENT_SWEEP) ||
             (requested == DB_PATTERN_GRADIENT_FILL))
                ? 1
                : 0;
    }
    if ((requested == DB_PATTERN_SNAKE_GRID) ||
        (requested == DB_PATTERN_SNAKE_RECT) ||
        (requested == DB_PATTERN_SNAKE_SHAPES)) {
        out_state->snake_cursor = UINT32_MAX;
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
    if (vertices == NULL) {
        return;
    }
    for (uint32_t tile_index = 0U; tile_index < tile_count; tile_index++) {
        const size_t tile_float_offset =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * stride_floats;
        float *unit = &vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, color_r,
                             color_g, color_b);
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

static inline float db_color_channel(uint32_t seed) {
    const float normalized = (float)(seed & 255U) / 255.0F;
    return DB_COLOR_CHANNEL_BIAS + (normalized * DB_COLOR_CHANNEL_SCALE);
}

static inline void db_palette_cycle_color_rgb(uint32_t cycle_index,
                                              float *out_r, float *out_g,
                                              float *out_b) {
    const uint32_t phase_seed = DB_U32_SALT_PALETTE;
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_PALETTE_SALT_BASE_STEP) ^ phase_seed);
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
            next_head = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                           "gradient_head_next", next_head, 1U);
            if (next_head > max_head) {
                next_head = 0U;
                wrap_count = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_wrap_count_next",
                                                wrap_count, 1U);
                next_cycle = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_palette_cycle_next",
                                                next_cycle, 1U);
            }
        } else {
            if (next_direction_down != 0) {
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
                sample_head = db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                                 "gradient_sample_head_next",
                                                 sample_head, 1U);
                if (sample_head > max_head) {
                    sample_head = 0U;
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

    plan.render_head_row = next_head;
    plan.render_direction_down = next_direction_down;
    plan.render_cycle_index = next_cycle;
    plan.next_head_row = next_head;
    plan.next_direction_down = next_direction_down;
    plan.next_cycle_index = next_cycle;
    return plan;
}

static inline db_gradient_step_t
db_gradient_step_from_runtime(db_pattern_t pattern, uint32_t head_row,
                              int mode_phase_flag, uint32_t cycle_index,
                              uint32_t head_step) {
    db_gradient_step_t result = {0};
    const int is_sweep = (pattern == DB_PATTERN_GRADIENT_SWEEP);
    result.plan =
        db_gradient_plan_next_frame(head_row, is_sweep ? mode_phase_flag : 1,
                                    cycle_index, is_sweep ? 0 : 1, head_step);
    result.render_direction_down =
        is_sweep ? result.plan.render_direction_down : 1;
    result.next_mode_phase_flag = result.plan.next_direction_down;
    return result;
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

static inline void
db_gradient_apply_step_to_runtime(db_benchmark_runtime_init_t *runtime,
                                  const db_gradient_step_t *step) {
    if ((runtime == NULL) || (step == NULL)) {
        return;
    }
    runtime->gradient_head_row = step->plan.next_head_row;
    runtime->mode_phase_flag = step->next_mode_phase_flag;
    runtime->gradient_cycle = step->plan.next_cycle_index;
}

static inline void db_gradient_row_color_rgb(uint32_t row_index,
                                             uint32_t head_row,
                                             int direction_down,
                                             uint32_t cycle_index, float *out_r,
                                             float *out_g, float *out_b) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_window_rows_effective();
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

    float blend = 1.0F;
    if (window_rows > 1U) {
        const float blend_t = (float)delta / (float)(window_rows - 1U);
        blend = (direction_down != 0) ? (1.0F - blend_t) : blend_t;
    }

    db_blend_rgb(source_r, source_g, source_b, target_r, target_g, target_b,
                 blend, out_r, out_g, out_b);
}

#endif
