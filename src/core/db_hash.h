#ifndef DRIVERBENCH_DB_HASH_H
#define DRIVERBENCH_DB_HASH_H

#include <stddef.h>
#include <stdint.h>

#define DB_HASH_MIX_SHIFT_A 16U
#define DB_HASH_MIX_SHIFT_B 15U
#define DB_HASH_MIX_MUL_A 0x7FEB352DU
#define DB_HASH_MIX_MUL_B 0x846CA68BU
#define DB_U32_GOLDEN_RATIO 0x9E3779B9U
#define DB_U32_SALT_COLOR_R 0x27D4EB2FU
#define DB_U32_SALT_COLOR_G 0x165667B1U
#define DB_U32_SALT_COLOR_B 0x85EBCA77U
#define DB_U32_SALT_ORIGIN_Y 0xC2B2AE35U
#define DB_U32_SALT_PALETTE 0xA511E9B3U
#define DB_FNV1A64_OFFSET UINT64_C(1469598103934665603)
#define DB_FNV1A64_PRIME UINT64_C(1099511628211)

uint32_t db_fold_u64_to_u32(uint64_t value);
uint32_t db_mix_u32(uint32_t value);
uint64_t db_fnv1a64_extend(uint64_t hash, const void *data, size_t size);
uint64_t db_fnv1a64_bytes(const void *data, size_t size);
uint64_t db_fnv1a64_mix_u64(uint64_t hash, uint64_t value);
uint64_t db_hash_rgba8_pixels_canonical(const uint8_t *pixels, uint32_t width,
                                        uint32_t height, size_t stride_bytes,
                                        int rows_bottom_to_top);

#endif
