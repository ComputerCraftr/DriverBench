#ifndef DRIVERBENCH_BENCHMARK_MODE_RUNTIME_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_MODE_RUNTIME_INTERNAL_H

#include "benchmarks/db_benchmark_mode_flags.h"
#include "benchmarks/db_benchmark_runtime.h"

typedef struct {
    int uses_dirty_backbuffer_mode;
    int uses_ff_rect_draw_mode;
    int uses_history_pipeline;
} db_benchmark_pipeline_flags_t;

typedef struct {
    db_pattern_mode_flags_t pattern;
    db_benchmark_pipeline_flags_t pipeline;
} db_benchmark_mode_runtime_flags_t;

static inline db_benchmark_mode_runtime_flags_t
db_benchmark_mode_runtime_flags(const db_benchmark_runtime_init_t *runtime) {
    db_benchmark_mode_runtime_flags_t flags = {0};
    if (runtime == NULL) {
        return flags;
    }
    flags.pattern = db_pattern_mode_flags(runtime->pattern);
    flags.pipeline.uses_dirty_backbuffer_mode =
        (runtime->backbuffer_draw_full == 0);
    flags.pipeline.uses_ff_rect_draw_mode =
        (flags.pattern.is_gradient != 0) || (flags.pattern.is_bands != 0);
    flags.pipeline.uses_history_pipeline =
        (flags.pattern.is_snake != 0) || (flags.pattern.is_gradient != 0);
    return flags;
}

#endif
