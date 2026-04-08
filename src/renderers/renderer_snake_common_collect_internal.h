#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_COLLECT_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_COLLECT_INTERNAL_H

#include "renderer_snake_types.h"

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
    const uint32_t *color_bits, void *user_data) {
    db_snake_compact_block_collect_ctx_t *ctx =
        (db_snake_compact_block_collect_ctx_t *)user_data;
    const uint32_t open_row_end =
        ((ctx != NULL) && (ctx->open_block != NULL))
            ? db_checked_span_end_u32(
                  DB_SNAKE_COMMON_BACKEND, "snake_compact_open_row_end",
                  ctx->open_block->row_start, ctx->open_block->row_count)
            : 0U;
    const int can_extend_open =
        (ctx != NULL) && (ctx->open_block != NULL) &&
        (ctx->open_block_valid != NULL) && (*ctx->open_block_valid != 0) &&
        (open_row_end == row) && (ctx->open_block->col_start == col_start) &&
        (ctx->open_block->col_count == col_count) &&
        (db_equal_u32_rgb3(ctx->open_block->color_bits, color_bits) != 0);
    if ((ctx == NULL) || (color_bits == NULL)) {
        return 0;
    }
    if (can_extend_open != 0) {
        ctx->open_block->row_count = db_checked_add_u32(
            DB_SNAKE_COMMON_BACKEND, "snake_compact_open_row_count",
            ctx->open_block->row_count, 1U);
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

static inline int db_snake_collect_damage_and_compact_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end, void *user_data) {
    db_snake_dual_block_collect_ctx_t *ctx =
        (db_snake_dual_block_collect_ctx_t *)user_data;
    if (ctx == NULL) {
        return 0;
    }
    if ((ctx->damage_ctx != NULL) &&
        (db_snake_collect_damage_blocks_row_segment_cb(row, col_start, col_end,
                                                       ctx->damage_ctx) == 0)) {
        return 0;
    }
    if ((ctx->compact_ctx != NULL) &&
        (db_snake_collect_compact_blocks_row_segment_cb(
             row, col_start, col_end, ctx->compact_ctx) == 0)) {
        return 0;
    }
    return 1;
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
    db_grid_block_t *out_blocks, size_t out_capacity, size_t *out_count,
    const db_grid_block_t *open_block, int open_block_valid) {
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
    const db_snake_shape_cache_t *shape_cache, db_grid_block_t *out_blocks,
    size_t out_capacity, size_t *out_count) {
    if (plan == NULL) {
        return 0;
    }
    return db_snake_collect_damage_blocks(
        region, plan->prev_start, plan->prev_count, plan->active_cursor,
        plan->batch_size, shape_cache, out_blocks, out_capacity, out_count);
}

static inline int db_snake_collect_blocks_for_plan(
    const db_snake_region_t *region, const db_snake_plan_t *plan,
    const db_snake_shape_cache_t *shape_cache, uint32_t cols, uint32_t rows,
    db_snake_get_color_bits_cb_t get_color_bits, void *color_user_data,
    db_grid_block_t *out_damage_blocks, size_t damage_capacity,
    size_t *out_damage_count, db_snake_compact_block_t *out_compact_blocks,
    size_t compact_capacity, size_t *out_compact_count) {
    if ((region == NULL) || (plan == NULL)) {
        return 0;
    }

    db_snake_damage_block_collect_ctx_t damage_ctx = {0};
    db_snake_damage_block_collect_ctx_t *damage_ctx_ptr = NULL;
    if ((out_damage_blocks != NULL) && (out_damage_count != NULL)) {
        *out_damage_count = 0U;
        damage_ctx = (db_snake_damage_block_collect_ctx_t){
            .out_blocks = out_damage_blocks,
            .out_capacity = damage_capacity,
            .out_count = out_damage_count,
            .open_block = {0U, 0U, 0U, 0U},
            .open_block_valid = 0,
        };
        damage_ctx_ptr = &damage_ctx;
    }

    db_snake_compact_block_row_segment_collect_ctx_t compact_ctx = {0};
    db_snake_compact_block_row_segment_collect_ctx_t *compact_ctx_ptr = NULL;
    if ((out_compact_blocks != NULL) && (out_compact_count != NULL) &&
        (get_color_bits != NULL)) {
        *out_compact_count = 0U;
        compact_ctx = (db_snake_compact_block_row_segment_collect_ctx_t){
            .cols = cols,
            .rows = rows,
            .get_color_bits = get_color_bits,
            .color_user_data = color_user_data,
            .out_blocks = out_compact_blocks,
            .out_capacity = compact_capacity,
            .out_count = out_compact_count,
            .open_block = {0},
            .open_block_valid = 0,
        };
        compact_ctx_ptr = &compact_ctx;
    }

    if ((damage_ctx_ptr == NULL) && (compact_ctx_ptr == NULL)) {
        return 0;
    }

    db_snake_dual_block_collect_ctx_t dual_ctx = {
        .damage_ctx = damage_ctx_ptr,
        .compact_ctx = compact_ctx_ptr,
    };
    if (db_snake_for_each_damage_row_segment(
            region, plan->prev_start, plan->prev_count, plan->active_cursor,
            plan->batch_size, shape_cache,
            db_snake_collect_damage_and_compact_blocks_row_segment_cb,
            &dual_ctx) == 0) {
        return 0;
    }
    if ((damage_ctx_ptr != NULL) &&
        (db_snake_append_open_damage_block(
             out_damage_blocks, damage_capacity, out_damage_count,
             &damage_ctx.open_block, damage_ctx.open_block_valid) == 0)) {
        return 0;
    }
    if ((compact_ctx_ptr != NULL) && (compact_ctx.open_block_valid != 0) &&
        (db_snake_append_open_compact_block(
             out_compact_blocks, compact_capacity, out_compact_count,
             &compact_ctx.open_block, compact_ctx.open_block_valid) == 0)) {
        return 0;
    }
    return 1;
}

#endif
