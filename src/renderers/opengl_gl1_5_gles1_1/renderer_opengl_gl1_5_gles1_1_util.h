#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_UTIL_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_UTIL_H

#include <stddef.h>
#include <stdint.h>

size_t db_rect_tile_bytes(size_t floats_per_vertex);

float db_gl1_ndc_from_pixel_coord(int pixel_coord, int viewport_extent);

int db_gl1_row_range_to_rect(uint32_t row_start, uint32_t row_count,
                             uint32_t total_rows, int viewport_width,
                             int viewport_height, int *x_out, int *y_out,
                             int *width_out, int *height_out);

#endif
