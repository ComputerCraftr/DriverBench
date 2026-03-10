#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../config/benchmark_config.h"
#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_common.h"
#include "renderer_snake_shape_common.h"

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
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
    uint32_t color_bits[3];
} db_snake_compact_block_t;

typedef struct {
    db_damage_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_damage_block_t open_block;
    int open_block_valid;
} db_snake_damage_block_collect_ctx_t;

typedef int (*db_snake_emit_row_segment_cb_t)(uint32_t row, uint32_t col_start,
                                              uint32_t col_end,
                                              void *user_data);

typedef void (*db_snake_get_color_bits_cb_t)(uint32_t row, uint32_t col,
                                             void *user_data,
                                             uint32_t color_bits[3]);

typedef int (*db_snake_emit_color_run_cb_t)(uint32_t row, uint32_t col_start,
                                            uint32_t col_count,
                                            const uint32_t color_bits[3],
                                            void *user_data);

typedef struct {
    db_snake_compact_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_snake_compact_block_t *open_block;
    int *open_block_valid;
} db_snake_compact_block_collect_ctx_t;

typedef struct {
    uint32_t cols;
    uint32_t rows;
    db_snake_get_color_bits_cb_t get_color_bits;
    void *color_user_data;
    db_snake_compact_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_snake_compact_block_t open_block;
    int open_block_valid;
} db_snake_compact_block_row_segment_collect_ctx_t;

typedef struct {
    db_snake_emit_row_segment_cb_t emit;
    void *user_data;
    uint32_t open_row;
    uint32_t open_col_start;
    uint32_t open_col_end;
    int open_valid;
} db_snake_row_segment_emit_state_t;

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

enum db_snake_shape_profile_value_index_t {
    DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_X = 0U,
    DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_Y = 1U,
    DB_SNAKE_PROFILE_VAL_DIAMOND_RADIUS = 2U,
    DB_SNAKE_PROFILE_VAL_TRIANGLE_BOTTOM_WIDTH = 3U,
    DB_SNAKE_PROFILE_VAL_TRAPEZOID_TOP_WIDTH = 4U,
    DB_SNAKE_PROFILE_VAL_TRAPEZOID_BOTTOM_WIDTH = 5U,
    DB_SNAKE_PROFILE_VAL_RECT_HALF_WIDTH = 6U,
    DB_SNAKE_PROFILE_VAL_RECT_HALF_HEIGHT = 7U,
    DB_SNAKE_PROFILE_VAL_EXTENT_X = 8U,
    DB_SNAKE_PROFILE_VAL_EXTENT_Y = 9U,
    DB_SNAKE_PROFILE_VAL_ROTATE_COS = 10U,
    DB_SNAKE_PROFILE_VAL_ROTATE_SIN = 11U,
    DB_SNAKE_PROFILE_VAL_COUNT = 12U,
};

typedef struct {
    float values[DB_SNAKE_PROFILE_VAL_COUNT];
    uint32_t triangle_variant;
} db_snake_shape_profile_f32_t;

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

static inline void
db_snake_shape_profile_to_f32(const db_snake_shape_profile_t *profile,
                              db_snake_shape_profile_f32_t *out_profile_f32) {
    if ((profile == NULL) || (out_profile_f32 == NULL)) {
        return;
    }
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_X] =
        db_double_to_f32(profile->circle_radius_x);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_Y] =
        db_double_to_f32(profile->circle_radius_y);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_DIAMOND_RADIUS] =
        db_double_to_f32(profile->diamond_radius);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRIANGLE_BOTTOM_WIDTH] =
        db_double_to_f32(profile->triangle_bottom_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_TOP_WIDTH] =
        db_double_to_f32(profile->trapezoid_top_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_BOTTOM_WIDTH] =
        db_double_to_f32(profile->trapezoid_bottom_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_RECT_HALF_WIDTH] =
        db_double_to_f32(profile->rect_half_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_RECT_HALF_HEIGHT] =
        db_double_to_f32(profile->rect_half_height);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_EXTENT_X] =
        db_double_to_f32(profile->extent_x);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_EXTENT_Y] =
        db_double_to_f32(profile->extent_y);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_ROTATE_COS] =
        db_double_to_f32(profile->rotate_cos);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_ROTATE_SIN] =
        db_double_to_f32(profile->rotate_sin);
    out_profile_f32->triangle_variant = profile->triangle_variant;
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
    return (uint32_t)BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_snake_grid_cols_effective(void) {
    return (uint32_t)BENCH_WINDOW_WIDTH_PX;
}

static inline int db_snake_for_each_color_run_in_row_segment(
    uint32_t row, uint32_t segment_col_start, uint32_t segment_col_end,
    db_snake_get_color_bits_cb_t get_color_bits, void *get_color_user_data,
    db_snake_emit_color_run_cb_t emit_color_run, void *emit_user_data) {
    if ((segment_col_start >= segment_col_end) || (get_color_bits == NULL) ||
        (emit_color_run == NULL)) {
        return 1;
    }

    uint32_t run_col_start = segment_col_start;
    uint32_t run_color_bits[3] = {0U, 0U, 0U};
    get_color_bits(row, segment_col_start, get_color_user_data, run_color_bits);
    for (uint32_t col = segment_col_start + 1U; col < segment_col_end; col++) {
        uint32_t color_bits[3] = {0U, 0U, 0U};
        get_color_bits(row, col, get_color_user_data, color_bits);
        if (db_equal_u32_rgb3(color_bits, run_color_bits) != 0) {
            continue;
        }
        if (emit_color_run(row, run_col_start, col - run_col_start,
                           run_color_bits, emit_user_data) == 0) {
            return 0;
        }
        run_col_start = col;
        db_copy_u32_rgb3(run_color_bits, color_bits);
    }
    return emit_color_run(row, run_col_start, segment_col_end - run_col_start,
                          run_color_bits, emit_user_data);
}

static inline int
db_snake_append_open_compact_block(db_snake_compact_block_t *out_blocks,
                                   size_t out_capacity, size_t *out_count,
                                   const db_snake_compact_block_t *open_block,
                                   int open_block_valid) {
    if ((out_blocks == NULL) || (out_count == NULL) || (open_block == NULL) ||
        (open_block_valid == 0)) {
        return 0;
    }
    if (*out_count >= out_capacity) {
        return 0;
    }
    out_blocks[*out_count] = *open_block;
    (*out_count)++;
    return 1;
}

static inline int db_snake_emit_compact_block_color_run(
    uint32_t row, uint32_t col_start, uint32_t col_count,
    const uint32_t color_bits[3], void *user_data) {
    db_snake_compact_block_collect_ctx_t *ctx =
        (db_snake_compact_block_collect_ctx_t *)user_data;
    const int can_extend_open =
        (ctx != NULL) && (ctx->open_block != NULL) &&
        (ctx->open_block_valid != NULL) && (*ctx->open_block_valid != 0) &&
        ((ctx->open_block->row_start + ctx->open_block->row_count) == row) &&
        (ctx->open_block->col_start == col_start) &&
        (ctx->open_block->col_count == col_count) &&
        (db_equal_u32_rgb3(ctx->open_block->color_bits, color_bits) != 0);
    if ((ctx == NULL) || (color_bits == NULL)) {
        return 0;
    }
    if (can_extend_open != 0) {
        ctx->open_block->row_count++;
        return 1;
    }
    if ((ctx->open_block != NULL) && (ctx->open_block_valid != NULL) &&
        (*ctx->open_block_valid != 0) &&
        (db_snake_append_open_compact_block(ctx->out_blocks, ctx->out_capacity,
                                            ctx->out_count, ctx->open_block,
                                            *ctx->open_block_valid) == 0)) {
        return 0;
    }
    *ctx->open_block = (db_snake_compact_block_t){
        .row_start = row,
        .row_count = 1U,
        .col_start = col_start,
        .col_count = col_count,
        .color_bits = {color_bits[0], color_bits[1], color_bits[2]},
    };
    *ctx->open_block_valid = 1;
    return 1;
}

static inline int db_snake_collect_compact_blocks_from_row_segment(
    uint32_t row, uint32_t segment_col_start, uint32_t segment_col_end,
    db_snake_get_color_bits_cb_t get_color_bits, void *color_user_data,
    db_snake_compact_block_t *out_blocks, size_t out_capacity,
    size_t *out_count, db_snake_compact_block_t *open_block,
    int *open_block_valid) {
    db_snake_compact_block_collect_ctx_t collect_ctx = {
        .out_blocks = out_blocks,
        .out_capacity = out_capacity,
        .out_count = out_count,
        .open_block = open_block,
        .open_block_valid = open_block_valid,
    };
    return db_snake_for_each_color_run_in_row_segment(
        row, segment_col_start, segment_col_end, get_color_bits,
        color_user_data, db_snake_emit_compact_block_color_run, &collect_ctx);
}

static inline int db_snake_collect_compact_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end, void *user_data) {
    db_snake_compact_block_row_segment_collect_ctx_t *ctx =
        (db_snake_compact_block_row_segment_collect_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->get_color_bits == NULL) ||
        (ctx->out_blocks == NULL) || (ctx->out_count == NULL)) {
        return 0;
    }
    if ((row >= ctx->rows) || (col_end <= col_start) || (col_end > ctx->cols)) {
        return 1;
    }
    return db_snake_collect_compact_blocks_from_row_segment(
        row, col_start, col_end, ctx->get_color_bits, ctx->color_user_data,
        ctx->out_blocks, ctx->out_capacity, ctx->out_count, &ctx->open_block,
        &ctx->open_block_valid);
}

static inline double db_snake_color_channel(uint32_t seed) {
    const double normalized = (double)(seed & UINT8_MAX) / DB_U8_MAX;
    return DB_SNAKE_COMMON_COLOR_BIAS +
           (normalized * DB_SNAKE_COMMON_COLOR_SCALE);
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

static inline size_t
db_snake_scratch_capacity_from_work_units(uint32_t work_unit_count) {
    return (size_t)db_u32_max(work_unit_count, 1U);
}

static inline db_snake_region_t
db_snake_region_from_index(uint32_t seed, uint32_t shape_index) {
    db_snake_region_t region = {0};
    const uint32_t rows = db_snake_grid_rows_effective();
    const uint32_t cols = db_snake_grid_cols_effective();
    if ((rows == 0U) || (cols == 0U)) {
        return region;
    }

    const uint32_t seed_base =
        db_mix_u32(seed + (shape_index * DB_U32_SALT_COLOR_B) + 1U);
    const uint32_t min_w = (cols >= DB_SNAKE_REGION_MIN_DIM_THRESHOLD)
                               ? DB_SNAKE_REGION_MIN_DIM_LARGE
                               : DB_SNAKE_REGION_MIN_DIM_SMALL;
    const uint32_t min_h = (rows >= DB_SNAKE_REGION_MIN_DIM_THRESHOLD)
                               ? DB_SNAKE_REGION_MIN_DIM_LARGE
                               : DB_SNAKE_REGION_MIN_DIM_SMALL;
    const uint32_t max_w = db_u32_max(
        min_w,
        db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "region_max_w",
                           cols / DB_SNAKE_REGION_MAX_DIM_DIVISOR, min_w));
    const uint32_t max_h = db_u32_max(
        min_h,
        db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "region_max_h",
                           rows / DB_SNAKE_REGION_MAX_DIM_DIVISOR, min_h));
    region.width = db_u32_range(db_mix_u32(seed_base ^ DB_U32_SALT_PALETTE),
                                min_w, db_u32_min(max_w, cols));
    region.height =
        db_u32_range(db_mix_u32(seed_base ^ DB_SNAKE_REGION_SALT_HEIGHT), min_h,
                     db_u32_min(max_h, rows));
    const uint32_t max_x = db_u32_saturating_sub(cols, region.width);
    const uint32_t max_y = db_u32_saturating_sub(rows, region.height);
    region.x = db_u32_range(
        db_mix_u32(seed_base ^ DB_SNAKE_REGION_SALT_ORIGIN_X), 0U, max_x);
    region.y = db_u32_range(
        db_mix_u32(seed_base ^ DB_SNAKE_REGION_SALT_ORIGIN_Y), 0U, max_y);
    const double region_color_rgb[3] = {
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R)),
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G)),
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B)),
    };
    db_copy_f64_rgb3(region.color_rgb, region_color_rgb);
    return region;
}

static inline uint32_t
db_snake_tile_index_from_step(const db_snake_region_t *region, uint32_t step) {
    if ((region == NULL) || (region->width == 0U) || (region->height == 0U)) {
        return 0U;
    }
    const uint32_t cols = db_snake_grid_cols_effective();
    const uint32_t local_row = step / region->width;
    const uint32_t local_col_step = step % region->width;
    const uint32_t local_col = ((local_row & 1U) == 0U)
                                   ? local_col_step
                                   : ((region->width - 1U) - local_col_step);
    return ((region->y + local_row) * cols) + (region->x + local_col);
}

static inline int
db_snake_step_resolve_tile(const db_snake_region_t *region,
                           const db_snake_shape_cache_t *shape_cache,
                           uint32_t step, uint32_t cols, uint32_t rows,
                           db_snake_step_tile_t *out_tile) {
    if ((region == NULL) || (out_tile == NULL) || (cols == 0U) ||
        (rows == 0U)) {
        return 0;
    }
    const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    if ((row >= rows) || (col >= cols)) {
        return 0;
    }
    if (shape_cache != NULL) {
        const int inside =
            db_snake_shape_cache_contains_tile(shape_cache, row, col);
        if (inside == 0) {
            return 0;
        }
    }
    *out_tile = (db_snake_step_tile_t){
        .tile_index = tile_index,
        .row = row,
        .col = col,
    };
    return 1;
}

static inline uint32_t
db_snake_plan_active_batch_limit(const db_snake_plan_t *plan,
                                 uint32_t capacity) {
    if ((plan == NULL) || (capacity == 0U)) {
        return 0U;
    }
    return (plan->batch_size < capacity) ? plan->batch_size : capacity;
}

static inline int db_snake_plan_resolve_active_tile(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache, uint32_t update_index,
    uint32_t cols, uint32_t rows, db_snake_step_tile_t *out_tile) {
    if ((plan == NULL) || (region == NULL) || (out_tile == NULL)) {
        return 0;
    }
    const uint32_t step = plan->active_cursor + update_index;
    if (step >= plan->target_tile_count) {
        return 0;
    }
    return db_snake_step_resolve_tile(region, shape_cache, step, cols, rows,
                                      out_tile);
}

static inline int db_snake_plan_resolve_prev_tile(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache, uint32_t prev_offset,
    uint32_t cols, uint32_t rows, db_snake_step_tile_t *out_tile) {
    if ((plan == NULL) || (region == NULL) || (out_tile == NULL)) {
        return 0;
    }
    const uint32_t step = plan->prev_start + prev_offset;
    if (step >= plan->target_tile_count) {
        return 0;
    }
    return db_snake_step_resolve_tile(region, shape_cache, step, cols, rows,
                                      out_tile);
}

static inline int
db_snake_flush_open_row_segment(db_snake_row_segment_emit_state_t *state) {
    if ((state == NULL) || (state->open_valid == 0) || (state->emit == NULL)) {
        return 1;
    }
    if (state->emit(state->open_row, state->open_col_start, state->open_col_end,
                    state->user_data) == 0) {
        return 0;
    }
    state->open_valid = 0;
    return 1;
}

static inline int db_snake_accumulate_row_segment(
    db_snake_row_segment_emit_state_t *state, uint32_t row, uint32_t col_start,
    uint32_t col_end, const db_snake_shape_cache_t *shape_cache) {
    if ((state == NULL) || (col_end <= col_start)) {
        return 1;
    }
    if (shape_cache != NULL) {
        if (db_snake_shape_cache_clip_row_span(shape_cache, row, &col_start,
                                               &col_end) == 0) {
            return 1;
        }
    }
    if (state->open_valid != 0) {
        const int same_row = (state->open_row == row);
        const int overlaps_or_adjacent = (col_start <= state->open_col_end) &&
                                         (state->open_col_start <= col_end);
        if ((same_row != 0) && (overlaps_or_adjacent != 0)) {
            state->open_col_start =
                db_u32_min(state->open_col_start, col_start);
            state->open_col_end = db_u32_max(state->open_col_end, col_end);
            return 1;
        }
        if (db_snake_flush_open_row_segment(state) == 0) {
            return 0;
        }
    }
    state->open_row = row;
    state->open_col_start = col_start;
    state->open_col_end = col_end;
    state->open_valid = 1;
    return 1;
}

static inline int db_snake_emit_step_segments_for_region(
    db_snake_row_segment_emit_state_t *state, uint32_t region_x,
    uint32_t region_y, uint32_t region_cols, uint32_t region_rows,
    uint32_t step_start, uint32_t step_count,
    const db_snake_shape_cache_t *shape_cache) {
    if ((state == NULL) || (region_cols == 0U) || (region_rows == 0U) ||
        (step_count == 0U)) {
        return 1;
    }

    uint32_t remaining = step_count;
    uint32_t step_cursor = step_start;
    while (remaining > 0U) {
        const uint32_t local_row = step_cursor / region_cols;
        if (local_row >= region_rows) {
            return 1;
        }
        const uint32_t local_col_step = step_cursor % region_cols;
        const uint32_t steps_left_in_row = region_cols - local_col_step;
        const uint32_t chunk_steps = db_u32_min(remaining, steps_left_in_row);
        uint32_t first_local_col = 0U;
        if ((local_row & 1U) == 0U) {
            first_local_col = local_col_step;
        } else {
            first_local_col =
                (region_cols - 1U) - (local_col_step + chunk_steps - 1U);
        }

        if (db_snake_accumulate_row_segment(
                state, region_y + local_row, region_x + first_local_col,
                region_x + first_local_col + chunk_steps, shape_cache) == 0) {
            return 0;
        }
        step_cursor += chunk_steps;
        remaining -= chunk_steps;
    }
    return 1;
}

static inline int db_snake_for_each_damage_row_segment(
    const db_snake_region_t *region, uint32_t settled_start,
    uint32_t settled_count, uint32_t active_start, uint32_t active_count,
    const db_snake_shape_cache_t *shape_cache,
    db_snake_emit_row_segment_cb_t emit, void *user_data) {
    if ((region == NULL) || (emit == NULL) || (region->width == 0U) ||
        (region->height == 0U)) {
        return 0;
    }
    db_snake_row_segment_emit_state_t state = {
        .emit = emit,
        .user_data = user_data,
        .open_row = 0U,
        .open_col_start = 0U,
        .open_col_end = 0U,
        .open_valid = 0,
    };
    if (db_snake_emit_step_segments_for_region(
            &state, region->x, region->y, region->width, region->height,
            settled_start, settled_count, shape_cache) == 0) {
        return 0;
    }
    if (db_snake_emit_step_segments_for_region(
            &state, region->x, region->y, region->width, region->height,
            active_start, active_count, shape_cache) == 0) {
        return 0;
    }
    return db_snake_flush_open_row_segment(&state);
}

static inline int db_snake_collect_compact_blocks(
    const db_snake_region_t *region, uint32_t settled_start,
    uint32_t settled_count, uint32_t active_start, uint32_t active_count,
    const db_snake_shape_cache_t *shape_cache, uint32_t cols, uint32_t rows,
    db_snake_get_color_bits_cb_t get_color_bits, void *color_user_data,
    db_snake_compact_block_t *out_blocks, size_t out_capacity,
    size_t *out_count) {
    if ((region == NULL) || (out_blocks == NULL) || (out_count == NULL) ||
        (get_color_bits == NULL)) {
        return 0;
    }
    *out_count = 0U;
    db_snake_compact_block_row_segment_collect_ctx_t ctx = {
        .cols = cols,
        .rows = rows,
        .get_color_bits = get_color_bits,
        .color_user_data = color_user_data,
        .out_blocks = out_blocks,
        .out_capacity = out_capacity,
        .out_count = out_count,
        .open_block = {0},
        .open_block_valid = 0,
    };
    if (db_snake_for_each_damage_row_segment(
            region, settled_start, settled_count, active_start, active_count,
            shape_cache, db_snake_collect_compact_blocks_row_segment_cb,
            &ctx) == 0) {
        return 0;
    }
    if ((ctx.open_block_valid != 0) &&
        (db_snake_append_open_compact_block(out_blocks, out_capacity, out_count,
                                            &ctx.open_block,
                                            ctx.open_block_valid) == 0)) {
        return 0;
    }
    return 1;
}

static inline int db_snake_collect_compact_blocks_for_plan(
    const db_snake_region_t *region, const db_snake_plan_t *plan,
    const db_snake_shape_cache_t *shape_cache, uint32_t cols, uint32_t rows,
    db_snake_get_color_bits_cb_t get_color_bits, void *color_user_data,
    db_snake_compact_block_t *out_blocks, size_t out_capacity,
    size_t *out_count) {
    if (plan == NULL) {
        return 0;
    }
    return db_snake_collect_compact_blocks(
        region, plan->prev_start, plan->prev_count, plan->active_cursor,
        plan->batch_size, shape_cache, cols, rows, get_color_bits,
        color_user_data, out_blocks, out_capacity, out_count);
}

static inline int db_snake_append_open_damage_block(
    db_damage_block_t *out_blocks, size_t out_capacity, size_t *out_count,
    const db_damage_block_t *open_block, int open_block_valid) {
    if (open_block_valid == 0) {
        return 1;
    }
    if ((out_blocks == NULL) || (out_count == NULL) || (open_block == NULL) ||
        (*out_count >= out_capacity)) {
        return 0;
    }
    out_blocks[*out_count] = *open_block;
    (*out_count)++;
    return 1;
}

static inline int db_snake_collect_damage_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end, void *user_data) {
    db_snake_damage_block_collect_ctx_t *ctx =
        (db_snake_damage_block_collect_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->out_blocks == NULL) ||
        (ctx->out_count == NULL) || (col_start >= col_end)) {
        return 0;
    }
    const uint32_t col_count = col_end - col_start;
    if ((ctx->open_block_valid != 0) &&
        ((ctx->open_block.row_start + ctx->open_block.row_count) == row) &&
        (ctx->open_block.col_start == col_start) &&
        (ctx->open_block.col_count == col_count)) {
        ctx->open_block.row_count++;
        return 1;
    }
    if ((ctx->open_block_valid != 0) &&
        (db_snake_append_open_damage_block(ctx->out_blocks, ctx->out_capacity,
                                           ctx->out_count, &ctx->open_block,
                                           ctx->open_block_valid) == 0)) {
        return 0;
    }
    ctx->open_block = (db_damage_block_t){
        .row_start = row,
        .row_count = 1U,
        .col_start = col_start,
        .col_count = col_count,
    };
    ctx->open_block_valid = 1;
    return 1;
}

static inline int db_snake_collect_damage_blocks(
    const db_snake_region_t *region, uint32_t settled_start,
    uint32_t settled_count, uint32_t active_start, uint32_t active_count,
    const db_snake_shape_cache_t *shape_cache, db_damage_block_t *out_blocks,
    size_t out_capacity, size_t *out_count) {
    if ((region == NULL) || (out_blocks == NULL) || (out_count == NULL)) {
        return 0;
    }
    *out_count = 0U;
    db_snake_damage_block_collect_ctx_t ctx = {
        .out_blocks = out_blocks,
        .out_capacity = out_capacity,
        .out_count = out_count,
        .open_block = {0U, 0U, 0U, 0U},
        .open_block_valid = 0,
    };
    if (db_snake_for_each_damage_row_segment(
            region, settled_start, settled_count, active_start, active_count,
            shape_cache, db_snake_collect_damage_blocks_row_segment_cb,
            &ctx) == 0) {
        return 0;
    }
    return db_snake_append_open_damage_block(out_blocks, out_capacity,
                                             out_count, &ctx.open_block,
                                             ctx.open_block_valid);
}

static inline int db_snake_collect_damage_blocks_for_plan(
    const db_snake_region_t *region, const db_snake_plan_t *plan,
    const db_snake_shape_cache_t *shape_cache, db_damage_block_t *out_blocks,
    size_t out_capacity, size_t *out_count) {
    if (plan == NULL) {
        return 0;
    }
    return db_snake_collect_damage_blocks(
        region, plan->prev_start, plan->prev_count, plan->active_cursor,
        plan->batch_size, shape_cache, out_blocks, out_capacity, out_count);
}

static inline db_snake_plan_t db_snake_plan_next_step_for_region(
    const db_snake_region_t *region, uint32_t active_shape_index,
    uint32_t active_cursor, uint32_t prev_start, uint32_t prev_count,
    int phase_flag, uint32_t cursor_step, int toggle_clearing_on_complete,
    int advance_shape_index_on_complete) {
    db_snake_plan_t plan = {0};
    plan.active_shape_index = active_shape_index;
    plan.active_cursor = active_cursor;
    plan.prev_start = prev_start;
    plan.prev_count = prev_count;
    plan.phase_flag = phase_flag;
    if (region == NULL) {
        plan.next_cursor = active_cursor;
        plan.next_prev_start = prev_start;
        plan.next_prev_count = prev_count;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = active_shape_index;
        return plan;
    }
    const uint32_t target_tile_count = region->width * region->height;
    plan.target_tile_count = target_tile_count;
    if (target_tile_count == 0U) {
        plan.next_cursor = active_cursor;
        plan.next_prev_start = prev_start;
        plan.next_prev_count = prev_count;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = active_shape_index;
        return plan;
    }

    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(target_tile_count);
    const uint32_t cursor_step_effective = db_u32_max(cursor_step, 1U);

    if (plan.active_cursor == DB_SNAKE_CURSOR_PRE_ENTRY) {
        plan.active_cursor = 0U;
        plan.batch_size = 0U;
        plan.phase_completed = 0;
        plan.next_cursor = 0U;
        plan.next_prev_start = 0U;
        plan.next_prev_count = 0U;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = plan.active_shape_index;
        plan.wrapped = 0;
        return plan;
    }

    plan.batch_size = tiles_per_step;
    plan.phase_completed = (plan.active_cursor >= target_tile_count) ? 1 : 0;
    plan.next_shape_index = plan.active_shape_index;
    plan.wrapped = 0;
    plan.next_prev_start = plan.active_cursor;
    uint32_t advanced_count = 0U;
    plan.next_phase_flag = phase_flag;
    if (plan.phase_completed != 0) {
        plan.next_cursor = DB_SNAKE_CURSOR_PRE_ENTRY;
        if (toggle_clearing_on_complete != 0) {
            plan.next_phase_flag = !phase_flag;
        }
        if (advance_shape_index_on_complete != 0) {
            plan.next_shape_index = plan.active_shape_index + 1U;
            if (plan.next_shape_index < plan.active_shape_index) {
                plan.wrapped = 1;
            }
        }
    } else {
        plan.next_cursor =
            db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "snake_next_cursor",
                               plan.active_cursor, cursor_step_effective);
        if (plan.next_cursor > target_tile_count) {
            plan.next_cursor = target_tile_count;
        }
        advanced_count =
            db_checked_sub_u32(DB_SNAKE_COMMON_BACKEND, "snake_advanced_count",
                               plan.next_cursor, plan.active_cursor);
    }
    plan.next_prev_count =
        plan.phase_completed ? 0U : db_u32_max(plan.batch_size, advanced_count);
    return plan;
}

static inline db_snake_plan_t
db_snake_plan_next_step(const db_snake_plan_request_t *request) {
    db_snake_plan_t plan = {0};
    if (request == NULL) {
        return plan;
    }

    if (request->is_grid_mode != 0) {
        const db_snake_region_t grid_region = {
            .x = 0U,
            .y = 0U,
            .width = db_snake_grid_cols_effective(),
            .height = db_snake_grid_rows_effective(),
            .color_rgb = {0.0, 0.0, 0.0},
        };
        return db_snake_plan_next_step_for_region(
            &grid_region, 0U, request->cursor, request->prev_start,
            request->prev_count, request->phase_flag, request->speed_step, 1,
            0);
    }

    const db_snake_region_t region =
        db_snake_region_from_index(request->seed, request->shape_index);
    return db_snake_plan_next_step_for_region(
        &region, request->shape_index, request->cursor, request->prev_start,
        request->prev_count, 0, request->speed_step, 0, 1);
}

static inline double db_window_blend_factor(uint32_t window_index,
                                            uint32_t window_size) {
    const uint32_t span = db_u32_max(window_size, 1U);
    if (span <= 1U) {
        return 1.0;
    }
    return ((double)((span - 1U) - window_index)) / (double)(span - 1U);
}

static inline void db_grid_target_color_rgb3(int phase_flag,
                                             double out_rgb[3]) {
    if (out_rgb == NULL) {
        return;
    }
    static const double phase0_rgb[3] = {(double)BENCH_GRID_PHASE0_R,
                                         (double)BENCH_GRID_PHASE0_G,
                                         (double)BENCH_GRID_PHASE0_B};
    static const double phase1_rgb[3] = {(double)BENCH_GRID_PHASE1_R,
                                         (double)BENCH_GRID_PHASE1_G,
                                         (double)BENCH_GRID_PHASE1_B};
    if (phase_flag != 0) {
        db_copy_f64_rgb3(out_rgb, phase0_rgb);
        return;
    }
    db_copy_f64_rgb3(out_rgb, phase1_rgb);
}

static inline db_snake_step_target_t
db_snake_step_target_from_plan(int is_grid_mode, uint32_t pattern_seed,
                               const db_snake_plan_t *plan) {
    db_snake_step_target_t result = {0};
    if (plan == NULL) {
        return result;
    }
    if (is_grid_mode != 0) {
        result.region = (db_snake_region_t){
            .x = 0U,
            .y = 0U,
            .width = db_snake_grid_cols_effective(),
            .height = db_snake_grid_rows_effective(),
            .color_rgb = {0.0, 0.0, 0.0},
        };
        double target_rgb[3] = {0.0, 0.0, 0.0};
        db_grid_target_color_rgb3(plan->phase_flag, target_rgb);
        db_copy_f64_rgb3(result.target_rgb, target_rgb);
        result.force_full_fill_on_phase_complete = 1;
        result.has_next_phase_flag = 1;
        result.next_phase_flag = plan->next_phase_flag;
        return result;
    }

    result.region =
        db_snake_region_from_index(pattern_seed, plan->active_shape_index);
    db_copy_f64_rgb3(result.target_rgb, result.region.color_rgb);
    result.shape_kind = db_snake_shapes_kind_from_index(
        pattern_seed, plan->active_shape_index, DB_U32_SALT_PALETTE);
    result.has_next_shape_index = 1;
    result.next_shape_index = plan->next_shape_index;
    return result;
}

static inline db_snake_step_eval_t
db_snake_step_eval_from_runtime(db_pattern_t pattern, uint32_t pattern_seed,
                                uint32_t shape_index, uint32_t cursor,
                                uint32_t prev_start, uint32_t prev_count,
                                int phase_flag, uint32_t bench_speed_step) {
    db_snake_step_eval_t result = {0};
    result.is_grid_mode = (pattern == DB_PATTERN_SNAKE_GRID);
    result.is_shapes_mode = (pattern == DB_PATTERN_SNAKE_SHAPES);
    const db_snake_plan_request_t request = db_snake_plan_request_make(
        result.is_grid_mode, pattern_seed, shape_index, cursor, prev_start,
        prev_count, phase_flag, bench_speed_step);
    result.plan = db_snake_plan_next_step(&request);
    result.target = db_snake_step_target_from_plan(result.is_grid_mode,
                                                   pattern_seed, &result.plan);
    result.shape_kind = (result.is_shapes_mode != 0) ? result.target.shape_kind
                                                     : DB_SNAKE_SHAPE_RECT;
    return result;
}

#endif
