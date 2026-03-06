#include "renderer_opengl_gl1_5_gles1_1_util.h"

#include "../../core/db_numeric.h"

size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

size_t db_gl1_gradient_dirty_row_total(const db_dirty_row_range_t *dirty_ranges,
                                       size_t dirty_count) {
    size_t total_rows = 0U;
    for (size_t i = 0U; i < dirty_count; i++) {
        total_rows += (size_t)dirty_ranges[i].row_count;
    }
    return total_rows;
}

float db_gl1_ndc_from_pixel_coord(int pixel_coord, int viewport_extent) {
    if (viewport_extent <= 0) {
        return 0.0F;
    }
    const double normalized =
        ((double)pixel_coord * 2.0) / (double)viewport_extent;
    return db_double_to_f32(normalized - 1.0);
}

int db_gl1_row_range_to_rect(uint32_t row_start, uint32_t row_count,
                             uint32_t total_rows, int viewport_width,
                             int viewport_height, int *x_out, int *y_out,
                             int *width_out, int *height_out) {
    if ((row_count == 0U) || (total_rows == 0U) || (row_start >= total_rows) ||
        (viewport_width <= 0) || (viewport_height <= 0) || (x_out == NULL) ||
        (y_out == NULL) || (width_out == NULL) || (height_out == NULL)) {
        return 0;
    }
    const uint32_t row_end = db_u32_min(total_rows, row_start + row_count);
    if (row_end <= row_start) {
        return 0;
    }

    int py_top = (int)(((uint64_t)row_start * (uint64_t)viewport_height) /
                       (uint64_t)total_rows);
    int py_bottom = (int)(((uint64_t)row_end * (uint64_t)viewport_height) /
                          (uint64_t)total_rows);
    if (row_end == total_rows) {
        py_bottom = viewport_height;
    }
    if (py_top < 0) {
        py_top = 0;
    }
    if (py_bottom > viewport_height) {
        py_bottom = viewport_height;
    }
    if (py_bottom <= py_top) {
        return 0;
    }

    int rect_y = viewport_height - py_bottom;
    int rect_h = py_bottom - py_top;
    if (rect_y < 0) {
        rect_h += rect_y;
        rect_y = 0;
    }
    if ((rect_y + rect_h) > viewport_height) {
        rect_h = viewport_height - rect_y;
    }
    if (rect_h <= 0) {
        return 0;
    }

    *x_out = 0;
    *y_out = rect_y;
    *width_out = viewport_width;
    *height_out = rect_h;
    return 1;
}
