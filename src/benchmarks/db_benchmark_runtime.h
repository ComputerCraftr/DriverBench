#ifndef DRIVERBENCH_CORE_BENCHMARK_RUNTIME_H
#define DRIVERBENCH_CORE_BENCHMARK_RUNTIME_H

#include <stdint.h>

typedef enum {
    DB_PATTERN_GRADIENT_SWEEP = 0,
    DB_PATTERN_BANDS = 1,
    DB_PATTERN_SNAKE_GRID = 2,
    DB_PATTERN_GRADIENT_FILL = 3,
    DB_PATTERN_SNAKE_RECT = 4,
    DB_PATTERN_SNAKE_SHAPES = 5,
} db_pattern_t;

typedef struct {
    uint32_t head_row;
    uint32_t cycle_index;
    int direction_down;
} db_gradient_state_t;

typedef struct {
    uint32_t shape_index;
    uint32_t cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int grid_phase_flag;
    int phase_completed;
} db_snake_state_t;

typedef struct {
    db_pattern_t pattern;
    uint64_t simulation_work;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    db_snake_state_t snake;
    db_gradient_state_t gradient;
    uint32_t bench_speed_step;
    uint32_t pattern_seed;
    int backbuffer_draw_full;
} db_benchmark_runtime_init_t;

#endif
