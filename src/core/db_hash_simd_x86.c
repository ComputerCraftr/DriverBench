#if defined(__x86_64__) || defined(__i386__)
#include "db_core.h"
#include "db_hash.h"
#include "db_hash_simd_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#define DB_HASH_TARGET_SSE41 __attribute__((target("sse4.1")))
#define DB_HASH_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define DB_HASH_TARGET_SSE41
#define DB_HASH_TARGET_AVX2
#endif

// x86 hashing kernels.
static inline int db_hash_ptr_aligned(const void *ptr, size_t alignment) {
    return (((uintptr_t)ptr) % alignment) == 0U;
}

DB_HASH_TARGET_SSE41 static inline const __m128i *
db_x86_assume_aligned128(const void *ptr) {
    return (const __m128i *)DB_ASSUME_ALIGNED(ptr, 16U);
}

DB_HASH_TARGET_AVX2 static inline const __m256i *
db_x86_assume_aligned256(const void *ptr) {
    return (const __m256i *)DB_ASSUME_ALIGNED(ptr, 32U);
}

DB_HASH_TARGET_SSE41 static inline void
db_x86_store128_aligned_u32x4(uint32_t out_hashes[DB_BLOCK_HASH_VECTOR_WIDTH],
                              __m128i lane_hash) {
    _mm_store_si128((__m128i *)DB_ASSUME_ALIGNED(out_hashes, 16U), lane_hash);
}

DB_HASH_TARGET_AVX2 static inline __m256i db_x86_load256_aligned_u32x8(
    const uint32_t in_hashes[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2]) {
    return _mm256_load_si256(db_x86_assume_aligned256(in_hashes));
}

DB_HASH_TARGET_AVX2 static inline void db_x86_store256_aligned_u64x4(
    uint64_t out_packed[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2 / 2U],
    __m256i hashes_vec) {
    _mm256_store_si256((__m256i *)DB_ASSUME_ALIGNED(out_packed, 32U),
                       hashes_vec);
}

DB_HASH_TARGET_SSE41 static inline __m128i db_x86_load128_any(const void *ptr) {
    if (db_hash_ptr_aligned(ptr, 16U) != 0) {
        return _mm_load_si128(db_x86_assume_aligned128(ptr));
    }
    return _mm_loadu_si128((const __m128i *)ptr);
}

DB_HASH_TARGET_SSE41 static inline void
db_x86_transpose4x16_u8(__m128i row0, __m128i row1, __m128i row2, __m128i row3,
                        __m128i *vec0_out, __m128i *vec1_out, __m128i *vec2_out,
                        __m128i *vec3_out) {
    __m128i stage0 = _mm_unpacklo_epi8(row0, row1);
    __m128i stage1 = _mm_unpackhi_epi8(row0, row1);
    __m128i stage2 = _mm_unpacklo_epi8(row2, row3);
    __m128i stage3 = _mm_unpackhi_epi8(row2, row3);

    __m128i wide0 = _mm_unpacklo_epi16(stage0, stage2);
    __m128i wide1 = _mm_unpackhi_epi16(stage0, stage2);
    __m128i wide2 = _mm_unpacklo_epi16(stage1, stage3);
    __m128i wide3 = _mm_unpackhi_epi16(stage1, stage3);

    *vec0_out = wide0;
    *vec1_out = wide1;
    *vec2_out = wide2;
    *vec3_out = wide3;
}

DB_HASH_TARGET_SSE41 static inline __m128i
db_x86_widen4_u8_to_u32(__m128i packed_bytes) {
    return _mm_cvtepu8_epi32(packed_bytes);
}

DB_HASH_TARGET_SSE41 static inline __m128i
db_x86_fnv1a32_update_16steps(__m128i lane_hash, __m128i packed_vec,
                              __m128i prime_vec) {
    lane_hash = _mm_xor_si128(lane_hash, db_x86_widen4_u8_to_u32(packed_vec));
    lane_hash = _mm_mullo_epi32(lane_hash, prime_vec);

    __m128i shift1 = _mm_srli_si128(packed_vec, DB_BLOCK_HASH_SHIFT_4);
    lane_hash = _mm_xor_si128(lane_hash, db_x86_widen4_u8_to_u32(shift1));
    lane_hash = _mm_mullo_epi32(lane_hash, prime_vec);

    __m128i shift2 = _mm_srli_si128(packed_vec, DB_BLOCK_HASH_SHIFT_8);
    lane_hash = _mm_xor_si128(lane_hash, db_x86_widen4_u8_to_u32(shift2));
    lane_hash = _mm_mullo_epi32(lane_hash, prime_vec);

    __m128i shift3 = _mm_srli_si128(packed_vec, DB_BLOCK_HASH_SHIFT_12);
    lane_hash = _mm_xor_si128(lane_hash, db_x86_widen4_u8_to_u32(shift3));
    lane_hash = _mm_mullo_epi32(lane_hash, prime_vec);

    return lane_hash;
}

DB_HASH_TARGET_SSE41 void db_fnv1a32_4x64_sse41(
    const uint8_t *block0, const uint8_t *block1, const uint8_t *block2,
    const uint8_t *block3, uint32_t seed0, uint32_t seed1, uint32_t seed2,
    uint32_t seed3, uint32_t out_hashes[DB_BLOCK_HASH_VECTOR_WIDTH]) {
    __m128i lane_hash = _mm_setr_epi32(
        (int)(DB_FNV1A32_OFFSET ^ seed0), (int)(DB_FNV1A32_OFFSET ^ seed1),
        (int)(DB_FNV1A32_OFFSET ^ seed2), (int)(DB_FNV1A32_OFFSET ^ seed3));
    const __m128i prime_vec = _mm_set1_epi32((int)DB_FNV1A32_PRIME);

    for (size_t chunk_index = 0U;
         chunk_index < (DB_BLOCK_HASH_BYTES / DB_BLOCK_HASH_CHUNK_BYTES);
         chunk_index++) {
        const ptrdiff_t chunk_offset =
            (ptrdiff_t)(chunk_index * DB_BLOCK_HASH_CHUNK_BYTES);
        __m128i row0 = db_x86_load128_any(block0 + chunk_offset);
        __m128i row1 = db_x86_load128_any(block1 + chunk_offset);
        __m128i row2 = db_x86_load128_any(block2 + chunk_offset);
        __m128i row3 = db_x86_load128_any(block3 + chunk_offset);

        __m128i vec0 = _mm_setzero_si128();
        __m128i vec1 = _mm_setzero_si128();
        __m128i vec2 = _mm_setzero_si128();
        __m128i vec3 = _mm_setzero_si128();
        db_x86_transpose4x16_u8(row0, row1, row2, row3, &vec0, &vec1, &vec2,
                                &vec3);

        lane_hash = db_x86_fnv1a32_update_16steps(lane_hash, vec0, prime_vec);
        lane_hash = db_x86_fnv1a32_update_16steps(lane_hash, vec1, prime_vec);
        lane_hash = db_x86_fnv1a32_update_16steps(lane_hash, vec2, prime_vec);
        lane_hash = db_x86_fnv1a32_update_16steps(lane_hash, vec3, prime_vec);
    }

    alignas(16) uint32_t lane_results[DB_BLOCK_HASH_VECTOR_WIDTH];
    db_x86_store128_aligned_u32x4(lane_results, lane_hash);
    memcpy(out_hashes, lane_results,
           DB_BLOCK_HASH_VECTOR_WIDTH * sizeof(uint32_t));
}

DB_HASH_TARGET_AVX2 void db_fnv1a32_8x64_avx2(
    const uint8_t *block0, const uint8_t *block1, const uint8_t *block2,
    const uint8_t *block3, const uint8_t *block4, const uint8_t *block5,
    const uint8_t *block6, const uint8_t *block7, uint32_t seed0,
    uint32_t seed1, uint32_t seed2, uint32_t seed3, uint32_t seed4,
    uint32_t seed5, uint32_t seed6, uint32_t seed7, uint32_t out_hashes[8]) {
    __m256i lane_hash = _mm256_setr_epi32(
        (int)(DB_FNV1A32_OFFSET ^ seed0), (int)(DB_FNV1A32_OFFSET ^ seed1),
        (int)(DB_FNV1A32_OFFSET ^ seed2), (int)(DB_FNV1A32_OFFSET ^ seed3),
        (int)(DB_FNV1A32_OFFSET ^ seed4), (int)(DB_FNV1A32_OFFSET ^ seed5),
        (int)(DB_FNV1A32_OFFSET ^ seed6), (int)(DB_FNV1A32_OFFSET ^ seed7));
    const __m256i prime_vec = _mm256_set1_epi32((int)DB_FNV1A32_PRIME);

    for (size_t byte_index = 0U; byte_index < DB_BLOCK_HASH_BYTES;
         byte_index++) {
        const __m256i input_bytes =
            _mm256_setr_epi32((int)block0[byte_index], (int)block1[byte_index],
                              (int)block2[byte_index], (int)block3[byte_index],
                              (int)block4[byte_index], (int)block5[byte_index],
                              (int)block6[byte_index], (int)block7[byte_index]);
        lane_hash = _mm256_xor_si256(lane_hash, input_bytes);
        lane_hash = _mm256_mullo_epi32(lane_hash, prime_vec);
    }

    alignas(32) uint32_t lane_results[DB_BLOCK_HASH_VECTOR_WIDTH_AVX2];
    _mm256_store_si256((__m256i *)DB_ASSUME_ALIGNED(lane_results, 32U),
                       lane_hash);
    for (size_t lane_index = 0U; lane_index < DB_BLOCK_HASH_VECTOR_WIDTH_AVX2;
         lane_index++) {
        out_hashes[lane_index] = lane_results[lane_index];
    }
}

// x86 packing helpers.
DB_HASH_TARGET_AVX2 void
db_x86_pack8_u32_to4_u64_avx2(const uint32_t *in_hashes, uint64_t *out_packed) {
    // Callers pass local alignas(32) lane/result arrays, so aligned AVX loads
    // and stores are valid in this hot pack path.
    const __m256i hashes_vec = db_x86_load256_aligned_u32x8(in_hashes);
    db_x86_store256_aligned_u64x4(out_packed, hashes_vec);
}

DB_HASH_TARGET_SSE41 void
db_x86_pack4_u32_to2_u64_sse41(const uint32_t *in_hashes,
                               uint64_t *out_packed) {
    // Callers pass local alignas(16) lane/result arrays, so aligned SSE loads
    // are valid in this hot pack path.
    const __m128i hashes_vec =
        _mm_load_si128(db_x86_assume_aligned128(in_hashes));
    const __m128i upper_vec = _mm_srli_si128(hashes_vec, 8);
#if defined(__x86_64__) || defined(_M_X64)
    out_packed[DB_BLOCK_HASH_LANE_0] = (uint64_t)_mm_cvtsi128_si64(hashes_vec);
    out_packed[DB_BLOCK_HASH_LANE_1] = (uint64_t)_mm_cvtsi128_si64(upper_vec);
#else
    out_packed[DB_BLOCK_HASH_LANE_0] =
        (uint64_t)(uint32_t)_mm_cvtsi128_si32(hashes_vec) |
        ((uint64_t)(uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(hashes_vec, 4))
         << 32U);
    out_packed[DB_BLOCK_HASH_LANE_1] =
        (uint64_t)(uint32_t)_mm_cvtsi128_si32(upper_vec) |
        ((uint64_t)(uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(upper_vec, 4))
         << 32U);
#endif
}

int db_hash_select_x86_kernel(void) {
#if defined(__GNUC__) || defined(__clang__)
    static int selected_kernel = -1;
    if (selected_kernel >= 0) {
        return selected_kernel;
    }
    __builtin_cpu_init();
    if ((int)(__builtin_cpu_supports("avx2")) != 0) {
        selected_kernel = DB_HASH_X86_KERNEL_AVX2;
    } else if ((int)(__builtin_cpu_supports("sse4.1")) != 0) {
        selected_kernel = DB_HASH_X86_KERNEL_SSE41;
    } else {
        selected_kernel = DB_HASH_X86_KERNEL_SCALAR;
    }
    return selected_kernel;
#else
    return DB_HASH_X86_KERNEL_SCALAR;
#endif
}

#undef DB_HASH_TARGET_SSE41
#undef DB_HASH_TARGET_AVX2
#endif
