#ifndef DRIVERBENCH_BENCHMARK_GRADIENT_PROGRESSION_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_GRADIENT_PROGRESSION_INTERNAL_H

#include "benchmarks/db_benchmark_gradient_internal.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_benchmark_types_internal.h"

static inline db_gradient_damage_plan_t
db_gradient_progression_eval(const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_gradient_damage_plan_t){0};
    }
    return db_gradient_step_from_runtime(
        runtime->pattern, runtime->gradient.head_row,
        runtime->gradient.direction_down, runtime->gradient.cycle_index,
        runtime->bench_speed_step);
}

static inline void
db_gradient_progression_apply(db_benchmark_runtime_init_t *runtime,
                              const db_gradient_damage_plan_t *plan) {
    if ((runtime != NULL) && (plan != NULL)) {
        db_gradient_apply_step_to_runtime(runtime, plan);
    }
}

#endif
