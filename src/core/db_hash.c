#include "db_hash.h"

#include <stddef.h>
#include <stdint.h>

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
    const uint8_t *bytes = (const uint8_t *)data;
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

uint64_t db_hash_rgba8_pixels_canonical(const uint8_t *pixels, uint32_t width,
                                        uint32_t height, size_t stride_bytes,
                                        int rows_bottom_to_top) {
    if ((pixels == NULL) || (width == 0U) || (height == 0U)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)width * 4U;
    if (stride_bytes < row_bytes) {
        return 0U;
    }

    uint64_t hash = DB_FNV1A64_OFFSET;
    for (uint32_t row = 0U; row < height; row++) {
        const uint32_t src_row =
            (rows_bottom_to_top != 0) ? (height - 1U - row) : row;
        const size_t src_offset = (size_t)src_row * stride_bytes;
        hash = db_fnv1a64_extend(hash, pixels + src_offset, row_bytes);
    }
    return hash;
}
