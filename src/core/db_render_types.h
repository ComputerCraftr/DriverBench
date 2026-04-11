#ifndef DRIVERBENCH_CORE_RENDER_TYPES_H
#define DRIVERBENCH_CORE_RENDER_TYPES_H

#include "db_buffer_convert.h"
#include "db_core.h"
#include "db_geometry.h"
#include "db_log.h"
#include "db_numeric.h"

#define DB_RECT_VERTEX_COUNT 6U
#define DB_VERTEX_POSITION_FLOAT_COUNT 2U
#define DB_VERTEX_COLOR_FLOAT_COUNT 3U
#define DB_VERTEX_FLOAT_STRIDE                                                 \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_VERTEX_COLOR_FLOAT_COUNT)
#define DB_ES_VERTEX_COLOR_FLOAT_COUNT 4U
#define DB_ES_VERTEX_FLOAT_STRIDE                                              \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_ES_VERTEX_COLOR_FLOAT_COUNT)
#define DB_RENDER_CONTRACT_BACKEND "render_contract"
#define DB_RGBA16F_CHANNELS_PER_PIXEL 4U
#define DB_RGBA16F_BYTES_PER_PIXEL                                             \
    (sizeof(uint16_t) * DB_RGBA16F_CHANNELS_PER_PIXEL)
#define DB_RGBA8_BYTES_PER_PIXEL 4U

typedef enum {
    DB_PIXEL_FORMAT_RGBA8 = 0,
    DB_PIXEL_FORMAT_RGBA16F = 1,
} db_pixel_format_t;

typedef struct {
    uint32_t pixel_width;
    uint32_t pixel_height;
    void *pixels;
    db_pixel_format_t format;
} db_pixel_surface_t;

static inline int
db_pixel_surface_uses_rgba16f(const db_pixel_surface_t *surface) {
    return DB_BOOL((surface != NULL) &&
                   (surface->format == DB_PIXEL_FORMAT_RGBA16F));
}

static inline size_t
db_pixel_surface_pixel_bytes(const db_pixel_surface_t *surface) {
    if (db_pixel_surface_uses_rgba16f(surface) != 0) {
        return DB_RGBA16F_BYTES_PER_PIXEL;
    }
    return DB_RGBA8_BYTES_PER_PIXEL;
}

static inline uint8_t *
db_pixel_surface_bytes_mut(const db_pixel_surface_t *surface) {
    if (surface == NULL) {
        return NULL;
    }
    return (uint8_t *)surface->pixels;
}

static inline const uint8_t *
db_pixel_surface_bytes_const(const db_pixel_surface_t *surface) {
    if (surface == NULL) {
        return NULL;
    }
    return (const uint8_t *)surface->pixels;
}

static inline void *
db_pixel_surface_data_mut(const db_pixel_surface_t *surface) {
    return (void *)db_pixel_surface_bytes_mut(surface);
}

static inline const void *
db_pixel_surface_data_const(const db_pixel_surface_t *surface) {
    return (const void *)db_pixel_surface_bytes_const(surface);
}

static inline const void *
db_pixel_surface_data_at_offset_const(const db_pixel_surface_t *surface,
                                      size_t offset_bytes) {
    const uint8_t *base = db_pixel_surface_bytes_const(surface);
    if (base == NULL) {
        return NULL;
    }
    return (const void *)(base + offset_bytes);
}

static inline db_grid_block_t db_grid_block_full(uint32_t row_count,
                                                 uint32_t col_count) {
    return (db_grid_block_t){
        .row_start = 0U,
        .row_count = row_count,
        .col_start = 0U,
        .col_count = col_count,
    };
}

static inline db_damage_block_t db_damage_block_full(uint32_t row_count,
                                                     uint32_t col_count) {
    return (db_damage_block_t){
        .row_start = 0U,
        .row_count = row_count,
        .col_start = 0U,
        .col_count = col_count,
    };
}

static inline uint32_t db_checked_span_end_u32(const char *backend,
                                               const char *field_name,
                                               uint32_t start, uint32_t count) {
    return db_checked_add_u32(backend, field_name, start, count);
}

static inline uint32_t
db_grid_block_row_end_or_fail(const char *field_name,
                              const db_grid_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_span_end_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                                   block->row_start, block->row_count);
}

static inline uint32_t
db_grid_block_col_end_or_fail(const char *field_name,
                              const db_grid_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_span_end_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                                   block->col_start, block->col_count);
}

static inline uint32_t
db_damage_block_row_end_or_fail(const char *field_name,
                                const db_damage_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_span_end_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                                   block->row_start, block->row_count);
}

static inline uint32_t
db_damage_block_col_end_or_fail(const char *field_name,
                                const db_damage_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_span_end_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                                   block->col_start, block->col_count);
}

static inline uint32_t
db_grid_block_span_units_or_fail(const char *field_name,
                                 const db_grid_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_mul_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                              block->row_count, block->col_count);
}

static inline uint32_t
db_damage_block_span_units_or_fail(const char *field_name,
                                   const db_damage_block_t *block) {
    if (block == NULL) {
        DB_RUNTIME_FAIL(DB_RENDER_CONTRACT_BACKEND, "%s block is NULL",
                        field_name);
    }
    return db_checked_mul_u32(DB_RENDER_CONTRACT_BACKEND, field_name,
                              block->row_count, block->col_count);
}

static inline db_damage_block_t
db_damage_block_from_grid_block(const db_grid_block_t *block) {
    if (block == NULL) {
        return (db_damage_block_t){0U, 0U, 0U, 0U};
    }
    return (db_damage_block_t){
        .row_start = block->row_start,
        .row_count = block->row_count,
        .col_start = block->col_start,
        .col_count = block->col_count,
    };
}

static inline int db_grid_block_to_pixel_block(
    uint32_t grid_cols, uint32_t grid_rows, const db_grid_block_t *grid_block,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_block) {
    if ((out_block == NULL) || (grid_cols == 0U) || (grid_rows == 0U) ||
        (grid_block == NULL) || (pixel_width == 0U) || (pixel_height == 0U) ||
        (grid_block->row_count == 0U) || (grid_block->col_count == 0U) ||
        (grid_block->row_start >= grid_rows) ||
        (grid_block->col_start >= grid_cols)) {
        return 0;
    }
    const uint32_t row_end = DB_MIN(
        grid_rows, db_grid_block_row_end_or_fail("grid_row_end", grid_block));
    const uint32_t col_end = DB_MIN(
        grid_cols, db_grid_block_col_end_or_fail("grid_col_end", grid_block));
    if ((row_end <= grid_block->row_start) ||
        (col_end <= grid_block->col_start)) {
        return 0;
    }
    const uint32_t x0 =
        (uint32_t)(((uint64_t)grid_block->col_start * (uint64_t)pixel_width) /
                   (uint64_t)grid_cols);
    uint32_t x1 = (uint32_t)(((uint64_t)col_end * (uint64_t)pixel_width) /
                             (uint64_t)grid_cols);
    if (col_end == grid_cols) {
        x1 = pixel_width;
    }
    const uint32_t y0 =
        (uint32_t)(((uint64_t)grid_block->row_start * (uint64_t)pixel_height) /
                   (uint64_t)grid_rows);
    uint32_t y1 = (uint32_t)(((uint64_t)row_end * (uint64_t)pixel_height) /
                             (uint64_t)grid_rows);
    if (row_end == grid_rows) {
        y1 = pixel_height;
    }
    if ((x1 <= x0) || (y1 <= y0)) {
        return 0;
    }
    *out_block = (db_damage_block_t){
        .row_start = y0,
        .row_count = y1 - y0,
        .col_start = x0,
        .col_count = x1 - x0,
    };
    return 1;
}

static inline int db_grid_tile_to_pixel_block(
    uint32_t grid_cols, uint32_t grid_rows, uint32_t tile_index,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *out_block) {
    if (grid_cols == 0U) {
        return 0;
    }
    const db_grid_block_t grid_block = {
        .row_start = tile_index / grid_cols,
        .row_count = 1U,
        .col_start = tile_index % grid_cols,
        .col_count = 1U,
    };
    return db_grid_block_to_pixel_block(grid_cols, grid_rows, &grid_block,
                                        pixel_width, pixel_height, out_block);
}

static inline uint32_t db_grid_axis_edge_to_pixel_coord(uint32_t grid_extent,
                                                        uint32_t edge_index,
                                                        uint32_t pixel_extent) {
    if ((grid_extent == 0U) || (pixel_extent == 0U)) {
        return 0U;
    }
    if (edge_index >= grid_extent) {
        return pixel_extent;
    }
    return (uint32_t)(((uint64_t)edge_index * (uint64_t)pixel_extent) /
                      (uint64_t)grid_extent);
}

static inline float db_pixel_coord_to_ndc_f32(uint32_t pixel_coord,
                                              uint32_t pixel_extent) {
    if (pixel_extent == 0U) {
        return 0.0F;
    }
    const double normalized =
        ((double)pixel_coord * 2.0) / (double)pixel_extent;
    return db_double_to_f32(normalized - 1.0);
}

static inline void db_pixel_bounds_to_ndc_f32(uint32_t x0_px, uint32_t y0_px,
                                              uint32_t x1_px, uint32_t y1_px,
                                              uint32_t pixel_width,
                                              uint32_t pixel_height, float *x0,
                                              float *y0, float *x1, float *y1) {
    if ((x0 == NULL) || (y0 == NULL) || (x1 == NULL) || (y1 == NULL)) {
        return;
    }
    *x0 = db_pixel_coord_to_ndc_f32(x0_px, pixel_width);
    *x1 = db_pixel_coord_to_ndc_f32(x1_px, pixel_width);
    *y0 = db_pixel_coord_to_ndc_f32(y0_px, pixel_height);
    *y1 = db_pixel_coord_to_ndc_f32(y1_px, pixel_height);
}

static inline void db_rgb_pixels_write_index_f64(void *pixels,
                                                 db_pixel_format_t format,
                                                 size_t idx,
                                                 const double *rgb) {
    if (rgb == NULL) {
        return;
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        uint16_t *pixels_rgba16f = (uint16_t *)pixels;
        const size_t base = idx * DB_RGBA16F_CHANNELS_PER_PIXEL;
        pixels_rgba16f[base + 0U] = db_f64_to_f16_via_f32(rgb[0]);
        pixels_rgba16f[base + 1U] = db_f64_to_f16_via_f32(rgb[1]);
        pixels_rgba16f[base + 2U] = db_f64_to_f16_via_f32(rgb[2]);
        pixels_rgba16f[base + 3U] = DB_F16_ONE;
        return;
    }
    uint32_t *pixels_rgba8 = (uint32_t *)pixels;
    pixels_rgba8[idx] =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
}

// CPU pixel surfaces consume canonical f64 color data only at these helpers.
// Legal export boundaries here are:
// - f64 -> rgba8 packed u8
// - f64 -> rgba16f packed f16
// GPU-facing float submission must use dedicated f64 -> f32 helpers at the
// actual GL/Vulkan consumption sites instead of routing through CPU pixel APIs.
static inline void db_rgb_pixels_read_index_f64(const void *pixels,
                                                db_pixel_format_t format,
                                                size_t idx, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        const uint16_t *pixels_rgba16f = (const uint16_t *)pixels;
        const size_t base = idx * DB_RGBA16F_CHANNELS_PER_PIXEL;
        db_rgb_f16_to_f64_rgb3(&pixels_rgba16f[base], out_rgb);
        return;
    }
    const uint32_t *pixels_rgba8 = (const uint32_t *)pixels;
    db_unpack_rgba8888_rgb01(pixels_rgba8[idx], out_rgb);
}

static inline void db_rgb_pixels_fill_solid_f64(uint32_t width, uint32_t height,
                                                void *pixels,
                                                db_pixel_format_t format,
                                                const double *rgb) {
    if ((rgb == NULL) || (width == 0U) || (height == 0U)) {
        return;
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        const uint16_t rgba_f16[4] = {
            db_f64_to_f16_via_f32(rgb[0]), db_f64_to_f16_via_f32(rgb[1]),
            db_f64_to_f16_via_f32(rgb[2]), DB_F16_ONE};
        uint16_t *dst = (uint16_t *)pixels;
        const size_t row_stride = (size_t)width * DB_RGBA16F_CHANNELS_PER_PIXEL;
        for (uint32_t row = 0U; row < height; row++) {
            db_fill_rgba16f_buffer(dst, width, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
    uint32_t *dst = (uint32_t *)pixels;
    for (uint32_t row = 0U; row < height; row++) {
        db_fill_u32_buffer(dst, width, packed_color);
        dst += width;
    }
}

static inline void db_rgb_pixels_fill_damage_block_f64(
    uint32_t width, uint32_t height, void *pixels, db_pixel_format_t format,
    uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count, const double *rgb) {
    if ((rgb == NULL) || (row_start >= height) || (col_start >= width) ||
        (row_count == 0U) || (col_count == 0U)) {
        return;
    }
    const uint32_t span_rows = DB_MIN(row_count, height - row_start);
    const uint32_t span_cols = DB_MIN(col_count, width - col_start);
    if ((span_rows == 0U) || (span_cols == 0U)) {
        return;
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        const uint16_t rgba_f16[4] = {
            db_f64_to_f16_via_f32(rgb[0]), db_f64_to_f16_via_f32(rgb[1]),
            db_f64_to_f16_via_f32(rgb[2]), DB_F16_ONE};
        uint16_t *dst =
            (uint16_t *)pixels + ((((size_t)row_start * width) + col_start) *
                                  DB_RGBA16F_CHANNELS_PER_PIXEL);
        const size_t row_stride = (size_t)width * DB_RGBA16F_CHANNELS_PER_PIXEL;
        for (uint32_t row = 0U; row < span_rows; row++) {
            db_fill_rgba16f_buffer(dst, span_cols, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], UINT8_MAX);
    uint32_t *dst =
        (uint32_t *)pixels + (((size_t)row_start * width) + col_start);
    for (uint32_t row = 0U; row < span_rows; row++) {
        db_fill_u32_buffer(dst, span_cols, packed_color);
        dst += width;
    }
}

#endif
