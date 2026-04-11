#ifndef DRIVERBENCH_DB_BUFFER_CONVERT_H
#define DRIVERBENCH_DB_BUFFER_CONVERT_H

#include <stddef.h>
#include <stdint.h>

uint8_t db_f16_to_u8_clamped(uint16_t value);

void db_fill_u32_buffer(uint32_t *dst, uint32_t element_count,
                        uint32_t fill_value);
void db_fill_u64_buffer(uint64_t *dst, uint32_t element_count,
                        uint64_t fill_value);
void db_fill_rgba8_byte_pattern(uint8_t *dst, uint32_t pixel_count,
                                const uint8_t *rgba_u8);
void db_fill_rgba16f_buffer(void *dst, uint32_t pixel_count,
                            const uint16_t *rgba_f16);
void db_unpack_rgba8888_rgb_u8(uint32_t packed_rgba, uint32_t *rgb_u8_out);
void db_unpack_rgba8888_rgb01(uint32_t packed_rgba, double *rgb01_out);
uint32_t db_pack_xrgb8888_from_rgba8888(uint32_t packed_rgba);
uint32_t db_pack_xrgb8888_from_rgb16f3(const uint16_t *rgb16f3);
double db_hdr10_pq_encode_nits(double luminance_nits);
void db_hdr10_linear_srgb_to_bt2020_pq(const double *linear_srgb,
                                       double *encoded_bt2020_pq);
uint32_t db_pack_xrgb2101010_from_linear_srgb(const double *linear_srgb);
uint32_t db_pack_xrgb2101010_from_rgba8888(uint32_t packed_rgba);
uint32_t db_pack_xrgb2101010_from_rgb16f3(const uint16_t *rgb16f3);
uint32_t db_pack_rgb10a2_bt2020_pq_from_rgba8888(uint32_t packed_rgba);
uint32_t db_pack_rgb10a2_bt2020_pq_from_rgb16f3(const uint16_t *rgb16f3);
void db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(
    uint32_t *dst, const uint32_t *src, size_t src_stride_pixels,
    uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count);
void db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(
    uint32_t *dst, const uint16_t *src, size_t src_stride_pixels,
    uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count);
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
