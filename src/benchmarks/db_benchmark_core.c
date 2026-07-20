#include "db_benchmark_core.h"

#include "db_benchmark_checkpoint_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <string.h>

#include "../benchmarks/db_benchmark_emitters.h"
#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../core/db_frame_plan.h"
#include "../core/db_geometry.h"
#include "../core/db_hash.h"
#include "../core/db_log.h"
#include "../core/db_numeric.h"
#include "../core/db_render_ir.h"
#include "../core/db_render_result.h"
#include "../core/db_render_types.h"
#include "benchmarks/db_benchmark_geometry_internal.h"
#include "benchmarks/db_benchmark_gradient_internal.h"
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

static db_render_ir_status_t build_rebuild_ir(db_benchmark_core_t *core,
                                              db_frame_plan_t *plan) {
    db_render_ir_store_t *const store = &core->ir.rebuild;
    db_render_ir_store_reset(store);
    plan->external_bindings = (db_render_ir_external_binding_view_t){0};
    if (plan->rebuild_required == 0) {
        plan->rebuild_ir = db_render_ir_store_view(store);
        plan->rebuild_ir_hash = db_render_ir_hash(&plan->rebuild_ir);
        return DB_RENDER_IR_OK;
    }

    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t status = db_render_ir_add_resource(
        store,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = plan->grid_cols,
            .height = plan->grid_rows,
            .format = core->working_format},
        &target);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_begin_target(store, target);
    }
    if ((status == DB_RENDER_IR_OK) && (core->checkpoint.enabled != 0)) {
        db_render_ir_resource_id_t source = DB_RENDER_IR_INVALID_ID;
        const db_pixel_surface_t *const raster = &core->checkpoint.surface;
        status = db_render_ir_add_resource(
            store,
            &(const db_render_ir_resource_t){
                .kind = DB_RENDER_IR_RESOURCE_RASTER_SOURCE,
                .width = raster->pixel_width,
                .height = raster->pixel_height,
                .format = raster->format},
            &source);
        if (status == DB_RENDER_IR_OK) {
            db_render_ir_rect_t source_rect = {0};
            if (db_render_ir_rect_from_extent(raster->pixel_width,
                                              raster->pixel_height,
                                              &source_rect) == 0) {
                status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
            } else {
                status = db_render_ir_upload_image(
                    store, target, source, source_rect, 0, 0,
                    (db_render_ir_upload_semantics_t){
                        .replacement = DB_RENDER_IR_UPLOAD_REPLACE_EXACT,
                        .filter = DB_RENDER_IR_FILTER_NEAREST,
                        .conversion = DB_RENDER_IR_CONVERSION_EXACT,
                        .prior_content = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT,
                        .opacity = 1.0});
            }
        }
        if (status == DB_RENDER_IR_OK) {
            const size_t row_stride_bytes = db_checked_mul_size(
                "benchmark", "checkpoint row stride", raster->pixel_width,
                db_pixel_surface_pixel_bytes(raster));
            plan->external_binding_storage[0] =
                (db_render_ir_external_binding_t){
                    .resource = source,
                    .generation = core->checkpoint.generation,
                    .content_revision = core->checkpoint.content_revision,
                    .width = raster->pixel_width,
                    .height = raster->pixel_height,
                    .format = raster->format,
                    .row_stride_bytes = row_stride_bytes,
                    .size_bytes = core->checkpoint.surface_size_bytes,
                    .pixels = raster->pixels};
            plan->external_bindings = (db_render_ir_external_binding_view_t){
                .bindings = plan->external_binding_storage, .count = 1U};
        }
    } else if (status == DB_RENDER_IR_OK) {
        status = core->ir.rebuild_status;
        if (status == DB_RENDER_IR_OK) {
            status = db_render_ir_fill_rects(
                store, target, core->ir.rebuild_fills,
                core->ir.rebuild_fill_count, DB_RENDER_IR_INVALID_ID);
        }
    }
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_end_target(store, target);
    }
    if (status == DB_RENDER_IR_OK) {
        db_render_ir_region_id_t full_region = DB_RENDER_IR_INVALID_ID;
        db_render_ir_rect_t full_rect = {0};
        if (db_render_ir_rect_from_extent(plan->grid_cols, plan->grid_rows,
                                          &full_rect) == 0) {
            status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
        } else {
            status =
                db_render_ir_add_rect_region(store, full_rect, &full_region);
        }
        if (status == DB_RENDER_IR_OK) {
            status = db_render_ir_set_last_command_regions(store, full_region,
                                                           full_region);
        }
    }
    plan->rebuild_ir = db_render_ir_store_view(store);
    plan->rebuild_ir_hash = db_render_ir_hash(&plan->rebuild_ir);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_validate_bindings(&plan->rebuild_ir,
                                                plan->external_bindings);
    }
    return status;
}

static uint64_t fill_area_sum(const db_render_ir_fill_t *fills,
                              size_t fill_count) {
    uint64_t area = 0U;
    for (size_t index = 0U; index < fill_count; index++) {
        const uint64_t fill_area = db_render_ir_rect_area(fills[index].rect);
        area = db_u64_saturating_add(area, fill_area);
    }
    return area;
}

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

static db_benchmark_ir_emitter_t
db_benchmark_core_emitter_sink(db_benchmark_core_t *core) {
    if (core == NULL) {
        return (db_benchmark_ir_emitter_t){0};
    }
    return (db_benchmark_ir_emitter_t){
        .logical_blocks = core->geometry.logical_blocks,
        .logical_capacity = core->geometry.capacity,
        .fills = core->ir.optimizer_primary,
        .fill_capacity = core->ir.fill_capacity,
        .status = DB_BENCHMARK_IR_EMITTER_OK,
    };
}

static void
db_benchmark_core_publish_emitter(db_benchmark_core_t *core,
                                  db_frame_plan_t *plan,
                                  const db_benchmark_ir_emitter_t *sink) {
    if ((core == NULL) || (plan == NULL) || (sink == NULL)) {
        return;
    }
    db_render_ir_store_reset(&core->ir.raw);
    db_render_ir_store_reset(&core->ir.optimized);
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t ir_status = db_render_ir_add_resource(
        &core->ir.raw,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = plan->grid_cols,
            .height = plan->grid_rows,
            .format = core->working_format,
        },
        &target);
    if (ir_status == DB_RENDER_IR_OK) {
        ir_status = db_render_ir_begin_target(&core->ir.raw, target);
    }
    if (sink->fill_count > core->ir.fill_capacity) {
        ir_status = DB_RENDER_IR_CAPACITY;
    }
    if (ir_status == DB_RENDER_IR_OK) {
        ir_status =
            db_render_ir_fill_rects(&core->ir.raw, target, sink->fills,
                                    sink->fill_count, DB_RENDER_IR_INVALID_ID);
    }
    if (ir_status == DB_RENDER_IR_OK) {
        ir_status = db_render_ir_end_target(&core->ir.raw, target);
    }
    const db_render_ir_view_t raw_ir = db_render_ir_store_view(&core->ir.raw);
    if (ir_status == DB_RENDER_IR_OK) {
        ir_status = db_render_ir_validate(&raw_ir);
    }
    if (ir_status == DB_RENDER_IR_OK) {
        ir_status = db_render_ir_optimize(
            &raw_ir, &core->ir.optimized,
            (db_render_ir_optimizer_workspace_t){
                .primary = core->ir.optimizer_primary,
                .secondary = core->ir.optimizer_secondary,
                .coverage_bands = core->ir.optimizer_coverage_bands,
                .coverage_band_scratch =
                    core->ir.optimizer_coverage_band_scratch,
                .coverage_spans = core->ir.optimizer_coverage_spans,
                .coverage_span_scratch =
                    core->ir.optimizer_coverage_span_scratch,
                .capacity = core->ir.fill_capacity,
            });
    }
    plan->update_ir = db_render_ir_store_view(&core->ir.optimized);
    plan->update_ir_hash = db_render_ir_hash(&plan->update_ir);
    plan->update_metadata = db_render_ir_metadata(
        &plan->update_ir, ir_status, plan->grid_cols, plan->grid_rows);
    const uint64_t raw_area = fill_area_sum(raw_ir.fills, raw_ir.fill_count);
    const uint64_t optimized_area =
        fill_area_sum(plan->update_ir.fills, plan->update_ir.fill_count);
    plan->update_metadata.eliminated_area =
        db_u64_saturating_sub(raw_area, optimized_area);

    if (sink->status != DB_BENCHMARK_IR_EMITTER_OK) {
        plan->update_metadata.status = DB_RENDER_IR_CAPACITY;
    }
    if (plan->update_metadata.status != DB_RENDER_IR_OK) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "update_ir_rejected"),
            DB_LOG_TOKEN("status", db_render_ir_status_name(
                                       plan->update_metadata.status)),
            DB_LOG_U64("raw_commands", raw_ir.command_count),
            DB_LOG_U64("raw_command_bytes", raw_ir.command_size),
            DB_LOG_U64("raw_fills", raw_ir.fill_count),
            DB_LOG_U64("emitted_fills", sink->fill_count),
            DB_LOG_U64("emitted_damage", sink->logical_count),
            DB_LOG_U64("emitter_status", sink->status),
        };
        db_log_error("benchmark", "render_ir_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
    }
}

static db_render_ir_status_t
emit_solid_rect(db_render_ir_store_t *store, db_render_ir_resource_id_t target,
                uint32_t col_start, uint32_t col_count, uint32_t row_start,
                uint32_t row_count, const double rgb[3]) {
    if ((row_count == 0U) || (col_count == 0U)) {
        return DB_RENDER_IR_OK;
    }
    if ((col_start > INT32_MAX) || (col_count > INT32_MAX) ||
        (row_start > INT32_MAX) || (row_count > INT32_MAX)) {
        return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }
    return db_render_ir_fill_rects(
        store, target,
        &(const db_render_ir_fill_t){
            .rect = {.x = (int32_t)col_start,
                     .y = (int32_t)row_start,
                     .width = (int32_t)col_count,
                     .height = (int32_t)row_count},
            .color = {.rgba = {rgb[0], rgb[1], rgb[2], 1.0}}},
        1U, DB_RENDER_IR_INVALID_ID);
}

static void publish_gradient_ir(db_benchmark_core_t *core,
                                db_frame_plan_t *plan,
                                const db_gradient_damage_plan_t *gradient,
                                int full_frame,
                                db_grid_block_view_t logical_damage) {
    if ((core == NULL) || (plan == NULL) || (gradient == NULL)) {
        return;
    }
    db_render_ir_store_reset(&core->ir.raw);
    db_render_ir_store_reset(&core->ir.optimized);
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t status = db_render_ir_add_resource(
        &core->ir.raw,
        &(const db_render_ir_resource_t){
            .kind = DB_RENDER_IR_RESOURCE_CANONICAL_TARGET,
            .width = plan->grid_cols,
            .height = plan->grid_rows,
            .format = core->working_format,
        },
        &target);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_begin_target(&core->ir.raw, target);
    }

    double source_rgb[3] = {0};
    double target_rgb[3] = {0};
    db_palette_cycle_color_rgb3(gradient->render_state.cycle_index, source_rgb);
    db_palette_cycle_color_rgb3(gradient->render_state.cycle_index + 1U,
                                target_rgb);
    const double *const top_rgb =
        (gradient->render_state.direction_down != 0) ? target_rgb : source_rgb;
    const double *const bottom_rgb =
        (gradient->render_state.direction_down != 0) ? source_rgb : target_rgb;
    const uint32_t window_rows = db_gradient_window_rows_effective();
    const uint32_t transition_start =
        db_u32_saturating_sub(gradient->render_state.head_row, window_rows);
    const uint32_t transition_end = DB_MIN(
        plan->grid_rows,
        db_checked_add_u32("benchmark_core", "gradient_ir_transition_end",
                           transition_start, window_rows));
    const int64_t axis_start_i64 =
        (int64_t)gradient->render_state.head_row - (int64_t)window_rows;
    const int64_t axis_end_i64 = axis_start_i64 + (int64_t)window_rows - 1;
    if ((axis_start_i64 < INT32_MIN) || (axis_start_i64 > INT32_MAX) ||
        (axis_end_i64 < INT32_MIN) || (axis_end_i64 > INT32_MAX)) {
        status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }

    const db_grid_block_t full_damage =
        db_grid_block_full(plan->grid_rows, plan->grid_cols);
    const db_grid_block_view_t damage_view =
        (full_frame != 0)
            ? (db_grid_block_view_t){.blocks = &full_damage, .count = 1U}
            : logical_damage;
    for (size_t index = 0U;
         (index < damage_view.count) && (status == DB_RENDER_IR_OK); index++) {
        const db_grid_block_t damage = damage_view.blocks[index];
        const uint32_t row_end = DB_MIN(
            plan->grid_rows,
            db_checked_add_u32("benchmark_core", "gradient_ir_damage_end",
                               damage.row_start, damage.row_count));
        const uint32_t top_end = DB_MIN(row_end, transition_start);
        if (top_end > damage.row_start) {
            status = emit_solid_rect(&core->ir.raw, target, damage.col_start,
                                     damage.col_count, damage.row_start,
                                     top_end - damage.row_start, top_rgb);
        }
        const uint32_t blend_start = DB_MAX(damage.row_start, transition_start);
        const uint32_t blend_end = DB_MIN(row_end, transition_end);
        if ((status == DB_RENDER_IR_OK) && (blend_end > blend_start)) {
            const db_render_ir_color_t start_color = {
                .rgba = {source_rgb[0], source_rgb[1], source_rgb[2], 1.0}};
            const db_render_ir_color_t end_color = {
                .rgba = {target_rgb[0], target_rgb[1], target_rgb[2], 1.0}};
            status = db_render_ir_fill_linear_gradient(
                &core->ir.raw, target,
                (db_render_ir_rect_t){.x = (int32_t)damage.col_start,
                                      .y = (int32_t)blend_start,
                                      .width = (int32_t)damage.col_count,
                                      .height =
                                          (int32_t)(blend_end - blend_start)},
                (int32_t)axis_start_i64, (int32_t)axis_end_i64,
                gradient->render_state.direction_down, start_color, end_color,
                DB_RENDER_IR_INVALID_ID);
        }
        const uint32_t bottom_start = DB_MAX(damage.row_start, transition_end);
        if ((status == DB_RENDER_IR_OK) && (row_end > bottom_start)) {
            status = emit_solid_rect(&core->ir.raw, target, damage.col_start,
                                     damage.col_count, bottom_start,
                                     row_end - bottom_start, bottom_rgb);
        }
    }
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_end_target(&core->ir.raw, target);
    }
    const db_render_ir_view_t raw_ir = db_render_ir_store_view(&core->ir.raw);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_optimize(
            &raw_ir, &core->ir.optimized,
            (db_render_ir_optimizer_workspace_t){
                .primary = core->ir.optimizer_primary,
                .secondary = core->ir.optimizer_secondary,
                .coverage_bands = core->ir.optimizer_coverage_bands,
                .coverage_band_scratch =
                    core->ir.optimizer_coverage_band_scratch,
                .coverage_spans = core->ir.optimizer_coverage_spans,
                .coverage_span_scratch =
                    core->ir.optimizer_coverage_span_scratch,
                .capacity = core->ir.fill_capacity,
            });
    }
    plan->update_ir = db_render_ir_store_view(&core->ir.optimized);
    plan->update_ir_hash = db_render_ir_hash(&plan->update_ir);
    plan->update_metadata = db_render_ir_metadata(
        &plan->update_ir, status, plan->grid_cols, plan->grid_rows);
}

static int db_benchmark_core_clip_geometry_to_damage(
    db_benchmark_ir_emitter_t *emitter, db_render_ir_fill_t *output,
    size_t output_capacity, uint32_t grid_cols, uint32_t grid_rows) {
    if ((emitter == NULL) || (output == NULL)) {
        return 0;
    }
    size_t output_count = 0U;
    for (size_t geometry_index = 0U; geometry_index < emitter->fill_count;
         geometry_index++) {
        const db_render_ir_fill_t geometry = emitter->fills[geometry_index];
        db_grid_block_t geometry_block = {0};
        if (db_render_ir_rect_to_grid_block(geometry.rect, grid_cols, grid_rows,
                                            &geometry_block) == 0) {
            return 0;
        }
        const uint32_t geometry_row_start = geometry_block.row_start;
        const uint32_t geometry_col_start = geometry_block.col_start;
        const uint32_t geometry_row_end = db_checked_add_u32(
            "benchmark", "geometry row end", geometry_block.row_start,
            geometry_block.row_count);
        const uint32_t geometry_col_end = db_checked_add_u32(
            "benchmark", "geometry column end", geometry_block.col_start,
            geometry_block.col_count);
        for (size_t damage_index = 0U; damage_index < emitter->logical_count;
             damage_index++) {
            const db_grid_block_t damage =
                emitter->logical_blocks[damage_index];
            const uint32_t damage_row_end = damage.row_start + damage.row_count;
            const uint32_t damage_col_end = damage.col_start + damage.col_count;
            const uint32_t row_start =
                DB_MAX(geometry_row_start, damage.row_start);
            const uint32_t row_end = DB_MIN(geometry_row_end, damage_row_end);
            const uint32_t col_start =
                DB_MAX(geometry_col_start, damage.col_start);
            const uint32_t col_end = DB_MIN(geometry_col_end, damage_col_end);
            if ((row_end <= row_start) || (col_end <= col_start)) {
                continue;
            }
            if (output_count >= output_capacity) {
                return 0;
            }
            output[output_count++] = (db_render_ir_fill_t){
                .rect = {.x = (int32_t)col_start,
                         .y = (int32_t)row_start,
                         .width = (int32_t)(col_end - col_start),
                         .height = (int32_t)(row_end - row_start)},
                .color = geometry.color,
            };
        }
    }
    emitter->fills = output;
    emitter->fill_capacity = output_capacity;
    emitter->fill_count = output_count;
    return 1;
}

static void
db_benchmark_core_populate_snake_damage_plan(db_benchmark_core_t *core,
                                             db_frame_plan_t *out_plan) {
    if ((core == NULL) || (out_plan == NULL) ||
        (core->snake.progression.damage.blocks == NULL)) {
        return;
    }

    db_benchmark_ir_emitter_t emitter = db_benchmark_core_emitter_sink(core);
    db_benchmark_ir_emitter_reset(&emitter);
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
        emitter.status = DB_BENCHMARK_IR_EMITTER_INVALID;
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
        (void)db_benchmark_ir_emitter_add_damage(
            &emitter, &core->snake.progression.damage.blocks[index]);
    }
    if ((final_eval.is_grid_mode != 0) && (fast_forward.phase_full_fill != 0)) {
        emitter.fill_count = 0U;
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
                &emitter, core->ir.optimizer_secondary, core->geometry.capacity,
                out_plan->grid_cols, out_plan->grid_rows) == 0) {
            emitter.status = DB_BENCHMARK_IR_EMITTER_CAPACITY;
        }
    }
    if (core->checkpoint.enabled != 0) {
        if (db_benchmark_checkpoint_overlay_publish(&core->checkpoint,
                                                    &emitter) == 0) {
            emitter.status = DB_BENCHMARK_IR_EMITTER_CAPACITY;
        }
    }
    db_benchmark_core_publish_emitter(core, out_plan, &emitter);

    if ((out_plan->rebuild_required != 0) && (core->checkpoint.enabled == 0)) {
        db_benchmark_ir_emitter_t rebuild = {
            .logical_blocks = core->geometry.rebuild_logical_blocks,
            .logical_capacity = core->geometry.capacity,
            .fills = core->ir.optimizer_primary,
            .fill_capacity = core->ir.fill_capacity,
        };
        db_benchmark_ir_emitter_reset(&rebuild);
        const db_grid_block_t full =
            db_grid_block_full(out_plan->grid_rows, out_plan->grid_cols);
        (void)db_benchmark_ir_emitter_add_damage(&rebuild, &full);
        db_snake_semantic_emit_rebuild(
            &final_eval, simulated_runtime.pattern_seed,
            final_eval.is_grid_mode, final_eval.is_shapes_mode,
            &core->snake.progression.shape, &rebuild);
        core->ir.rebuild_status = (rebuild.fill_count <= core->ir.fill_capacity)
                                      ? DB_RENDER_IR_OK
                                      : DB_RENDER_IR_CAPACITY;
        size_t rebuild_bytes = 0U;
        if (db_try_mul_size(rebuild.fill_count, sizeof(*core->ir.rebuild_fills),
                            &rebuild_bytes) == 0) {
            core->ir.rebuild_status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
        }
        if (core->ir.rebuild_status == DB_RENDER_IR_OK) {
            memcpy(core->ir.rebuild_fills, rebuild.fills, rebuild_bytes);
            core->ir.rebuild_fill_count = rebuild.fill_count;
        }
        if (rebuild.status != DB_BENCHMARK_IR_EMITTER_OK) {
            core->ir.rebuild_status = DB_RENDER_IR_CAPACITY;
        }
    }
}

static db_frame_plan_status_t
frame_plan_status_from_ir(db_render_ir_status_t status) {
    switch (status) {
    case DB_RENDER_IR_OK:
        return DB_FRAME_PLAN_OK;
    case DB_RENDER_IR_CAPACITY:
        return DB_FRAME_PLAN_CAPACITY;
    case DB_RENDER_IR_ARITHMETIC_OVERFLOW:
        return DB_FRAME_PLAN_ARITHMETIC_OVERFLOW;
    case DB_RENDER_IR_COMPLEXITY_LIMIT:
        return DB_FRAME_PLAN_CAPACITY;
    case DB_RENDER_IR_INVALID:
        return DB_FRAME_PLAN_INVALID;
    }
    return DB_FRAME_PLAN_INVALID;
}

db_frame_plan_status_t
db_benchmark_core_generate_plan(db_benchmark_core_t *core, uint32_t frame_index,
                                const db_frame_plan_request_t *request,
                                db_frame_plan_t *out_plan) {
    if (core == NULL || out_plan == NULL) {
        return DB_FRAME_PLAN_INVALID;
    }
    *out_plan = (db_frame_plan_t){0};
    if ((core->runtime_flags.pattern.is_snake_region_mode != 0) &&
        (core->checkpoint.enabled == 0)) {
        return DB_FRAME_PLAN_CHECKPOINT_REQUIRED;
    }
    if ((core->runtime_flags.pattern.is_snake_region_mode != 0) &&
        (core->provisioned_requirements.checkpoint_required == 0)) {
        return DB_FRAME_PLAN_CHECKPOINT_REQUIRED;
    }
    if (core->provisioned_requirements.checkpoint_required != 0) {
        const db_frame_checkpoint_binding_t *const binding =
            &core->checkpoint_binding;
        if ((frame_index != core->provisioned_requirements.frame_index) ||
            (binding->valid == 0) ||
            (binding->resource_generation != core->checkpoint.generation) ||
            (binding->content_revision != core->checkpoint.content_revision) ||
            (binding->width != core->checkpoint.surface.pixel_width) ||
            (binding->height != core->checkpoint.surface.pixel_height) ||
            (binding->format != core->checkpoint.surface.format)) {
            return DB_FRAME_PLAN_INVALID;
        }
    }
    core->ir.rebuild_fill_count = 0U;
    core->ir.rebuild_status = DB_RENDER_IR_OK;
    out_plan->frame_index = frame_index;
    out_plan->preparation_token =
        (request != NULL) ? request->preparation_token : 0U;
    out_plan->checkpoint_binding_token = core->checkpoint_binding.binding_token;
    out_plan->presentation_replay_depth =
        (request != NULL) ? request->presentation_replay_depth : 0U;
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
        if (frame_index == 0U) {
            out_plan->rebuild_required = 1;
            if (out_plan->rebuild_reason == DB_FRAME_REBUILD_NONE) {
                out_plan->rebuild_reason = DB_FRAME_REBUILD_INITIAL_TARGET;
            }
        }
        const db_gradient_damage_plan_t grad_plan =
            db_gradient_progression_eval(&core->runtime);
        db_grid_block_t gradient_damage[2] = {{0}, {0}};
        const size_t gradient_damage_count = db_gradient_collect_dirty_blocks(
            &grad_plan, out_plan->grid_rows, out_plan->grid_cols,
            gradient_damage,
            sizeof(gradient_damage) / sizeof(gradient_damage[0]));
        publish_gradient_ir(
            core, out_plan, &grad_plan, out_plan->rebuild_required,
            (db_grid_block_view_t){.blocks = gradient_damage,
                                   .count = gradient_damage_count});
        if (out_plan->rebuild_required != 0) {
            /* The full semantic update is authoritative on recovery frames. */
            core->ir.rebuild_fill_count = 0U;
            core->ir.rebuild_status = DB_RENDER_IR_OK;
        }
        core->pending_runtime = core->runtime;
        db_gradient_progression_apply(&core->pending_runtime, &grad_plan);
        core->pending_runtime.simulation_work = db_checked_add_u64(
            "benchmark_core", "gradient_simulation_work",
            core->runtime.simulation_work,
            (uint64_t)DB_MAX(core->runtime.bench_speed_step, 1U));
    } else if (core->runtime_flags.pattern.is_bands != 0) {
        db_benchmark_ir_emitter_t emitter =
            db_benchmark_core_emitter_sink(core);
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
    if (out_plan->update_metadata.status != DB_RENDER_IR_OK) {
        return frame_plan_status_from_ir(out_plan->update_metadata.status);
    }
    if (core->ir.rebuild_status != DB_RENDER_IR_OK) {
        return frame_plan_status_from_ir(core->ir.rebuild_status);
    }

    const db_render_ir_status_t rebuild_status =
        build_rebuild_ir(core, out_plan);
    out_plan->rebuild_metadata =
        db_render_ir_metadata(&out_plan->rebuild_ir, rebuild_status,
                              out_plan->grid_cols, out_plan->grid_rows);
    out_plan->expected_state_hash =
        db_benchmark_runtime_state_hash_cross_renderer(
            &core->pending_runtime, out_plan->grid_cols, out_plan->grid_rows);
    if (rebuild_status != DB_RENDER_IR_OK) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "rebuild_ir_rejected"),
            DB_LOG_TOKEN("status", db_render_ir_status_name(rebuild_status)),
            DB_LOG_U64("commands", out_plan->rebuild_ir.command_count),
            DB_LOG_U64("command_bytes", out_plan->rebuild_ir.command_size),
            DB_LOG_U64("fills", out_plan->rebuild_ir.fill_count),
            DB_LOG_U64("source_fills", core->ir.rebuild_fill_count),
        };
        db_log_error("benchmark", "render_ir_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
    }
    const db_frame_plan_status_t plan_status =
        frame_plan_status_from_ir(rebuild_status);
    if (plan_status == DB_FRAME_PLAN_OK) {
        core->provisioned_requirements = (db_frame_requirements_t){0};
        core->checkpoint_binding = (db_frame_checkpoint_binding_t){0};
    }
    return plan_status;
}

void db_benchmark_core_apply_plan(db_benchmark_core_t *core,
                                  const db_frame_plan_t *plan,
                                  const db_render_result_t *result) {
    if ((core == NULL) || (plan == NULL) || (result == NULL) ||
        (result->success == 0)) {
        return;
    }
    if (db_benchmark_checkpoint_commit(&core->checkpoint, plan, result) != 0) {
        core->runtime = core->pending_runtime;
    }
}

void db_benchmark_core_abort_plan(db_benchmark_core_t *core) {
    if (core == NULL) {
        return;
    }
    core->pending_runtime = core->runtime;
    core->provisioned_requirements = (db_frame_requirements_t){0};
    core->checkpoint_binding = (db_frame_checkpoint_binding_t){0};
}
