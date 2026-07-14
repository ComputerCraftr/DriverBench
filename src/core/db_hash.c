#include "db_hash.h"
#include "db_render_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_alloc_policy.h"
#include "db_buffer_convert.h"
#include "db_core.h"
#include "db_numeric.h"

static _Thread_local uint8_t *g_hash_canonical_bytes = NULL;
static _Thread_local size_t g_hash_canonical_capacity = 0U;

static int db_hash_image_size(uint32_t width, uint32_t height,
                              size_t bytes_per_pixel, size_t *out_row_bytes,
                              size_t *out_image_bytes) {
    if ((out_row_bytes == NULL) || (out_image_bytes == NULL) ||
        (db_try_mul_size((size_t)width, bytes_per_pixel, out_row_bytes) == 0) ||
        (db_try_mul_size(*out_row_bytes, (size_t)height, out_image_bytes) ==
         0)) {
        return 0;
    }
    return 1;
}

static size_t db_hash_source_layout_size(db_pixel_format_t format,
                                         uint32_t width, uint32_t height,
                                         size_t stride_bytes) {
    const size_t pixel_bytes = db_pixel_format_bytes_per_pixel(format);
    size_t row_bytes = 0U;
    size_t source_bytes = 0U;
    if ((pixel_bytes == 0U) ||
        (db_try_mul_size((size_t)width, pixel_bytes, &row_bytes) == 0) ||
        (db_try_strided_size((size_t)height, stride_bytes, row_bytes,
                             &source_bytes) == 0)) {
        return 0U;
    }
    return source_bytes;
}

static inline uint8_t *db_hash_reserve_canonical_bytes(size_t bytes) {
    if (bytes == 0U) {
        return NULL;
    }
    // Canonical scratch stays cacheline-aligned for SIMD tree-leaf processing.
    db_reserve_aligned_array_capacity_or_fail(
        (void **)&g_hash_canonical_bytes, &g_hash_canonical_capacity, bytes,
        bytes, sizeof(uint8_t), DB_CACHELINE_ALIGNMENT_BYTES, 0U, "db_hash",
        "canonical_bytes");
    return g_hash_canonical_bytes;
}

static void db_hash_dump_bytes_to_path(const char *path, const void *bytes,
                                       size_t byte_count) {
    if ((path == NULL) || (path[0] == '\0') || (bytes == NULL)) {
        return;
    }
    FILE *const dump = fopen(path, "wb");
    if (dump == NULL) {
        return;
    }
    const size_t written = fwrite(bytes, 1U, byte_count, dump);
    const int close_result = fclose(dump);
    if ((written != byte_count) || (close_result != 0)) {
        (void)remove(path);
    }
}

static void db_hash_dump_bytes_from_environment(const char *variable,
                                                const void *bytes,
                                                size_t byte_count) {
    db_hash_dump_bytes_to_path(getenv(variable), bytes, byte_count);
}

uint64_t db_fnv1a64_extend(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0U; i < size; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= DB_FNV1A64_PRIME;
    }
    return hash;
}

uint64_t db_fnv1a64_bytes(const void *data, size_t size) {
    return db_fnv1a64_extend(DB_FNV1A64_OFFSET, data, size);
}

uint64_t db_fnv1a64_mix_u64(uint64_t hash, uint64_t value) {
    return db_fnv1a64_extend(hash, &value, sizeof(value));
}

uint64_t db_hash_rgba8_pixels_canonical(const void *pixels, uint32_t width,
                                        uint32_t height, size_t stride_bytes,
                                        int rows_bottom_to_top) {
    if ((pixels == NULL) || (width == 0U) || (height == 0U)) {
        return 0U;
    }
    size_t row_bytes = 0U;
    size_t packed_bytes = 0U;
    if (db_hash_image_size(width, height, DB_RGBA8_BYTES_PER_PIXEL, &row_bytes,
                           &packed_bytes) == 0) {
        return 0U;
    }
    if (stride_bytes < row_bytes) {
        return 0U;
    }
    if (db_hash_source_layout_size(DB_PIXEL_FORMAT_RGBA8, width, height,
                                   stride_bytes) == 0U) {
        return 0U;
    }

    if ((rows_bottom_to_top == 0) && (stride_bytes == row_bytes)) {
        return db_fnv1a64_tree(pixels, packed_bytes, DB_U32_SALT_PALETTE,
                               DB_FNV1A64_OFFSET);
    }

    const uint8_t *src_bytes = (const uint8_t *)pixels;
    uint8_t *const canonical_bytes =
        db_hash_reserve_canonical_bytes(packed_bytes);
    for (uint32_t row = 0U; row < height; row++) {
        const uint32_t src_row =
            (rows_bottom_to_top != 0U) ? (height - 1U - row) : row;
        const size_t src_offset = (size_t)src_row * stride_bytes;
        const size_t dst_offset = (size_t)row * row_bytes;
        memcpy(canonical_bytes + dst_offset, src_bytes + src_offset, row_bytes);
    }
    const uint64_t hash = db_fnv1a64_tree(
        canonical_bytes, packed_bytes, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    return hash;
}

uint64_t db_hash_sdr_framebuffer_rgba8_canonical(
    const void *pixels, uint32_t framebuffer_width, uint32_t framebuffer_height,
    size_t stride_bytes, int rows_bottom_to_top, uint32_t canonical_width,
    uint32_t canonical_height) {
    if ((pixels == NULL) || (framebuffer_width == 0U) ||
        (framebuffer_height == 0U) || (canonical_width == 0U) ||
        (canonical_height == 0U)) {
        return 0U;
    }
    const size_t framebuffer_row_bytes = db_checked_mul_size(
        "db_hash", "framebuffer_row_bytes", (size_t)framebuffer_width,
        DB_RGBA8_BYTES_PER_PIXEL);
    if (stride_bytes < framebuffer_row_bytes) {
        return 0U;
    }
    if (db_hash_source_layout_size(DB_PIXEL_FORMAT_RGBA8, framebuffer_width,
                                   framebuffer_height, stride_bytes) == 0U) {
        return 0U;
    }
    size_t canonical_row_bytes = 0U;
    size_t canonical_bytes = 0U;
    if (db_hash_image_size(canonical_width, canonical_height,
                           DB_RGBA8_BYTES_PER_PIXEL, &canonical_row_bytes,
                           &canonical_bytes) == 0) {
        return 0U;
    }
    uint8_t *const canonical = db_hash_reserve_canonical_bytes(canonical_bytes);
    const uint8_t *const source = (const uint8_t *)pixels;
    for (uint32_t y = 0U; y < canonical_height; y++) {
        const uint32_t source_y_top = db_checked_u64_to_u32(
            "db_hash", "canonical_source_y",
            ((((uint64_t)y * 2U) + 1U) * framebuffer_height) /
                ((uint64_t)canonical_height * 2U));
        const uint32_t source_y = (rows_bottom_to_top != 0)
                                      ? framebuffer_height - 1U - source_y_top
                                      : source_y_top;
        const uint8_t *const source_row =
            source + ((size_t)source_y * stride_bytes);
        uint8_t *const destination_row =
            canonical + ((size_t)y * canonical_row_bytes);
        for (uint32_t x = 0U; x < canonical_width; x++) {
            const uint32_t source_x = db_checked_u64_to_u32(
                "db_hash", "canonical_source_x",
                ((((uint64_t)x * 2U) + 1U) * framebuffer_width) /
                    ((uint64_t)canonical_width * 2U));
            const size_t source_base = (size_t)source_x * 4U;
            const size_t destination_base = (size_t)x * 4U;
            destination_row[destination_base] = source_row[source_base];
            destination_row[destination_base + 1U] =
                source_row[source_base + 1U];
            destination_row[destination_base + 2U] =
                source_row[source_base + 2U];
            destination_row[destination_base + 3U] = UINT8_MAX;
        }
    }
    return db_fnv1a64_tree(canonical, canonical_bytes, DB_U32_SALT_PALETTE,
                           DB_FNV1A64_OFFSET);
}

uint64_t db_hash_rgba16f_pixels_canonical(const uint16_t *pixels,
                                          uint32_t width, uint32_t height,
                                          size_t stride_bytes,
                                          int rows_bottom_to_top) {
    if ((pixels == NULL) || (width == 0U) || (height == 0U)) {
        return 0U;
    }
    size_t row_bytes = 0U;
    size_t packed_bytes = 0U;
    if (db_hash_image_size(width, height, DB_RGBA16F_BYTES_PER_PIXEL,
                           &row_bytes, &packed_bytes) == 0) {
        return 0U;
    }
    if (stride_bytes < row_bytes) {
        return 0U;
    }
    if (db_hash_source_layout_size(DB_PIXEL_FORMAT_RGBA16F, width, height,
                                   stride_bytes) == 0U) {
        return 0U;
    }

    if ((rows_bottom_to_top == 0) && (stride_bytes == row_bytes)) {
        return db_fnv1a64_tree(pixels, packed_bytes, DB_U32_SALT_ORIGIN_Y,
                               DB_FNV1A64_OFFSET);
    }

    const uint8_t *src_bytes = (const uint8_t *)pixels;
    uint8_t *const canonical_bytes =
        db_hash_reserve_canonical_bytes(packed_bytes);
    for (uint32_t row = 0U; row < height; row++) {
        const uint32_t src_row =
            (rows_bottom_to_top != 0U) ? (height - 1U - row) : row;
        const size_t src_offset = (size_t)src_row * stride_bytes;
        const size_t dst_offset = (size_t)row * row_bytes;
        memcpy(canonical_bytes + dst_offset, src_bytes + src_offset, row_bytes);
    }
    const uint64_t hash = db_fnv1a64_tree(
        canonical_bytes, packed_bytes, DB_U32_SALT_ORIGIN_Y, DB_FNV1A64_OFFSET);
    return hash;
}

uint64_t db_hash_working_rgba8(const void *pixels, db_pixel_format_t format,
                               uint32_t width, uint32_t height,
                               size_t stride_bytes, int rows_bottom_to_top) {
    if ((pixels == NULL) || (width == 0U) || (height == 0U)) {
        return 0U;
    }
    size_t row_bytes = 0U;
    size_t packed_bytes = 0U;
    if (db_hash_image_size(width, height, DB_RGBA8_BYTES_PER_PIXEL, &row_bytes,
                           &packed_bytes) == 0) {
        return 0U;
    }
    uint8_t *const canonical = db_hash_reserve_canonical_bytes(packed_bytes);
    if (db_working_rgba8_canonicalize(pixels, format, width, height,
                                      stride_bytes, rows_bottom_to_top,
                                      canonical, packed_bytes) == 0) {
        return 0U;
    }
    db_hash_dump_bytes_from_environment("DB_TEST_WORKING_RGBA8_DUMP", canonical,
                                        packed_bytes);
    const char *const raw_dump_path = getenv("DB_TEST_WORKING_RAW_DUMP");
    if ((raw_dump_path != NULL) && (raw_dump_path[0] != '\0')) {
        const size_t raw_bytes =
            db_hash_source_layout_size(format, width, height, stride_bytes);
        if (raw_bytes == 0U) {
            return 0U;
        }
        db_hash_dump_bytes_to_path(raw_dump_path, pixels, raw_bytes);
    }
    return db_fnv1a64_tree(canonical, packed_bytes, DB_U32_SALT_PALETTE,
                           DB_FNV1A64_OFFSET);
}

int db_working_rgba8_canonicalize(const void *pixels, db_pixel_format_t format,
                                  uint32_t width, uint32_t height,
                                  size_t stride_bytes, int rows_bottom_to_top,
                                  uint8_t *destination,
                                  size_t destination_size) {
    if ((pixels == NULL) || (destination == NULL) || (width == 0U) ||
        (height == 0U) ||
        ((format != DB_PIXEL_FORMAT_RGBA8) &&
         (format != DB_PIXEL_FORMAT_RGBA16F))) {
        return 0;
    }
    size_t row_bytes = 0U;
    size_t packed_bytes = 0U;
    if (db_hash_image_size(width, height, DB_RGBA8_BYTES_PER_PIXEL, &row_bytes,
                           &packed_bytes) == 0) {
        return 0;
    }
    if (destination_size < packed_bytes) {
        return 0;
    }
    if (db_hash_source_layout_size(format, width, height, stride_bytes) == 0U) {
        return 0;
    }
    if (format == DB_PIXEL_FORMAT_RGBA16F) {
        const size_t source_row_bytes =
            db_checked_mul_size("db_hash", "source_row_bytes", (size_t)width,
                                DB_RGBA16F_BYTES_PER_PIXEL);
        if (stride_bytes < source_row_bytes) {
            return 0U;
        }
        for (uint32_t row = 0U; row < height; row++) {
            const uint32_t source_row =
                (rows_bottom_to_top != 0) ? (height - 1U - row) : row;
            const uint8_t *const source =
                (const uint8_t *)pixels + ((size_t)source_row * stride_bytes);
            uint8_t *const destination_row =
                destination + ((size_t)row * row_bytes);
            for (uint32_t col = 0U; col < width; col++) {
                const size_t source_base = (size_t)col * 4U;
                const size_t destination_base = (size_t)col * 4U;
                uint16_t channels[3] = {0};
                memcpy(channels, source + (source_base * sizeof(uint16_t)),
                       sizeof(channels));
                destination_row[destination_base] =
                    db_f16_to_u8_clamped(channels[0]);
                destination_row[destination_base + 1U] =
                    db_f16_to_u8_clamped(channels[1]);
                destination_row[destination_base + 2U] =
                    db_f16_to_u8_clamped(channels[2]);
                destination_row[destination_base + 3U] = UINT8_MAX;
            }
        }
    } else {
        if (stride_bytes < row_bytes) {
            return 0U;
        }
        for (uint32_t row = 0U; row < height; row++) {
            const uint32_t source_row =
                (rows_bottom_to_top != 0) ? (height - 1U - row) : row;
            const uint8_t *const source =
                (const uint8_t *)pixels + ((size_t)source_row * stride_bytes);
            uint8_t *const destination_row =
                destination + ((size_t)row * row_bytes);
            memcpy(destination_row, source, row_bytes);
            for (uint32_t col = 0U; col < width; col++) {
                destination_row[((size_t)col * 4U) + 3U] = UINT8_MAX;
            }
        }
    }
    return 1;
}

db_rgba8_pixel_diff_t db_rgba8_pixel_diff(const uint8_t *expected,
                                          const uint8_t *actual, uint32_t width,
                                          uint32_t height) {
    db_rgba8_pixel_diff_t diff = {0};
    size_t row_bytes = 0U;
    if ((expected == NULL) || (actual == NULL) || (width == 0U) ||
        (height == 0U) ||
        (db_try_mul_size((size_t)width, DB_RGBA8_BYTES_PER_PIXEL, &row_bytes) ==
         0) ||
        (db_hash_source_layout_size(DB_PIXEL_FORMAT_RGBA8, width, height,
                                    row_bytes) == 0U)) {
        return diff;
    }
    for (uint32_t y = 0U; y < height; y++) {
        for (uint32_t x = 0U; x < width; x++) {
            const size_t base = (((size_t)y * width) + x) * 4U;
            if (memcmp(expected + base, actual + base, 4U) == 0) {
                continue;
            }
            if (diff.mismatch_count == 0U) {
                diff.first_x = x;
                diff.first_y = y;
                diff.min_x = x;
                diff.max_x = x;
                diff.min_y = y;
                diff.max_y = y;
                memcpy(diff.expected_rgba, expected + base, 4U);
                memcpy(diff.actual_rgba, actual + base, 4U);
                while ((diff.first_component < 4U) &&
                       (diff.expected_rgba[diff.first_component] ==
                        diff.actual_rgba[diff.first_component])) {
                    diff.first_component++;
                }
            } else {
                diff.min_x = DB_MIN(diff.min_x, x);
                diff.max_x = DB_MAX(diff.max_x, x);
                diff.min_y = DB_MIN(diff.min_y, y);
                diff.max_y = DB_MAX(diff.max_y, y);
            }
            diff.mismatch_count++;
        }
    }
    return diff;
}
