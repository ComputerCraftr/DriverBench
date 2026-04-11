#ifndef DRIVERBENCH_BENCHMARK_SNAKE_PROGRESSION_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_SNAKE_PROGRESSION_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_snake_emit_internal.h"
#include "benchmarks/db_snake_shape_internal.h"
#include "benchmarks/db_snake_types_internal.h"
#include "core/db_core.h"

typedef struct {
    db_grid_block_t *blocks;
    size_t capacity;
} db_snake_damage_workspace_t;

typedef struct {
    db_snake_shape_row_bounds_t *row_bounds;
    size_t row_bounds_capacity;
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    uint32_t active_tile_capacity;
} db_snake_shape_workspace_t;

typedef struct {
    db_snake_damage_workspace_t damage;
    db_snake_shape_workspace_t shape;
} db_snake_progression_workspace_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    db_snake_shape_kind_t shape_kind;
    int is_grid_mode;
    int is_shapes_mode;
} db_snake_progression_eval_t;

static inline void
db_snake_progression_workspace_init(db_snake_progression_workspace_t *workspace,
                                    const char *backend,
                                    uint32_t tile_capacity) {
    if ((workspace == NULL) || (backend == NULL) || (tile_capacity == 0U) ||
        (tile_capacity == 0U)) {
        return;
    }
    workspace->shape.active_tile_indices = (uint32_t *)db_calloc_or_fail(
        backend, "snake_active_tile_indices", (size_t)tile_capacity,
        sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    workspace->shape.active_tile_valid = (uint8_t *)db_calloc_or_fail(
        backend, "snake_active_tile_valid", (size_t)tile_capacity,
        sizeof(uint8_t), DB_CACHELINE_ALIGNMENT_BYTES);
    workspace->shape.active_tile_capacity = tile_capacity;
}

static inline void db_snake_progression_workspace_free(
    db_snake_progression_workspace_t *workspace) {
    if (workspace == NULL) {
        return;
    }
    free(workspace->shape.active_tile_indices);
    free(workspace->shape.active_tile_valid);
    workspace->shape.active_tile_indices = NULL;
    workspace->shape.active_tile_valid = NULL;
    workspace->shape.active_tile_capacity = 0U;
}

static inline db_snake_progression_eval_t
db_snake_progression_eval(const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_snake_progression_eval_t){0};
    }
    const db_snake_step_eval_t eval = db_snake_step_eval_from_runtime(
        runtime->pattern, runtime->pattern_seed, runtime->snake.shape_index,
        runtime->snake.cursor, runtime->snake.prev_start,
        runtime->snake.prev_count, runtime->snake.grid_phase_flag,
        runtime->bench_speed_step);
    return (db_snake_progression_eval_t){
        .plan = eval.plan,
        .target = eval.target,
        .shape_kind = eval.shape_kind,
        .is_grid_mode = eval.is_grid_mode,
        .is_shapes_mode = eval.is_shapes_mode,
    };
}

static inline void
db_snake_progression_apply(db_benchmark_runtime_init_t *runtime,
                           const db_snake_progression_eval_t *eval) {
    if ((runtime == NULL) || (eval == NULL)) {
        return;
    }
    const db_snake_plan_t *plan = &eval->plan;
    const db_snake_step_target_t *target = &eval->target;
    if (target->has_next_phase_flag != 0) {
        runtime->snake.grid_phase_flag = target->next_phase_flag;
    }
    if (eval->is_grid_mode == 0) {
        if (target->has_next_shape_index != 0) {
            runtime->snake.shape_index = target->next_shape_index;
        }
        if (plan->wrapped != 0) {
            runtime->snake.prev_count = 0U;
        }
    }
    runtime->snake.cursor = plan->next_cursor;
    runtime->snake.prev_start = plan->next_prev_start;
    runtime->snake.prev_count = plan->next_prev_count;
    runtime->snake.batch_size = plan->batch_size;
    runtime->snake.phase_completed = plan->phase_completed;
}

#endif
