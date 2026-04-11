#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_GRADIENT_INTERNAL_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_GRADIENT_INTERNAL_H

#include "benchmarks/db_benchmark_geometry_internal.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include <string.h>

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
        next_direction_down = DB_BOOL(direction_down);
    }
    uint32_t next_cycle = cycle_index;
    uint32_t next_head = head_row;
    uint32_t wrap_count = 0U;
    const uint32_t step_count = DB_MAX(head_step, 1U);
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
    uint32_t traversed_dirty_end = DB_MIN(prev_head_end, rows);
    if (traversed_dirty_end < traversed_dirty_start) {
        traversed_dirty_end = traversed_dirty_start;
    }
    {
        uint32_t sample_head = prev_head;
        int sample_direction_down = 1;
        if (restart_at_top_only == 0) {
            sample_direction_down = DB_BOOL(direction_down);
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
                DB_MIN(db_checked_add_u32(DB_BENCH_COMMON_BACKEND,
                                          "gradient_sample_head_end",
                                          sample_start, window_rows),
                       rows);
            traversed_dirty_start = DB_MIN(traversed_dirty_start, sample_start);
            traversed_dirty_end = DB_MAX(traversed_dirty_end, sample_end);
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
            const uint32_t dirty1_end = DB_MIN(next_head_end, rows);
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
    return db_gradient_plan_next_frame(
        head_row, is_sweep ? direction_down : 1, cycle_index,
        DB_BOOL(pattern != DB_PATTERN_GRADIENT_SWEEP), head_step);
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
    for (size_t index = 0U; index < 2U; index++) {
        if (out_count >= out_capacity) {
            DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                          "gradient_clamped_blocks",
                                          out_count + 1, out_capacity);
            break;
        }
        const db_grid_block_t block = raw_blocks[index];
        if ((block.row_count == 0U) || (block.col_count == 0U) ||
            (block.row_start >= max_rows)) {
            continue;
        }
        const uint32_t clamped_end = DB_MIN(
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
    for (size_t base_index = 0U; base_index < base_count; base_index++) {
        if (out_count >= out_capacity) {
            DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                          "gradient_subtract_replay_blocks",
                                          out_count + 1, out_capacity);
            break;
        }
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
            if (current_start < cut_start) {
                if (out_count >= out_capacity) {
                    DB_LOG_CAPACITY_EXCEEDED_ONCE(
                        DB_BENCH_COMMON_BACKEND,
                        "gradient_subtract_replay_blocks", out_count + 1,
                        out_capacity);
                    return out_count;
                }
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
        if (current_start < base_end) {
            if (out_count >= out_capacity) {
                DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                              "gradient_subtract_replay_blocks",
                                              out_count + 1, out_capacity);
                return out_count;
            }
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
    const size_t copy_limit = DB_MIN(block_count, copy_capacity);
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
        DB_LOG_CAPACITY_EXCEEDED_ONCE(DB_BENCH_COMMON_BACKEND,
                                      "grid_blocks_compact_full_width",
                                      block_count, out_capacity);
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
    double source_rgb[3] = {0};
    double target_rgb[3] = {0};
    db_palette_cycle_color_rgb3(cycle_index, source_rgb);
    db_palette_cycle_color_rgb3(cycle_index + 1U, target_rgb);
    if ((rows == 0U) || (window_rows == 0U)) {
        memcpy(out_rgb, target_rgb, 3U * sizeof(double));
        return;
    }

    const uint32_t row = row_index % rows;
    const int64_t head_start_i64 = (int64_t)head_row - (int64_t)window_rows;
    const int64_t head_end_i64 = head_start_i64 + (int64_t)window_rows;
    const int64_t row_i64 = (int64_t)row;
    if (row_i64 < head_start_i64) {
        if (direction_down != 0) {
            memcpy(out_rgb, target_rgb, 3U * sizeof(double));
        } else {
            memcpy(out_rgb, source_rgb, 3U * sizeof(double));
        }
        return;
    }
    if (row_i64 >= head_end_i64) {
        if (direction_down != 0) {
            memcpy(out_rgb, source_rgb, 3U * sizeof(double));
        } else {
            memcpy(out_rgb, target_rgb, 3U * sizeof(double));
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
        DB_MIN(rows, db_checked_span_end_u32(DB_BENCH_COMMON_BACKEND,
                                             "gradient_apply_row_end",
                                             row_start, row_count));
    if (row_end <= row_start) {
        return 0;
    }
    *iter = (db_gradient_row_segment_iter_t){
        .block = *block,
        .row_end = row_end,
        .transition_start = db_u32_saturating_sub(head_row, window_rows),
        .transition_end =
            DB_MIN(rows, db_checked_add_u32(
                             DB_BENCH_COMMON_BACKEND, "gradient_transition_end",
                             db_u32_saturating_sub(head_row, window_rows),
                             window_rows)),
        .next_row = row_start,
        .head_row = head_row,
        .cycle_index = cycle_index,
        .direction_down = direction_down,
        .phase = (window_rows == 0U) ? DB_GRADIENT_SEGMENT_PHASE_SOLID
                                     : DB_GRADIENT_SEGMENT_PHASE_TOP,
        .source_rgb = {0},
        .target_rgb = {0},
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
        const uint32_t top_end = DB_MIN(iter->row_end, iter->transition_start);
        iter->next_row = DB_MAX(block_row_start, iter->transition_start);
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
        const uint32_t blend_end = DB_MIN(iter->row_end, iter->transition_end);
        if (iter->next_row < blend_end) {
            double row_rgb[3] = {0};
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
        iter->next_row = DB_MAX(block_row_start, iter->transition_end);
        iter->phase = DB_GRADIENT_SEGMENT_PHASE_BOTTOM;
    }
    if (iter->phase == DB_GRADIENT_SEGMENT_PHASE_BOTTOM) {
        const uint32_t bottom_start =
            DB_MAX(block_row_start, iter->transition_end);
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
