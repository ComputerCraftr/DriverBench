#include "renderers/snake/renderer_snake_optimizer.h"

#include <stddef.h>
#include <stdint.h>

#include "renderers/renderer_benchmark_common_types_internal.h"
#include "renderers/renderer_snake_collect.h"

size_t db_snake_optimizer_build_pixel_blocks_from_damage_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *damage_blocks,
    size_t damage_count, db_damage_block_t *out_blocks, size_t out_capacity) {
    size_t out_count = 0U;
    if ((damage_blocks == NULL) || (out_blocks == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < damage_count) && (out_count < out_capacity); i++) {
        if (db_grid_block_to_pixel_block(
                grid_cols, grid_rows, &damage_blocks[i], pixel_width,
                pixel_height, &out_blocks[out_count]) != 0) {
            out_count++;
        }
    }
    return out_count;
}

size_t db_snake_optimizer_build_pixel_blocks_from_compact_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_snake_compact_block_t *compact_blocks,
    size_t compact_count, db_damage_block_t *out_blocks, size_t out_capacity) {
    size_t out_count = 0U;
    if ((compact_blocks == NULL) || (out_blocks == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < compact_count) && (out_count < out_capacity);
         i++) {
        const db_grid_block_t grid_block = {
            .row_start = compact_blocks[i].row_start,
            .row_count = compact_blocks[i].row_count,
            .col_start = compact_blocks[i].col_start,
            .col_count = compact_blocks[i].col_count,
        };
        if (db_grid_block_to_pixel_block(grid_cols, grid_rows, &grid_block,
                                         pixel_width, pixel_height,
                                         &out_blocks[out_count]) != 0) {
            out_count++;
        }
    }
    return out_count;
}

size_t db_snake_optimizer_build_repair_blocks(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t pixel_width,
    uint32_t pixel_height, const db_grid_block_t *damage_blocks,
    size_t damage_count, const db_snake_compact_block_t *compact_blocks,
    size_t compact_count, db_damage_block_t *out_blocks, size_t out_capacity) {
    if ((damage_blocks != NULL) && (damage_count > 0U)) {
        return db_snake_optimizer_build_pixel_blocks_from_damage_blocks(
            grid_cols, grid_rows, pixel_width, pixel_height, damage_blocks,
            damage_count, out_blocks, out_capacity);
    }
    return db_snake_optimizer_build_pixel_blocks_from_compact_blocks(
        grid_cols, grid_rows, pixel_width, pixel_height, compact_blocks,
        compact_count, out_blocks, out_capacity);
}
