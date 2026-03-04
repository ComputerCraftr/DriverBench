#include "renderer_opengl_gl1_5_gles1_1_primitives.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"

typedef struct {
    uint32_t total_rows;
    int viewport_h;
    int viewport_w;
} db_gl1_gradient_gpu_apply_ctx_t;

size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

void db_gl1_draw_solid_rect_pixels(int rect_x, int rect_y, int rect_width,
                                   int rect_height, int viewport_w,
                                   int viewport_h, float color_r, float color_g,
                                   float color_b) {
    if ((rect_width <= 0) || (rect_height <= 0) || (viewport_w <= 0) ||
        (viewport_h <= 0)) {
        return;
    }

    int x0 = rect_x;
    int y0 = rect_y;
    int x1 = rect_x + rect_width;
    int y1 = rect_y + rect_height;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > viewport_w) {
        x1 = viewport_w;
    }
    if (y1 > viewport_h) {
        y1 = viewport_h;
    }
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }

    db_gl_set_scissor_rect(x0, y0, x1 - x0, y1 - y0);
    db_gl_clear_color_rgb(color_r, color_g, color_b);
    db_gl_clear_color_buffer();
}

size_t db_gl1_gradient_dirty_row_total(const db_dirty_row_range_t *dirty_ranges,
                                       size_t dirty_count) {
    size_t total_rows = 0U;
    for (size_t i = 0U; i < dirty_count; i++) {
        total_rows += (size_t)dirty_ranges[i].row_count;
    }
    return total_rows;
}

int db_gl1_gradient_should_use_mesh(const db_dirty_row_range_t *dirty_ranges,
                                    size_t dirty_count,
                                    size_t mesh_row_threshold) {
    const size_t dirty_row_total =
        db_gl1_gradient_dirty_row_total(dirty_ranges, dirty_count);
    return (dirty_row_total > 0U) && (dirty_row_total <= mesh_row_threshold);
}

static void db_gl1_draw_gradient_row_color(uint32_t row, double row_r,
                                           double row_g, double row_b,
                                           void *user_data) {
    db_gl1_gradient_gpu_apply_ctx_t *ctx =
        (db_gl1_gradient_gpu_apply_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }
    int rect_x = 0;
    int rect_y = 0;
    int rect_width = 0;
    int rect_height = 0;
    if (db_gl_row_range_to_scissor_rect(
            row, 1U, ctx->total_rows, ctx->viewport_w, ctx->viewport_h, &rect_x,
            &rect_y, &rect_width, &rect_height) == 0) {
        return;
    }
    db_gl1_draw_solid_rect_pixels(
        rect_x, rect_y, rect_width, rect_height, ctx->viewport_w,
        ctx->viewport_h, db_double_to_f32(row_r), db_double_to_f32(row_g),
        db_double_to_f32(row_b));
}

void db_gl1_draw_gradient_dirty_rows_gpu(
    const char *backend_name, const db_dirty_row_range_t *dirty_ranges,
    size_t dirty_count, uint32_t head_row, int direction_down,
    uint32_t cycle_index, int viewport_w, int viewport_h) {
    if (viewport_w <= 0 || viewport_h <= 0) {
        return;
    }
    const uint32_t total_rows = db_grid_rows_effective();
    db_gl1_gradient_gpu_apply_ctx_t apply_ctx = {
        .total_rows = total_rows,
        .viewport_h = viewport_h,
        .viewport_w = viewport_w,
    };
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count_raw = dirty_ranges[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= total_rows)) {
            continue;
        }
        const uint32_t row_end = db_u32_min(
            total_rows, db_checked_add_u32(backend_name, "row_end", row_start,
                                           row_count_raw));
        const uint32_t row_count =
            db_checked_sub_u32(backend_name, "row_count", row_end, row_start);
        if (row_count == 0U) {
            continue;
        }
        db_for_each_gradient_row_color(
            row_start, row_count, head_row, direction_down, cycle_index,
            db_gl1_draw_gradient_row_color, &apply_ctx);
    }
}

void db_gl1_draw_bands_gpu(uint32_t cols, uint32_t band_count,
                           uint32_t frame_index, int viewport_width_px,
                           int viewport_height_px) {
    if (band_count == 0U) {
        return;
    }
    const int viewport_w = viewport_width_px;
    const int viewport_h = viewport_height_px;
    if (viewport_w <= 0 || viewport_h <= 0) {
        return;
    }
    for (uint32_t band = 0U; band < band_count; band++) {
        const uint32_t tile_x0 = (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                                            (uint64_t)band_count);
        const uint32_t tile_x1 =
            (uint32_t)(((uint64_t)(band + 1U) * (uint64_t)cols) /
                       (uint64_t)band_count);
        int x0 =
            (int)(((uint64_t)tile_x0 * (uint64_t)viewport_w) / (uint64_t)cols);
        int x1 =
            (int)(((uint64_t)tile_x1 * (uint64_t)viewport_w) / (uint64_t)cols);
        if (band + 1U == band_count) {
            x1 = (int)viewport_w;
        }

        double color_r_value = 0.0;
        double color_g_value = 0.0;
        double color_b_value = 0.0;
        db_band_color_rgb(band, band_count, frame_index, &color_r_value,
                          &color_g_value, &color_b_value);

        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > (int)viewport_w) {
            x1 = (int)viewport_w;
        }
        const int rect_w = x1 - x0;
        if (rect_w <= 0) {
            continue;
        }

        db_gl1_draw_solid_rect_pixels(
            x0, 0, rect_w, (int)viewport_h, viewport_w, viewport_h,
            db_double_to_f32(color_r_value), db_double_to_f32(color_g_value),
            db_double_to_f32(color_b_value));
    }
}
