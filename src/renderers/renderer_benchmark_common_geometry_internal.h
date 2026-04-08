#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_GEOMETRY_INTERNAL_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_GEOMETRY_INTERNAL_H

#include "renderer_benchmark_runtime.h"

static inline void db_grid_tile_bounds_ndc(uint32_t tile_index, float *x0,
                                           float *y0, float *x1, float *y1) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    const double inv_cols = 1.0 / (double)cols;
    const double inv_rows = 1.0 / (double)rows;

    *x0 = db_double_to_f32((2.0 * (double)col * inv_cols) - 1.0);
    *x1 = db_double_to_f32((2.0 * (double)(col + 1U) * inv_cols) - 1.0);
    *y1 = db_double_to_f32(1.0 - (2.0 * (double)row * inv_rows));
    *y0 = db_double_to_f32(1.0 - (2.0 * (double)(row + 1U) * inv_rows));
}

static inline void db_fill_rect_unit_pos(float *unit_base, float x0, float y0,
                                         float x1, float y1,
                                         size_t stride_floats) {
    // Triangle 1
    unit_base[0] = x0;
    unit_base[1] = y0;
    unit_base[stride_floats] = x1;
    unit_base[stride_floats + 1U] = y0;
    unit_base[2U * stride_floats] = x1;
    unit_base[(2U * stride_floats) + 1U] = y1;

    // Triangle 2
    unit_base[3U * stride_floats] = x0;
    unit_base[(3U * stride_floats) + 1U] = y0;
    unit_base[4U * stride_floats] = x1;
    unit_base[(4U * stride_floats) + 1U] = y1;
    unit_base[5U * stride_floats] = x0;
    unit_base[(5U * stride_floats) + 1U] = y1;
}

static inline void db_set_rect_unit_rgb(float *unit_base, size_t stride_floats,
                                        size_t color_offset_floats,
                                        const float *rgb) {
    if ((unit_base == NULL) || (rgb == NULL)) {
        return;
    }
    float *color = unit_base + color_offset_floats;
    for (uint32_t v = 0; v < DB_RECT_VERTEX_COUNT; v++) {
        db_copy_f32_rgb3(color, rgb);
        color += stride_floats;
    }
}

static inline void
db_set_rect_tile_range_rgb(float *vertices, uint32_t first_tile_index,
                           uint32_t tile_count, size_t stride_floats,
                           size_t color_offset_floats, const float *rgb) {
    if ((vertices == NULL) || (tile_count == 0U) || (rgb == NULL)) {
        return;
    }
    const size_t first_tile_offset =
        (size_t)first_tile_index * DB_RECT_VERTEX_COUNT * stride_floats;
    float *unit = &vertices[first_tile_offset];
    for (uint32_t tile = 0U; tile < tile_count; tile++) {
        db_set_rect_unit_rgb(unit, stride_floats, color_offset_floats, rgb);
        unit += (size_t)DB_RECT_VERTEX_COUNT * stride_floats;
    }
}

static inline void db_set_rect_unit_alpha(float *unit, size_t stride_floats,
                                          size_t alpha_offset_floats,
                                          float alpha_value) {
    if (unit == NULL) {
        return;
    }
    for (uint32_t vertex_index = 0U; vertex_index < DB_RECT_VERTEX_COUNT;
         vertex_index++) {
        const size_t base = (size_t)vertex_index * stride_floats;
        unit[base + alpha_offset_floats] = alpha_value;
    }
}

static inline void db_fill_grid_all_rgb_stride(float *vertices,
                                               uint32_t tile_count,
                                               size_t stride_floats,
                                               size_t color_offset_floats,
                                               const float *rgb) {
    db_set_rect_tile_range_rgb(vertices, 0U, tile_count, stride_floats,
                               color_offset_floats, rgb);
}

static inline void db_band_color_rgb3(uint32_t band_index, uint32_t band_count,
                                      uint32_t frame_index, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const double band_value = (double)band_index;
    const double frame_value = (double)frame_index;
    const double pulse_value =
        BENCH_PULSE_BASE +
        (BENCH_PULSE_AMP * sin((frame_value * BENCH_PULSE_FREQ) +
                               (band_value * BENCH_PULSE_PHASE)));
    const double color_r_value =
        pulse_value * (BENCH_COLOR_R_BASE +
                       (BENCH_COLOR_R_SCALE * band_value / (double)band_count));
    const double band_rgb[3] = {
        color_r_value, pulse_value * BENCH_COLOR_G_SCALE, 1.0 - color_r_value};
    db_copy_f64_rgb3(out_rgb, band_rgb);
}

static inline double db_color_channel(uint32_t seed) {
    const double normalized = db_u8_to_unit_f64(seed);
    return DB_COLOR_CHANNEL_BIAS + (normalized * DB_COLOR_CHANNEL_SCALE);
}

static inline void db_palette_cycle_color_rgb3(uint32_t cycle_index,
                                               double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    const uint32_t seed_base = db_mix_u32(
        ((cycle_index + 1U) * DB_PALETTE_SALT_BASE_STEP) ^ DB_U32_SALT_PALETTE);
    const double palette_rgb[3] = {
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_R)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_G)),
        db_color_channel(db_mix_u32(seed_base ^ DB_U32_SALT_COLOR_B)),
    };
    db_copy_f64_rgb3(out_rgb, palette_rgb);
}

#endif
