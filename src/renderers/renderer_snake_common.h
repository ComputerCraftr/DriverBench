#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
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
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
} db_snake_col_span_t;

typedef struct {
    db_snake_region_t region;
    double target_r;
    double target_g;
    double target_b;
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

static inline size_t db_snake_span_capacity_needed(uint32_t settled_count,
                                                   uint32_t active_count) {
    return (size_t)settled_count + (size_t)active_count;
}

static inline size_t
db_snake_plan_span_capacity_needed(const db_snake_plan_t *plan) {
    if (plan == NULL) {
        return 0U;
    }
    return db_snake_span_capacity_needed(plan->prev_count, plan->batch_size);
}

static inline int
db_snake_try_merge_adjacent_span(db_snake_col_span_t *existing,
                                 const db_snake_col_span_t *candidate) {
    if ((existing == NULL) || (candidate == NULL) ||
        (existing->row != candidate->row)) {
        return 0;
    }
    if (candidate->col_start > candidate->col_end) {
        return 0;
    }
    // Merge contiguous or overlapping spans. Traversal order can be either
    // direction within a row (snake), so handle both forward and reverse
    // adjacency.
    const int overlaps_or_adjacent =
        (candidate->col_start <= existing->col_end) &&
        (existing->col_start <= candidate->col_end);
    if (overlaps_or_adjacent == 0) {
        return 0;
    }
    existing->col_start = db_u32_min(existing->col_start, candidate->col_start);
    existing->col_end = db_u32_max(existing->col_end, candidate->col_end);
    return (existing->col_end > existing->col_start) ? 1 : 0;
}

static inline size_t db_snake_filter_spans_for_shape_cache(
    db_snake_col_span_t *spans, size_t span_count,
    const db_snake_shape_cache_t *shape_cache) {
    if ((spans == NULL) || (shape_cache == NULL) || (span_count == 0U)) {
        return span_count;
    }
    size_t out_count = 0U;
    for (size_t i = 0U; i < span_count; i++) {
        uint32_t clipped_start = spans[i].col_start;
        uint32_t clipped_end = spans[i].col_end;
        if (db_snake_shape_cache_clip_row_span(
                shape_cache, spans[i].row, &clipped_start, &clipped_end) == 0) {
            continue;
        }
        spans[out_count] = spans[i];
        spans[out_count].col_start = clipped_start;
        spans[out_count].col_end = clipped_end;
        out_count++;
    }
    return out_count;
}

static inline uint32_t db_snake_grid_rows_effective(void) {
    return (uint32_t)BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_snake_grid_cols_effective(void) {
    return (uint32_t)BENCH_WINDOW_WIDTH_PX;
}

static inline double db_snake_color_channel(uint32_t seed) {
    const double normalized = (double)(seed & 255U) / DB_U8_MAX;
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
    region.color_r =
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R));
    region.color_g =
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G));
    region.color_b =
        db_snake_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B));
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

static inline void db_snake_append_step_spans_for_region(
    db_snake_col_span_t *spans, size_t max_spans, size_t *inout_span_count,
    uint32_t region_x, uint32_t region_y, uint32_t region_cols,
    uint32_t region_rows, uint32_t step_start, uint32_t step_count) {
    if ((spans == NULL) || (inout_span_count == NULL) || (region_cols == 0U) ||
        (region_rows == 0U) || (step_count == 0U)) {
        return;
    }

    uint32_t remaining = step_count;
    uint32_t step_cursor = step_start;
    while (remaining > 0U) {
        const uint32_t local_row = step_cursor / region_cols;
        if (local_row >= region_rows) {
            return;
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

        if (*inout_span_count >= max_spans) {
            return;
        }
        const db_snake_col_span_t candidate = (db_snake_col_span_t){
            .row = region_y + local_row,
            .col_start = region_x + first_local_col,
            .col_end = region_x + first_local_col + chunk_steps,
        };
        if ((*inout_span_count > 0U) &&
            (db_snake_try_merge_adjacent_span(&spans[*inout_span_count - 1U],
                                              &candidate) != 0)) {
            // Merged with previous span; do not append a new entry.
        } else {
            spans[*inout_span_count] = candidate;
            (*inout_span_count)++;
        }
        step_cursor += chunk_steps;
        remaining -= chunk_steps;
    }
}

static inline size_t
db_snake_collect_damage_spans(db_snake_col_span_t *spans, size_t max_spans,
                              const db_snake_region_t *region,
                              uint32_t settled_start, uint32_t settled_count,
                              uint32_t active_start, uint32_t active_count,
                              const db_snake_shape_cache_t *shape_cache) {
    if ((spans == NULL) || (region == NULL) || (max_spans == 0U) ||
        (region->width == 0U) || (region->height == 0U)) {
        return 0U;
    }
    size_t span_count = 0U;
    db_snake_append_step_spans_for_region(
        spans, max_spans, &span_count, region->x, region->y, region->width,
        region->height, settled_start, settled_count);
    db_snake_append_step_spans_for_region(
        spans, max_spans, &span_count, region->x, region->y, region->width,
        region->height, active_start, active_count);
    if (shape_cache != NULL) {
        span_count = db_snake_filter_spans_for_shape_cache(spans, span_count,
                                                           shape_cache);
    }
    return span_count;
}

static inline size_t db_snake_collect_damage_spans_for_plan(
    db_snake_col_span_t *spans, size_t max_spans,
    const db_snake_region_t *region, const db_snake_plan_t *plan,
    const db_snake_shape_cache_t *shape_cache) {
    if (plan == NULL) {
        return 0U;
    }
    return db_snake_collect_damage_spans(
        spans, max_spans, region, plan->prev_start, plan->prev_count,
        plan->active_cursor, plan->batch_size, shape_cache);
}

static inline int db_snake_span_row_bounds(const db_snake_col_span_t *spans,
                                           size_t span_count, uint32_t max_rows,
                                           uint32_t *out_row_start,
                                           uint32_t *out_row_count) {
    if ((spans == NULL) || (max_rows == 0U) || (out_row_start == NULL) ||
        (out_row_count == NULL)) {
        return 0;
    }
    // Spans are emitted in monotonic row order; find first/last valid spans to
    // avoid a full scan in common cases.
    size_t first_index = 0U;
    while (first_index < span_count) {
        const db_snake_col_span_t span = spans[first_index];
        if ((span.col_end > span.col_start) && (span.row < max_rows)) {
            break;
        }
        first_index++;
    }
    if (first_index == span_count) {
        return 0;
    }
    size_t last_index = span_count;
    while (last_index > first_index) {
        const db_snake_col_span_t span = spans[last_index - 1U];
        if ((span.col_end > span.col_start) && (span.row < max_rows)) {
            break;
        }
        last_index--;
    }
    if (last_index <= first_index) {
        return 0;
    }
    const uint32_t row_start = spans[first_index].row;
    const uint32_t row_end_exclusive = spans[last_index - 1U].row + 1U;
    if ((row_end_exclusive <= row_start) || (row_start >= max_rows)) {
        return 0;
    }
    *out_row_start = row_start;
    *out_row_count = row_end_exclusive - row_start;
    return (*out_row_count != 0U) ? 1 : 0;
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
            .color_r = 0.0,
            .color_g = 0.0,
            .color_b = 0.0,
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

static inline void db_grid_target_color_rgb(int phase_flag, double *out_r,
                                            double *out_g, double *out_b) {
    if (phase_flag != 0) {
        *out_r = (double)BENCH_GRID_PHASE0_R;
        *out_g = (double)BENCH_GRID_PHASE0_G;
        *out_b = (double)BENCH_GRID_PHASE0_B;
        return;
    }
    *out_r = (double)BENCH_GRID_PHASE1_R;
    *out_g = (double)BENCH_GRID_PHASE1_G;
    *out_b = (double)BENCH_GRID_PHASE1_B;
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
            .color_r = 0.0,
            .color_g = 0.0,
            .color_b = 0.0,
        };
        db_grid_target_color_rgb(plan->phase_flag, &result.target_r,
                                 &result.target_g, &result.target_b);
        result.force_full_fill_on_phase_complete = 1;
        result.has_next_phase_flag = 1;
        result.next_phase_flag = plan->next_phase_flag;
        return result;
    }

    result.region =
        db_snake_region_from_index(pattern_seed, plan->active_shape_index);
    result.target_r = result.region.color_r;
    result.target_g = result.region.color_g;
    result.target_b = result.region.color_b;
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
