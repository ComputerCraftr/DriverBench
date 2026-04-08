#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_RUNTIME_INTERNAL_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_RUNTIME_INTERNAL_H

#include "renderer_benchmark_types.h"

typedef struct {
    db_grid_block_t draw_blocks[4];
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

typedef struct {
    db_grid_block_t block;
    double rgb[3];
} db_gradient_row_segment_t;

typedef struct {
    db_grid_block_t block;
    uint32_t row_end;
    uint32_t transition_start;
    uint32_t transition_end;
    uint32_t next_row;
    uint32_t head_row;
    uint32_t cycle_index;
    int direction_down;
    int phase;
    double source_rgb[3];
    double target_rgb[3];
} db_gradient_row_segment_iter_t;

enum {
    DB_GRADIENT_SEGMENT_PHASE_TOP = 0,
    DB_GRADIENT_SEGMENT_PHASE_BLEND = 1,
    DB_GRADIENT_SEGMENT_PHASE_BOTTOM = 2,
    DB_GRADIENT_SEGMENT_PHASE_DONE = 3,
    DB_GRADIENT_SEGMENT_PHASE_SOLID = 10,
};

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
    return BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_grid_cols_effective(void) {
    return BENCH_WINDOW_WIDTH_PX;
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

static inline void db_benchmark_seed_background_color_rgb3(
    const db_benchmark_runtime_init_t *runtime, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    static const double phase0_rgb[3] = {
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
    static const double phase1_rgb[3] = {
        BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B};

    if ((runtime != NULL) && (runtime->pattern == DB_PATTERN_SNAKE_GRID)) {
        // Snake grid alternates between two stable full-grid phases. Seed to
        // the current base phase so the first dirty update composes correctly.
        if (runtime->snake.grid_phase_flag == 0) {
            db_copy_f64_rgb3(out_rgb, phase0_rgb);
        } else {
            db_copy_f64_rgb3(out_rgb, phase1_rgb);
        }
        return;
    }

    // Keep non-snake seed behavior stable.
    db_copy_f64_rgb3(out_rgb, phase0_rgb);
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
    return db_checked_ulong_to_u32(backend_name, DB_RUNTIME_OPT_RANDOM_SEED,
                                   parsed);
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
    if (rounded_up > DB_BENCH_SPEED_STEP_MAX) {
        db_failf(backend_name,
                 "Invalid %s='%.9g' (max effective per-frame step: %u)",
                 DB_RUNTIME_OPT_BENCH_SPEED, parsed, DB_BENCH_SPEED_STEP_MAX);
    }
    return db_checked_double_to_u32(backend_name, DB_RUNTIME_OPT_BENCH_SPEED,
                                    rounded_up);
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

static inline void
db_log_renderer_capability_mode(const char *backend_name,
                                const char *capability_mode) {
    if ((backend_name == NULL) || (capability_mode == NULL)) {
        return;
    }
    db_infof(backend_name, "using capability mode: %s", capability_mode);
}

static inline void db_log_renderer_scheduler_mode(const char *backend_name,
                                                  const char *scheduler_mode) {
    if ((backend_name == NULL) || (scheduler_mode == NULL)) {
        return;
    }
    db_infof(backend_name, "using scheduler mode: %s", scheduler_mode);
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

#endif
