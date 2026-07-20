#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_EMIT_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_EMIT_INTERNAL_H

#include "../config/benchmark_config.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_snake_collect_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"
#include "core/db_core.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include <stdint.h>
#include <string.h>

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
    const uint32_t target_tile_count =
        db_checked_mul_u32(DB_SNAKE_COMMON_BACKEND, "snake_target_tile_count",
                           region->width, region->height);
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
    const uint32_t cursor_step_effective = DB_MAX(cursor_step, 1U);

    if (plan.active_cursor == DB_SNAKE_CURSOR_PRE_ENTRY) {
        plan.active_cursor = 0U;
        plan.next_prev_start = 0U;
        plan.next_prev_count = 0U;
        plan.prev_start = 0U;
        plan.prev_count = 0U;
    }

    plan.batch_size = tiles_per_step;
    plan.phase_completed = DB_BOOL(plan.active_cursor >= target_tile_count);
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
        plan.phase_completed ? 0U : DB_MAX(plan.batch_size, advanced_count);
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
            .color_rgb = {0},
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
    const uint32_t span = DB_MAX(window_size, 1U);
    if (span <= 1U) {
        return 1.0;
    }
    return DB_TO_F64((span - 1U) - window_index) / DB_TO_F64(span - 1U);
}

static inline db_snake_active_batch_t db_snake_bind_active_batch_scratch(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_active_tile_scratch_t *scratch,
    uint32_t *fallback_tile_indices, uint8_t *fallback_tile_valid) {
    if ((plan == NULL) || (region == NULL) || (region->width == 0U) ||
        (region->height == 0U) || (fallback_tile_indices == NULL) ||
        (fallback_tile_valid == NULL)) {
        return (db_snake_active_batch_t){0};
    }
    const uint32_t batch_limit = db_snake_plan_active_batch_limit(
        plan, DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT);
    db_snake_active_batch_t batch = {
        .active_tile_indices = fallback_tile_indices,
        .active_tile_valid = fallback_tile_valid,
        .batch_limit = batch_limit,
    };
    if ((scratch != NULL) && (scratch->active_tile_indices != NULL) &&
        (scratch->active_tile_valid != NULL) &&
        (scratch->active_tile_capacity >= batch_limit)) {
        batch.active_tile_indices = scratch->active_tile_indices;
        batch.active_tile_valid = scratch->active_tile_valid;
    }
    return batch;
}

static inline void db_snake_prepare_active_batch(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache_ptr, uint32_t cols, uint32_t rows,
    db_snake_active_batch_t *batch) {
    if ((plan == NULL) || (region == NULL) || (batch == NULL) || (cols == 0U) ||
        (rows == 0U) || (region->width == 0U) || (region->height == 0U) ||
        (batch->active_tile_indices == NULL) ||
        (batch->active_tile_valid == NULL)) {
        return;
    }
    for (uint32_t update_index = 0U; update_index < batch->batch_limit;
         update_index++) {
        batch->active_tile_valid[update_index] = 0U;
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_active_tile(plan, region, shape_cache_ptr,
                                              update_index, cols, rows,
                                              &tile) == 0) {
            continue;
        }
        batch->active_tile_indices[update_index] = tile.tile_index;
        batch->active_tile_valid[update_index] = 1U;
    }
}

static inline void db_snake_blend_active_rgb(const db_snake_plan_t *plan,
                                             uint32_t update_index,
                                             const double *prior_rgb,
                                             const double *target_rgb,
                                             double *out_rgb) {
    if ((plan == NULL) || (prior_rgb == NULL) || (target_rgb == NULL) ||
        (out_rgb == NULL)) {
        return;
    }
    const double blend_factor =
        db_window_blend_factor(update_index, BENCH_SNAKE_PHASE_WINDOW_TILES);
    db_blend_rgb3(prior_rgb, target_rgb, blend_factor, out_rgb);
}

static inline void db_grid_target_color_rgb3(int phase_flag, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    static const double phase0_rgb[3] = {
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
    static const double phase1_rgb[3] = {
        BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B};
    if (phase_flag != 0) {
        memcpy(out_rgb, phase0_rgb, 3U * sizeof(double));
        return;
    }
    memcpy(out_rgb, phase1_rgb, 3U * sizeof(double));
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
            .color_rgb = {0},
        };
        double target_rgb[3] = {0};
        db_grid_target_color_rgb3(plan->phase_flag, target_rgb);
        memcpy(result.target_rgb, target_rgb, 3U * sizeof(double));
        result.force_full_fill_on_phase_complete = 1;
        result.has_next_phase_flag = 1;
        result.next_phase_flag = plan->next_phase_flag;
        return result;
    }

    result.region =
        db_snake_region_from_index(pattern_seed, plan->active_shape_index);
    memcpy(result.target_rgb, result.region.color_rgb, 3U * sizeof(double));
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
