#include "db_hash.h"
#include "db_hash_simd_internal.h"
#include "db_numeric.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "db_core.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef __aarch64__
#include <arm_neon.h>
#endif

// Common hash/finalization helpers used by all backends.
static inline uint64_t db_fnv1a64_update_u64_bytes(uint64_t hash_value,
                                                   uint64_t packed_value) {
    hash_value ^= (uint8_t)packed_value;
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_8);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_16);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_24);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_32);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_40);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_48);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_56);
    hash_value *= DB_FNV1A64_PRIME;
    return hash_value;
}

static inline uint64_t db_fnv1a64_update_u32_bytes(uint64_t hash_value,
                                                   uint32_t packed_value) {
    hash_value ^= (uint8_t)packed_value;
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_8);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_16);
    hash_value *= DB_FNV1A64_PRIME;
    hash_value ^= (uint8_t)(packed_value >> DB_BLOCK_HASH_SHIFT_24);
    hash_value *= DB_FNV1A64_PRIME;
    return hash_value;
}

static inline uint64_t db_fnv_blockhash_finalize(uint64_t hash_value,
                                                 size_t total_bytes,
                                                 uint32_t seed32) {
    hash_value = db_fnv1a64_update_u32_bytes(hash_value, seed32);
    hash_value = db_fnv1a64_update_u64_bytes(hash_value, (uint64_t)total_bytes);
    return hash_value;
}

// Scalar hashing kernels.
static inline uint32_t db_fnv1a32_block64_scalar(const uint8_t *byte_ptr,
                                                 uint32_t seed_xor) {
    uint32_t block_hash = DB_FNV1A32_OFFSET ^ seed_xor;
    for (size_t byte_index = 0U; byte_index < DB_BLOCK_HASH_BYTES;
         byte_index++) {
        block_hash ^= byte_ptr[byte_index];
        block_hash *= DB_FNV1A32_PRIME;
    }
    return block_hash;
}

#ifndef __aarch64__
static inline void db_fnv1a32_4x64_scalar(
    const uint8_t *block0, const uint8_t *block1, const uint8_t *block2,
    const uint8_t *block3, uint32_t seed0, uint32_t seed1, uint32_t seed2,
    uint32_t seed3, uint32_t out_hashes[DB_BLOCK_HASH_VECTOR_WIDTH]) {
    uint32_t hash0 = DB_FNV1A32_OFFSET ^ seed0;
    uint32_t hash1 = DB_FNV1A32_OFFSET ^ seed1;
    uint32_t hash2 = DB_FNV1A32_OFFSET ^ seed2;
    uint32_t hash3 = DB_FNV1A32_OFFSET ^ seed3;

    for (size_t byte_index = 0U; byte_index < DB_BLOCK_HASH_BYTES;
         byte_index++) {
        hash0 ^= block0[byte_index];
        hash0 *= DB_FNV1A32_PRIME;
        hash1 ^= block1[byte_index];
        hash1 *= DB_FNV1A32_PRIME;
        hash2 ^= block2[byte_index];
        hash2 *= DB_FNV1A32_PRIME;
        hash3 ^= block3[byte_index];
        hash3 *= DB_FNV1A32_PRIME;
    }

    out_hashes[DB_BLOCK_HASH_LANE_0] = hash0;
    out_hashes[DB_BLOCK_HASH_LANE_1] = hash1;
    out_hashes[DB_BLOCK_HASH_LANE_2] = hash2;
    out_hashes[DB_BLOCK_HASH_LANE_3] = hash3;
}

static inline void db_fnv1a32_8x64_scalar(
    const uint8_t *block0, const uint8_t *block1, const uint8_t *block2,
    const uint8_t *block3, const uint8_t *block4, const uint8_t *block5,
    const uint8_t *block6, const uint8_t *block7, uint32_t seed0,
    uint32_t seed1, uint32_t seed2, uint32_t seed3, uint32_t seed4,
    uint32_t seed5, uint32_t seed6, uint32_t seed7,
    uint32_t out_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2]) {
    uint32_t hash0 = DB_FNV1A32_OFFSET ^ seed0;
    uint32_t hash1 = DB_FNV1A32_OFFSET ^ seed1;
    uint32_t hash2 = DB_FNV1A32_OFFSET ^ seed2;
    uint32_t hash3 = DB_FNV1A32_OFFSET ^ seed3;
    uint32_t hash4 = DB_FNV1A32_OFFSET ^ seed4;
    uint32_t hash5 = DB_FNV1A32_OFFSET ^ seed5;
    uint32_t hash6 = DB_FNV1A32_OFFSET ^ seed6;
    uint32_t hash7 = DB_FNV1A32_OFFSET ^ seed7;

    for (size_t byte_index = 0U; byte_index < DB_BLOCK_HASH_BYTES;
         byte_index++) {
        hash0 ^= block0[byte_index];
        hash0 *= DB_FNV1A32_PRIME;
        hash1 ^= block1[byte_index];
        hash1 *= DB_FNV1A32_PRIME;
        hash2 ^= block2[byte_index];
        hash2 *= DB_FNV1A32_PRIME;
        hash3 ^= block3[byte_index];
        hash3 *= DB_FNV1A32_PRIME;
        hash4 ^= block4[byte_index];
        hash4 *= DB_FNV1A32_PRIME;
        hash5 ^= block5[byte_index];
        hash5 *= DB_FNV1A32_PRIME;
        hash6 ^= block6[byte_index];
        hash6 *= DB_FNV1A32_PRIME;
        hash7 ^= block7[byte_index];
        hash7 *= DB_FNV1A32_PRIME;
    }

    out_hashes[DB_BLOCK_HASH_LANE_0] = hash0;
    out_hashes[DB_BLOCK_HASH_LANE_1] = hash1;
    out_hashes[DB_BLOCK_HASH_LANE_2] = hash2;
    out_hashes[DB_BLOCK_HASH_LANE_3] = hash3;
    out_hashes[DB_BLOCK_HASH_LANE_4] = hash4;
    out_hashes[DB_BLOCK_HASH_LANE_5] = hash5;
    out_hashes[DB_BLOCK_HASH_LANE_6] = hash6;
    out_hashes[DB_BLOCK_HASH_LANE_7] = hash7;
}

// Scalar packing helpers.
static inline void db_pack4_u32_to2_u64_scalar_ilp(const uint32_t *in_hashes,
                                                   uint64_t *out_packed) {
    out_packed[DB_BLOCK_HASH_LANE_0] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_0] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_1] << 32U);
    out_packed[DB_BLOCK_HASH_LANE_1] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_2] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_3] << 32U);
}

static inline void db_pack8_u32_to4_u64_scalar_ilp(const uint32_t *in_hashes,
                                                   uint64_t *out_packed) {
    out_packed[DB_BLOCK_HASH_LANE_0] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_0] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_1] << 32U);
    out_packed[DB_BLOCK_HASH_LANE_1] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_2] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_3] << 32U);
    out_packed[DB_BLOCK_HASH_LANE_2] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_4] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_5] << 32U);
    out_packed[DB_BLOCK_HASH_LANE_3] =
        (uint64_t)in_hashes[DB_BLOCK_HASH_LANE_6] |
        ((uint64_t)in_hashes[DB_BLOCK_HASH_LANE_7] << 32U);
}
#endif

#ifdef __aarch64__
// NEON hashing kernels.
static inline void db_neon_transpose4x16_u8(uint8x16_t row0, uint8x16_t row1,
                                            uint8x16_t row2, uint8x16_t row3,
                                            uint8x16_t *vec0_out,
                                            uint8x16_t *vec1_out,
                                            uint8x16_t *vec2_out,
                                            uint8x16_t *vec3_out) {
    const uint8x16x2_t zip01 = vzipq_u8(row0, row1);
    const uint8x16x2_t zip23 = vzipq_u8(row2, row3);

    const uint16x8_t part0 = vreinterpretq_u16_u8(zip01.val[0]);
    const uint16x8_t part1 = vreinterpretq_u16_u8(zip01.val[1]);
    const uint16x8_t part2 = vreinterpretq_u16_u8(zip23.val[0]);
    const uint16x8_t part3 = vreinterpretq_u16_u8(zip23.val[1]);

    const uint16x8x2_t wide0 = vzipq_u16(part0, part2);
    const uint16x8x2_t wide1 = vzipq_u16(part1, part3);

    *vec0_out = vreinterpretq_u8_u16(wide0.val[0]);
    *vec1_out = vreinterpretq_u8_u16(wide0.val[1]);
    *vec2_out = vreinterpretq_u8_u16(wide1.val[0]);
    *vec3_out = vreinterpretq_u8_u16(wide1.val[1]);
}

static inline uint32x4_t db_neon_widen4_u8_to_u32(uint8x16_t packed_vec,
                                                  int shift_bytes) {
    uint8x16_t shifted = packed_vec;
    switch (shift_bytes) {
    case 0:
        shifted = packed_vec;
        break;
    case DB_BLOCK_HASH_SHIFT_4:
        shifted = vextq_u8(packed_vec, packed_vec, DB_BLOCK_HASH_SHIFT_4);
        break;
    case DB_BLOCK_HASH_SHIFT_8:
        shifted = vextq_u8(packed_vec, packed_vec, DB_BLOCK_HASH_SHIFT_8);
        break;
    case DB_BLOCK_HASH_SHIFT_12:
        shifted = vextq_u8(packed_vec, packed_vec, DB_BLOCK_HASH_SHIFT_12);
        break;
    default:
        break;
    }
    uint8x8_t low8 = vget_low_u8(shifted);
    uint16x8_t low16 = vmovl_u8(low8);
    return vmovl_u16(vget_low_u16(low16));
}

static inline uint32x4_t db_neon_fnv1a32_update_16steps(uint32x4_t lane_hash,
                                                        uint8x16_t packed_vec) {
    lane_hash = veorq_u32(lane_hash, db_neon_widen4_u8_to_u32(packed_vec, 0));
    lane_hash = vmulq_n_u32(lane_hash, DB_FNV1A32_PRIME);

    lane_hash = veorq_u32(
        lane_hash, db_neon_widen4_u8_to_u32(packed_vec, DB_BLOCK_HASH_SHIFT_4));
    lane_hash = vmulq_n_u32(lane_hash, DB_FNV1A32_PRIME);

    lane_hash = veorq_u32(
        lane_hash, db_neon_widen4_u8_to_u32(packed_vec, DB_BLOCK_HASH_SHIFT_8));
    lane_hash = vmulq_n_u32(lane_hash, DB_FNV1A32_PRIME);

    lane_hash =
        veorq_u32(lane_hash,
                  db_neon_widen4_u8_to_u32(packed_vec, DB_BLOCK_HASH_SHIFT_12));
    lane_hash = vmulq_n_u32(lane_hash, DB_FNV1A32_PRIME);

    return lane_hash;
}

static inline void db_fnv1a32_4x64_neon(
    const uint8_t *block0, const uint8_t *block1, const uint8_t *block2,
    const uint8_t *block3, uint32_t seed0, uint32_t seed1, uint32_t seed2,
    uint32_t seed3, uint32_t out_hashes[DB_BLOCK_HASH_VECTOR_WIDTH]) {
    uint32x4_t lane_hash = (uint32x4_t){
        (DB_FNV1A32_OFFSET ^ seed0),
        (DB_FNV1A32_OFFSET ^ seed1),
        (DB_FNV1A32_OFFSET ^ seed2),
        (DB_FNV1A32_OFFSET ^ seed3),
    };

    for (size_t chunk_index = 0U;
         chunk_index < (DB_BLOCK_HASH_BYTES / DB_BLOCK_HASH_CHUNK_BYTES);
         chunk_index++) {
        const ptrdiff_t chunk_offset =
            (ptrdiff_t)(chunk_index * DB_BLOCK_HASH_CHUNK_BYTES);
        uint8x16_t row0 = vld1q_u8(block0 + chunk_offset);
        uint8x16_t row1 = vld1q_u8(block1 + chunk_offset);
        uint8x16_t row2 = vld1q_u8(block2 + chunk_offset);
        uint8x16_t row3 = vld1q_u8(block3 + chunk_offset);

        uint8x16_t vec0 = vdupq_n_u8(0U);
        uint8x16_t vec1 = vdupq_n_u8(0U);
        uint8x16_t vec2 = vdupq_n_u8(0U);
        uint8x16_t vec3 = vdupq_n_u8(0U);
        db_neon_transpose4x16_u8(row0, row1, row2, row3, &vec0, &vec1, &vec2,
                                 &vec3);

        lane_hash = db_neon_fnv1a32_update_16steps(lane_hash, vec0);
        lane_hash = db_neon_fnv1a32_update_16steps(lane_hash, vec1);
        lane_hash = db_neon_fnv1a32_update_16steps(lane_hash, vec2);
        lane_hash = db_neon_fnv1a32_update_16steps(lane_hash, vec3);
    }

    out_hashes[DB_BLOCK_HASH_LANE_0] =
        vgetq_lane_u32(lane_hash, DB_BLOCK_HASH_LANE_0);
    out_hashes[DB_BLOCK_HASH_LANE_1] =
        vgetq_lane_u32(lane_hash, DB_BLOCK_HASH_LANE_1);
    out_hashes[DB_BLOCK_HASH_LANE_2] =
        vgetq_lane_u32(lane_hash, DB_BLOCK_HASH_LANE_2);
    out_hashes[DB_BLOCK_HASH_LANE_3] =
        vgetq_lane_u32(lane_hash, DB_BLOCK_HASH_LANE_3);
}

// NEON packing helpers.
static inline void db_neon_pack4_u32_to2_u64(const uint32_t *in_hashes,
                                             uint64_t *out_packed) {
    const uint32x4_t hashes_vec = vld1q_u32(in_hashes);
    const uint64x2_t packed_vec = vreinterpretq_u64_u32(hashes_vec);
    vst1q_u64(out_packed, packed_vec);
}
#endif

static inline uint64_t db_fnv_blockhash_u64_internal(
    const void *data_ptr, size_t total_bytes, uint32_t seed32, uint64_t seed64,
    uint32_t *out_block_hashes, size_t *out_num_blocks) {
    const uint8_t *byte_ptr = (const uint8_t *)data_ptr;
    const size_t full_blocks = total_bytes / DB_BLOCK_HASH_BYTES;
    const size_t tail_bytes = total_bytes % DB_BLOCK_HASH_BYTES;
    const size_t block_count = full_blocks + (size_t)DB_BOOL(tail_bytes);

    if (out_num_blocks != NULL) {
        *out_num_blocks = block_count;
    }

    const uint64_t init_hash = seed64;
    uint64_t final_hash = init_hash;
    size_t block_index = 0U;

#if defined(__x86_64__) || defined(__i386__)
    const int x86_kernel = db_hash_select_x86_kernel();
    if (x86_kernel == DB_HASH_X86_KERNEL_AVX2) {
        for (; block_index + DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 <= full_blocks;
             block_index += DB_BLOCK_HASH_VECTOR_WIDTH_AVX2) {
            alignas(32) uint32_t lane_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2];
            const uint8_t *block_ptrs[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2];
            uint32_t seeds[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2];
            for (size_t lane_index = 0U;
                 lane_index < DB_BLOCK_HASH_VECTOR_WIDTH_AVX2; lane_index++) {
                block_ptrs[lane_index] =
                    byte_ptr +
                    ((block_index + lane_index) * DB_BLOCK_HASH_BYTES);
                seeds[lane_index] =
                    seed32 ^ db_fold_u64_to_u32((uint64_t)block_index +
                                                (uint64_t)lane_index);
            }
            db_fnv1a32_8x64_avx2(
                block_ptrs[DB_BLOCK_HASH_LANE_0],
                block_ptrs[DB_BLOCK_HASH_LANE_1],
                block_ptrs[DB_BLOCK_HASH_LANE_2],
                block_ptrs[DB_BLOCK_HASH_LANE_3],
                block_ptrs[DB_BLOCK_HASH_LANE_4],
                block_ptrs[DB_BLOCK_HASH_LANE_5],
                block_ptrs[DB_BLOCK_HASH_LANE_6],
                block_ptrs[DB_BLOCK_HASH_LANE_7], seeds[DB_BLOCK_HASH_LANE_0],
                seeds[DB_BLOCK_HASH_LANE_1], seeds[DB_BLOCK_HASH_LANE_2],
                seeds[DB_BLOCK_HASH_LANE_3], seeds[DB_BLOCK_HASH_LANE_4],
                seeds[DB_BLOCK_HASH_LANE_5], seeds[DB_BLOCK_HASH_LANE_6],
                seeds[DB_BLOCK_HASH_LANE_7], lane_hashes);

            if (out_block_hashes != NULL) {
                for (size_t lane_index = 0U;
                     lane_index < DB_BLOCK_HASH_VECTOR_WIDTH_AVX2;
                     lane_index++) {
                    out_block_hashes[block_index + lane_index] =
                        lane_hashes[lane_index];
                }
            }

            alignas(32)
                uint64_t packed_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U];
            db_x86_pack8_u32_to4_u64_avx2(lane_hashes, packed_hashes);
            for (size_t pair_index = 0U;
                 pair_index < (DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U);
                 pair_index++) {
                final_hash = db_fnv1a64_update_u64_bytes(
                    final_hash, packed_hashes[pair_index]);
            }
        }
    }
#endif

    for (; block_index + DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 <= full_blocks;
         block_index += DB_BLOCK_HASH_VECTOR_WIDTH_AVX2) {
        alignas(32) uint32_t lane_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2];
        const uint8_t *block0 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_0) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block1 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_1) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block2 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_2) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block3 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_3) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block4 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_4) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block5 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_5) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block6 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_6) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block7 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_7) * DB_BLOCK_HASH_BYTES);

        const uint32_t seed0 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_0);
        const uint32_t seed1 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_1);
        const uint32_t seed2 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_2);
        const uint32_t seed3 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_3);
        const uint32_t seed4 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_4);
        const uint32_t seed5 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_5);
        const uint32_t seed6 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_6);
        const uint32_t seed7 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_7);

#ifdef __aarch64__
        uint32_t half0[DB_BLOCK_HASH_VECTOR_WIDTH];
        uint32_t half1[DB_BLOCK_HASH_VECTOR_WIDTH];
        db_fnv1a32_4x64_neon(block0, block1, block2, block3, seed0, seed1,
                             seed2, seed3, half0);
        db_fnv1a32_4x64_neon(block4, block5, block6, block7, seed4, seed5,
                             seed6, seed7, half1);
        memcpy(lane_hashes, half0,
               DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
        memcpy(lane_hashes + DB_BLOCK_HASH_VECTOR_WIDTH, half1,
               DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
#elif defined(__x86_64__) || defined(__i386__)
        if (x86_kernel == DB_HASH_X86_KERNEL_SSE41 ||
            x86_kernel == DB_HASH_X86_KERNEL_AVX2) {
            uint32_t half0[DB_BLOCK_HASH_VECTOR_WIDTH];
            uint32_t half1[DB_BLOCK_HASH_VECTOR_WIDTH];
            db_fnv1a32_4x64_sse41(block0, block1, block2, block3, seed0, seed1,
                                  seed2, seed3, half0);
            db_fnv1a32_4x64_sse41(block4, block5, block6, block7, seed4, seed5,
                                  seed6, seed7, half1);
            memcpy(lane_hashes, half0,
                   DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
            memcpy(lane_hashes + DB_BLOCK_HASH_VECTOR_WIDTH, half1,
                   DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
        } else {
            db_fnv1a32_8x64_scalar(block0, block1, block2, block3, block4,
                                   block5, block6, block7, seed0, seed1, seed2,
                                   seed3, seed4, seed5, seed6, seed7,
                                   lane_hashes);
        }
#else
        db_fnv1a32_8x64_scalar(block0, block1, block2, block3, block4, block5,
                               block6, block7, seed0, seed1, seed2, seed3,
                               seed4, seed5, seed6, seed7, lane_hashes);
#endif

        if (out_block_hashes != NULL) {
            for (size_t lane = 0U; lane < DB_BLOCK_HASH_VECTOR_WIDTH_AVX2;
                 lane++) {
                out_block_hashes[block_index + lane] = lane_hashes[lane];
            }
        }

#if defined(__x86_64__) || defined(__i386__)
        if (x86_kernel == DB_HASH_X86_KERNEL_AVX2) {
            alignas(32)
                uint64_t packed_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U];
            db_x86_pack8_u32_to4_u64_avx2(lane_hashes, packed_hashes);
            for (size_t pair_index = 0U;
                 pair_index < (DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U);
                 pair_index++) {
                final_hash = db_fnv1a64_update_u64_bytes(
                    final_hash, packed_hashes[pair_index]);
            }
        } else if (x86_kernel == DB_HASH_X86_KERNEL_SSE41) {
            alignas(16) uint64_t packed0[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            alignas(16) uint64_t packed1[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_x86_pack4_u32_to2_u64_sse41(lane_hashes, packed0);
            db_x86_pack4_u32_to2_u64_sse41(
                lane_hashes + DB_BLOCK_HASH_VECTOR_WIDTH, packed1);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed0[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed0[DB_BLOCK_HASH_LANE_1]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed1[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed1[DB_BLOCK_HASH_LANE_1]);
        } else {
            uint64_t packed_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U];
            db_pack8_u32_to4_u64_scalar_ilp(lane_hashes, packed_hashes);
            for (size_t pair_index = 0U;
                 pair_index < (DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U);
                 pair_index++) {
                final_hash = db_fnv1a64_update_u64_bytes(
                    final_hash, packed_hashes[pair_index]);
            }
        }
#elifdef __aarch64__
        {
            uint64_t packed0[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            uint64_t packed1[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_neon_pack4_u32_to2_u64(lane_hashes, packed0);
            db_neon_pack4_u32_to2_u64(lane_hashes + DB_BLOCK_HASH_VECTOR_WIDTH,
                                      packed1);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed0[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed0[DB_BLOCK_HASH_LANE_1]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed1[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed1[DB_BLOCK_HASH_LANE_1]);
        }
#else
        {
            uint64_t packed_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U];
            db_pack8_u32_to4_u64_scalar_ilp(lane_hashes, packed_hashes);
            for (size_t pair_index = 0U;
                 pair_index < (DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U);
                 pair_index++) {
                final_hash = db_fnv1a64_update_u64_bytes(
                    final_hash, packed_hashes[pair_index]);
            }
        }
#endif
    }

    for (; block_index + DB_BLOCK_HASH_VECTOR_WIDTH <= full_blocks;
         block_index += DB_BLOCK_HASH_VECTOR_WIDTH) {
        alignas(16) uint32_t lane_hashes[DB_BLOCK_HASH_VECTOR_WIDTH];
        const uint8_t *block0 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_0) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block1 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_1) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block2 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_2) * DB_BLOCK_HASH_BYTES);
        const uint8_t *block3 =
            byte_ptr +
            ((block_index + DB_BLOCK_HASH_LANE_3) * DB_BLOCK_HASH_BYTES);

        const uint32_t seed0 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_0);
        const uint32_t seed1 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_1);
        const uint32_t seed2 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_2);
        const uint32_t seed3 =
            seed32 ^
            db_fold_u64_to_u32((uint64_t)block_index + DB_BLOCK_HASH_LANE_3);

#ifdef __aarch64__
        db_fnv1a32_4x64_neon(block0, block1, block2, block3, seed0, seed1,
                             seed2, seed3, lane_hashes);
#elif defined(__x86_64__) || defined(__i386__)
        if (x86_kernel == DB_HASH_X86_KERNEL_SSE41 ||
            x86_kernel == DB_HASH_X86_KERNEL_AVX2) {
            db_fnv1a32_4x64_sse41(block0, block1, block2, block3, seed0, seed1,
                                  seed2, seed3, lane_hashes);
        } else {
            db_fnv1a32_4x64_scalar(block0, block1, block2, block3, seed0, seed1,
                                   seed2, seed3, lane_hashes);
        }
#else
        db_fnv1a32_4x64_scalar(block0, block1, block2, block3, seed0, seed1,
                               seed2, seed3, lane_hashes);
#endif

        if (out_block_hashes != NULL) {
            memcpy(out_block_hashes + block_index, lane_hashes,
                   DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
        }

#if defined(__x86_64__) || defined(__i386__)
        if (x86_kernel == DB_HASH_X86_KERNEL_SSE41 ||
            x86_kernel == DB_HASH_X86_KERNEL_AVX2) {
            alignas(16) uint64_t packed[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_x86_pack4_u32_to2_u64_sse41(lane_hashes, packed);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed[DB_BLOCK_HASH_LANE_1]);
        } else {
            uint64_t packed[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_pack4_u32_to2_u64_scalar_ilp(lane_hashes, packed);
            const uint64_t packed0 = packed[DB_BLOCK_HASH_LANE_0];
            const uint64_t packed1 = packed[DB_BLOCK_HASH_LANE_1];
            final_hash = db_fnv1a64_update_u64_bytes(final_hash, packed0);
            final_hash = db_fnv1a64_update_u64_bytes(final_hash, packed1);
        }
#elifdef __aarch64__
        {
            uint64_t packed[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_neon_pack4_u32_to2_u64(lane_hashes, packed);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed[DB_BLOCK_HASH_LANE_0]);
            final_hash = db_fnv1a64_update_u64_bytes(
                final_hash, packed[DB_BLOCK_HASH_LANE_1]);
        }
#else
        {
            uint64_t packed[DB_BLOCK_HASH_VECTOR_WIDTH / 2U];
            db_pack4_u32_to2_u64_scalar_ilp(lane_hashes, packed);
            const uint64_t packed0 = packed[DB_BLOCK_HASH_LANE_0];
            const uint64_t packed1 = packed[DB_BLOCK_HASH_LANE_1];
            final_hash = db_fnv1a64_update_u64_bytes(final_hash, packed0);
            final_hash = db_fnv1a64_update_u64_bytes(final_hash, packed1);
        }
#endif
    }

    for (; block_index < full_blocks; block_index++) {
        const uint32_t block_hash = db_fnv1a32_block64_scalar(
            byte_ptr + (block_index * DB_BLOCK_HASH_BYTES),
            seed32 ^ db_fold_u64_to_u32((uint64_t)block_index));
        if (out_block_hashes != NULL) {
            out_block_hashes[block_index] = block_hash;
        }
        final_hash = db_fnv1a64_update_u32_bytes(final_hash, block_hash);
    }

    if (tail_bytes > 0U) {
        uint8_t tail_block[DB_BLOCK_HASH_BYTES] = {0U};
        memcpy(tail_block, byte_ptr + (full_blocks * DB_BLOCK_HASH_BYTES),
               tail_bytes);
        const uint32_t tail_hash = db_fnv1a32_block64_scalar(
            tail_block, seed32 ^ db_fold_u64_to_u32((uint64_t)full_blocks));
        if (out_block_hashes != NULL) {
            out_block_hashes[full_blocks] = tail_hash;
        }
        final_hash = db_fnv1a64_update_u32_bytes(final_hash, tail_hash);
    }

    return db_fnv_blockhash_finalize(final_hash, total_bytes, seed32);
}

uint64_t db_fnv_blockhash_u64(const void *data, size_t len_bytes,
                              uint32_t seed32, uint64_t seed64) {
    if (len_bytes == 0U) {
        return db_fnv_blockhash_finalize(seed64, 0U, seed32);
    }
    if (data == NULL) {
        db_failf("db_hash",
                 "db_fnv_blockhash_u64 received NULL data with len_bytes=%zu",
                 len_bytes);
    }
    return db_fnv_blockhash_u64_internal(data, len_bytes, seed32, seed64, NULL,
                                         NULL);
}
