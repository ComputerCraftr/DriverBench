#ifndef DRIVERBENCH_DB_BUFFER_CONVERT_H
#define DRIVERBENCH_DB_BUFFER_CONVERT_H

#include <stddef.h>
#include <stdint.h>

void db_copy_bytes(void *dst, const void *src, size_t byte_count);
void db_copy_f32_buffer(float *dst, const float *src, size_t element_count);
void db_copy_u32_buffer(uint32_t *dst, const uint32_t *src,
                        size_t element_count);
void db_copy_rows_u8(uint8_t *dst, size_t dst_stride_bytes, const uint8_t *src,
                     size_t src_stride_bytes, size_t row_bytes,
                     uint32_t row_count);
void db_convert_rgba8_to_xrgb8888_rows(uint32_t *dst, size_t dst_stride_pixels,
                                       const uint32_t *src,
                                       size_t src_stride_pixels,
                                       uint32_t width_pixels,
                                       uint32_t height_rows);

#endif
