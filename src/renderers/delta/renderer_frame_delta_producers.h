#ifndef DRIVERBENCH_RENDERER_FRAME_DELTA_PRODUCERS_H
#define DRIVERBENCH_RENDERER_FRAME_DELTA_PRODUCERS_H

#include <stddef.h>
#include <stdint.h>

#include "renderers/delta/renderer_frame_delta.h"
#include "renderers/renderer_history_common.h"
#include "renderers/snake/renderer_snake_optimizer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    db_pattern_t pattern;
    const db_snake_region_t *region;
    const db_snake_plan_t *plan;
    const db_snake_shape_cache_t *shape_cache;
    uint32_t cols;
    uint32_t rows;
    uint32_t pixel_width;
    uint32_t pixel_height;
    int force_full_recovery;
    db_snake_get_color_bits_cb_t get_color_bits;
    void *color_user_data;
    db_grid_block_t *damage_blocks;
    size_t damage_capacity;
    db_frame_delta_compact_block_t *compact_blocks;
    size_t compact_capacity;
    db_damage_block_t *repair_blocks;
    size_t repair_capacity;
} db_frame_delta_snake_producer_t;

typedef struct {
    db_pattern_t pattern;
    uint32_t head_row;
    int direction_down;
    uint32_t cycle_index;
    uint32_t head_step;
    uint32_t rows;
    uint32_t cols;
    uint32_t pixel_width;
    uint32_t pixel_height;
    db_grid_block_t *damage_blocks;
    size_t damage_capacity;
    db_frame_delta_compact_block_t *compact_blocks;
    size_t compact_capacity;
    db_damage_block_t *repair_blocks;
    size_t repair_capacity;
} db_frame_delta_gradient_producer_t;

typedef struct {
    db_pattern_t pattern;
    uint32_t rows;
    uint32_t cols;
    uint32_t pixel_width;
    uint32_t pixel_height;
    db_grid_block_t *damage_blocks;
    size_t damage_capacity;
    db_damage_block_t *repair_blocks;
    size_t repair_capacity;
} db_frame_delta_bands_producer_t;

int db_frame_delta_produce_snake(const db_frame_delta_snake_producer_t *request,
                                 db_frame_delta_plan_t *out_plan);
int db_frame_delta_produce_gradient(
    const db_frame_delta_gradient_producer_t *request,
    db_frame_delta_plan_t *out_plan);
int db_frame_delta_produce_bands(const db_frame_delta_bands_producer_t *request,
                                 db_frame_delta_plan_t *out_plan);

#ifdef __cplusplus
}
#endif

#endif
