#include "db_hash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "db_buffer_convert.h"
#include "db_core.h"

uint32_t db_fold_u64_to_u32(uint64_t value) {
    return (uint32_t)(value ^ (value >> 32U));
}

uint32_t db_mix_u32(uint32_t value) {
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    value *= DB_HASH_MIX_MUL_A;
    value ^= value >> DB_HASH_MIX_SHIFT_B;
    value *= DB_HASH_MIX_MUL_B;
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    return value;
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
    const size_t row_bytes = (size_t)width * 4U;
    if (stride_bytes < row_bytes) {
        return 0U;
    }

    const size_t packed_bytes = row_bytes * (size_t)height;
    if ((rows_bottom_to_top == 0) && (stride_bytes == row_bytes)) {
        return db_fnv_blockhash_u64(pixels, packed_bytes, DB_U32_SALT_PALETTE,
                                    DB_FNV1A64_OFFSET);
    }

    const uint8_t *src_bytes = (const uint8_t *)pixels;
    uint8_t *const canonical_bytes = (uint8_t *)db_alloc_aligned_array_or_fail(
        "db_hash", "rgba8_canonical_bytes", packed_bytes, sizeof(uint8_t),
        DB_CACHELINE_ALIGNMENT_BYTES);
    for (uint32_t row = 0U; row < height; row++) {
        const uint32_t src_row =
            (rows_bottom_to_top != 0U) ? (height - 1U - row) : row;
        const size_t src_offset = (size_t)src_row * stride_bytes;
        const size_t dst_offset = (size_t)row * row_bytes;
        db_copy_bytes(canonical_bytes + dst_offset, src_bytes + src_offset,
                      row_bytes);
    }
    const uint64_t hash = db_fnv_blockhash_u64(
        canonical_bytes, packed_bytes, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    free(canonical_bytes);
    return hash;
}

uint64_t db_hash_rgba32f_pixels_canonical(const float *pixels, uint32_t width,
                                          uint32_t height, size_t stride_bytes,
                                          int rows_bottom_to_top) {
    if ((pixels == NULL) || (width == 0U) || (height == 0U)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)width * 4U * sizeof(float);
    if (stride_bytes < row_bytes) {
        return 0U;
    }

    const size_t packed_bytes = row_bytes * (size_t)height;
    if ((rows_bottom_to_top == 0) && (stride_bytes == row_bytes)) {
        return db_fnv_blockhash_u64((const void *)pixels, packed_bytes,
                                    DB_U32_SALT_ORIGIN_Y, DB_FNV1A64_OFFSET);
    }

    const uint8_t *src_bytes = (const uint8_t *)pixels;
    uint8_t *const canonical_bytes = (uint8_t *)db_alloc_aligned_array_or_fail(
        "db_hash", "rgba32f_canonical_bytes", packed_bytes, sizeof(uint8_t),
        DB_CACHELINE_ALIGNMENT_BYTES);
    for (uint32_t row = 0U; row < height; row++) {
        const uint32_t src_row =
            (rows_bottom_to_top != 0U) ? (height - 1U - row) : row;
        const size_t src_offset = (size_t)src_row * stride_bytes;
        const size_t dst_offset = (size_t)row * row_bytes;
        db_copy_bytes(canonical_bytes + dst_offset, src_bytes + src_offset,
                      row_bytes);
    }
    const uint64_t hash =
        db_fnv_blockhash_u64((const void *)canonical_bytes, packed_bytes,
                             DB_U32_SALT_ORIGIN_Y, DB_FNV1A64_OFFSET);
    free(canonical_bytes);
    return hash;
}
