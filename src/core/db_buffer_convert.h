#ifndef DRIVERBENCH_DB_BUFFER_CONVERT_H
#define DRIVERBENCH_DB_BUFFER_CONVERT_H

#include <stddef.h>
#include <stdint.h>

void db_copy_bytes(void *dst, const void *src, size_t byte_count);
void db_move_bytes(void *dst, const void *src, size_t byte_count);
void db_copy_u32_buffer(uint32_t *dst, const uint32_t *src,
                        size_t element_count);
void db_fill_u32_buffer(uint32_t *dst, uint32_t element_count,
                        uint32_t fill_value);
void db_fill_rgba8_byte_pattern(uint8_t *dst, uint32_t pixel_count, uint8_t red,
                                uint8_t green, uint8_t blue, uint8_t alpha);
void db_fill_rgba16f_buffer(uint16_t *dst, uint32_t pixel_count, uint16_t red,
                            uint16_t green, uint16_t blue, uint16_t alpha);
void db_convert_rgba8_to_xrgb8888_block(uint32_t *dst, size_t dst_stride_pixels,
                                        const uint32_t *src,
                                        size_t src_stride_pixels,
                                        uint32_t row_start, uint32_t row_count,
                                        uint32_t col_start, uint32_t col_count);
void db_convert_rgba16f_to_xrgb8888_block(
    uint32_t *dst, size_t dst_stride_pixels, const uint16_t *src,
    size_t src_stride_pixels, uint32_t row_start, uint32_t row_count,
    uint32_t col_start, uint32_t col_count);
void db_convert_rgba16f_to_rgba8888_block(
    uint32_t *dst, size_t dst_stride_pixels, const uint16_t *src,
    size_t src_stride_pixels, uint32_t row_start, uint32_t row_count,
    uint32_t col_start, uint32_t col_count, uint32_t alpha_u8);

#endif
