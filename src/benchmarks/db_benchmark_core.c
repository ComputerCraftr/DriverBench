#include "db_benchmark_core.h"

#include "db_benchmark_checkpoint_internal.h"

#include <stdint.h>
#include <stdlib.h>

#include <string.h>

#include "../benchmarks/db_benchmark_emitters.h"
#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../core/db_frame_plan.h"
#include "../core/db_geometry.h"
#include "../core/db_geometry_builder.h"
#include "../core/db_hash.h"
#include "../core/db_log.h"
#include "../core/db_numeric.h"
#include "../core/db_render_result.h"
#include "../core/db_render_types.h"
#include "benchmarks/db_benchmark_mode_runtime_internal.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "benchmarks/db_gradient_progression_internal.h"
#include "benchmarks/db_snake_collect_internal.h"
#include "benchmarks/db_snake_fast_forward_internal.h"
#include "benchmarks/db_snake_progression_internal.h"
#include "benchmarks/db_snake_semantic_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"

static db_snake_shape_cache_t
db_benchmark_core_snake_shape_cache(const db_benchmark_core_t *core,
                                    const db_snake_progression_eval_t *eval) {
    db_snake_shape_cache_t cache = {0};
    if ((core == NULL) || (eval == NULL) || (eval->is_shapes_mode == 0) ||
        (core->snake.progression.shape.row_bounds == NULL)) {
        return cache;
    }
    (void)db_snake_shape_cache_init_from_index(
        &cache, core->snake.progression.shape.row_bounds,
        core->snake.progression.shape.row_bounds_capacity,
        core->runtime.pattern_seed, eval->plan.active_shape_index,
        DB_U32_SALT_PALETTE, &eval->target.region, eval->shape_kind);
    return cache;
}

static db_block_emitter_sink_t
db_benchmark_core_emitter_sink(db_benchmark_core_t *core) {
    if (core == NULL) {
        return (db_block_emitter_sink_t){0};
    }
    return (db_block_emitter_sink_t){
        .logical_blocks = core->geometry.logical_blocks,
        .logical_capacity = core->geometry.capacity,
        .colored_blocks = core->geometry.colored_blocks,
        .colored_capacity = core->geometry.capacity,
        .status = DB_BLOCK_EMITTER_STATUS_OK,
    };
}

static void
db_benchmark_core_publish_emitter(db_benchmark_core_t *core,
                                  db_frame_plan_t *plan,
                                  const db_block_emitter_sink_t *sink) {
    if ((core == NULL) || (plan == NULL) || (sink == NULL)) {
        return;
    }
    plan->geometry.logical_damage = (db_grid_block_view_t){
        .blocks = sink->logical_blocks,
        .count = sink->logical_count,
    };
    size_t compacted_count = 0U;
    const db_geometry_f64_status_t compact_status =
        db_colored_f64_blocks_compact(
            (db_colored_f64_block_view_t){.blocks = sink->colored_blocks,
                                          .count = sink->colored_count},
            core->geometry.scratch_blocks, core->geometry.capacity,
            core->geometry.prepared_blocks, core->geometry.capacity,
            &compacted_count);
    plan->geometry.current_blocks = (db_colored_f64_block_view_t){
        .blocks = core->geometry.prepared_blocks,
        .count = compacted_count,
    };
    plan->geometry_overflowed =
        DB_BOOL((sink->status == DB_BLOCK_EMITTER_STATUS_OVERFLOW) ||
                (compact_status != DB_GEOMETRY_F64_STATUS_OK));
}

static int
db_benchmark_core_clip_geometry_to_damage(db_block_emitter_sink_t *emitter,
                                          db_colored_f64_block_t *output,
                                          size_t output_capacity) {
    if ((emitter == NULL) || (output == NULL)) {
        return 0;
    }
    size_t output_count = 0U;
    for (size_t geometry_index = 0U; geometry_index < emitter->colored_count;
         geometry_index++) {
        const db_colored_f64_block_t geometry =
            emitter->colored_blocks[geometry_index];
        const uint32_t geometry_row_end =
            geometry.row_start + geometry.row_count;
        const uint32_t geometry_col_end =
            geometry.col_start + geometry.col_count;
        for (size_t damage_index = 0U; damage_index < emitter->logical_count;
             damage_index++) {
            const db_grid_block_t damage =
                emitter->logical_blocks[damage_index];
            const uint32_t damage_row_end = damage.row_start + damage.row_count;
            const uint32_t damage_col_end = damage.col_start + damage.col_count;
            const uint32_t row_start =
                DB_MAX(geometry.row_start, damage.row_start);
            const uint32_t row_end = DB_MIN(geometry_row_end, damage_row_end);
            const uint32_t col_start =
                DB_MAX(geometry.col_start, damage.col_start);
            const uint32_t col_end = DB_MIN(geometry_col_end, damage_col_end);
            if ((row_end <= row_start) || (col_end <= col_start)) {
                continue;
            }
            if (output_count >= output_capacity) {
                return 0;
            }
            output[output_count++] = (db_colored_f64_block_t){
                .row_start = row_start,
                .row_count = row_end - row_start,
                .col_start = col_start,
                .col_count = col_end - col_start,
                .rgb = {geometry.rgb[0], geometry.rgb[1], geometry.rgb[2]},
            };
        }
    }
    emitter->colored_blocks = output;
    emitter->colored_capacity = output_capacity;
    emitter->colored_count = output_count;
    return 1;
}

static void
db_benchmark_core_populate_snake_damage_plan(db_benchmark_core_t *core,
                                             db_frame_plan_t *out_plan) {
    if ((core == NULL) || (out_plan == NULL) ||
        (core->snake.progression.damage.blocks == NULL)) {
        return;
    }

    db_block_emitter_sink_t emitter = db_benchmark_core_emitter_sink(core);
    db_block_emitter_sink_reset(&emitter);
    db_snake_plan_t coverage_plan = {0};
    const uint32_t requested_ticks = DB_MAX(core->runtime.bench_speed_step, 1U);
    const uint32_t initial_shape_index = core->runtime.snake.shape_index;
    const uint32_t initial_cursor = core->runtime.snake.cursor;
    const uint32_t initial_prev_start = core->runtime.snake.prev_start;

    if (core->checkpoint.enabled != 0) {
        db_benchmark_checkpoint_overlay_begin(&core->checkpoint);
    }
    db_snake_fast_forward_result_t fast_forward = {0};
    if (db_snake_fast_forward_execute(
            &core->runtime, requested_ticks,
            core->runtime_flags.pattern.is_snake_region_mode,
            core->runtime_flags.pattern.is_snake_shapes,
            &core->snake.progression,
            (core->checkpoint.enabled != 0) ? &core->checkpoint : NULL,
            &emitter, &fast_forward) == 0) {
        emitter.status = DB_BLOCK_EMITTER_STATUS_INVALID;
        return;
    }
    const db_snake_progression_eval_t final_eval = fast_forward.final_eval;
    const db_benchmark_runtime_init_t simulated_runtime = fast_forward.runtime;
    out_plan->simulation_tick_count = fast_forward.processed_ticks;
    out_plan->simulation_chunk_count = fast_forward.chunk_count;
    out_plan->simulation_boundary_count = fast_forward.boundary_count;
    out_plan->simulation_terminal_item_count = fast_forward.terminal_tile_count;
    core->pending_runtime = simulated_runtime;
    core->pending_runtime.simulation_work = db_checked_add_u64(
        "benchmark_core", "snake_simulation_work",
        core->runtime.simulation_work, (uint64_t)requested_ticks);

    const db_snake_shape_cache_t shape_cache =
        db_benchmark_core_snake_shape_cache(core, &final_eval);
    const db_snake_shape_cache_t *shape_cache_ptr =
        (final_eval.is_shapes_mode != 0) ? &shape_cache : NULL;
    coverage_plan = final_eval.plan;
    const int preserve_full_recovery_snapshot =
        DB_BOOL(fast_forward.phase_full_fill != 0);
    if (preserve_full_recovery_snapshot != 0) {
        coverage_plan.prev_start = 0U;
        coverage_plan.prev_count = 0U;
        coverage_plan.active_cursor = 0U;
        coverage_plan.batch_size = coverage_plan.target_tile_count;
    } else if ((core->runtime_flags.pattern.is_snake_region_mode == 0) &&
               (fast_forward.covered_first != UINT32_MAX) &&
               (fast_forward.covered_last > fast_forward.covered_first)) {
        coverage_plan.prev_start = 0U;
        coverage_plan.prev_count = 0U;
        coverage_plan.active_cursor = fast_forward.covered_first;
        coverage_plan.batch_size = db_checked_sub_u32(
            "benchmark_core", "snake_coverage_count", fast_forward.covered_last,
            fast_forward.covered_first);
    } else if ((core->runtime_flags.pattern.is_snake_shapes == 0) &&
               (final_eval.plan.phase_completed == 0)) {
        uint32_t settled_start = 0U;
        if ((final_eval.plan.active_shape_index == initial_shape_index) &&
            (initial_cursor != DB_SNAKE_CURSOR_PRE_ENTRY)) {
            settled_start = DB_MIN(initial_prev_start, initial_cursor);
        }
        coverage_plan.prev_start = settled_start;
        coverage_plan.prev_count = 0U;
        if (coverage_plan.active_cursor > settled_start) {
            coverage_plan.prev_count =
                db_checked_sub_u32("benchmark_core", "snake_settled_count",
                                   coverage_plan.active_cursor, settled_start);
        }
    }
    size_t damage_count = 0U;
    if (db_snake_collect_damage_blocks_for_plan(
            &final_eval.target.region, &coverage_plan, shape_cache_ptr,
            core->snake.progression.damage.blocks,
            core->snake.progression.damage.capacity, &damage_count) == 0) {
        core->snake.progression.damage.blocks[0] =
            db_grid_block_full(out_plan->grid_rows, out_plan->grid_cols);
        damage_count = 1U;
        out_plan->rebuild_required = 1;
    }
    for (size_t index = 0U; index < damage_count; index++) {
        (void)db_geometry_builder_add_damage(
            &emitter, &core->snake.progression.damage.blocks[index]);
    }
    if ((final_eval.is_grid_mode != 0) && (fast_forward.phase_full_fill != 0)) {
        emitter.colored_count = 0U;
        db_snake_semantic_emit_rebuild(
            &final_eval, simulated_runtime.pattern_seed,
            final_eval.is_grid_mode, final_eval.is_shapes_mode,
            &core->snake.progression.shape, &emitter);
    } else if (final_eval.is_grid_mode != 0) {
        db_snake_semantic_emit_rebuild(
            &final_eval, simulated_runtime.pattern_seed,
            final_eval.is_grid_mode, final_eval.is_shapes_mode,
            &core->snake.progression.shape, &emitter);
        if (db_benchmark_core_clip_geometry_to_damage(
                &emitter, core->geometry.rebuild_colored_blocks,
                core->geometry.capacity) == 0) {
            emitter.status = DB_BLOCK_EMITTER_STATUS_OVERFLOW;
        }
    }
    if (core->checkpoint.enabled != 0) {
        db_benchmark_checkpoint_overlay_publish(&core->checkpoint, &emitter);
    }
    db_benchmark_core_publish_emitter(core, out_plan, &emitter);

    if ((out_plan->rebuild_required != 0) && (core->checkpoint.enabled == 0)) {
        db_block_emitter_sink_t rebuild = {
            .logical_blocks = core->geometry.rebuild_logical_blocks,
            .logical_capacity = core->geometry.capacity,
            .colored_blocks = core->geometry.rebuild_colored_blocks,
            .colored_capacity = core->geometry.capacity,
        };
        db_block_emitter_sink_reset(&rebuild);
        const db_grid_block_t full =
            db_grid_block_full(out_plan->grid_rows, out_plan->grid_cols);
        (void)db_geometry_builder_add_damage(&rebuild, &full);
        db_snake_semantic_emit_rebuild(
            &final_eval, simulated_runtime.pattern_seed,
            final_eval.is_grid_mode, final_eval.is_shapes_mode,
            &core->snake.progression.shape, &rebuild);
        size_t rebuild_count = 0U;
        const db_geometry_f64_status_t status = db_colored_f64_blocks_compact(
            (db_colored_f64_block_view_t){.blocks = rebuild.colored_blocks,
                                          .count = rebuild.colored_count},
            core->geometry.scratch_blocks, core->geometry.capacity,
            core->geometry.prepared_rebuild_blocks, core->geometry.capacity,
            &rebuild_count);
        out_plan->rebuild_seed = (db_frame_rebuild_seed_t){
            .kind = DB_FRAME_REBUILD_SEED_GEOMETRY,
            .geometry =
                {
                    .blocks = core->geometry.prepared_rebuild_blocks,
                    .count = rebuild_count,
                },
        };
        out_plan->geometry_overflowed =
            DB_BOOL((out_plan->geometry_overflowed != 0) ||
                    (rebuild.status != DB_GEOMETRY_BUILDER_OK) ||
                    (status != DB_GEOMETRY_F64_STATUS_OK));
    }
}

void db_benchmark_core_init(db_benchmark_core_t *core,
                            const db_benchmark_runtime_init_t *init_state,
                            db_pixel_format_t working_format) {
    if (core == NULL || init_state == NULL) {
        return;
    }
    *core = (db_benchmark_core_t){0};
    core->runtime = *init_state;
    core->runtime_flags = db_benchmark_mode_runtime_flags(&core->runtime);
    core->initialized = 1;

    if (core->runtime_flags.pattern.is_snake_region_mode != 0) {
        double seed_rgb[3] = {0.0, 0.0, 0.0};
        db_benchmark_seed_background_color_rgb3(&core->runtime, seed_rgb);
        db_benchmark_checkpoint_init(
            &core->checkpoint, db_grid_cols_effective(),
            db_grid_rows_effective(), working_format, seed_rgb);
    }

    core->geometry.capacity = DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY;
    core->geometry.logical_blocks = (db_grid_block_t *)db_malloc_or_fail(
        "benchmark_core", "emitted_logical_blocks", core->geometry.capacity,
        sizeof(*core->geometry.logical_blocks));
    core->geometry.colored_blocks = (db_colored_f64_block_t *)db_malloc_or_fail(
        "benchmark_core", "emitted_colored_blocks", core->geometry.capacity,
        sizeof(*core->geometry.colored_blocks));
    core->geometry.prepared_blocks =
        (db_colored_f64_block_t *)db_malloc_or_fail(
            "benchmark_core", "prepared_colored_blocks",
            core->geometry.capacity, sizeof(*core->geometry.prepared_blocks));
    core->geometry.rebuild_logical_blocks =
        (db_grid_block_t *)db_malloc_or_fail(
            "benchmark_core", "rebuild_logical_blocks", core->geometry.capacity,
            sizeof(*core->geometry.rebuild_logical_blocks));
    core->geometry.rebuild_colored_blocks =
        (db_colored_f64_block_t *)db_malloc_or_fail(
            "benchmark_core", "rebuild_colored_blocks", core->geometry.capacity,
            sizeof(*core->geometry.rebuild_colored_blocks));
    core->geometry.prepared_rebuild_blocks =
        (db_colored_f64_block_t *)db_malloc_or_fail(
            "benchmark_core", "prepared_rebuild_blocks",
            core->geometry.capacity,
            sizeof(*core->geometry.prepared_rebuild_blocks));
    core->geometry.scratch_blocks = (db_colored_f64_block_t *)db_malloc_or_fail(
        "benchmark_core", "geometry_scratch_blocks", core->geometry.capacity,
        sizeof(*core->geometry.scratch_blocks));

    if (core->runtime_flags.pattern.is_snake != 0) {
        db_snake_progression_workspace_init(
            &core->snake.progression, "benchmark_core",
            DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT);
        if (core->runtime_flags.pattern.is_snake_shapes != 0) {
            core->snake.progression.shape.row_bounds =
                (db_snake_shape_row_bounds_t *)db_malloc_or_fail(
                    "benchmark_core", "snake_shape_row_bounds",
                    db_grid_rows_effective(),
                    sizeof(*core->snake.progression.shape.row_bounds));
            core->snake.progression.shape.row_bounds_capacity =
                db_checked_u32_to_size("benchmark_core",
                                       "snake_shape_row_bounds_capacity",
                                       db_grid_rows_effective());
        }

        core->snake.progression.damage.blocks =
            (db_grid_block_t *)db_malloc_or_fail(
                "benchmark_core", "snake_damage_blocks",
                DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT,
                sizeof(*core->snake.progression.damage.blocks));
        core->snake.progression.damage.capacity =
            DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT;
    }
}

void db_benchmark_core_generate_plan(db_benchmark_core_t *core,
                                     uint32_t frame_index,
                                     const db_frame_plan_request_t *request,
                                     db_frame_plan_t *out_plan) {
    if (core == NULL || out_plan == NULL) {
        return;
    }
    *out_plan = (db_frame_plan_t){0};
    out_plan->frame_index = frame_index;
    core->pending_runtime = core->runtime;
    out_plan->grid_cols = db_grid_cols_effective();
    out_plan->grid_rows = db_grid_rows_effective();
    out_plan->pixel_width = ((request != NULL) && (request->pixel_width > 0U))
                                ? request->pixel_width
                                : db_grid_cols_effective();
    out_plan->pixel_height = ((request != NULL) && (request->pixel_height > 0U))
                                 ? request->pixel_height
                                 : db_grid_rows_effective();
    out_plan->full_draw_required =
        DB_BOOL((request != NULL) && (request->force_full_draw != 0));
    if ((request != NULL) && (request->force_rebuild != 0)) {
        out_plan->rebuild_required = 1;
        out_plan->rebuild_reason =
            (request->rebuild_reason != DB_FRAME_REBUILD_NONE)
                ? request->rebuild_reason
                : DB_FRAME_REBUILD_EXPLICIT;
    }

    if (core->runtime_flags.pattern.is_snake != 0) {
        out_plan->seeded_background = DB_BOOL(frame_index == 0U);
        if ((out_plan->seeded_background != 0) &&
            (out_plan->rebuild_reason == DB_FRAME_REBUILD_NONE)) {
            out_plan->rebuild_reason = DB_FRAME_REBUILD_SEED;
        }
        out_plan->rebuild_required =
            DB_BOOL((out_plan->rebuild_required != 0) ||
                    (out_plan->seeded_background != 0));
        db_benchmark_core_populate_snake_damage_plan(core, out_plan);
        if ((out_plan->rebuild_required != 0) &&
            (out_plan->rebuild_reason == DB_FRAME_REBUILD_NONE)) {
            out_plan->rebuild_reason = DB_FRAME_REBUILD_GEOMETRY_RECOVERY;
        }
    } else if (core->runtime_flags.pattern.is_gradient != 0) {
        const db_gradient_damage_plan_t grad_plan =
            db_gradient_progression_eval(&core->runtime);
        db_block_emitter_sink_t emitter = db_benchmark_core_emitter_sink(core);
        (void)db_benchmark_emit_gradient(
            out_plan->grid_cols, out_plan->grid_rows, &grad_plan, 0, &emitter);
        db_benchmark_core_publish_emitter(core, out_plan, &emitter);
        db_block_emitter_sink_t rebuild_emitter = {
            .logical_blocks = core->geometry.rebuild_logical_blocks,
            .logical_capacity = core->geometry.capacity,
            .colored_blocks = core->geometry.rebuild_colored_blocks,
            .colored_capacity = core->geometry.capacity,
        };
        (void)db_benchmark_emit_gradient(out_plan->grid_cols,
                                         out_plan->grid_rows, &grad_plan, 1,
                                         &rebuild_emitter);
        size_t rebuild_count = 0U;
        const db_geometry_f64_status_t rebuild_status =
            db_colored_f64_blocks_compact(
                (db_colored_f64_block_view_t){
                    .blocks = rebuild_emitter.colored_blocks,
                    .count = rebuild_emitter.colored_count},
                core->geometry.scratch_blocks, core->geometry.capacity,
                core->geometry.prepared_rebuild_blocks, core->geometry.capacity,
                &rebuild_count);
        out_plan->rebuild_seed = (db_frame_rebuild_seed_t){
            .kind = DB_FRAME_REBUILD_SEED_GEOMETRY,
            .geometry =
                {
                    .blocks = core->geometry.prepared_rebuild_blocks,
                    .count = rebuild_count,
                },
        };
        out_plan->geometry_overflowed = DB_BOOL(
            (out_plan->geometry_overflowed != 0) ||
            (rebuild_emitter.status == DB_BLOCK_EMITTER_STATUS_OVERFLOW) ||
            (rebuild_status != DB_GEOMETRY_F64_STATUS_OK));
        core->pending_runtime = core->runtime;
        db_gradient_progression_apply(&core->pending_runtime, &grad_plan);
        core->pending_runtime.simulation_work = db_checked_add_u64(
            "benchmark_core", "gradient_simulation_work",
            core->runtime.simulation_work,
            (uint64_t)DB_MAX(core->runtime.bench_speed_step, 1U));
    } else if (core->runtime_flags.pattern.is_bands != 0) {
        db_block_emitter_sink_t emitter = db_benchmark_core_emitter_sink(core);
        core->pending_runtime.simulation_work = db_checked_add_u64(
            "benchmark_core", "bands_simulation_work",
            core->runtime.simulation_work,
            (uint64_t)DB_MAX(core->runtime.bench_speed_step, 1U));
        (void)db_benchmark_emit_bands(
            out_plan->grid_cols, out_plan->grid_rows, BENCH_BANDS,
            db_checked_u64_to_u32("benchmark_core", "bands_work",
                                  core->pending_runtime.simulation_work),
            &emitter);
        db_benchmark_core_publish_emitter(core, out_plan, &emitter);
    }
    if (out_plan->geometry_overflowed != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "canonical_geometry_overflow"),
            DB_LOG_U64("frame", frame_index),
            DB_LOG_U64("capacity", core->geometry.capacity),
            DB_LOG_U64("shape_index", core->runtime.snake.shape_index),
        };
        db_log_fail("benchmark", "frame_plan_error", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }

    if ((out_plan->rebuild_required != 0) && (core->checkpoint.enabled != 0)) {
        db_benchmark_checkpoint_publish_seed(&core->checkpoint, out_plan);
    } else if ((out_plan->rebuild_required != 0) &&
               (out_plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_NONE)) {
        out_plan->rebuild_seed = (db_frame_rebuild_seed_t){
            .kind = DB_FRAME_REBUILD_SEED_GEOMETRY,
            .geometry = out_plan->geometry.current_blocks,
        };
    }
    out_plan->geometry.operation = DB_GEOMETRY_EXECUTION_NO_OP;
    if (out_plan->rebuild_seed.kind != DB_FRAME_REBUILD_SEED_NONE) {
        out_plan->geometry.operation = DB_GEOMETRY_EXECUTION_REBUILD;
    } else if (out_plan->geometry.current_blocks.count > 0U) {
        out_plan->geometry.operation = DB_GEOMETRY_EXECUTION_INCREMENTAL;
    }
    out_plan->geometry.overflowed = out_plan->geometry_overflowed;
    out_plan->expected_state_hash =
        db_benchmark_runtime_state_hash_cross_renderer(
            &core->pending_runtime, out_plan->grid_cols, out_plan->grid_rows);
}

void db_benchmark_core_apply_plan(db_benchmark_core_t *core,
                                  const db_frame_plan_t *plan,
                                  const db_render_result_t *result) {
    if ((core == NULL) || (plan == NULL) || (result == NULL) ||
        (result->success == 0)) {
        return;
    }
    db_benchmark_checkpoint_commit(&core->checkpoint, plan, result);
    core->runtime = core->pending_runtime;
}

void db_benchmark_core_shutdown(db_benchmark_core_t *core) {
    if (core == NULL || core->initialized == 0) {
        return;
    }
    if (core->runtime_flags.pattern.is_snake != 0) {
        free(core->snake.progression.damage.blocks);
        free(core->snake.progression.shape.row_bounds);
        db_snake_progression_workspace_free(&core->snake.progression);
    }
    db_benchmark_checkpoint_shutdown(&core->checkpoint);
    free(core->geometry.logical_blocks);
    free(core->geometry.colored_blocks);
    free(core->geometry.prepared_blocks);
    free(core->geometry.rebuild_logical_blocks);
    free(core->geometry.rebuild_colored_blocks);
    free(core->geometry.prepared_rebuild_blocks);
    free(core->geometry.scratch_blocks);
    core->geometry.logical_blocks = NULL;
    core->geometry.colored_blocks = NULL;
    core->geometry.prepared_blocks = NULL;
    core->geometry.rebuild_logical_blocks = NULL;
    core->geometry.rebuild_colored_blocks = NULL;
    core->geometry.prepared_rebuild_blocks = NULL;
    core->geometry.scratch_blocks = NULL;
    core->geometry.capacity = 0U;
    core->initialized = 0;
}
