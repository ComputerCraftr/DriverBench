#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_COLLECT_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_COLLECT_INTERNAL_H

#include "../config/benchmark_config.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include <stdint.h>
#include <string.h>

static inline double db_snake_color_channel(uint32_t seed) {
    const double normalized = DB_TO_F64(seed & UINT8_MAX) / DB_U8_MAX;
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
    return db_checked_u32_to_size("db_snake_scratch_capacity", "capacity",
                                  DB_MAX(work_unit_count, 1U));
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
    const uint32_t max_w = DB_MAX(
        min_w,
        db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "region_max_w",
                           cols / DB_SNAKE_REGION_MAX_DIM_DIVISOR, min_w));
    const uint32_t max_h = DB_MAX(
        min_h,
        db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "region_max_h",
                           rows / DB_SNAKE_REGION_MAX_DIM_DIVISOR, min_h));
    region.width = db_u32_range(db_mix_u32(seed_base ^ DB_U32_SALT_PALETTE),
                                min_w, DB_MIN(max_w, cols));
    region.height =
        db_u32_range(db_mix_u32(seed_base ^ DB_SNAKE_REGION_SALT_HEIGHT), min_h,
                     DB_MIN(max_h, rows));
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
    memcpy(region.color_rgb, region_color_rgb, 3U * sizeof(double));
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
    return DB_MIN(plan->batch_size, capacity);
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
                    state->sink) == 0) {
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
            state->open_col_start = DB_MIN(state->open_col_start, col_start);
            state->open_col_end = DB_MAX(state->open_col_end, col_end);
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
        const uint32_t chunk_steps = DB_MIN(remaining, steps_left_in_row);
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
    db_snake_emit_row_segment_cb_t emit, db_snake_row_segment_sink_t *sink) {
    if ((region == NULL) || (emit == NULL) || (region->width == 0U) ||
        (region->height == 0U)) {
        return 0;
    }
    db_snake_row_segment_emit_state_t state = {
        .emit = emit,
        .sink = sink,
        .open_row = 0U,
        .open_col_start = 0U,
        .open_col_end = 0U,
        .open_valid = 0,
    };
    uint32_t final_settled_start = settled_start;
    uint32_t final_settled_count = settled_count;
    uint32_t final_active_start = active_start;
    uint32_t final_active_count = active_count;

    if (settled_count > 0U && active_count > 0U) {
        const uint32_t settled_end = settled_start + settled_count;
        if (settled_start <= active_start && settled_end >= active_start) {
            // Overlapping or adjacent: settled covers [S, E], active starts at
            // A. Merge into settled range [S, max(E, A+count)].
            const uint32_t active_end = active_start + active_count;
            const uint32_t merged_end = DB_MAX(active_end, settled_end);
            final_settled_count = merged_end - settled_start;
            final_active_count = 0U;
        }
    }

    if (db_snake_emit_step_segments_for_region(
            &state, region->x, region->y, region->width, region->height,
            final_settled_start, final_settled_count, shape_cache) == 0) {
        return 0;
    }
    if (db_snake_emit_step_segments_for_region(
            &state, region->x, region->y, region->width, region->height,
            final_active_start, final_active_count, shape_cache) == 0) {
        return 0;
    }
    return db_snake_flush_open_row_segment(&state);
}

static inline int db_snake_append_open_damage_block(
    db_grid_block_t *out_blocks, size_t out_capacity, size_t *out_count,
    const db_grid_block_t *open_block, int open_block_valid) {
    if (open_block_valid == 0) {
        return 1;
    }
    if ((out_blocks == NULL) || (out_count == NULL) || (open_block == NULL)) {
        return 0;
    }
    if (*out_count >= out_capacity) {
        DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_SNAKE_COMMON_BACKEND,
                                      "damage_block_append", *out_count + 1,
                                      out_capacity);
        return 0;
    }
    out_blocks[*out_count] = *open_block;
    (*out_count)++;
    return 1;
}

static inline int db_snake_collect_damage_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end,
    db_snake_row_segment_sink_t *sink) {
    db_snake_damage_block_collect_ctx_t *ctx =
        (sink != NULL) ? sink->damage_ctx : NULL;
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
    ctx->open_block = (db_grid_block_t){
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
    const db_snake_shape_cache_t *shape_cache, db_grid_block_t *out_blocks,
    size_t out_capacity, size_t *out_count) {
    if ((region == NULL) || (out_blocks == NULL) || (out_count == NULL)) {
        return 0;
    }
    *out_count = 0U;
    db_snake_damage_block_collect_ctx_t ctx = {
        .out_blocks = out_blocks,
        .out_capacity = out_capacity,
        .out_count = out_count,
        .open_block = {0},
        .open_block_valid = 0,
    };
    if (db_snake_for_each_damage_row_segment(
            region, settled_start, settled_count, active_start, active_count,
            shape_cache, db_snake_collect_damage_blocks_row_segment_cb,
            &(db_snake_row_segment_sink_t){.damage_ctx = &ctx}) == 0) {
        return 0;
    }
    return db_snake_append_open_damage_block(out_blocks, out_capacity,
                                             out_count, &ctx.open_block,
                                             ctx.open_block_valid);
}

static inline int db_snake_collect_damage_blocks_for_plan(
    const db_snake_region_t *region, const db_snake_plan_t *plan,
    const db_snake_shape_cache_t *shape_cache, db_grid_block_t *out_blocks,
    size_t out_capacity, size_t *out_count) {
    if (plan == NULL) {
        return 0;
    }
    return db_snake_collect_damage_blocks(
        region, plan->prev_start, plan->prev_count, plan->active_cursor,
        plan->batch_size, shape_cache, out_blocks, out_capacity, out_count);
}

#endif
