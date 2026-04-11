#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_RUNTIME_INTERNAL_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_RUNTIME_INTERNAL_H

#include "../config/benchmark_config.h"
#include "../config/runtime_options.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../core/db_log.h"
#include "../core/db_numeric.h"
#include "../core/db_renderer_support.h"
#include "benchmarks/db_benchmark_mode_flags.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include <string.h>
#include <time.h>

typedef struct {
    db_grid_block_t draw_blocks[4];
    size_t draw_count;
    db_gradient_state_t state;
} db_gradient_backbuffer_replay_state_t;

typedef struct {
    const char *benchmark_mode_text;
    const char *bench_speed_text;
    const char *random_seed_text;
    int backbuffer_draw_full;
} db_benchmark_runtime_options_t;

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
    const db_benchmark_runtime_init_t *runtime, uint32_t render_width,
    uint32_t render_height) {
    if (runtime == NULL) {
        return 0U;
    }
    uint64_t hash = DB_FNV1A64_OFFSET;
    hash = db_fnv1a64_mix_u64(hash, (uint64_t)runtime->pattern);
    hash = db_fnv1a64_mix_u64(hash, runtime->simulation_work);
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

static inline void db_benchmark_seed_background_color_rgb3(
    const db_benchmark_runtime_init_t *runtime, double *rgb);

static inline db_renderer_execution_config_t
db_benchmark_renderer_execution_config(
    const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_renderer_execution_config_t){0};
    }
    const db_pattern_mode_flags_t flags =
        db_pattern_mode_flags(runtime->pattern);
    double seed_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(runtime, seed_rgb);
    return (db_renderer_execution_config_t){
        .work_unit_count = runtime->work_unit_count,
        .grid_cols = db_grid_cols_effective(),
        .grid_rows = db_grid_rows_effective(),
        .seed_rgba_f64 = {seed_rgb[0], seed_rgb[1], seed_rgb[2], 1.0},
        .backbuffer_draw_full = runtime->backbuffer_draw_full,
        .backbuffer_replay_enabled =
            DB_BOOL((runtime->backbuffer_draw_full == 0) &&
                    ((flags.is_gradient != 0) || (flags.is_snake != 0))),
        .pipeline =
            {
                .uses_dirty_backbuffer_mode =
                    DB_BOOL(runtime->backbuffer_draw_full == 0),
                .uses_ff_rect_draw_mode =
                    DB_BOOL((flags.is_gradient != 0) || (flags.is_bands != 0)),
                .uses_history_pipeline =
                    DB_BOOL((flags.is_snake != 0) || (flags.is_gradient != 0)),
            },
    };
}

static inline int
db_parse_benchmark_pattern_from_text(const char *mode,
                                     db_pattern_t *out_pattern) {
    if (mode == NULL) {
        *out_pattern = DB_PATTERN_GRADIENT_SWEEP;
        return 1;
    }
    static const struct {
        const char *name;
        db_pattern_t pattern;
    } mapping[] = {
        {DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_PATTERN_GRADIENT_SWEEP},
        {DB_BENCHMARK_MODE_BANDS, DB_PATTERN_BANDS},
        {DB_BENCHMARK_MODE_SNAKE_GRID, DB_PATTERN_SNAKE_GRID},
        {DB_BENCHMARK_MODE_GRADIENT_FILL, DB_PATTERN_GRADIENT_FILL},
        {DB_BENCHMARK_MODE_SNAKE_RECT, DB_PATTERN_SNAKE_RECT},
        {DB_BENCHMARK_MODE_SNAKE_SHAPES, DB_PATTERN_SNAKE_SHAPES},
    };
    for (size_t i = 0U; i < (sizeof(mapping) / sizeof(mapping[0])); i++) {
        if (strcmp(mode, mapping[i].name) == 0) {
            *out_pattern = mapping[i].pattern;
            return 1;
        }
    }
    *out_pattern = DB_PATTERN_GRADIENT_SWEEP;
    return 0;
}

static inline int
db_parse_benchmark_pattern_from_runtime(db_pattern_t *out_pattern) {
    return db_parse_benchmark_pattern_from_text(
        db_runtime_option_get(DB_RUNTIME_OPT_BENCHMARK_MODE), out_pattern);
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
        DB_MIN(db_grid_rows_effective(), DB_GRADIENT_WINDOW_ROWS);
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

    const db_pattern_mode_flags_t pattern_flags =
        db_pattern_mode_flags(runtime->pattern);

    if ((runtime != NULL) && (pattern_flags.is_snake != 0)) {
        if ((pattern_flags.is_snake_region_mode == 0) &&
            (runtime->snake.grid_phase_flag != 0)) {
            memcpy(out_rgb, phase1_rgb, 3U * sizeof(double));
            return;
        }
        memcpy(out_rgb, phase0_rgb, 3U * sizeof(double));
        return;
    }

    // Keep non-snake seed behavior stable.
    memcpy(out_rgb, phase0_rgb, 3U * sizeof(double));
}

static inline uint32_t db_pattern_seed_from_time(void) {
    const time_t now = time(NULL);
    if (now == (time_t)-1) {
        DB_RUNTIME_FAIL(DB_BENCH_COMMON_BACKEND,
                        "time() failed for random seed");
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
db_benchmark_random_seed_from_text_or_time(const char *backend_name,
                                           const char *value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return db_pattern_seed_from_time();
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 0);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        DB_RUNTIME_FAIL(backend_name, "Invalid %s='%s'",
                        DB_RUNTIME_OPT_RANDOM_SEED, value);
    }
    return db_checked_ulong_to_u32(backend_name, DB_RUNTIME_OPT_RANDOM_SEED,
                                   parsed);
}

static inline uint32_t
db_benchmark_random_seed_from_runtime_or_time(const char *backend_name) {
    return db_benchmark_random_seed_from_text_or_time(
        backend_name, db_runtime_option_get(DB_RUNTIME_OPT_RANDOM_SEED));
}

static inline uint32_t
db_benchmark_speed_step_from_text(const char *backend_name, const char *value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 1U;
    }
    char *end = NULL;
    const double parsed = strtod(value, &end);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        !isfinite(parsed) || (parsed <= 0.0)) {
        DB_RUNTIME_FAIL(backend_name, "Invalid %s='%s' (expected: > 0)",
                        DB_RUNTIME_OPT_BENCH_SPEED, value);
    }
    double rounded_up = ceil(parsed);
    if (rounded_up < 1.0) {
        rounded_up = 1.0;
    }
    if (rounded_up > DB_BENCH_SPEED_STEP_MAX) {
        DB_RUNTIME_FAIL(backend_name,
                        "Invalid %s='%.9g' (max effective per-frame step: %u)",
                        DB_RUNTIME_OPT_BENCH_SPEED, parsed,
                        DB_BENCH_SPEED_STEP_MAX);
    }
    return db_checked_double_to_u32(backend_name, DB_RUNTIME_OPT_BENCH_SPEED,
                                    rounded_up);
}

static inline uint32_t
db_benchmark_speed_step_from_runtime(const char *backend_name) {
    return db_benchmark_speed_step_from_text(
        backend_name, db_runtime_option_get(DB_RUNTIME_OPT_BENCH_SPEED));
}

static inline void db_log_benchmark_mode(const char *backend_name,
                                         db_pattern_t pattern,
                                         uint32_t pattern_seed,
                                         uint32_t bench_speed_step) {
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("benchmark_mode", db_pattern_mode_name(pattern)),
        DB_LOG_U64("seed", pattern_seed),
        DB_LOG_U64("speed_step", bench_speed_step),
        DB_LOG_U64("grid_rows", db_grid_rows_effective()),
        DB_LOG_U64("grid_cols", db_grid_cols_effective()),
    };
    db_log_info(backend_name, "benchmark_config", fields,
                DB_LOG_FIELD_COUNT(fields));
    const db_pattern_mode_flags_t flags = db_pattern_mode_flags(pattern);
    if (flags.is_gradient != 0) {
        const db_log_field_t gradient_fields[] = {
            DB_LOG_U64("window_rows", db_gradient_window_rows_effective()),
        };
        db_log_info(backend_name, "gradient_config", gradient_fields,
                    DB_LOG_FIELD_COUNT(gradient_fields));
    }
}

static inline uint32_t db_pattern_work_unit_count(void) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t cols = db_grid_cols_effective();
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
    return db_pattern_work_unit_count();
}

static inline int db_init_benchmark_runtime_from_options(
    const char *backend_name, const db_benchmark_runtime_options_t *options,
    db_benchmark_runtime_init_t *out_state) {
    const db_benchmark_runtime_options_t safe_options =
        (options != NULL) ? *options : (db_benchmark_runtime_options_t){0};
    db_pattern_t requested = DB_PATTERN_GRADIENT_SWEEP;
    if (!db_parse_benchmark_pattern_from_text(safe_options.benchmark_mode_text,
                                              &requested)) {
        const char *mode = safe_options.benchmark_mode_text;
        DB_RUNTIME_FAIL(
            backend_name, "Invalid %s='%s' (expected: %s|%s|%s|%s|%s|%s)",
            DB_RUNTIME_OPT_BENCHMARK_MODE, (mode != NULL) ? mode : "",
            DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_BENCHMARK_MODE_BANDS,
            DB_BENCHMARK_MODE_SNAKE_GRID, DB_BENCHMARK_MODE_GRADIENT_FILL,
            DB_BENCHMARK_MODE_SNAKE_RECT, DB_BENCHMARK_MODE_SNAKE_SHAPES);
    }

    *out_state = (db_benchmark_runtime_init_t){0};
    out_state->pattern = requested;
    out_state->work_unit_count = db_pattern_work_unit_count();
    if (out_state->work_unit_count == 0U) {
        DB_RUNTIME_FAIL(backend_name,
                        "Invalid work-unit geometry for mode '%s'",
                        db_pattern_mode_name(requested));
    }
    const uint64_t draw_vertex_count_u64 =
        (uint64_t)out_state->work_unit_count * DB_RECT_VERTEX_COUNT;
    if (draw_vertex_count_u64 > UINT32_MAX) {
        DB_RUNTIME_FAIL(backend_name,
                        "draw vertex count overflow for mode '%s'",
                        db_pattern_mode_name(requested));
    }
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        backend_name, "draw_vertex_count", draw_vertex_count_u64);
    out_state->bench_speed_step = db_benchmark_speed_step_from_text(
        backend_name, safe_options.bench_speed_text);
    out_state->backbuffer_draw_full =
        DB_BOOL(safe_options.backbuffer_draw_full);

    if ((requested != DB_PATTERN_BANDS) &&
        (requested != DB_PATTERN_SNAKE_GRID)) {
        out_state->pattern_seed = db_benchmark_random_seed_from_text_or_time(
            backend_name, safe_options.random_seed_text);
    }
    if ((requested == DB_PATTERN_GRADIENT_SWEEP) ||
        (requested == DB_PATTERN_GRADIENT_FILL)) {
        out_state->gradient.cycle_index = db_benchmark_cycle_from_seed(
            out_state->pattern_seed, DB_U32_SALT_PALETTE);
        out_state->gradient.head_row = 0U;
        out_state->gradient.direction_down = 0;
        if (requested == DB_PATTERN_GRADIENT_SWEEP) {
            out_state->gradient.head_row = 0U;
            out_state->gradient.direction_down = 0;
            out_state->gradient.cycle_index =
                db_u32_wrapping_sub(out_state->gradient.cycle_index, 1U);
        } else if (requested == DB_PATTERN_GRADIENT_FILL) {
            out_state->gradient.head_row = db_checked_add_u32(
                DB_BENCH_COMMON_BACKEND, "gradient_init_head_max",
                db_grid_rows_effective(), db_gradient_window_rows_effective());
            out_state->gradient.direction_down = 1;
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

static inline int
db_init_benchmark_runtime_common(const char *backend_name,
                                 db_benchmark_runtime_init_t *out_state) {
    const char *backbuffer_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE);
    return db_init_benchmark_runtime_from_options(
        backend_name,
        &(const db_benchmark_runtime_options_t){
            .benchmark_mode_text =
                db_runtime_option_get(DB_RUNTIME_OPT_BENCHMARK_MODE),
            .bench_speed_text =
                db_runtime_option_get(DB_RUNTIME_OPT_BENCH_SPEED),
            .random_seed_text =
                db_runtime_option_get(DB_RUNTIME_OPT_RANDOM_SEED),
            .backbuffer_draw_full = (backbuffer_mode != NULL) &&
                                    (strcmp(backbuffer_mode, "full") == 0),
        },
        out_state);
}

#endif
