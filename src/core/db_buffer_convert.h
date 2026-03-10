#ifndef DRIVERBENCH_DB_BUFFER_CONVERT_H
#define DRIVERBENCH_DB_BUFFER_CONVERT_H

#include <stddef.h>
#include <stdint.h>

void db_copy_bytes(void *dst, const void *src, size_t byte_count);
void db_move_bytes(void *dst, const void *src, size_t byte_count);
void db_copy_f32_vec2(float dst[2], const float src[2]);
void db_copy_f32_rgb3(float dst[3], const float src[3]);
void db_copy_f32_rgba4(float dst[4], const float src[4]);
void db_copy_f64_rgb3(double dst[3], const double src[3]);
void db_copy_u32_rgb3(uint32_t dst[3], const uint32_t src[3]);
void db_copy_u32_buffer(uint32_t *dst, const uint32_t *src,
                        size_t element_count);
void db_fill_u32_buffer(uint32_t *dst, uint32_t element_count,
                        uint32_t fill_value);
void db_fill_rgba8_byte_pattern(uint8_t *dst, uint32_t pixel_count,
                                const uint8_t rgba_u8[4]);
void db_fill_rgba16f_buffer(uint16_t *dst, uint32_t pixel_count,
                            const uint16_t rgba_f16[4]);
void db_unpack_rgba8888_rgb_u8(uint32_t packed_rgba, uint32_t rgb_u8_out[3]);
void db_unpack_rgba8888_rgb01(uint32_t packed_rgba, double rgb01_out[3]);
uint32_t db_pack_xrgb8888_from_rgba8888(uint32_t packed_rgba);
uint32_t db_pack_xrgb8888_from_rgb16f3(const uint16_t *rgb16f3);
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
