#include "db_snake_fast_forward_internal.h"

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_snake_collect_internal.h"
#include "benchmarks/db_snake_progression_internal.h"
#include "benchmarks/db_snake_semantic_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"
#include "core/db_core.h"
#include "core/db_geometry.h"
#include "core/db_geometry_builder.h"
#include "core/db_numeric.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    db_benchmark_checkpoint_t *checkpoint;
    const double *rgb;
} settled_write_context_t;

static int write_settled_row(uint32_t row, uint32_t col_start, uint32_t col_end,
                             db_snake_row_segment_sink_t *sink) {
    const settled_write_context_t *const context =
        (sink != NULL) ? (const settled_write_context_t *)sink->geometry_sink
                       : NULL;
    if ((context == NULL) || (context->checkpoint == NULL) ||
        (context->rgb == NULL) || (col_end <= col_start)) {
        return 0;
    }
    db_benchmark_checkpoint_overlay_write(
        context->checkpoint,
        &(const db_colored_f64_block_t){
            .row_start = row,
            .row_count = 1U,
            .col_start = col_start,
            .col_count =
                db_checked_sub_u32("benchmark_snake_fast_forward",
                                   "settled_row_col_count", col_end, col_start),
            .rgb = {context->rgb[0], context->rgb[1], context->rgb[2]},
        });
    return 1;
}

static db_snake_shape_cache_t
shape_cache_for_eval(db_snake_progression_workspace_t *workspace,
                     const db_benchmark_runtime_init_t *runtime,
                     const db_snake_progression_eval_t *eval) {
    return db_snake_semantic_shape_cache(
        &workspace->shape, runtime->pattern_seed, eval->plan.active_shape_index,
        &eval->target.region, eval->is_shapes_mode);
}

static uint32_t clipped_range_count(uint32_t start, uint64_t count,
                                    uint32_t limit) {
    if ((start >= limit) || (count == 0U)) {
        return 0U;
    }
    const uint64_t available =
        (uint64_t)db_checked_sub_u32("benchmark_snake_fast_forward",
                                     "clipped_range_available", limit, start);
    return db_checked_u64_to_u32("benchmark_snake_fast_forward",
                                 "clipped_range_count",
                                 DB_MIN(count, available));
}

static void note_grid_range(db_snake_fast_forward_result_t *result,
                            uint32_t start, uint32_t count, uint32_t limit) {
    if ((result == NULL) || (count == 0U) || (start >= limit)) {
        return;
    }
    const uint32_t end =
        db_checked_add_u32("benchmark_snake_fast_forward", "covered_range_end",
                           start, clipped_range_count(start, count, limit));
    result->covered_first = DB_MIN(result->covered_first, start);
    result->covered_last = DB_MAX(result->covered_last, end);
}

static void add_plan_damage(const db_snake_progression_eval_t *eval,
                            const db_snake_shape_cache_t *shape_cache,
                            const db_snake_plan_t *coverage,
                            db_snake_progression_workspace_t *workspace,
                            db_geometry_builder_t *emitter) {
    size_t count = 0U;
    if (db_snake_collect_damage_blocks_for_plan(
            &eval->target.region, coverage, shape_cache,
            workspace->damage.blocks, workspace->damage.capacity,
            &count) == 0) {
        emitter->status = DB_GEOMETRY_BUILDER_OVERFLOW;
        return;
    }
    for (size_t index = 0U; index < count; index++) {
        (void)db_geometry_builder_add_damage(emitter,
                                             &workspace->damage.blocks[index]);
    }
}

static void write_settled_range(const db_snake_progression_eval_t *eval,
                                const db_snake_shape_cache_t *shape_cache,
                                uint32_t start, uint32_t count,
                                db_benchmark_checkpoint_t *checkpoint) {
    if ((count == 0U) || (checkpoint == NULL)) {
        return;
    }
    settled_write_context_t context = {
        .checkpoint = checkpoint,
        .rgb = eval->target.target_rgb,
    };
    (void)db_snake_for_each_damage_row_segment(
        &eval->target.region, start, count, 0U, 0U, shape_cache,
        write_settled_row,
        &(db_snake_row_segment_sink_t){.geometry_sink = &context});
}

static db_snake_progression_eval_t execute_movement_chunk(
    db_benchmark_runtime_init_t *runtime, uint32_t tick_count, int region_mode,
    db_snake_progression_workspace_t *workspace,
    db_benchmark_checkpoint_t *checkpoint, db_geometry_builder_t *emitter,
    db_snake_fast_forward_result_t *result) {
    db_benchmark_runtime_init_t unit_runtime = *runtime;
    unit_runtime.bench_speed_step = 1U;
    const db_snake_progression_eval_t first_eval =
        db_snake_progression_eval(&unit_runtime);
    const uint32_t first_cursor = first_eval.plan.active_cursor;
    const uint32_t prior_tick_count = db_checked_sub_u32(
        "benchmark_snake_fast_forward", "prior_tick_count", tick_count, 1U);
    const uint32_t terminal_cursor =
        db_checked_add_u32("benchmark_snake_fast_forward", "terminal_cursor",
                           first_cursor, prior_tick_count);
    db_benchmark_runtime_init_t terminal_runtime = *runtime;
    terminal_runtime.snake.cursor = terminal_cursor;
    if (tick_count > 1U) {
        terminal_runtime.snake.prev_start =
            db_checked_sub_u32("benchmark_snake_fast_forward",
                               "terminal_prev_start", terminal_cursor, 1U);
        terminal_runtime.snake.prev_count = first_eval.plan.batch_size;
    }
    terminal_runtime.bench_speed_step = 1U;
    const db_snake_progression_eval_t terminal_eval =
        db_snake_progression_eval(&terminal_runtime);
    const db_snake_shape_cache_t shape_cache =
        shape_cache_for_eval(workspace, runtime, &terminal_eval);
    const db_snake_shape_cache_t *const shape_cache_ptr =
        (terminal_eval.is_shapes_mode != 0) ? &shape_cache : NULL;

    uint64_t moving_span = 0U;
    if (tick_count > 1U) {
        moving_span = db_checked_add_u64(
            "benchmark_snake_fast_forward", "moving_span",
            (uint64_t)db_checked_sub_u32("benchmark_snake_fast_forward",
                                         "settled_tick_count", tick_count, 2U),
            (uint64_t)first_eval.plan.batch_size);
    }
    const uint64_t active_span = db_checked_add_u64(
        "benchmark_snake_fast_forward", "active_span",
        (uint64_t)prior_tick_count, (uint64_t)first_eval.plan.batch_size);
    const uint32_t moving_count = clipped_range_count(
        first_cursor, moving_span, first_eval.plan.target_tile_count);
    const db_snake_plan_t coverage = {
        .active_shape_index = first_eval.plan.active_shape_index,
        .prev_start = first_eval.plan.prev_start,
        .prev_count = first_eval.plan.prev_count,
        .active_cursor = first_cursor,
        .batch_size = clipped_range_count(first_cursor, active_span,
                                          first_eval.plan.target_tile_count),
        .target_tile_count = first_eval.plan.target_tile_count,
    };
    if (region_mode != 0) {
        write_settled_range(&first_eval, shape_cache_ptr,
                            first_eval.plan.prev_start,
                            first_eval.plan.prev_count, checkpoint);
        if (tick_count > 1U) {
            write_settled_range(&first_eval, shape_cache_ptr, first_cursor,
                                moving_count, checkpoint);
        }
        db_snake_plan_t terminal = terminal_eval.plan;
        terminal.prev_count = 0U;
        db_snake_semantic_emit_tiles(
            &terminal, &terminal_eval.target.region, shape_cache_ptr,
            terminal_eval.target.target_rgb, terminal_eval.is_grid_mode,
            &workspace->shape, checkpoint, emitter);
        add_plan_damage(&terminal_eval, shape_cache_ptr, &coverage, workspace,
                        emitter);
    } else {
        note_grid_range(result, coverage.prev_start, coverage.prev_count,
                        coverage.target_tile_count);
        note_grid_range(result, coverage.active_cursor, coverage.batch_size,
                        coverage.target_tile_count);
    }
    db_snake_progression_apply(runtime, &terminal_eval);
    result->processed_ticks =
        db_checked_add_u32("benchmark_snake_fast_forward", "processed_ticks",
                           result->processed_ticks, tick_count);
    result->chunk_count = db_checked_add_u32(
        "benchmark_snake_fast_forward", "chunk_count", result->chunk_count, 1U);
    result->terminal_tile_count = terminal_eval.plan.batch_size;
    return terminal_eval;
}

static db_snake_progression_eval_t
execute_completion(db_benchmark_runtime_init_t *runtime, int region_mode,
                   int shapes_mode, db_snake_progression_workspace_t *workspace,
                   db_benchmark_checkpoint_t *checkpoint,
                   db_geometry_builder_t *emitter,
                   db_snake_fast_forward_result_t *result) {
    db_benchmark_runtime_init_t unit_runtime = *runtime;
    unit_runtime.bench_speed_step = 1U;
    const db_snake_progression_eval_t eval =
        db_snake_progression_eval(&unit_runtime);
    const db_snake_shape_cache_t shape_cache =
        shape_cache_for_eval(workspace, runtime, &eval);
    const db_snake_shape_cache_t *const shape_cache_ptr =
        (eval.is_shapes_mode != 0) ? &shape_cache : NULL;
    if (region_mode != 0) {
        db_snake_semantic_emit_tiles(&eval.plan, &eval.target.region,
                                     shape_cache_ptr, eval.target.target_rgb,
                                     eval.is_grid_mode, &workspace->shape,
                                     checkpoint, emitter);
        add_plan_damage(&eval, shape_cache_ptr, &eval.plan, workspace, emitter);
        if (shapes_mode == 0) {
            const db_colored_f64_block_t block = {
                .row_start = eval.target.region.y,
                .row_count = eval.target.region.height,
                .col_start = eval.target.region.x,
                .col_count = eval.target.region.width,
                .rgb = {eval.target.target_rgb[0], eval.target.target_rgb[1],
                        eval.target.target_rgb[2]},
            };
            db_benchmark_checkpoint_overlay_write(checkpoint, &block);
            (void)db_geometry_builder_add_damage(
                emitter, &(const db_grid_block_t){
                             .row_start = block.row_start,
                             .row_count = block.row_count,
                             .col_start = block.col_start,
                             .col_count = block.col_count,
                         });
        }
    } else {
        note_grid_range(result, eval.plan.prev_start, eval.plan.prev_count,
                        eval.plan.target_tile_count);
        result->phase_full_fill = 1;
    }
    db_snake_progression_apply(runtime, &eval);
    result->processed_ticks =
        db_checked_add_u32("benchmark_snake_fast_forward", "processed_ticks",
                           result->processed_ticks, 1U);
    result->chunk_count = db_checked_add_u32(
        "benchmark_snake_fast_forward", "chunk_count", result->chunk_count, 1U);
    result->boundary_count =
        db_checked_add_u32("benchmark_snake_fast_forward", "boundary_count",
                           result->boundary_count, 1U);
    result->terminal_tile_count = 0U;
    return eval;
}

int db_snake_fast_forward_execute(const db_benchmark_runtime_init_t *runtime,
                                  uint32_t requested_ticks, int region_mode,
                                  int shapes_mode,
                                  db_snake_progression_workspace_t *workspace,
                                  db_benchmark_checkpoint_t *checkpoint,
                                  db_geometry_builder_t *emitter,
                                  db_snake_fast_forward_result_t *out_result) {
    if ((runtime == NULL) || (workspace == NULL) || (emitter == NULL) ||
        (out_result == NULL) || (requested_ticks == 0U) ||
        ((region_mode != 0) && (checkpoint == NULL))) {
        return 0;
    }
    *out_result = (db_snake_fast_forward_result_t){
        .runtime = *runtime,
        .covered_first = UINT32_MAX,
    };
    uint32_t remaining = requested_ticks;
    while (remaining > 0U) {
        db_benchmark_runtime_init_t unit_runtime = out_result->runtime;
        unit_runtime.bench_speed_step = 1U;
        const db_snake_progression_eval_t eval =
            db_snake_progression_eval(&unit_runtime);
        const uint32_t movement_ticks = db_checked_sub_u32(
            "benchmark_snake_fast_forward", "movement_ticks",
            eval.plan.target_tile_count, eval.plan.active_cursor);
        if (remaining <= movement_ticks) {
            out_result->final_eval = execute_movement_chunk(
                &out_result->runtime, remaining, region_mode, workspace,
                checkpoint, emitter, out_result);
            remaining = 0U;
            continue;
        }
        if (movement_ticks > 0U) {
            out_result->final_eval = execute_movement_chunk(
                &out_result->runtime, movement_ticks, region_mode, workspace,
                checkpoint, emitter, out_result);
            remaining = db_checked_sub_u32("benchmark_snake_fast_forward",
                                           "remaining_ticks", remaining,
                                           movement_ticks);
        }
        out_result->final_eval =
            execute_completion(&out_result->runtime, region_mode, shapes_mode,
                               workspace, checkpoint, emitter, out_result);
        remaining = db_checked_sub_u32("benchmark_snake_fast_forward",
                                       "remaining_ticks", remaining, 1U);
    }
    return 1;
}
