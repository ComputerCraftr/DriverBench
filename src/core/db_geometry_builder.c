#include "db_geometry_builder.h"
#include "db_geometry.h"
#include "db_numeric.h"
#include <stddef.h>
#include <stdint.h>

void db_geometry_builder_reset(db_geometry_builder_t *builder) {
    if (builder == NULL) {
        return;
    }
    builder->logical_count = 0U;
    builder->colored_count = 0U;
    builder->status = DB_GEOMETRY_BUILDER_OK;
}

int db_geometry_builder_add_damage(db_geometry_builder_t *builder,
                                   const db_grid_block_t *block) {
    if ((builder == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U)) {
        if (builder != NULL) {
            builder->status = DB_GEOMETRY_BUILDER_INVALID;
        }
        return 0;
    }
    for (size_t index = 0U; index < builder->logical_count; index++) {
        db_grid_block_t *const existing = &builder->logical_blocks[index];
        const uint64_t existing_row_end =
            (uint64_t)existing->row_start + existing->row_count;
        const uint64_t existing_col_end =
            (uint64_t)existing->col_start + existing->col_count;
        const uint64_t block_row_end =
            (uint64_t)block->row_start + block->row_count;
        const uint64_t block_col_end =
            (uint64_t)block->col_start + block->col_count;
        if ((existing->row_start == block->row_start) &&
            (existing->row_count == block->row_count) &&
            ((uint64_t)block->col_start <= existing_col_end) &&
            ((uint64_t)existing->col_start <= block_col_end)) {
            const uint32_t start = (existing->col_start < block->col_start)
                                       ? existing->col_start
                                       : block->col_start;
            const uint64_t end = (existing_col_end > block_col_end)
                                     ? existing_col_end
                                     : block_col_end;
            existing->col_start = start;
            existing->col_count = (uint32_t)(end - start);
            return 1;
        }
        if ((existing->col_start == block->col_start) &&
            (existing->col_count == block->col_count) &&
            ((uint64_t)block->row_start <= existing_row_end) &&
            ((uint64_t)existing->row_start <= block_row_end)) {
            const uint32_t start = (existing->row_start < block->row_start)
                                       ? existing->row_start
                                       : block->row_start;
            const uint64_t end = (existing_row_end > block_row_end)
                                     ? existing_row_end
                                     : block_row_end;
            existing->row_start = start;
            existing->row_count = (uint32_t)(end - start);
            return 1;
        }
    }
    if (builder->logical_count >= builder->logical_capacity) {
        builder->status = DB_GEOMETRY_BUILDER_OVERFLOW;
        return 0;
    }
    builder->logical_blocks[builder->logical_count++] = *block;
    return 1;
}

int db_geometry_builder_add_block(db_geometry_builder_t *builder,
                                  const db_colored_f64_block_t *block) {
    if ((builder == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U)) {
        if (builder != NULL) {
            builder->status = DB_GEOMETRY_BUILDER_INVALID;
        }
        return 0;
    }
    if (builder->colored_count > 0U) {
        db_colored_f64_block_t *const previous =
            &builder->colored_blocks[builder->colored_count - 1U];
        const uint32_t previous_row_end =
            previous->row_start + previous->row_count;
        if ((previous_row_end == block->row_start) &&
            (previous->col_start == block->col_start) &&
            (previous->col_count == block->col_count) &&
            (db_equal_f64_rgb3(previous->rgb, block->rgb) != 0)) {
            previous->row_count += block->row_count;
            return 1;
        }
    }
    if (builder->colored_count >= builder->colored_capacity) {
        builder->status = DB_GEOMETRY_BUILDER_OVERFLOW;
        return 0;
    }
    builder->colored_blocks[builder->colored_count++] = *block;
    return 1;
}

int db_geometry_builder_add_span(db_geometry_builder_t *builder, uint32_t row,
                                 uint32_t col_start, uint32_t col_end,
                                 const double rgb[3]) {
    if ((rgb == NULL) || (col_end <= col_start)) {
        if (builder != NULL) {
            builder->status = DB_GEOMETRY_BUILDER_INVALID;
        }
        return 0;
    }
    return db_geometry_builder_add_block(builder,
                                         &(const db_colored_f64_block_t){
                                             .row_start = row,
                                             .row_count = 1U,
                                             .col_start = col_start,
                                             .col_count = col_end - col_start,
                                             .rgb = {rgb[0], rgb[1], rgb[2]},
                                         });
}
