#ifndef DRIVERBENCH_RENDERER_FRAME_DELTA_CONSUMERS_H
#define DRIVERBENCH_RENDERER_FRAME_DELTA_CONSUMERS_H

#include <stddef.h>
#include <stdint.h>

#include "renderers/delta/renderer_frame_delta.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t db_frame_delta_normalize_grid_blocks(
    const db_grid_block_t *blocks, size_t block_count, uint32_t max_rows,
    uint32_t full_width_cols, db_grid_block_t *out_blocks, size_t out_capacity);
size_t db_frame_delta_subtract_replay_blocks(const db_grid_block_t *base_blocks,
                                             size_t base_count,
                                             const db_grid_block_t *cut_blocks,
                                             size_t cut_count,
                                             db_grid_block_t *out_blocks,
                                             size_t out_capacity);
size_t db_frame_delta_build_gradient_curr_draw_blocks(
    const db_grid_block_t *skipped_blocks, size_t skipped_count,
    const db_grid_block_t *dirty_blocks, size_t dirty_count,
    uint32_t full_width_cols, db_grid_block_t *out_blocks, size_t out_capacity);
size_t db_frame_delta_build_pixel_blocks_from_grid_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *blocks, size_t block_count,
    db_damage_block_t *out_blocks, size_t out_capacity);
size_t db_frame_delta_build_pixel_blocks_from_compact_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_frame_delta_compact_block_t *blocks,
    size_t block_count, db_damage_block_t *out_blocks, size_t out_capacity);
size_t db_frame_delta_build_repair_blocks_from_plan(
    const db_frame_delta_plan_t *plan, uint32_t grid_cols, uint32_t grid_rows,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_blocks,
    size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif
