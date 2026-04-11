#ifndef DRIVERBENCH_CORE_BENCHMARK_CORE_H
#define DRIVERBENCH_CORE_BENCHMARK_CORE_H

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_mode_runtime_internal.h"
#include "benchmarks/db_snake_progression_internal.h"
#include "core/db_frame_plan.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    db_grid_block_t *logical_blocks;
    db_colored_f64_block_t *colored_blocks;
    db_colored_f64_block_t *prepared_blocks;
    db_grid_block_t *rebuild_logical_blocks;
    db_colored_f64_block_t *rebuild_colored_blocks;
    db_colored_f64_block_t *prepared_rebuild_blocks;
    db_colored_f64_block_t *scratch_blocks;
    size_t capacity;
} db_benchmark_geometry_workspace_t;

typedef struct {
    db_snake_progression_workspace_t progression;
} db_benchmark_snake_workspace_t;

typedef struct {
    db_benchmark_runtime_init_t runtime;
    db_benchmark_runtime_init_t pending_runtime;
    db_benchmark_mode_runtime_flags_t runtime_flags;
    db_benchmark_geometry_workspace_t geometry;
    db_benchmark_snake_workspace_t snake;
    db_benchmark_checkpoint_t checkpoint;
    int initialized;
} db_benchmark_core_t;

void db_benchmark_core_init(db_benchmark_core_t *core,
                            const db_benchmark_runtime_init_t *init_state,
                            db_pixel_format_t working_format);
void db_benchmark_core_generate_plan(db_benchmark_core_t *core,
                                     uint32_t frame_index,
                                     const db_frame_plan_request_t *request,
                                     db_frame_plan_t *out_plan);
void db_benchmark_core_apply_plan(db_benchmark_core_t *core,
                                  const db_frame_plan_t *plan,
                                  const db_render_result_t *result);
void db_benchmark_core_shutdown(db_benchmark_core_t *core);

#endif
