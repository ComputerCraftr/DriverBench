#ifndef DRIVERBENCH_RENDERER_FRAME_DELTA_H
#define DRIVERBENCH_RENDERER_FRAME_DELTA_H

#include <stddef.h>
#include <stdint.h>

#include "renderers/renderer_benchmark_gradient.h"
#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_snake_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef db_snake_compact_block_t db_frame_delta_compact_block_t;

typedef enum {
    DB_FRAME_DELTA_MODE_NO_OP = 0,
    DB_FRAME_DELTA_MODE_DAMAGE_ONLY = 1,
    DB_FRAME_DELTA_MODE_COMPACT_GEOMETRY = 2,
    DB_FRAME_DELTA_MODE_FULL_REBUILD = 3,
} db_frame_delta_mode_t;

typedef struct {
    db_pattern_t benchmark_kind;
    db_frame_delta_mode_t mode;
    const db_grid_block_t *logical_damage_blocks;
    size_t logical_damage_block_count;
    const db_frame_delta_compact_block_t *compact_blocks;
    size_t compact_block_count;
    const db_damage_block_t *repair_blocks;
    size_t repair_block_count;
    db_gradient_damage_plan_t gradient_plan;
    int replay_safe;
    int ring_repair_safe;
    int requires_full_seed;
    int overflowed;
} db_frame_delta_plan_t;

void db_frame_delta_plan_reset(db_frame_delta_plan_t *plan,
                               db_pattern_t benchmark_kind);
int db_frame_delta_plan_is_full_frame(const db_frame_delta_plan_t *plan,
                                      uint32_t rows, uint32_t cols);

#ifdef __cplusplus
}
#endif

#endif
