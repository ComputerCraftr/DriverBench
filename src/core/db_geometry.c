#include "db_geometry.h"

#include "db_numeric.h"
#include "db_render_types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

db_geometry_f64_status_t db_colored_f64_blocks_compact(
    db_colored_f64_block_view_t input, db_colored_f64_block_t *scratch,
    size_t scratch_capacity, db_colored_f64_block_t *output,
    size_t output_capacity, size_t *output_count) {
    if ((output_count == NULL) ||
        ((input.count > 0U) && (input.blocks == NULL)) ||
        ((scratch_capacity > 0U) && (scratch == NULL)) ||
        ((output_capacity > 0U) && (output == NULL))) {
        return DB_GEOMETRY_F64_STATUS_INVALID;
    }
    *output_count = 0U;
    if (input.count > scratch_capacity) {
        return DB_GEOMETRY_F64_STATUS_OVERFLOW;
    }
    size_t compacted_count = 0U;
    for (size_t i = 0U; i < input.count; i++) {
        const db_colored_f64_block_t block = input.blocks[i];
        if ((block.row_count == 0U) || (block.col_count == 0U)) {
            continue;
        }
        if (compacted_count > 0U) {
            db_colored_f64_block_t *const previous =
                &output[compacted_count - 1U];
            if (db_equal_f64_rgb3(previous->rgb, block.rgb) != 0) {
                const uint32_t previous_col_end =
                    previous->col_start + previous->col_count;
                const uint32_t block_col_end =
                    block.col_start + block.col_count;
                if ((previous->row_start == block.row_start) &&
                    (previous->row_count == block.row_count) &&
                    (block.col_start <= previous_col_end) &&
                    (previous->col_start <= block_col_end)) {
                    const uint32_t col_start =
                        (previous->col_start < block.col_start)
                            ? previous->col_start
                            : block.col_start;
                    const uint32_t col_end = (previous_col_end > block_col_end)
                                                 ? previous_col_end
                                                 : block_col_end;
                    previous->col_start = col_start;
                    previous->col_count = col_end - col_start;
                    continue;
                }
                const uint32_t previous_row_end =
                    previous->row_start + previous->row_count;
                const uint32_t block_row_end =
                    block.row_start + block.row_count;
                if ((previous->col_start == block.col_start) &&
                    (previous->col_count == block.col_count) &&
                    (block.row_start <= previous_row_end) &&
                    (previous->row_start <= block_row_end)) {
                    const uint32_t row_start =
                        (previous->row_start < block.row_start)
                            ? previous->row_start
                            : block.row_start;
                    const uint32_t row_end = (previous_row_end > block_row_end)
                                                 ? previous_row_end
                                                 : block_row_end;
                    previous->row_start = row_start;
                    previous->row_count = row_end - row_start;
                    continue;
                }
            }
        }
        if (compacted_count >= output_capacity) {
            return DB_GEOMETRY_F64_STATUS_OVERFLOW;
        }
        output[compacted_count++] = block;
    }
    *output_count = compacted_count;
    return DB_GEOMETRY_F64_STATUS_OK;
}

static int merge_pixel_blocks(db_damage_block_t *lhs,
                              const db_damage_block_t *rhs) {
    const uint64_t lhs_row_end = (uint64_t)lhs->row_start + lhs->row_count;
    const uint64_t rhs_row_end = (uint64_t)rhs->row_start + rhs->row_count;
    const uint64_t lhs_col_end = (uint64_t)lhs->col_start + lhs->col_count;
    const uint64_t rhs_col_end = (uint64_t)rhs->col_start + rhs->col_count;
    if ((lhs->row_start == rhs->row_start) &&
        (lhs->row_count == rhs->row_count) &&
        ((uint64_t)rhs->col_start <= lhs_col_end) &&
        ((uint64_t)lhs->col_start <= rhs_col_end)) {
        const uint32_t start = DB_MIN(lhs->col_start, rhs->col_start);
        const uint64_t end = DB_MAX(lhs_col_end, rhs_col_end);
        lhs->col_start = start;
        lhs->col_count = (uint32_t)(end - start);
        return 1;
    }
    if ((lhs->col_start == rhs->col_start) &&
        (lhs->col_count == rhs->col_count) &&
        ((uint64_t)rhs->row_start <= lhs_row_end) &&
        ((uint64_t)lhs->row_start <= rhs_row_end)) {
        const uint32_t start = DB_MIN(lhs->row_start, rhs->row_start);
        const uint64_t end = DB_MAX(lhs_row_end, rhs_row_end);
        lhs->row_start = start;
        lhs->row_count = (uint32_t)(end - start);
        return 1;
    }
    return 0;
}

db_geometry_f64_status_t db_colored_f64_blocks_pixel_union(
    db_colored_f64_block_view_t input, uint32_t grid_cols, uint32_t grid_rows,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *output,
    size_t output_capacity, size_t *output_count) {
    if ((output_count == NULL) ||
        ((input.count > 0U) && (input.blocks == NULL)) ||
        ((output_capacity > 0U) && (output == NULL)) || (grid_cols == 0U) ||
        (grid_rows == 0U) || (pixel_width == 0U) || (pixel_height == 0U)) {
        return DB_GEOMETRY_F64_STATUS_INVALID;
    }
    *output_count = 0U;
    for (size_t input_index = 0U; input_index < input.count; input_index++) {
        const db_colored_f64_block_t *const colored =
            &input.blocks[input_index];
        const db_grid_block_t grid_block = {
            .row_start = colored->row_start,
            .row_count = colored->row_count,
            .col_start = colored->col_start,
            .col_count = colored->col_count,
        };
        db_damage_block_t candidate = {0};
        if (db_grid_block_to_pixel_block(grid_cols, grid_rows, &grid_block,
                                         pixel_width, pixel_height,
                                         &candidate) == 0) {
            continue;
        }
        size_t output_index = 0U;
        while (output_index < *output_count) {
            if (merge_pixel_blocks(&candidate, &output[output_index]) == 0) {
                output_index++;
                continue;
            }
            (*output_count)--;
            if (output_index < *output_count) {
                memmove(&output[output_index], &output[output_index + 1U],
                        (*output_count - output_index) * sizeof(*output));
            }
            output_index = 0U;
        }
        if (*output_count >= output_capacity) {
            if (output_capacity > 0U) {
                output[0] = (db_damage_block_t){
                    .row_count = pixel_height,
                    .col_count = pixel_width,
                };
                *output_count = 1U;
            }
            return DB_GEOMETRY_F64_STATUS_OVERFLOW;
        }
        output[(*output_count)++] = candidate;
    }
    return DB_GEOMETRY_F64_STATUS_OK;
}

size_t db_damage_blocks_from_grid_blocks_or_full(const db_grid_block_t *blocks,
                                                 size_t block_count,
                                                 uint32_t max_rows,
                                                 uint32_t full_width_cols,
                                                 db_damage_block_t *output,
                                                 size_t output_capacity) {
    if ((output == NULL) || (output_capacity == 0U) || (max_rows == 0U) ||
        (full_width_cols == 0U) || (blocks == NULL) || (block_count == 0U)) {
        return 0U;
    }
    if (block_count > output_capacity) {
        output[0] = (db_damage_block_t){
            .row_start = 0U,
            .row_count = max_rows,
            .col_start = 0U,
            .col_count = full_width_cols,
        };
        return 1U;
    }
    for (size_t index = 0U; index < block_count; index++) {
        output[index] = (db_damage_block_t){
            .row_start = blocks[index].row_start,
            .row_count = blocks[index].row_count,
            .col_start = blocks[index].col_start,
            .col_count = blocks[index].col_count,
        };
    }
    return block_count;
}

void db_geometry_history_init(db_geometry_history_t *history,
                              db_colored_f64_block_t *blocks,
                              size_t *frame_counts, size_t blocks_per_frame,
                              uint32_t frame_capacity) {
    if ((history == NULL) && (frame_counts != NULL)) {
        frame_counts[0] = 0U;
        return;
    }
    if (history == NULL) {
        return;
    }
    *history = (db_geometry_history_t){
        .blocks = blocks,
        .frame_counts = frame_counts,
        .blocks_per_frame = blocks_per_frame,
        .frame_capacity = frame_capacity,
    };
    db_geometry_history_reset(history);
}

void db_geometry_history_reset(db_geometry_history_t *history) {
    if (history == NULL) {
        return;
    }
    history->frame_count = 0U;
    history->next_frame = 0U;
    if (history->frame_counts != NULL) {
        for (uint32_t index = 0U; index < history->frame_capacity; index++) {
            history->frame_counts[index] = 0U;
        }
    }
}

db_geometry_f64_status_t db_geometry_history_append(
    db_geometry_history_t *history, db_colored_f64_block_view_t current,
    db_colored_f64_block_t *scratch, size_t scratch_capacity) {
    if ((history == NULL) || (history->blocks == NULL) ||
        (history->frame_counts == NULL) || (history->blocks_per_frame == 0U) ||
        (history->frame_capacity == 0U)) {
        return DB_GEOMETRY_F64_STATUS_INVALID;
    }
    const uint32_t slot = history->next_frame;
    db_colored_f64_block_t *const output =
        history->blocks + ((size_t)slot * history->blocks_per_frame);
    size_t compacted_count = 0U;
    const db_geometry_f64_status_t status = db_colored_f64_blocks_compact(
        current, scratch, scratch_capacity, output, history->blocks_per_frame,
        &compacted_count);
    if (status != DB_GEOMETRY_F64_STATUS_OK) {
        return status;
    }
    history->frame_counts[slot] = compacted_count;
    history->next_frame = (slot + 1U) % history->frame_capacity;
    if (history->frame_count < history->frame_capacity) {
        history->frame_count++;
    }
    return DB_GEOMETRY_F64_STATUS_OK;
}

db_geometry_f64_status_t db_geometry_history_assemble(
    const db_geometry_history_t *history, uint32_t previous_frame_count,
    db_colored_f64_block_view_t current, db_colored_f64_block_t *output,
    size_t output_capacity, size_t *historical_count, size_t *output_count) {
    if ((history == NULL) || (historical_count == NULL) ||
        (output_count == NULL) ||
        ((output_capacity > 0U) && (output == NULL)) ||
        ((current.count > 0U) && (current.blocks == NULL)) ||
        (history->frame_capacity == 0U) || (history->blocks_per_frame == 0U) ||
        (history->blocks == NULL) || (history->frame_counts == NULL)) {
        return DB_GEOMETRY_F64_STATUS_INVALID;
    }
    *historical_count = 0U;
    *output_count = 0U;
    uint32_t selected = history->frame_count;
    if (previous_frame_count < selected) {
        selected = previous_frame_count;
    }
    const uint32_t start =
        (history->next_frame + history->frame_capacity - selected) %
        history->frame_capacity;
    for (uint32_t frame = 0U; frame < selected; frame++) {
        const uint32_t slot = (start + frame) % history->frame_capacity;
        const size_t count = history->frame_counts[slot];
        if ((*output_count + count) > output_capacity) {
            return DB_GEOMETRY_F64_STATUS_OVERFLOW;
        }
        memcpy(output + *output_count,
               history->blocks + ((size_t)slot * history->blocks_per_frame),
               count * sizeof(*output));
        *output_count += count;
    }
    *historical_count = *output_count;
    if ((*output_count + current.count) > output_capacity) {
        return DB_GEOMETRY_F64_STATUS_OVERFLOW;
    }
    if (current.count > 0U) {
        memcpy(output + *output_count, current.blocks,
               current.count * sizeof(*output));
        *output_count += current.count;
    }
    return DB_GEOMETRY_F64_STATUS_OK;
}
