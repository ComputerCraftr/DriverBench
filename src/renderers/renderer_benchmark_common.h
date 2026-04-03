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
#include "../config/runtime_options.h"
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
#define DB_RGBA16F_CHANNELS_PER_PIXEL 4U
#define DB_RGBA16F_BYTES_PER_PIXEL                                             \
    (sizeof(uint16_t) * DB_RGBA16F_CHANNELS_PER_PIXEL)
#define DB_RGBA8_BYTES_PER_PIXEL 4U

typedef enum {
    DB_PATTERN_GRADIENT_SWEEP = 0,
    DB_PATTERN_BANDS = 1,
    DB_PATTERN_SNAKE_GRID = 2,
    DB_PATTERN_GRADIENT_FILL = 3,
    DB_PATTERN_SNAKE_RECT = 4,
    DB_PATTERN_SNAKE_SHAPES = 5,
} db_pattern_t;

static inline uint32_t db_checked_pattern_to_u32(const char *backend,
                                                 const char *field_name,
                                                 db_pattern_t pattern) {
    return db_checked_int_to_u32(backend, field_name, (int)pattern);
}

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
    uint32_t col_start;
    uint32_t col_count;
} db_grid_block_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
} db_damage_block_t;

typedef struct {
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t *pixels_rgba8;
    uint16_t *pixels_rgba16f;
    int uses_rgba16f;
} db_benchmark_pixel_surface_t;

static inline db_grid_block_t db_grid_block_full(uint32_t row_count,
                                                 uint32_t col_count) {
    return (db_grid_block_t){
        .row_start = 0U,
        .row_count = row_count,
        .col_start = 0U,
        .col_count = col_count,
    };
}

static inline db_damage_block_t db_damage_block_full(uint32_t row_count,
                                                     uint32_t col_count) {
    return (db_damage_block_t){
        .row_start = 0U,
        .row_count = row_count,
        .col_start = 0U,
        .col_count = col_count,
    };
}

static inline uint32_t db_checked_span_end_u32(const char *backend,
                                               const char *field_name,
                                               uint32_t start, uint32_t count) {
    return db_checked_add_u32(backend, field_name, start, count);
}

static inline uint32_t
db_grid_block_row_end_or_fail(const char *field_name,
                              const db_grid_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND, field_name,
                                   block->row_start, block->row_count);
}

static inline uint32_t
db_grid_block_col_end_or_fail(const char *field_name,
                              const db_grid_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND, field_name,
                                   block->col_start, block->col_count);
}

static inline uint32_t
db_damage_block_row_end_or_fail(const char *field_name,
                                const db_damage_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND, field_name,
                                   block->row_start, block->row_count);
}

static inline uint32_t
db_damage_block_col_end_or_fail(const char *field_name,
                                const db_damage_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND, field_name,
                                   block->col_start, block->col_count);
}

static inline uint32_t
db_grid_block_span_units_or_fail(const char *field_name,
                                 const db_grid_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_mul_u32(DB_BENCH_COMMON_BACKEND, field_name,
                              block->row_count, block->col_count);
}

static inline uint32_t
db_damage_block_span_units_or_fail(const char *field_name,
                                   const db_damage_block_t *block) {
    if (block == NULL) {
        db_failf(DB_BENCH_COMMON_BACKEND, "%s block is NULL", field_name);
    }
    return db_checked_mul_u32(DB_BENCH_COMMON_BACKEND, field_name,
                              block->row_count, block->col_count);
}

static inline db_damage_block_t
db_damage_block_from_grid_block(const db_grid_block_t *block) {
    if (block == NULL) {
        return (db_damage_block_t){0U, 0U, 0U, 0U};
    }
    return (db_damage_block_t){
        .row_start = block->row_start,
        .row_count = block->row_count,
        .col_start = block->col_start,
        .col_count = block->col_count,
    };
}

static inline int db_grid_block_to_pixel_block(
    uint32_t grid_cols, uint32_t grid_rows, const db_grid_block_t *grid_block,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_block) {
    if ((out_block == NULL) || (grid_cols == 0U) || (grid_rows == 0U) ||
        (grid_block == NULL) || (pixel_width == 0U) || (pixel_height == 0U) ||
        (grid_block->row_count == 0U) || (grid_block->col_count == 0U) ||
        (grid_block->row_start >= grid_rows) ||
        (grid_block->col_start >= grid_cols)) {
        return 0;
    }
    const uint32_t row_end = db_u32_min(
        grid_rows, db_grid_block_row_end_or_fail("grid_row_end", grid_block));
    const uint32_t col_end = db_u32_min(
        grid_cols, db_grid_block_col_end_or_fail("grid_col_end", grid_block));
    if ((row_end <= grid_block->row_start) ||
        (col_end <= grid_block->col_start)) {
        return 0;
    }
    const uint32_t x0 =
        (uint32_t)(((uint64_t)grid_block->col_start * (uint64_t)pixel_width) /
                   (uint64_t)grid_cols);
    uint32_t x1 = (uint32_t)(((uint64_t)col_end * (uint64_t)pixel_width) /
                             (uint64_t)grid_cols);
    if (col_end == grid_cols) {
        x1 = pixel_width;
    }
    const uint32_t y0 =
        (uint32_t)(((uint64_t)grid_block->row_start * (uint64_t)pixel_height) /
                   (uint64_t)grid_rows);
    uint32_t y1 = (uint32_t)(((uint64_t)row_end * (uint64_t)pixel_height) /
                             (uint64_t)grid_rows);
    if (row_end == grid_rows) {
        y1 = pixel_height;
    }
    if ((x1 <= x0) || (y1 <= y0)) {
        return 0;
    }
    *out_block = (db_damage_block_t){
        .row_start = y0,
        .row_count = y1 - y0,
        .col_start = x0,
        .col_count = x1 - x0,
    };
    return 1;
}

static inline int db_grid_tile_to_pixel_block(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t tile_index,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_block) {
    if (grid_cols == 0U) {
        return 0;
    }
    const db_grid_block_t grid_block = {
        .row_start = tile_index / grid_cols,
        .row_count = 1U,
        .col_start = tile_index % grid_cols,
        .col_count = 1U,
    };
    return db_grid_block_to_pixel_block(grid_cols, grid_rows, &grid_block,
                                        pixel_width, pixel_height, out_block);
}

static inline uint32_t db_grid_axis_edge_to_pixel_coord(uint32_t grid_extent,
                                                        uint32_t edge_index,
                                                        uint32_t pixel_extent) {
    if ((grid_extent == 0U) || (pixel_extent == 0U)) {
        return 0U;
    }
    if (edge_index >= grid_extent) {
        return pixel_extent;
    }
    return (uint32_t)(((uint64_t)edge_index * (uint64_t)pixel_extent) /
                      (uint64_t)grid_extent);
}

static inline float db_pixel_coord_to_ndc_f32(uint32_t pixel_coord,
                                              uint32_t pixel_extent) {
    if (pixel_extent == 0U) {
        return 0.0F;
    }
    const double normalized =
        ((double)pixel_coord * 2.0) / (double)pixel_extent;
    return db_double_to_f32(normalized - 1.0);
}

static inline void db_pixel_bounds_to_ndc_f32(uint32_t x0_px, uint32_t y0_px,
                                              uint32_t x1_px, uint32_t y1_px,
                                              uint32_t pixel_width,
                                              uint32_t pixel_height, float *x0,
                                              float *y0, float *x1, float *y1) {
    if ((x0 == NULL) || (y0 == NULL) || (x1 == NULL) || (y1 == NULL)) {
        return;
    }
    *x0 = db_pixel_coord_to_ndc_f32(x0_px, pixel_width);
    *x1 = db_pixel_coord_to_ndc_f32(x1_px, pixel_width);
    *y0 = db_pixel_coord_to_ndc_f32(y0_px, pixel_height);
    *y1 = db_pixel_coord_to_ndc_f32(y1_px, pixel_height);
}

static inline void db_rgb_pixels_write_index_f64(uint32_t *pixels_rgba8,
                                                 uint16_t *pixels_rgba16f,
                                                 int uses_rgba16f, size_t idx,
                                                 const double *rgb) {
    if (rgb == NULL) {
        return;
    }
    if (uses_rgba16f != 0) {
        const size_t base = idx * DB_RGBA16F_CHANNELS_PER_PIXEL;
        pixels_rgba16f[base + 0U] = db_double_to_f16(rgb[0]);
        pixels_rgba16f[base + 1U] = db_double_to_f16(rgb[1]);
        pixels_rgba16f[base + 2U] = db_double_to_f16(rgb[2]);
        pixels_rgba16f[base + 3U] = DB_F16_ONE;
        return;
    }
    pixels_rgba8[idx] =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
}

// CPU pixel surfaces consume canonical f64 color data only at these helpers.
// Legal export boundaries here are:
// - f64 -> rgba8 packed u8
// - f64 -> rgba16f packed f16
// GPU-facing float submission must use dedicated f64 -> f32 helpers at the
// actual GL/Vulkan consumption sites instead of routing through CPU pixel APIs.
static inline void db_rgb_pixels_read_index_f64(const uint32_t *pixels_rgba8,
                                                const uint16_t *pixels_rgba16f,
                                                int uses_rgba16f, size_t idx,
                                                double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    if (uses_rgba16f != 0) {
        const size_t base = idx * DB_RGBA16F_CHANNELS_PER_PIXEL;
        db_rgb_f16_to_f64_rgb3(&pixels_rgba16f[base], out_rgb);
        return;
    }
    db_unpack_rgba8888_rgb01(pixels_rgba8[idx], out_rgb);
}

static inline void db_rgb_pixels_fill_solid_f64(uint32_t width, uint32_t height,
                                                uint32_t *pixels_rgba8,
                                                uint16_t *pixels_rgba16f,
                                                int uses_rgba16f,
                                                const double *rgb) {
    if ((rgb == NULL) || (width == 0U) || (height == 0U)) {
        return;
    }
    if (uses_rgba16f != 0) {
        const uint16_t rgba_f16[4] = {db_double_to_f16(rgb[0]),
                                      db_double_to_f16(rgb[1]),
                                      db_double_to_f16(rgb[2]), DB_F16_ONE};
        uint16_t *dst = pixels_rgba16f;
        const size_t row_stride = (size_t)width * DB_RGBA16F_CHANNELS_PER_PIXEL;
        for (uint32_t row = 0U; row < height; row++) {
            db_fill_rgba16f_buffer(dst, width, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
    uint32_t *dst = pixels_rgba8;
    for (uint32_t row = 0U; row < height; row++) {
        db_fill_u32_buffer(dst, width, packed_color);
        dst += width;
    }
}

static inline void db_rgb_pixels_fill_damage_block_f64(
    uint32_t width, uint32_t height, uint32_t *pixels_rgba8,
    uint16_t *pixels_rgba16f, int uses_rgba16f, uint32_t row_start,
    uint32_t row_count, uint32_t col_start, uint32_t col_count,
    const double *rgb) {
    if ((rgb == NULL) || (row_start >= height) || (col_start >= width) ||
        (row_count == 0U) || (col_count == 0U)) {
        return;
    }
    const uint32_t span_rows = db_u32_min(row_count, height - row_start);
    const uint32_t span_cols = db_u32_min(col_count, width - col_start);
    if ((span_rows == 0U) || (span_cols == 0U)) {
        return;
    }
    if (uses_rgba16f != 0) {
        const uint16_t rgba_f16[4] = {db_double_to_f16(rgb[0]),
                                      db_double_to_f16(rgb[1]),
                                      db_double_to_f16(rgb[2]), DB_F16_ONE};
        uint16_t *dst =
            pixels_rgba16f + ((((size_t)row_start * width) + col_start) *
                              DB_RGBA16F_CHANNELS_PER_PIXEL);
        const size_t row_stride = (size_t)width * DB_RGBA16F_CHANNELS_PER_PIXEL;
        for (uint32_t row = 0U; row < span_rows; row++) {
            db_fill_rgba16f_buffer(dst, span_cols, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
    uint32_t *dst = pixels_rgba8 + (((size_t)row_start * width) + col_start);
    for (uint32_t row = 0U; row < span_rows; row++) {
        db_fill_u32_buffer(dst, span_cols, packed_color);
        dst += width;
    }
}

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
                                        const float *rgb) {
    if ((unit_base == NULL) || (rgb == NULL)) {
        return;
    }
    float *color = unit_base + color_offset_floats;
    for (uint32_t v = 0; v < DB_RECT_VERTEX_COUNT; v++) {
        db_copy_f32_rgb3(color, rgb);
        color += stride_floats;
    }
}

static inline void
db_set_rect_tile_range_rgb(float *vertices, uint32_t first_tile_index,
                           uint32_t tile_count, size_t stride_floats,
                           size_t color_offset_floats, const float *rgb) {
    if ((vertices == NULL) || (tile_count == 0U) || (rgb == NULL)) {
        return;
    }
    const size_t first_tile_offset =
        (size_t)first_tile_index * DB_RECT_VERTEX_COUNT * stride_floats;
    float *unit = &vertices[first_tile_offset];
    for (uint32_t tile = 0U; tile < tile_count; tile++) {
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, rgb);
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

static inline void db_fill_grid_all_rgb_stride(float *vertices,
                                               uint32_t tile_count,
                                               size_t stride_floats,
                                               size_t color_offset_floats,
                                               const float *rgb) {
    db_set_rect_tile_range_rgb(vertices, 0U, tile_count, stride_floats,
                               color_offset_floats, rgb);
}

static inline void db_band_color_rgb3(uint32_t band_index, uint32_t band_count,
                                      uint32_t frame_index, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const double band_value = (double)band_index;
    const double frame_value = (double)frame_index;
    const double pulse_value =
        BENCH_PULSE_BASE +
        (BENCH_PULSE_AMP * sin((frame_value * BENCH_PULSE_FREQ) +
                               (band_value * BENCH_PULSE_PHASE)));
    const double color_r_value =
        pulse_value * (BENCH_COLOR_R_BASE +
                       (BENCH_COLOR_R_SCALE * band_value / (double)band_count));
    const double band_rgb[3] = {
        color_r_value, pulse_value * BENCH_COLOR_G_SCALE, 1.0 - color_r_value};
    db_copy_f64_rgb3(out_rgb, band_rgb);
}

static inline double db_color_channel(uint32_t seed) {
    const double normalized = db_u8_to_unit_f64(seed);
    return DB_COLOR_CHANNEL_BIAS + (normalized * DB_COLOR_CHANNEL_SCALE);
}

static inline void db_palette_cycle_color_rgb3(uint32_t cycle_index,
                                               double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_PALETTE_SALT_BASE_STEP) ^ DB_U32_SALT_PALETTE);
    const double palette_rgb[3] = {
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B)),
    };
    db_copy_f64_rgb3(out_rgb, palette_rgb);
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
db_gradient_collect_dirty_blocks(const db_gradient_damage_plan_t *plan,
                                 uint32_t max_rows, uint32_t full_width_cols,
                                 db_grid_block_t *out_blocks,
                                 size_t out_capacity) {
    if ((plan == NULL) || (out_blocks == NULL) || (out_capacity == 0U) ||
        (max_rows == 0U) || (full_width_cols == 0U)) {
        return 0U;
    }
    size_t out_count = 0U;
    const db_grid_block_t raw_blocks[2] = {
        {.row_start = plan->dirty_row_start,
         .row_count = plan->dirty_row_count,
         .col_start = 0U,
         .col_count = full_width_cols},
        {.row_start = plan->dirty_row_start_second,
         .row_count = plan->dirty_row_count_second,
         .col_start = 0U,
         .col_count = full_width_cols},
    };
    for (size_t index = 0U; (index < 2U) && (out_count < out_capacity);
         index++) {
        const db_grid_block_t block = raw_blocks[index];
        if ((block.row_count == 0U) || (block.col_count == 0U) ||
            (block.row_start >= max_rows)) {
            continue;
        }
        const uint32_t clamped_end = db_u32_min(
            max_rows,
            db_grid_block_row_end_or_fail("gradient_clamped_end", &block));
        if (clamped_end <= block.row_start) {
            continue;
        }
        out_blocks[out_count++] = (db_grid_block_t){
            .row_start = block.row_start,
            .row_count = clamped_end - block.row_start,
            .col_start = 0U,
            .col_count = full_width_cols,
        };
    }
    return out_count;
}

static inline size_t db_gradient_subtract_replay_blocks(
    const db_grid_block_t *base_blocks, size_t base_count,
    const db_grid_block_t *cut_blocks, size_t cut_count,
    db_grid_block_t *out_blocks, size_t out_capacity) {
    if ((base_blocks == NULL) || (base_count == 0U) || (out_blocks == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }
    uint32_t full_width_cols = 0U;
    if (base_count > 0U) {
        full_width_cols = base_blocks[0].col_count;
    } else if (cut_count > 0U) {
        full_width_cols = cut_blocks[0].col_count;
    }
    size_t out_count = 0U;
    size_t cut_index = 0U;
    for (size_t base_index = 0U;
         (base_index < base_count) && (out_count < out_capacity);
         base_index++) {
        const db_grid_block_t base = base_blocks[base_index];
        if ((base.row_count == 0U) || (base.col_count == 0U)) {
            continue;
        }
        const uint32_t base_start = base.row_start;
        const uint32_t base_end =
            db_checked_add_u32(DB_BENCH_COMMON_BACKEND, "gradient_base_end",
                               base_start, base.row_count);
        uint32_t current_start = base_start;
        while (cut_index < cut_count) {
            const db_grid_block_t cut = cut_blocks[cut_index];
            if ((cut.row_count == 0U) || (cut.col_count == 0U)) {
                cut_index++;
                continue;
            }
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
                out_blocks[out_count++] = (db_grid_block_t){
                    .row_start = current_start,
                    .row_count = db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                                    "gradient_left_count",
                                                    cut_start, current_start),
                    .col_start = 0U,
                    .col_count = full_width_cols,
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
            out_blocks[out_count++] = (db_grid_block_t){
                .row_start = current_start,
                .row_count = db_checked_sub_u32(DB_BENCH_COMMON_BACKEND,
                                                "gradient_right_count",
                                                base_end, current_start),
                .col_start = 0U,
                .col_count = full_width_cols,
            };
        }
    }
    return out_count;
}

static inline size_t db_gradient_append_merged_blocks(
    const db_grid_block_t *blocks, size_t block_count,
    db_grid_block_t *out_blocks, size_t out_capacity, size_t out_count) {
    if ((blocks == NULL) || (out_blocks == NULL) || (out_capacity == 0U) ||
        (out_count >= out_capacity)) {
        return out_count;
    }

    const size_t copy_capacity = out_capacity - out_count;
    DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                  "gradient_append_merged_blocks", block_count,
                                  copy_capacity);
    const size_t copy_limit =
        (block_count < copy_capacity) ? block_count : copy_capacity;
    for (size_t index = 0U; index < copy_limit; index++) {
        const db_grid_block_t block = blocks[index];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        if (out_count == 0U) {
            out_blocks[out_count++] = block;
            continue;
        }
        db_grid_block_t *tail = &out_blocks[out_count - 1U];
        const uint32_t tail_end = db_checked_add_u32(
            DB_BENCH_COMMON_BACKEND, "gradient_append_tail_end",
            tail->row_start, tail->row_count);
        const uint32_t block_end = db_checked_add_u32(
            DB_BENCH_COMMON_BACKEND, "gradient_append_block_end",
            block.row_start, block.row_count);
        if ((block.col_start == tail->col_start) &&
            (block.col_count == tail->col_count) &&
            (block.row_start <= tail_end)) {
            if (block_end > tail_end) {
                tail->row_count = db_checked_sub_u32(
                    DB_BENCH_COMMON_BACKEND, "gradient_append_merged_count",
                    block_end, tail->row_start);
            }
            continue;
        }
        if (out_count >= out_capacity) {
            break;
        }
        out_blocks[out_count++] = block;
    }
    return out_count;
}

static inline size_t db_grid_blocks_compact_full_width_or_full(
    const db_grid_block_t *blocks, size_t block_count, uint32_t max_rows,
    uint32_t full_width_cols, db_grid_block_t *out_blocks,
    size_t out_capacity) {
    if ((blocks == NULL) || (block_count == 0U) || (out_blocks == NULL) ||
        (out_capacity == 0U) || (max_rows == 0U) || (full_width_cols == 0U)) {
        return 0U;
    }
    if (block_count > out_capacity) {
        out_blocks[0] = db_grid_block_full(max_rows, full_width_cols);
        return 1U;
    }
    const size_t compact_count = db_gradient_append_merged_blocks(
        blocks, block_count, out_blocks, out_capacity, 0U);
    if (compact_count == 0U) {
        return 0U;
    }
    if ((compact_count == 1U) && (out_blocks[0].row_start == 0U) &&
        (out_blocks[0].row_count == max_rows) &&
        (out_blocks[0].col_start == 0U) &&
        (out_blocks[0].col_count == full_width_cols)) {
        return 1U;
    }
    return compact_count;
}

static inline size_t db_damage_blocks_from_grid_blocks_or_full(
    const db_grid_block_t *blocks, size_t block_count, uint32_t max_rows,
    uint32_t full_width_cols, db_damage_block_t *out_blocks,
    size_t out_capacity) {
    if ((out_blocks == NULL) || (out_capacity == 0U) || (max_rows == 0U) ||
        (full_width_cols == 0U)) {
        return 0U;
    }
    if ((blocks == NULL) || (block_count == 0U)) {
        return 0U;
    }
    if (block_count > out_capacity) {
        out_blocks[0] = db_damage_block_full(max_rows, full_width_cols);
        return 1U;
    }
    for (size_t index = 0U; index < block_count; index++) {
        out_blocks[index] = db_damage_block_from_grid_block(&blocks[index]);
    }
    return block_count;
}

static inline size_t db_gradient_build_curr_draw_blocks(
    const db_grid_block_t *skipped_blocks, size_t skipped_count,
    const db_grid_block_t *dirty_blocks, size_t dirty_count,
    uint32_t full_width_cols, db_grid_block_t *out_blocks,
    size_t out_capacity) {
    if ((out_blocks == NULL) || (out_capacity == 0U) ||
        (full_width_cols == 0U)) {
        return 0U;
    }
    size_t out_count = db_gradient_append_merged_blocks(
        skipped_blocks, skipped_count, out_blocks, out_capacity, 0U);
    out_count = db_gradient_append_merged_blocks(
        dirty_blocks, dirty_count, out_blocks, out_capacity, out_count);
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

static inline void db_gradient_row_color_rgb3(uint32_t row_index,
                                              uint32_t head_row,
                                              int direction_down,
                                              uint32_t cycle_index,
                                              double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_window_rows_effective();
    double source_rgb[3] = {0.0, 0.0, 0.0};
    double target_rgb[3] = {0.0, 0.0, 0.0};
    db_palette_cycle_color_rgb3(cycle_index, source_rgb);
    db_palette_cycle_color_rgb3(cycle_index + 1U, target_rgb);
    if ((rows == 0U) || (window_rows == 0U)) {
        db_copy_f64_rgb3(out_rgb, target_rgb);
        return;
    }

    const uint32_t row = row_index % rows;
    const int64_t head_start_i64 = (int64_t)head_row - (int64_t)window_rows;
    const int64_t head_end_i64 = head_start_i64 + (int64_t)window_rows;
    const int64_t row_i64 = (int64_t)row;
    if (row_i64 < head_start_i64) {
        if (direction_down != 0) {
            db_copy_f64_rgb3(out_rgb, target_rgb);
        } else {
            db_copy_f64_rgb3(out_rgb, source_rgb);
        }
        return;
    }
    if (row_i64 >= head_end_i64) {
        if (direction_down != 0) {
            db_copy_f64_rgb3(out_rgb, source_rgb);
        } else {
            db_copy_f64_rgb3(out_rgb, target_rgb);
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
    db_blend_rgb3(source_rgb, target_rgb, blend, out_rgb);
}

static inline int db_gradient_row_segment_iter_init(
    const db_grid_block_t *block, uint32_t head_row, int direction_down,
    uint32_t cycle_index, db_gradient_row_segment_iter_t *iter) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t window_rows = db_gradient_window_rows_effective();
    if ((block == NULL) || (iter == NULL) || (rows == 0U) ||
        (block->row_count == 0U) || (block->col_count == 0U)) {
        return 0;
    }
    const uint32_t row_start = block->row_start;
    const uint32_t row_count = block->row_count;
    const uint32_t row_end =
        db_u32_min(rows, db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND,
                                                 "gradient_apply_row_end",
                                                 row_start, row_count));
    if (row_end <= row_start) {
        return 0;
    }
    *iter = (db_gradient_row_segment_iter_t){
        .block = *block,
        .row_end = row_end,
        .transition_start = db_u32_saturating_sub(head_row, window_rows),
        .transition_end = db_u32_min(
            rows,
            db_checked_add_u32(
                DB_BENCH_COMMON_BACKEND, "gradient_transition_end",
                db_u32_saturating_sub(head_row, window_rows), window_rows)),
        .next_row = row_start,
        .head_row = head_row,
        .cycle_index = cycle_index,
        .direction_down = direction_down,
        .phase = (window_rows == 0U) ? DB_GRADIENT_SEGMENT_PHASE_SOLID
                                     : DB_GRADIENT_SEGMENT_PHASE_TOP,
        .source_rgb = {0.0, 0.0, 0.0},
        .target_rgb = {0.0, 0.0, 0.0},
    };
    db_palette_cycle_color_rgb3(cycle_index, iter->source_rgb);
    db_palette_cycle_color_rgb3(cycle_index + 1U, iter->target_rgb);
    return 1;
}

static inline int
db_gradient_row_segment_iter_next(db_gradient_row_segment_iter_t *iter,
                                  db_gradient_row_segment_t *out_segment) {
    if ((iter == NULL) || (out_segment == NULL)) {
        return 0;
    }
    const double *top_rgb =
        (iter->direction_down != 0) ? iter->target_rgb : iter->source_rgb;
    const double *bottom_rgb =
        (iter->direction_down != 0) ? iter->source_rgb : iter->target_rgb;
    const uint32_t block_row_start = iter->block.row_start;
    const uint32_t block_col_start = iter->block.col_start;
    const uint32_t block_col_count = iter->block.col_count;

    if (iter->phase == DB_GRADIENT_SEGMENT_PHASE_SOLID) {
        *out_segment = (db_gradient_row_segment_t){
            .block =
                {
                    .row_start = block_row_start,
                    .row_count = iter->row_end - block_row_start,
                    .col_start = block_col_start,
                    .col_count = block_col_count,
                },
            .rgb = {iter->target_rgb[0], iter->target_rgb[1],
                    iter->target_rgb[2]},
        };
        iter->phase = DB_GRADIENT_SEGMENT_PHASE_DONE;
        return 1;
    }
    if (iter->phase == DB_GRADIENT_SEGMENT_PHASE_TOP) {
        const uint32_t top_end =
            db_u32_min(iter->row_end, iter->transition_start);
        iter->next_row = db_u32_max(block_row_start, iter->transition_start);
        iter->phase = DB_GRADIENT_SEGMENT_PHASE_BLEND;
        if (top_end > block_row_start) {
            *out_segment = (db_gradient_row_segment_t){
                .block =
                    {
                        .row_start = block_row_start,
                        .row_count = top_end - block_row_start,
                        .col_start = block_col_start,
                        .col_count = block_col_count,
                    },
                .rgb = {top_rgb[0], top_rgb[1], top_rgb[2]},
            };
            return 1;
        }
    }
    if (iter->phase == DB_GRADIENT_SEGMENT_PHASE_BLEND) {
        const uint32_t blend_end =
            db_u32_min(iter->row_end, iter->transition_end);
        if (iter->next_row < blend_end) {
            double row_rgb[3] = {0.0, 0.0, 0.0};
            const uint32_t row = iter->next_row;
            db_gradient_row_color_rgb3(row, iter->head_row,
                                       iter->direction_down, iter->cycle_index,
                                       row_rgb);
            *out_segment = (db_gradient_row_segment_t){
                .block =
                    {
                        .row_start = row,
                        .row_count = 1U,
                        .col_start = block_col_start,
                        .col_count = block_col_count,
                    },
                .rgb = {row_rgb[0], row_rgb[1], row_rgb[2]},
            };
            iter->next_row = row + 1U;
            return 1;
        }
        iter->next_row = db_u32_max(block_row_start, iter->transition_end);
        iter->phase = DB_GRADIENT_SEGMENT_PHASE_BOTTOM;
    }
    if (iter->phase == DB_GRADIENT_SEGMENT_PHASE_BOTTOM) {
        const uint32_t bottom_start =
            db_u32_max(block_row_start, iter->transition_end);
        iter->phase = DB_GRADIENT_SEGMENT_PHASE_DONE;
        if (iter->row_end > bottom_start) {
            *out_segment = (db_gradient_row_segment_t){
                .block =
                    {
                        .row_start = bottom_start,
                        .row_count = iter->row_end - bottom_start,
                        .col_start = block_col_start,
                        .col_count = block_col_count,
                    },
                .rgb = {bottom_rgb[0], bottom_rgb[1], bottom_rgb[2]},
            };
            return 1;
        }
    }
    return 0;
}

#endif
