#ifndef DRIVERBENCH_DISPLAY_CPU_PRESENT_COMMON_H
#define DRIVERBENCH_DISPLAY_CPU_PRESENT_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"

static inline uint32_t
db_display_pack_native_pixel(const db_pixel_surface_t *source,
                             size_t source_index,
                             db_native_output_format_t native_format) {
    if (source->format == DB_PIXEL_FORMAT_RGBA16F) {
        const uint16_t *const pixels = (const uint16_t *)source->pixels;
        const uint16_t *const rgb =
            &pixels[source_index * DB_RGBA16F_CHANNELS_PER_PIXEL];
        return (native_format == DB_NATIVE_OUTPUT_XRGB2101010)
                   ? db_pack_xrgb2101010_from_rgb16f3(rgb)
                   : db_pack_xrgb8888_from_rgb16f3(rgb);
    }
    const uint32_t packed = ((const uint32_t *)source->pixels)[source_index];
    return (native_format == DB_NATIVE_OUTPUT_XRGB2101010)
               ? db_pack_xrgb2101010_from_rgba8888(packed)
               : db_pack_xrgb8888_from_rgba8888(packed);
}

static inline const char *db_display_cpu_present_validate(
    const char *backend, const db_pixel_surface_t *source,
    const uint8_t *destination, uint32_t destination_width,
    uint32_t destination_height, uint32_t destination_stride_bytes,
    db_native_output_format_t native_format) {
    const char *const safe_backend =
        (backend != NULL) ? backend : "cpu_present";
    if ((source == NULL) || (source->pixels == NULL) ||
        (source->pixel_width == 0U) || (source->pixel_height == 0U) ||
        (destination == NULL) || (destination_width == 0U) ||
        (destination_height == 0U) ||
        ((native_format != DB_NATIVE_OUTPUT_XRGB8888) &&
         (native_format != DB_NATIVE_OUTPUT_XRGB2101010))) {
        DB_RUNTIME_FAIL(safe_backend, "invalid CPU presentation transform");
    }
    const size_t destination_row_bytes =
        db_checked_mul_size(safe_backend, "destination row bytes",
                            destination_width, sizeof(uint32_t));
    if (destination_stride_bytes < destination_row_bytes) {
        DB_RUNTIME_FAIL(safe_backend, "CPU presentation stride is too small");
    }
    (void)db_checked_mul_size(safe_backend, "destination surface bytes",
                              destination_height, destination_stride_bytes);
    const size_t source_pixel_count =
        db_checked_mul_size(safe_backend, "source surface pixels",
                            source->pixel_width, source->pixel_height);
    (void)db_checked_mul_size(safe_backend, "source surface bytes",
                              source_pixel_count,
                              db_pixel_surface_pixel_bytes(source));
    return safe_backend;
}

static inline uint64_t db_display_scale_surface_region_validated(
    const char *backend, const db_pixel_surface_t *source, uint8_t *destination,
    uint32_t destination_width, uint32_t destination_height,
    uint32_t destination_stride_bytes, db_damage_block_t region,
    db_native_output_format_t native_format) {
    if ((region.row_start >= destination_height) ||
        (region.col_start >= destination_width) || (region.row_count == 0U) ||
        (region.col_count == 0U)) {
        return 0U;
    }
    region.row_count =
        DB_MIN(region.row_count, destination_height - region.row_start);
    region.col_count =
        DB_MIN(region.col_count, destination_width - region.col_start);
    if ((source->pixel_width == destination_width) &&
        (source->pixel_height == destination_height) &&
        (db_pointer_is_aligned(destination, _Alignof(uint32_t)) != 0) &&
        ((destination_stride_bytes % sizeof(uint32_t)) == 0U)) {
        uint32_t *const native_pixels =
            DB_ASSUME_ALIGNED(destination, _Alignof(uint32_t));
        const size_t native_stride =
            destination_stride_bytes / sizeof(uint32_t);
        if (native_format == DB_NATIVE_OUTPUT_XRGB2101010) {
            if (source->format == DB_PIXEL_FORMAT_RGBA16F) {
                db_convert_rgba16f_to_xrgb2101010_block(
                    native_pixels, native_stride,
                    (const uint16_t *)source->pixels, source->pixel_width,
                    region.row_start, region.row_count, region.col_start,
                    region.col_count);
            } else {
                db_convert_rgba8_to_xrgb2101010_block(
                    native_pixels, native_stride,
                    (const uint32_t *)source->pixels, source->pixel_width,
                    region.row_start, region.row_count, region.col_start,
                    region.col_count);
            }
        } else if (source->format == DB_PIXEL_FORMAT_RGBA16F) {
            db_convert_rgba16f_to_xrgb8888_block(
                native_pixels, native_stride, (const uint16_t *)source->pixels,
                source->pixel_width, region.row_start, region.row_count,
                region.col_start, region.col_count);
        } else {
            db_convert_rgba8_to_xrgb8888_block(
                native_pixels, native_stride, (const uint32_t *)source->pixels,
                source->pixel_width, region.row_start, region.row_count,
                region.col_start, region.col_count);
        }
        uint64_t pixel_count = 0U;
        uint64_t bytes_written = 0U;
        if ((db_try_mul_u64(region.row_count, region.col_count, &pixel_count) ==
             0) ||
            (db_try_mul_u64(pixel_count, sizeof(uint32_t), &bytes_written) ==
             0)) {
            DB_RUNTIME_FAIL(backend,
                            "identity CPU presentation byte count overflow");
        }
        return bytes_written;
    }
    const uint32_t row_end = region.row_start + region.row_count;
    const uint32_t column_end = region.col_start + region.col_count;
    uint64_t bytes_written = 0U;
    for (uint32_t row = region.row_start; row < row_end; row++) {
        const uint32_t source_row = db_checked_u64_to_u32(
            backend, "source_row",
            ((uint64_t)row * source->pixel_height) / destination_height);
        uint8_t *const destination_row =
            destination + ((size_t)row * destination_stride_bytes);
        for (uint32_t column = region.col_start; column < column_end;
             column++) {
            const uint32_t source_column = db_checked_u64_to_u32(
                backend, "source_column",
                ((uint64_t)column * source->pixel_width) / destination_width);
            const size_t source_index =
                ((size_t)source_row * source->pixel_width) + source_column;
            const uint32_t packed = db_display_pack_native_pixel(
                source, source_index, native_format);
            memcpy(destination_row + ((size_t)column * sizeof(packed)), &packed,
                   sizeof(packed));
            bytes_written += sizeof(packed);
        }
    }
    return bytes_written;
}

static inline void db_display_scale_surface_to_native(
    const char *backend, const db_pixel_surface_t *source, uint8_t *destination,
    uint32_t destination_width, uint32_t destination_height,
    uint32_t destination_stride_bytes,
    db_native_output_format_t native_format) {
    const char *const safe_backend = db_display_cpu_present_validate(
        backend, source, destination, destination_width, destination_height,
        destination_stride_bytes, native_format);
    (void)db_display_scale_surface_region_validated(
        safe_backend, source, destination, destination_width,
        destination_height, destination_stride_bytes,
        db_damage_block_full(destination_height, destination_width),
        native_format);
}

static inline void db_display_scale_surface_to_xrgb8888(
    const char *backend, const db_pixel_surface_t *source, uint8_t *destination,
    uint32_t destination_width, uint32_t destination_height,
    uint32_t destination_stride_bytes) {
    db_display_scale_surface_to_native(
        backend, source, destination, destination_width, destination_height,
        destination_stride_bytes, DB_NATIVE_OUTPUT_XRGB8888);
}

static inline uint64_t db_display_scale_surface_region_to_native(
    const char *backend, const db_pixel_surface_t *source, uint8_t *destination,
    uint32_t destination_width, uint32_t destination_height,
    uint32_t destination_stride_bytes, const db_damage_block_t *region,
    db_native_output_format_t native_format) {
    if (region == NULL) {
        const char *const safe_backend =
            (backend != NULL) ? backend : "cpu_present";
        DB_RUNTIME_FAIL(safe_backend, "invalid CPU presentation region");
    }
    const char *const safe_backend = db_display_cpu_present_validate(
        backend, source, destination, destination_width, destination_height,
        destination_stride_bytes, native_format);
    return db_display_scale_surface_region_validated(
        safe_backend, source, destination, destination_width,
        destination_height, destination_stride_bytes, *region, native_format);
}

static inline uint64_t db_display_scale_surface_region_to_xrgb8888(
    const char *backend, const db_pixel_surface_t *source, uint8_t *destination,
    uint32_t destination_width, uint32_t destination_height,
    uint32_t destination_stride_bytes, const db_damage_block_t *region) {
    return db_display_scale_surface_region_to_native(
        backend, source, destination, destination_width, destination_height,
        destination_stride_bytes, region, DB_NATIVE_OUTPUT_XRGB8888);
}

#endif
