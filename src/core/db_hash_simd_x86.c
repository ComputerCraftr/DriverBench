#if defined(__x86_64__) || defined(__i386__)
#include "db_hash.h"
#include "db_hash_simd_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#define DB_HASH_TARGET_SSE2 __attribute__((target("sse2")))
#define DB_HASH_TARGET_AVX2 __attribute__((target("avx2")))
#define DB_HASH_TARGET_AVX512                                                  \
    __attribute__((target("avx512f,avx512vl,avx512dq")))
#else
#define DB_HASH_TARGET_SSE2
#define DB_HASH_TARGET_AVX2
#define DB_HASH_TARGET_AVX512
#endif

static int64_t db_hash_u64_bits_to_i64(uint64_t value) {
    int64_t result = 0;
    memcpy(&result, &value, sizeof(result));
    return result;
}

DB_HASH_TARGET_SSE2 static inline __m128i
db_fnv1a64_multiply_sse2(__m128i value) {
    __m128i result = value;
    result = _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_1));
    result = _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_4));
    result = _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_5));
    result = _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_7));
    result = _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_8));
    return _mm_add_epi64(result, _mm_slli_epi64(value, DB_FNV_PRIME_SHIFT_40));
}

DB_HASH_TARGET_AVX2 static inline __m256i
db_fnv1a64_multiply_avx2(__m256i value) {
    __m256i result = value;
    result = _mm256_add_epi64(result,
                              _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_1));
    result = _mm256_add_epi64(result,
                              _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_4));
    result = _mm256_add_epi64(result,
                              _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_5));
    result = _mm256_add_epi64(result,
                              _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_7));
    result = _mm256_add_epi64(result,
                              _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_8));
    return _mm256_add_epi64(result,
                            _mm256_slli_epi64(value, DB_FNV_PRIME_SHIFT_40));
}

DB_HASH_TARGET_AVX512 static inline __m128i
db_fnv1a64_multiply_avx512_2x(__m128i value) {
    return _mm_mullo_epi64(
        value, _mm_set1_epi64x(db_hash_u64_bits_to_i64(DB_FNV1A64_PRIME)));
}

DB_HASH_TARGET_AVX512 static inline __m256i
db_fnv1a64_multiply_avx512_4x(__m256i value) {
    return _mm256_mullo_epi64(
        value, _mm256_set1_epi64x(db_hash_u64_bits_to_i64(DB_FNV1A64_PRIME)));
}

DB_HASH_TARGET_SSE2 void
db_fnv1a64_2x_sse2(const uint8_t *data0, const uint8_t *data1, size_t length,
                   uint64_t initial0, uint64_t initial1,
                   uint64_t out_hashes[DB_FNV_TREE_SSE2_LANES]) {
    __m128i hashes = _mm_set_epi64x(db_hash_u64_bits_to_i64(initial1),
                                    db_hash_u64_bits_to_i64(initial0));
    for (size_t index = 0U; index < length; index++) {
        const __m128i bytes =
            _mm_set_epi64x((int64_t)data1[index], (int64_t)data0[index]);
        hashes = db_fnv1a64_multiply_sse2(_mm_xor_si128(hashes, bytes));
    }
    _mm_storeu_si128((__m128i_u *)out_hashes, hashes);
}

DB_HASH_TARGET_AVX2 void
db_fnv1a64_4x_avx2(const uint8_t *data0, const uint8_t *data1,
                   const uint8_t *data2, const uint8_t *data3, size_t length,
                   uint64_t initial0, uint64_t initial1, uint64_t initial2,
                   uint64_t initial3,
                   uint64_t out_hashes[DB_FNV_TREE_AVX2_LANES]) {
    __m256i hashes = _mm256_set_epi64x(
        db_hash_u64_bits_to_i64(initial3), db_hash_u64_bits_to_i64(initial2),
        db_hash_u64_bits_to_i64(initial1), db_hash_u64_bits_to_i64(initial0));
    for (size_t index = 0U; index < length; index++) {
        const __m256i bytes =
            _mm256_set_epi64x((int64_t)data3[index], (int64_t)data2[index],
                              (int64_t)data1[index], (int64_t)data0[index]);
        hashes = db_fnv1a64_multiply_avx2(_mm256_xor_si256(hashes, bytes));
    }
    _mm256_storeu_si256((__m256i_u *)out_hashes, hashes);
}

DB_HASH_TARGET_AVX512 void
db_fnv1a64_2x_avx512(const uint8_t *data0, const uint8_t *data1, size_t length,
                     uint64_t initial0, uint64_t initial1,
                     uint64_t out_hashes[DB_FNV_TREE_SSE2_LANES]) {
    __m128i hashes = _mm_set_epi64x(db_hash_u64_bits_to_i64(initial1),
                                    db_hash_u64_bits_to_i64(initial0));
    for (size_t index = 0U; index < length; index++) {
        const __m128i bytes =
            _mm_set_epi64x((int64_t)data1[index], (int64_t)data0[index]);
        hashes = db_fnv1a64_multiply_avx512_2x(_mm_xor_si128(hashes, bytes));
    }
    _mm_storeu_si128((__m128i_u *)out_hashes, hashes);
}

DB_HASH_TARGET_AVX512 void
db_fnv1a64_4x_avx512(const uint8_t *data0, const uint8_t *data1,
                     const uint8_t *data2, const uint8_t *data3, size_t length,
                     uint64_t initial0, uint64_t initial1, uint64_t initial2,
                     uint64_t initial3,
                     uint64_t out_hashes[DB_FNV_TREE_AVX2_LANES]) {
    __m256i hashes = _mm256_set_epi64x(
        db_hash_u64_bits_to_i64(initial3), db_hash_u64_bits_to_i64(initial2),
        db_hash_u64_bits_to_i64(initial1), db_hash_u64_bits_to_i64(initial0));
    for (size_t index = 0U; index < length; index++) {
        const __m256i bytes =
            _mm256_set_epi64x((int64_t)data3[index], (int64_t)data2[index],
                              (int64_t)data1[index], (int64_t)data0[index]);
        hashes = db_fnv1a64_multiply_avx512_4x(_mm256_xor_si256(hashes, bytes));
    }
    _mm256_storeu_si256((__m256i_u *)out_hashes, hashes);
}

int db_hash_select_x86_kernel(void) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    if (((int)(__builtin_cpu_supports("avx512vl")) != 0) &&
        ((int)(__builtin_cpu_supports("avx512dq")) != 0)) {
        return DB_HASH_X86_KERNEL_AVX512;
    }
    if ((int)(__builtin_cpu_supports("avx2")) != 0) {
        return DB_HASH_X86_KERNEL_AVX2;
    }
    if ((int)(__builtin_cpu_supports("sse2")) != 0) {
        return DB_HASH_X86_KERNEL_SSE2;
    }
#endif
    return DB_HASH_X86_KERNEL_SCALAR;
}

#undef DB_HASH_TARGET_SSE2
#undef DB_HASH_TARGET_AVX2
#undef DB_HASH_TARGET_AVX512
#endif
