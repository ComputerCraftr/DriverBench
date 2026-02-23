#include "db_buffer_convert.h"

#include <stdint.h>
#include <string.h>

void db_copy_bytes(void *dst, const void *src, size_t byte_count) {
    if ((dst == NULL) || (src == NULL) || (byte_count == 0U)) {
        return;
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, src, byte_count);
}

void db_copy_f32_buffer(float *dst, const float *src, size_t element_count) {
    db_copy_bytes(dst, src, element_count * sizeof(float));
}

void db_copy_u32_buffer(uint32_t *dst, const uint32_t *src,
                        size_t element_count) {
    db_copy_bytes(dst, src, element_count * sizeof(uint32_t));
}

void db_copy_rows_u8(uint8_t *dst, size_t dst_stride_bytes, const uint8_t *src,
                     size_t src_stride_bytes, size_t row_bytes,
                     uint32_t row_count) {
    if ((dst == NULL) || (src == NULL) || (row_bytes == 0U) ||
        (row_count == 0U)) {
        return;
    }
    for (uint32_t row = 0U; row < row_count; row++) {
        db_copy_bytes(dst + ((size_t)row * dst_stride_bytes),
                      src + ((size_t)row * src_stride_bytes), row_bytes);
    }
}

void db_convert_rgba8_to_xrgb8888_rows(uint32_t *dst, size_t dst_stride_pixels,
                                       const uint32_t *src,
                                       size_t src_stride_pixels,
                                       uint32_t width_pixels,
                                       uint32_t height_rows) {
    if ((dst == NULL) || (src == NULL) || (width_pixels == 0U) ||
        (height_rows == 0U)) {
        return;
    }

    for (uint32_t row = 0U; row < height_rows; row++) {
        const uint32_t *src_row = src + ((size_t)row * src_stride_pixels);
        uint32_t *dst_row = dst + ((size_t)row * dst_stride_pixels);
        for (uint32_t col = 0U; col < width_pixels; col++) {
            const uint32_t rgba = src_row[col];
            const uint32_t red = (rgba & 0x000000FFU);
            const uint32_t green = (rgba & 0x0000FF00U);
            const uint32_t blue = (rgba & 0x00FF0000U);
            dst_row[col] = (red << 16U) | green | (blue >> 16U);
        }
    }
}
