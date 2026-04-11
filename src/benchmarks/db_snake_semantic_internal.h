#ifndef DRIVERBENCH_BENCHMARK_SNAKE_SEMANTIC_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_SNAKE_SEMANTIC_INTERNAL_H

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_emitters.h"
#include "benchmarks/db_snake_progression_internal.h"
#include "core/db_geometry_builder.h"

typedef struct {
    db_geometry_builder_t *builder;
    const double *rgb;
} db_snake_geometry_sink_t;

static inline int
db_snake_semantic_emit_row_segment(uint32_t row, uint32_t col_start,
                                   uint32_t col_end,
                                   db_snake_row_segment_sink_t *sink) {
    const db_snake_geometry_sink_t *const geometry =
        (sink != NULL) ? (const db_snake_geometry_sink_t *)sink->geometry_sink
                       : NULL;
    return ((geometry != NULL) && (geometry->builder != NULL) &&
            (geometry->rgb != NULL))
               ? db_geometry_builder_add_span(geometry->builder, row, col_start,
                                              col_end, geometry->rgb)
               : 0;
}

static inline db_snake_shape_cache_t db_snake_semantic_shape_cache(
    db_snake_shape_workspace_t *workspace, uint32_t seed, uint32_t shape_index,
    const db_snake_region_t *region, int shapes_mode) {
    db_snake_shape_cache_t cache = {0};
    if ((shapes_mode != 0) && (workspace != NULL) &&
        (workspace->row_bounds != NULL)) {
        const db_snake_shape_kind_t kind = db_snake_shapes_kind_from_index(
            seed, shape_index, DB_U32_SALT_PALETTE);
        (void)db_snake_shape_cache_init_from_index(
            &cache, workspace->row_bounds, workspace->row_bounds_capacity, seed,
            shape_index, DB_U32_SALT_PALETTE, region, kind);
    }
    return cache;
}

static inline void db_snake_semantic_emit_tiles(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache, const double target_rgb[3],
    int grid_mode, db_snake_shape_workspace_t *workspace,
    db_benchmark_checkpoint_t *checkpoint, db_geometry_builder_t *builder) {
    uint32_t fallback_indices[DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT] = {0U};
    uint8_t fallback_valid[DB_SNAKE_OPTIMIZER_COMPACT_BLOCK_LIMIT] = {0U};
    const db_snake_active_tile_scratch_t scratch = {
        .active_tile_indices = workspace->active_tile_indices,
        .active_tile_valid = workspace->active_tile_valid,
        .active_tile_capacity = workspace->active_tile_capacity,
    };
    db_snake_active_batch_t batch = db_snake_bind_active_batch_scratch(
        plan, region, &scratch, fallback_indices, fallback_valid);
    db_snake_prepare_active_batch(plan, region, shape_cache,
                                  db_snake_grid_cols_effective(),
                                  db_snake_grid_rows_effective(), &batch);
    for (uint32_t offset = 0U; offset < plan->prev_count; offset++) {
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_prev_tile(plan, region, shape_cache, offset,
                                            db_snake_grid_cols_effective(),
                                            db_snake_grid_rows_effective(),
                                            &tile) != 0) {
            const db_colored_f64_block_t block = {
                .row_start = tile.row,
                .row_count = 1U,
                .col_start = tile.col,
                .col_count = 1U,
                .rgb = {target_rgb[0], target_rgb[1], target_rgb[2]},
            };
            if (checkpoint != NULL) {
                db_benchmark_checkpoint_overlay_write(checkpoint, &block);
            } else {
                (void)db_geometry_builder_add_block(builder, &block);
            }
        }
    }
    for (uint32_t active = 0U; active < batch.batch_limit; active++) {
        if (batch.active_tile_valid[active] == 0U) {
            continue;
        }
        const uint32_t tile = batch.active_tile_indices[active];
        const uint32_t row = tile / db_snake_grid_cols_effective();
        const uint32_t col = tile % db_snake_grid_cols_effective();
        double prior_rgb[3] = {0.0};
        if (grid_mode != 0) {
            db_grid_target_color_rgb3(!plan->phase_flag, prior_rgb);
        } else {
            db_benchmark_checkpoint_read_with_overlay(checkpoint, row, col,
                                                      prior_rgb);
        }
        double rgb[3] = {0.0};
        db_snake_blend_active_rgb(plan, active, prior_rgb, target_rgb, rgb);
        const db_colored_f64_block_t block = {
            .row_start = row,
            .row_count = 1U,
            .col_start = col,
            .col_count = 1U,
            .rgb = {rgb[0], rgb[1], rgb[2]},
        };
        if (checkpoint != NULL) {
            db_benchmark_checkpoint_overlay_write(checkpoint, &block);
        } else {
            (void)db_geometry_builder_add_block(builder, &block);
        }
    }
}

static inline void
db_snake_semantic_emit_rebuild(const db_snake_progression_eval_t *eval,
                               uint32_t seed, int grid_mode, int shapes_mode,
                               db_snake_shape_workspace_t *workspace,
                               db_geometry_builder_t *builder) {
    double base_rgb[3] = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                          BENCH_GRID_PHASE0_B};
    if (grid_mode != 0) {
        db_grid_target_color_rgb3(!eval->plan.phase_flag, base_rgb);
    }
    (void)db_geometry_builder_add_block(
        builder, &(const db_colored_f64_block_t){
                     .row_start = 0U,
                     .row_count = db_snake_grid_rows_effective(),
                     .col_start = 0U,
                     .col_count = db_snake_grid_cols_effective(),
                     .rgb = {base_rgb[0], base_rgb[1], base_rgb[2]},
                 });
    if ((grid_mode != 0) && (eval->plan.phase_completed != 0)) {
        (void)db_geometry_builder_add_block(
            builder,
            &(const db_colored_f64_block_t){
                .row_start = 0U,
                .row_count = db_snake_grid_rows_effective(),
                .col_start = 0U,
                .col_count = db_snake_grid_cols_effective(),
                .rgb = {eval->target.target_rgb[0], eval->target.target_rgb[1],
                        eval->target.target_rgb[2]},
            });
        return;
    }
    if ((grid_mode != 0) && (eval->plan.phase_completed == 0) &&
        (eval->plan.active_cursor > 0U)) {
        const db_snake_geometry_sink_t geometry_sink = {
            .builder = builder,
            .rgb = eval->target.target_rgb,
        };
        (void)db_snake_for_each_damage_row_segment(
            &eval->target.region, 0U, eval->plan.active_cursor, 0U, 0U, NULL,
            db_snake_semantic_emit_row_segment,
            &(db_snake_row_segment_sink_t){
                .geometry_sink = &geometry_sink,
            });
    }
    const db_snake_shape_cache_t cache = db_snake_semantic_shape_cache(
        workspace, seed, eval->plan.active_shape_index, &eval->target.region,
        shapes_mode);
    const db_snake_shape_cache_t *cache_ptr =
        (shapes_mode != 0) ? &cache : NULL;
    db_snake_plan_t current = eval->plan;
    current.prev_start = 0U;
    current.prev_count = (grid_mode != 0) ? 0U
                                          : DB_MIN(current.active_cursor,
                                                   current.target_tile_count);
    if (current.phase_completed != 0) {
        current.prev_count = current.target_tile_count;
        current.batch_size = 0U;
    }
    db_snake_semantic_emit_tiles(&current, &eval->target.region, cache_ptr,
                                 eval->target.target_rgb, grid_mode, workspace,
                                 NULL, builder);
}

#endif
