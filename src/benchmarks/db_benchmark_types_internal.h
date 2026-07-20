#ifndef DRIVERBENCH_BENCHMARK_TYPES_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_TYPES_INTERNAL_H

#include "benchmarks/db_benchmark_runtime.h"
#include <stdint.h>

#define DB_BENCH_COMMON_BACKEND "benchmark"
#define DB_BENCHMARK_MODE_BANDS "bands"
#define DB_BENCHMARK_MODE_SNAKE_GRID "snake_grid"
#define DB_BENCHMARK_MODE_GRADIENT_SWEEP "gradient_sweep"
#define DB_BENCHMARK_MODE_GRADIENT_FILL "gradient_fill"
#define DB_BENCHMARK_MODE_SNAKE_RECT "snake_rect"
#define DB_BENCHMARK_MODE_SNAKE_SHAPES "snake_shapes"
#define DB_BENCH_SPEED_STEP_MAX 1024U
#define DB_COLOR_CHANNEL_BIAS 0.20
#define DB_COLOR_CHANNEL_SCALE 0.75
#define DB_GRADIENT_WINDOW_ROWS 32U
#define DB_GRADIENT_DRAW_RANGE_WORK_CAP 8U
#define DB_PALETTE_SALT_BASE_STEP DB_U32_GOLDEN_RATIO

typedef struct {
    db_gradient_state_t render_state;
    db_gradient_state_t next_state;
    uint32_t dirty_row_start;
    uint32_t dirty_row_count;
    uint32_t dirty_row_start_second;
    uint32_t dirty_row_count_second;
} db_gradient_damage_plan_t;

#endif
