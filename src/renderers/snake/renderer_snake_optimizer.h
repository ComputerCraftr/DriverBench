#ifndef DRIVERBENCH_RENDERER_SNAKE_OPTIMIZER_H
#define DRIVERBENCH_RENDERER_SNAKE_OPTIMIZER_H

#include <stddef.h>
#include <stdint.h>

#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_snake_collect.h"
size_t db_snake_optimizer_build_pixel_blocks_from_damage_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *damage_blocks,
    size_t damage_count, db_damage_block_t *out_blocks, size_t out_capacity);
size_t db_snake_optimizer_build_pixel_blocks_from_compact_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_snake_compact_block_t *compact_blocks,
    size_t compact_count, db_damage_block_t *out_blocks, size_t out_capacity);
size_t db_snake_optimizer_build_repair_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *damage_blocks,
    size_t damage_count, const db_snake_compact_block_t *compact_blocks,
    size_t compact_count, db_damage_block_t *out_blocks, size_t out_capacity);

#endif
