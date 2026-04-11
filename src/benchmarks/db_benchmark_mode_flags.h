#ifndef DRIVERBENCH_RENDERER_BENCHMARK_TYPES_H
#define DRIVERBENCH_RENDERER_BENCHMARK_TYPES_H

#include "../core/db_core.h"
#include "db_benchmark_runtime.h"

typedef struct {
    int is_bands;
    int is_gradient;
    int is_gradient_sweep;
    int is_snake_region_mode;
    int is_snake_shapes;
    int is_snake;
} db_pattern_mode_flags_t;

static inline db_pattern_mode_flags_t
db_pattern_mode_flags(db_pattern_t pattern) {
    db_pattern_mode_flags_t flags = {0};
    flags.is_bands = (pattern == DB_PATTERN_BANDS);
    flags.is_gradient = (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                        (pattern == DB_PATTERN_GRADIENT_FILL);
    flags.is_gradient_sweep = (pattern == DB_PATTERN_GRADIENT_SWEEP);
    flags.is_snake_shapes = (pattern == DB_PATTERN_SNAKE_SHAPES);
    flags.is_snake_region_mode =
        (pattern == DB_PATTERN_SNAKE_RECT) || (flags.is_snake_shapes != 0);
    flags.is_snake =
        (pattern == DB_PATTERN_SNAKE_GRID) || (flags.is_snake_region_mode != 0);
    return flags;
}

static inline uint32_t db_checked_pattern_enum_to_u32(const char *backend,
                                                      const char *field_name,
                                                      db_pattern_t pattern) {
    return db_checked_int_to_u32(backend, field_name, (int)pattern);
}

#endif
