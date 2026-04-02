#include "renderer_opengl_gl1_5_gles1_1_util.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"

size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

float db_gl1_ndc_from_pixel_coord(int pixel_coord, int viewport_extent) {
    if ((pixel_coord < 0) || (viewport_extent <= 0)) {
        return 0.0F;
    }
    return db_pixel_coord_to_ndc_f32(
        db_checked_int_to_u32("renderer_opengl_gl1_5_gles1_1_util",
                              "pixel_coord", pixel_coord),
        db_checked_int_to_u32("renderer_opengl_gl1_5_gles1_1_util",
                              "viewport_extent", viewport_extent));
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
    const uint32_t row_end = db_u32_min(
        total_rows, db_checked_add_u32("renderer_opengl_gl1_5_gles1_1_util",
                                       "row_end", row_start, row_count));
    const uint32_t viewport_height_u32 =
        db_checked_int_to_u32("renderer_opengl_gl1_5_gles1_1_util",
                              "viewport_height", viewport_height);
    if (row_end <= row_start) {
        return 0;
    }

    const uint32_t py_top_u32 = db_grid_axis_edge_to_pixel_coord(
        total_rows, row_start, viewport_height_u32);
    const uint32_t py_bottom_u32 = db_grid_axis_edge_to_pixel_coord(
        total_rows, row_end, viewport_height_u32);
    int py_top = db_checked_u32_to_i32("renderer_opengl_gl1_5_gles1_1_util",
                                       "py_top", py_top_u32);
    int py_bottom = db_checked_u32_to_i32("renderer_opengl_gl1_5_gles1_1_util",
                                          "py_bottom", py_bottom_u32);
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
