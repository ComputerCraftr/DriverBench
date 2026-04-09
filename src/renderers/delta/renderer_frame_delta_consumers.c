#include "renderers/delta/renderer_frame_delta_consumers.h"

#include <stddef.h>
#include <stdint.h>

#include "renderers/delta/renderer_frame_delta.h"
#include "renderers/renderer_benchmark_common_gradient_internal.h"
#include "renderers/renderer_benchmark_common_types_internal.h"

size_t db_frame_delta_normalize_grid_blocks(const db_grid_block_t *blocks,
                                            size_t block_count,
                                            uint32_t max_rows,
                                            uint32_t full_width_cols,
                                            db_grid_block_t *out_blocks,
                                            size_t out_capacity) {
    return db_grid_blocks_compact_full_width_or_full(blocks, block_count,
                                                     max_rows, full_width_cols,
                                                     out_blocks, out_capacity);
}

size_t db_frame_delta_subtract_replay_blocks(const db_grid_block_t *base_blocks,
                                             size_t base_count,
                                             const db_grid_block_t *cut_blocks,
                                             size_t cut_count,
                                             db_grid_block_t *out_blocks,
                                             size_t out_capacity) {
    return db_gradient_subtract_replay_blocks(base_blocks, base_count,
                                              cut_blocks, cut_count, out_blocks,
                                              out_capacity);
}

size_t db_frame_delta_build_gradient_curr_draw_blocks(
    const db_grid_block_t *skipped_blocks, size_t skipped_count,
    const db_grid_block_t *dirty_blocks, size_t dirty_count,
    uint32_t full_width_cols, db_grid_block_t *out_blocks,
    size_t out_capacity) {
    return db_gradient_build_curr_draw_blocks(
        skipped_blocks, skipped_count, dirty_blocks, dirty_count,
        full_width_cols, out_blocks, out_capacity);
}

size_t db_frame_delta_build_pixel_blocks_from_grid_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *blocks, size_t block_count,
    db_damage_block_t *out_blocks, size_t out_capacity) {
    size_t out_count = 0U;
    if ((blocks == NULL) || (out_blocks == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < block_count) && (out_count < out_capacity); i++) {
        if (db_grid_block_to_pixel_block(grid_cols, grid_rows, &blocks[i],
                                         pixel_width, pixel_height,
                                         &out_blocks[out_count]) != 0) {
            out_count++;
        }
    }
    return out_count;
}

size_t db_frame_delta_build_pixel_blocks_from_compact_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_frame_delta_compact_block_t *blocks,
    size_t block_count, db_damage_block_t *out_blocks, size_t out_capacity) {
    size_t out_count = 0U;
    if ((blocks == NULL) || (out_blocks == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < block_count) && (out_count < out_capacity); i++) {
        const db_grid_block_t grid_block = {
            .row_start = blocks[i].row_start,
            .row_count = blocks[i].row_count,
            .col_start = blocks[i].col_start,
            .col_count = blocks[i].col_count,
        };
        if (db_grid_block_to_pixel_block(grid_cols, grid_rows, &grid_block,
                                         pixel_width, pixel_height,
                                         &out_blocks[out_count]) != 0) {
            out_count++;
        }
    }
    return out_count;
}

size_t db_frame_delta_build_repair_blocks_from_plan(
    const db_frame_delta_plan_t *plan, uint32_t grid_cols, uint32_t grid_rows,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_blocks,
    size_t out_capacity) {
    if (plan == NULL) {
        return 0U;
    }
    if ((plan->logical_damage_blocks != NULL) &&
        (plan->logical_damage_block_count > 0U)) {
        return db_frame_delta_build_pixel_blocks_from_grid_blocks(
            grid_cols, grid_rows, pixel_width, pixel_height,
            plan->logical_damage_blocks, plan->logical_damage_block_count,
            out_blocks, out_capacity);
    }
    return db_frame_delta_build_pixel_blocks_from_compact_blocks(
        grid_cols, grid_rows, pixel_width, pixel_height, plan->compact_blocks,
        plan->compact_block_count, out_blocks, out_capacity);
}
