#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_TYPES_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_TYPES_INTERNAL_H

#include "../config/benchmark_config.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "benchmarks/db_snake_shape_internal.h"

#define DB_SNAKE_COMMON_BACKEND "renderer_snake_common"
#define DB_SNAKE_COMMON_COLOR_BIAS 0.20
#define DB_SNAKE_COMMON_COLOR_SCALE 0.75
#define DB_SNAKE_CURSOR_PRE_ENTRY UINT32_MAX
#define DB_SNAKE_REGION_MAX_DIM_DIVISOR 3U
#define DB_SNAKE_REGION_MIN_DIM_LARGE 8U
#define DB_SNAKE_REGION_MIN_DIM_SMALL 1U
#define DB_SNAKE_REGION_MIN_DIM_THRESHOLD 16U
#define DB_SNAKE_REGION_SALT_HEIGHT 0x63D83595U
#define DB_SNAKE_REGION_SALT_ORIGIN_X DB_U32_GOLDEN_RATIO
#define DB_SNAKE_REGION_SALT_ORIGIN_Y DB_U32_SALT_ORIGIN_Y
#define DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT 4096U

typedef struct {
    uint32_t active_shape_index;
    uint32_t active_cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int phase_flag;
    int phase_completed;
    uint32_t next_prev_start;
    uint32_t next_prev_count;
    int next_phase_flag;
    uint32_t target_tile_count;
    uint32_t next_shape_index;
    uint32_t next_cursor;
    int wrapped;
} db_snake_plan_t;

typedef struct {
    int is_grid_mode;
    uint32_t seed;
    uint32_t shape_index;
    uint32_t cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    int phase_flag;
    uint32_t speed_step;
} db_snake_plan_request_t;

typedef struct {
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    uint32_t batch_limit;
} db_snake_active_batch_t;

typedef struct {
    db_grid_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_grid_block_t open_block;
    int open_block_valid;
} db_snake_damage_block_collect_ctx_t;

typedef struct {
    db_snake_damage_block_collect_ctx_t *damage_ctx;
    const void *geometry_sink;
} db_snake_row_segment_sink_t;

typedef int (*db_snake_emit_row_segment_cb_t)(
    uint32_t row, uint32_t col_start, uint32_t col_end,
    db_snake_row_segment_sink_t *sink);

typedef struct {
    db_snake_emit_row_segment_cb_t emit;
    db_snake_row_segment_sink_t *sink;
    uint32_t open_row;
    uint32_t open_col_start;
    uint32_t open_col_end;
    int open_valid;
} db_snake_row_segment_emit_state_t;

static inline int db_snake_collect_damage_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end,
    db_snake_row_segment_sink_t *sink);

typedef struct {
    db_snake_region_t region;
    double target_rgb[3];
    int force_full_fill_on_phase_complete;
    int has_next_phase_flag;
    int next_phase_flag;
    int has_next_shape_index;
    uint32_t next_shape_index;
    db_snake_shape_kind_t shape_kind;
} db_snake_step_target_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    db_snake_shape_kind_t shape_kind;
    int is_grid_mode;
    int is_shapes_mode;
} db_snake_step_eval_t;

typedef struct {
    uint32_t tile_index;
    uint32_t row;
    uint32_t col;
} db_snake_step_tile_t;

typedef struct {
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    size_t active_tile_capacity;
} db_snake_active_tile_scratch_t;

static inline db_snake_plan_request_t
db_snake_plan_request_make(int is_grid_mode, uint32_t seed,
                           uint32_t shape_index, uint32_t cursor,
                           uint32_t prev_start, uint32_t prev_count,
                           int phase_flag, uint32_t speed_step) {
    const db_snake_plan_request_t request = {
        .is_grid_mode = is_grid_mode,
        .seed = seed,
        .shape_index = shape_index,
        .cursor = cursor,
        .prev_start = prev_start,
        .prev_count = prev_count,
        .phase_flag = phase_flag,
        .speed_step = speed_step,
    };
    return request;
}

static inline size_t
db_snake_upload_range_capacity_needed(uint32_t settled_count,
                                      uint32_t active_count) {
    return (size_t)settled_count + (size_t)active_count;
}

static inline size_t
db_snake_plan_upload_range_capacity_needed(const db_snake_plan_t *plan) {
    if (plan == NULL) {
        return 0U;
    }
    return db_snake_upload_range_capacity_needed(plan->prev_count,
                                                 plan->batch_size);
}

static inline uint32_t db_snake_grid_rows_effective(void) {
    return BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_snake_grid_cols_effective(void) {
    return BENCH_WINDOW_WIDTH_PX;
}

#endif
