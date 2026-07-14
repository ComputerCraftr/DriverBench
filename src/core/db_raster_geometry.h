#ifndef DRIVERBENCH_CORE_RASTER_GEOMETRY_H
#define DRIVERBENCH_CORE_RASTER_GEOMETRY_H

#include "db_numeric.h"
#include "db_render_types.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void db_grid_tile_bounds_ndc_for_extent(uint32_t cols,
                                                      uint32_t rows,
                                                      uint32_t tile_index,
                                                      float *x0, float *y0,
                                                      float *x1, float *y1) {
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    const double inv_cols = 1.0 / DB_TO_F64(cols);
    const double inv_rows = 1.0 / DB_TO_F64(rows);
    *x0 = db_double_to_f32((2.0 * DB_TO_F64(col) * inv_cols) - 1.0);
    *x1 = db_double_to_f32((2.0 * DB_TO_F64(col + 1U) * inv_cols) - 1.0);
    *y1 = db_double_to_f32(1.0 - (2.0 * DB_TO_F64(row) * inv_rows));
    *y0 = db_double_to_f32(1.0 - (2.0 * DB_TO_F64(row + 1U) * inv_rows));
}

static inline void
db_grid_block_bounds_ndc_for_extent(uint32_t cols, uint32_t rows,
                                    const db_grid_block_t *block, float *x0,
                                    float *y0, float *x1, float *y1) {
    if ((cols == 0U) || (rows == 0U) || (block == NULL) || (x0 == NULL) ||
        (y0 == NULL) || (x1 == NULL) || (y1 == NULL)) {
        return;
    }
    const uint32_t col_end =
        db_grid_block_col_end_or_fail("raster_col_end", block);
    const uint32_t row_end =
        db_grid_block_row_end_or_fail("raster_row_end", block);
    const double inv_cols = 1.0 / DB_TO_F64(cols);
    const double inv_rows = 1.0 / DB_TO_F64(rows);
    *x0 =
        db_double_to_f32((2.0 * DB_TO_F64(block->col_start) * inv_cols) - 1.0);
    *x1 = db_double_to_f32((2.0 * DB_TO_F64(col_end) * inv_cols) - 1.0);
    *y1 =
        db_double_to_f32(1.0 - (2.0 * DB_TO_F64(block->row_start) * inv_rows));
    *y0 = db_double_to_f32(1.0 - (2.0 * DB_TO_F64(row_end) * inv_rows));
}

static inline void db_fill_rect_unit_pos(float *unit, float x0, float y0,
                                         float x1, float y1, size_t stride) {
    unit[0] = x0;
    unit[1] = y0;
    unit[stride] = x1;
    unit[stride + 1U] = y0;
    unit[2U * stride] = x1;
    unit[(2U * stride) + 1U] = y1;
    unit[3U * stride] = x0;
    unit[(3U * stride) + 1U] = y0;
    unit[4U * stride] = x1;
    unit[(4U * stride) + 1U] = y1;
    unit[5U * stride] = x0;
    unit[(5U * stride) + 1U] = y1;
}

static inline void db_set_rect_unit_rgb(float *unit, size_t stride,
                                        size_t color_offset, const float *rgb) {
    if ((unit == NULL) || (rgb == NULL)) {
        return;
    }
    float *color = unit + color_offset;
    for (uint32_t vertex = 0U; vertex < DB_RECT_VERTEX_COUNT; vertex++) {
        memcpy(color, rgb, 3U * sizeof(float));
        color += stride;
    }
}

static inline void db_set_rect_unit_alpha(float *unit, size_t stride,
                                          size_t alpha_offset, float alpha) {
    if (unit == NULL) {
        return;
    }
    for (uint32_t vertex = 0U; vertex < DB_RECT_VERTEX_COUNT; vertex++) {
        unit[((size_t)vertex * stride) + alpha_offset] = alpha;
    }
}

#endif
