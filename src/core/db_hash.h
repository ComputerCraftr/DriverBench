#ifndef DRIVERBENCH_DB_HASH_H
#define DRIVERBENCH_DB_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "db_render_types.h"

#define DB_U32_GOLDEN_RATIO 0x9E3779B9U
#define DB_U32_SALT_COLOR_R 0x27D4EB2FU
#define DB_U32_SALT_COLOR_G 0x165667B1U
#define DB_U32_SALT_COLOR_B 0x85EBCA77U
#define DB_U32_SALT_ORIGIN_Y 0xC2B2AE35U
#define DB_U32_SALT_PALETTE 0xA511E9B3U
#define DB_FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define DB_FNV1A64_PRIME UINT64_C(1099511628211)

typedef struct {
    size_t mismatch_count;
    uint32_t first_x;
    uint32_t first_y;
    uint32_t min_x;
    uint32_t min_y;
    uint32_t max_x;
    uint32_t max_y;
    uint8_t expected_rgba[4];
    uint8_t actual_rgba[4];
} db_rgba8_pixel_diff_t;

uint64_t db_fnv1a64_extend(uint64_t hash, const void *data, size_t size);
uint64_t db_fnv1a64_bytes(const void *data, size_t size);
uint64_t db_fnv1a64_mix_u64(uint64_t hash, uint64_t value);
// Versioned, domain-separated FNV-1a64 tree hash. This is intentionally not
// equivalent to serial byte-wise FNV-1a64 over the original input stream.
#define DB_FNV1A64_TREE_ALGORITHM "fnv1a64_tree_v1"
#define DB_FNV1A64_SERIAL_ALGORITHM "fnv1a64_serial_v1"
uint64_t db_fnv1a64_tree(const void *data, size_t len_bytes, uint32_t domain,
                         uint64_t initial_hash);
uint64_t db_hash_rgba8_pixels_canonical(const void *pixels, uint32_t width,
                                        uint32_t height, size_t stride_bytes,
                                        int rows_bottom_to_top);
// Normalizes an SDR presentation readback to logical top-down opaque RGBA8.
// The source bytes are already in the resolved SDR transfer representation.
uint64_t db_hash_sdr_framebuffer_rgba8_canonical(
    const void *pixels, uint32_t framebuffer_width, uint32_t framebuffer_height,
    size_t stride_bytes, int rows_bottom_to_top, uint32_t canonical_width,
    uint32_t canonical_height);
uint64_t db_hash_rgba16f_pixels_canonical(const uint16_t *pixels,
                                          uint32_t width, uint32_t height,
                                          size_t stride_bytes,
                                          int rows_bottom_to_top);
uint64_t db_hash_working_rgba8(const void *pixels, db_pixel_format_t format,
                               uint32_t width, uint32_t height,
                               size_t stride_bytes, int rows_bottom_to_top);
int db_working_rgba8_canonicalize(const void *pixels, db_pixel_format_t format,
                                  uint32_t width, uint32_t height,
                                  size_t stride_bytes, int rows_bottom_to_top,
                                  uint8_t *destination,
                                  size_t destination_size);
db_rgba8_pixel_diff_t db_rgba8_pixel_diff(const uint8_t *expected,
                                          const uint8_t *actual, uint32_t width,
                                          uint32_t height);

#endif
