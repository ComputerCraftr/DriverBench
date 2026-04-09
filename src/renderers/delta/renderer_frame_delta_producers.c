#include "renderers/delta/renderer_frame_delta_producers.h"

#include <stddef.h>

#include "renderers/delta/renderer_frame_delta.h"
#include "renderers/delta/renderer_frame_delta_consumers.h"
#include "renderers/renderer_benchmark_common_gradient_internal.h"
#include "renderers/renderer_benchmark_gradient.h"
#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_snake_collect.h"

static size_t db_frame_delta_copy_grid_blocks_to_compact_blocks(
    const db_grid_block_t *blocks, size_t block_count,
    db_frame_delta_compact_block_t *out_blocks, size_t out_capacity) {
    size_t out_count = 0U;
    if ((blocks == NULL) || (out_blocks == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < block_count) && (out_count < out_capacity); i++) {
        out_blocks[out_count++] = (db_frame_delta_compact_block_t){
            .row_start = blocks[i].row_start,
            .row_count = blocks[i].row_count,
            .col_start = blocks[i].col_start,
            .col_count = blocks[i].col_count,
            .color_bits = {0U, 0U, 0U},
        };
    }
    return out_count;
}

static void db_frame_delta_finish_plan(
    db_frame_delta_plan_t *plan, db_pattern_t pattern,
    const db_grid_block_t *damage_blocks, size_t damage_count,
    const db_frame_delta_compact_block_t *compact_blocks, size_t compact_count,
    const db_damage_block_t *repair_blocks, size_t repair_count,
    int replay_safe, int ring_repair_safe, int requires_full_seed,
    int overflowed) {
    db_frame_delta_plan_reset(plan, pattern);
    if (plan == NULL) {
        return;
    }
    plan->logical_damage_blocks = damage_blocks;
    plan->logical_damage_block_count = damage_count;
    plan->compact_blocks = compact_blocks;
    plan->compact_block_count = compact_count;
    plan->repair_blocks = repair_blocks;
    plan->repair_block_count = repair_count;
    plan->replay_safe = replay_safe;
    plan->ring_repair_safe = ring_repair_safe;
    plan->requires_full_seed = requires_full_seed;
    plan->overflowed = overflowed;
    if ((damage_count == 0U) && (compact_count == 0U)) {
        plan->mode = DB_FRAME_DELTA_MODE_NO_OP;
    } else if ((damage_count == 1U) && (damage_blocks != NULL) &&
               (damage_blocks[0].row_start == 0U) &&
               (damage_blocks[0].col_start == 0U) &&
               (damage_blocks[0].row_count > 0U) &&
               (damage_blocks[0].col_count > 0U) && (requires_full_seed != 0)) {
        plan->mode = DB_FRAME_DELTA_MODE_FULL_REBUILD;
    } else if (compact_count > 0U) {
        plan->mode = DB_FRAME_DELTA_MODE_COMPACT_GEOMETRY;
    } else {
        plan->mode = DB_FRAME_DELTA_MODE_DAMAGE_ONLY;
    }
}

int db_frame_delta_produce_snake(const db_frame_delta_snake_producer_t *request,
                                 db_frame_delta_plan_t *out_plan) {
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    size_t repair_count = 0U;
    if ((request == NULL) || (out_plan == NULL) || (request->region == NULL) ||
        (request->plan == NULL) || (request->damage_blocks == NULL) ||
        (request->damage_capacity == 0U)) {
        return 0;
    }
    if (request->force_full_recovery != 0) {
        request->damage_blocks[0] =
            db_grid_block_full(request->rows, request->cols);
        if ((request->repair_blocks != NULL) &&
            (request->repair_capacity > 0U)) {
            repair_count = db_frame_delta_build_pixel_blocks_from_grid_blocks(
                request->cols, request->rows, request->pixel_width,
                request->pixel_height, request->damage_blocks, 1U,
                request->repair_blocks, request->repair_capacity);
        }
        db_frame_delta_finish_plan(
            out_plan, request->pattern, request->damage_blocks, 1U, NULL, 0U,
            request->repair_blocks, repair_count, 0, 0, 1, 0);
        return 1;
    }

    if (db_snake_collect_blocks_for_plan(
            request->region, request->plan, request->shape_cache, request->cols,
            request->rows, request->get_color_bits, request->color_user_data,
            request->damage_blocks, request->damage_capacity, &damage_count,
            request->compact_blocks, request->compact_capacity,
            &compact_count) == 0) {
        request->damage_blocks[0] =
            db_grid_block_full(request->rows, request->cols);
        if ((request->repair_blocks != NULL) &&
            (request->repair_capacity > 0U)) {
            repair_count = db_frame_delta_build_pixel_blocks_from_grid_blocks(
                request->cols, request->rows, request->pixel_width,
                request->pixel_height, request->damage_blocks, 1U,
                request->repair_blocks, request->repair_capacity);
        }
        db_frame_delta_finish_plan(
            out_plan, request->pattern, request->damage_blocks, 1U, NULL, 0U,
            request->repair_blocks, repair_count, 0, 0, 1, 1);
        return 1;
    }
    if ((request->repair_blocks != NULL) && (request->repair_capacity > 0U)) {
        repair_count = db_frame_delta_build_repair_blocks_from_plan(
            &(db_frame_delta_plan_t){
                .logical_damage_blocks = request->damage_blocks,
                .logical_damage_block_count = damage_count,
                .compact_blocks = request->compact_blocks,
                .compact_block_count = compact_count,
            },
            request->cols, request->rows, request->pixel_width,
            request->pixel_height, request->repair_blocks,
            request->repair_capacity);
    }
    db_frame_delta_finish_plan(
        out_plan, request->pattern, request->damage_blocks, damage_count,
        request->compact_blocks, compact_count, request->repair_blocks,
        repair_count, 1, 1, 0, 0);
    return 1;
}

int db_frame_delta_produce_gradient(
    const db_frame_delta_gradient_producer_t *request,
    db_frame_delta_plan_t *out_plan) {
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    size_t repair_count = 0U;
    if ((request == NULL) || (out_plan == NULL) ||
        (request->damage_blocks == NULL) || (request->damage_capacity == 0U) ||
        (request->rows == 0U) || (request->cols == 0U)) {
        return 0;
    }
    const db_gradient_damage_plan_t plan = db_gradient_plan_next_frame(
        request->head_row, request->direction_down, request->cycle_index,
        (request->pattern == DB_PATTERN_GRADIENT_SWEEP) ? 0 : 1,
        request->head_step);
    damage_count = db_gradient_collect_dirty_blocks(
        &plan, request->rows, request->cols, request->damage_blocks,
        request->damage_capacity);
    if ((request->compact_blocks != NULL) && (request->compact_capacity > 0U)) {
        db_grid_block_t normalized_blocks[2U] = {
            {0U, 0U, 0U, 0U},
            {0U, 0U, 0U, 0U},
        };
        const size_t normalized_count = db_frame_delta_normalize_grid_blocks(
            request->damage_blocks, damage_count, request->rows, request->cols,
            normalized_blocks,
            sizeof(normalized_blocks) / sizeof(normalized_blocks[0]));
        compact_count = db_frame_delta_copy_grid_blocks_to_compact_blocks(
            normalized_blocks, normalized_count, request->compact_blocks,
            request->compact_capacity);
    }
    if ((request->repair_blocks != NULL) && (request->repair_capacity > 0U)) {
        repair_count = db_frame_delta_build_pixel_blocks_from_grid_blocks(
            request->cols, request->rows, request->pixel_width,
            request->pixel_height, request->damage_blocks, damage_count,
            request->repair_blocks, request->repair_capacity);
    }
    db_frame_delta_finish_plan(
        out_plan, request->pattern, request->damage_blocks, damage_count,
        request->compact_blocks, compact_count, request->repair_blocks,
        repair_count, 1, 1, 0, 0);
    out_plan->gradient_plan = plan;
    if ((damage_count == 1U) && (request->damage_blocks[0].row_start == 0U) &&
        (request->damage_blocks[0].row_count == request->rows) &&
        (request->damage_blocks[0].col_start == 0U) &&
        (request->damage_blocks[0].col_count == request->cols)) {
        out_plan->mode = DB_FRAME_DELTA_MODE_FULL_REBUILD;
        out_plan->requires_full_seed = 1;
    }
    return 1;
}

int db_frame_delta_produce_bands(const db_frame_delta_bands_producer_t *request,
                                 db_frame_delta_plan_t *out_plan) {
    size_t repair_count = 0U;
    if ((request == NULL) || (out_plan == NULL) ||
        (request->damage_blocks == NULL) || (request->damage_capacity == 0U) ||
        (request->rows == 0U) || (request->cols == 0U)) {
        return 0;
    }
    request->damage_blocks[0] =
        db_grid_block_full(request->rows, request->cols);
    if ((request->repair_blocks != NULL) && (request->repair_capacity > 0U)) {
        repair_count = db_frame_delta_build_pixel_blocks_from_grid_blocks(
            request->cols, request->rows, request->pixel_width,
            request->pixel_height, request->damage_blocks, 1U,
            request->repair_blocks, request->repair_capacity);
    }
    db_frame_delta_finish_plan(
        out_plan, request->pattern, request->damage_blocks, 1U, NULL, 0U,
        request->repair_blocks, repair_count, 0, 0, 1, 0);
    return 1;
}
