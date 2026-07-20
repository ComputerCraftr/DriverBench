#ifndef DRIVERBENCH_BENCHMARK_SNAKE_FAST_FORWARD_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_SNAKE_FAST_FORWARD_INTERNAL_H

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_emitters.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_snake_progression_internal.h"
#include <stdint.h>

typedef struct {
    db_benchmark_runtime_init_t runtime;
    db_snake_progression_eval_t final_eval;
    uint32_t covered_first;
    uint32_t covered_last;
    uint32_t processed_ticks;
    uint32_t chunk_count;
    uint32_t boundary_count;
    uint32_t terminal_tile_count;
    int phase_full_fill;
} db_snake_fast_forward_result_t;

int db_snake_fast_forward_execute(const db_benchmark_runtime_init_t *runtime,
                                  uint32_t requested_ticks, int region_mode,
                                  int shapes_mode,
                                  db_snake_progression_workspace_t *workspace,
                                  db_benchmark_checkpoint_t *checkpoint,
                                  db_benchmark_ir_emitter_t *emitter,
                                  db_snake_fast_forward_result_t *out_result);

#endif
