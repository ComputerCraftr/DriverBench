#ifndef DRIVERBENCH_CORE_DB_HASH_SIMD_INTERNAL_H
#define DRIVERBENCH_CORE_DB_HASH_SIMD_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

enum {
    DB_BLOCK_HASH_BYTES = 64,
    DB_BLOCK_HASH_VECTOR_WIDTH = 4,
    DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 = 8,
    DB_BLOCK_HASH_LANE_0 = 0,
    DB_BLOCK_HASH_LANE_1 = 1,
    DB_BLOCK_HASH_LANE_2 = 2,
    DB_BLOCK_HASH_LANE_3 = 3,
    DB_BLOCK_HASH_LANE_4 = 4,
    DB_BLOCK_HASH_LANE_5 = 5,
    DB_BLOCK_HASH_LANE_6 = 6,
    DB_BLOCK_HASH_LANE_7 = 7,
    DB_BLOCK_HASH_CHUNK_BYTES = 16,
    DB_BLOCK_HASH_SHIFT_4 = 4,
    DB_BLOCK_HASH_SHIFT_8 = 8,
    DB_BLOCK_HASH_SHIFT_12 = 12,
    DB_BLOCK_HASH_SHIFT_16 = 16,
    DB_BLOCK_HASH_SHIFT_24 = 24,
    DB_BLOCK_HASH_SHIFT_32 = 32,
    DB_BLOCK_HASH_SHIFT_40 = 40,
    DB_BLOCK_HASH_SHIFT_48 = 48,
    DB_BLOCK_HASH_SHIFT_56 = 56,
};

enum {
    DB_HASH_X86_KERNEL_SCALAR = 0,
    DB_HASH_X86_KERNEL_SSE41 = 1,
    DB_HASH_X86_KERNEL_AVX2 = 2,
};

int db_hash_select_x86_kernel(void);
void db_fnv1a32_4x64_sse41(const uint8_t *block0, const uint8_t *block1,
                           const uint8_t *block2, const uint8_t *block3,
                           uint32_t seed0, uint32_t seed1, uint32_t seed2,
                           uint32_t seed3, uint32_t out_hashes[4]);
void db_fnv1a32_8x64_avx2(const uint8_t *block0, const uint8_t *block1,
                          const uint8_t *block2, const uint8_t *block3,
                          const uint8_t *block4, const uint8_t *block5,
                          const uint8_t *block6, const uint8_t *block7,
                          uint32_t seed0, uint32_t seed1, uint32_t seed2,
                          uint32_t seed3, uint32_t seed4, uint32_t seed5,
                          uint32_t seed6, uint32_t seed7,
                          uint32_t out_hashes[8]);
void db_x86_pack8_u32_to4_u64_avx2(const uint32_t *in_hashes,
                                   uint64_t *out_packed);
void db_x86_pack4_u32_to2_u64_sse41(const uint32_t *in_hashes,
                                    uint64_t *out_packed);

#endif
