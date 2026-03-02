#ifndef RENDERER_OPENGL_GL1_5_GLES1_1_PRIMITIVES_H
#define RENDERER_OPENGL_GL1_5_GLES1_1_PRIMITIVES_H

#include <stddef.h>
#include <stdint.h>

#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"

size_t db_rect_tile_bytes(size_t floats_per_vertex);

void db_gl1_draw_solid_rect_pixels(int rect_x, int rect_y, int rect_width,
                                   int rect_height, int viewport_w,
                                   int viewport_h, float color_r, float color_g,
                                   float color_b);

int db_gl1_snake_span_to_scissor_rect(const db_snake_col_span_t *span,
                                      uint32_t total_cols, uint32_t total_rows,
                                      int viewport_w, int viewport_h,
                                      int *x_out, int *y_out, int *width_out,
                                      int *height_out);

size_t db_gl1_gradient_dirty_row_total(const db_dirty_row_range_t *dirty_ranges,
                                       size_t dirty_count);

int db_gl1_gradient_should_use_mesh(const db_dirty_row_range_t *dirty_ranges,
                                    size_t dirty_count,
                                    size_t mesh_row_threshold);

void db_gl1_draw_gradient_dirty_rows_gpu(
    const char *backend_name, const db_dirty_row_range_t *dirty_ranges,
    size_t dirty_count, uint32_t head_row, int direction_down,
    uint32_t cycle_index, int viewport_w, int viewport_h);

void db_gl1_draw_bands_gpu(uint32_t cols, uint32_t band_count,
                           uint32_t frame_index, int viewport_width_px,
                           int viewport_height_px);

#endif
