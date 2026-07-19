#ifndef DRIVERBENCH_CORE_DB_HASH_SIMD_INTERNAL_H
#define DRIVERBENCH_CORE_DB_HASH_SIMD_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

enum {
    DB_FNV_TREE_VERSION = 1,
    DB_FNV_TREE_LEAF_TAG = 0,
    DB_FNV_TREE_PARENT_TAG = 1,
    DB_FNV_TREE_UNARY_TAG = 2,
    DB_FNV_TREE_ROOT_TAG = 3,
    DB_FNV_TREE_PREFIX_TAG_SHIFT = 8,
    DB_FNV_TREE_LEAF_PREFIX =
        DB_FNV_TREE_VERSION |
        (DB_FNV_TREE_LEAF_TAG << DB_FNV_TREE_PREFIX_TAG_SHIFT),
    DB_FNV_TREE_PARENT_PREFIX =
        DB_FNV_TREE_VERSION |
        (DB_FNV_TREE_PARENT_TAG << DB_FNV_TREE_PREFIX_TAG_SHIFT),
    DB_FNV_TREE_UNARY_PREFIX =
        DB_FNV_TREE_VERSION |
        (DB_FNV_TREE_UNARY_TAG << DB_FNV_TREE_PREFIX_TAG_SHIFT),
    DB_FNV_TREE_ROOT_PREFIX =
        DB_FNV_TREE_VERSION |
        (DB_FNV_TREE_ROOT_TAG << DB_FNV_TREE_PREFIX_TAG_SHIFT),
    DB_FNV_TREE_LEAF_BYTES = 1024,
    DB_FNV_TREE_LEAF_HEADER_BYTES = 18,
    DB_FNV_TREE_PARENT_RECORD_BYTES = 58,
    DB_FNV_TREE_UNARY_RECORD_BYTES = 34,
    DB_FNV_TREE_ROOT_RECORD_BYTES = 34,
    DB_FNV_TREE_SSE2_LANES = 2,
    DB_FNV_TREE_AVX2_LANES = 4,
    DB_FNV_TREE_PARENT_U64_FIELDS = 6,
    DB_FNV_PRIME_SHIFT_1 = 1,
    DB_FNV_PRIME_SHIFT_4 = 4,
    DB_FNV_PRIME_SHIFT_5 = 5,
    DB_FNV_PRIME_SHIFT_7 = 7,
    DB_FNV_PRIME_SHIFT_8 = 8,
    DB_FNV_PRIME_SHIFT_40 = 40,
};

enum {
    DB_HASH_X86_KERNEL_SCALAR = 0,
    DB_HASH_X86_KERNEL_SSE2 = 1,
    DB_HASH_X86_KERNEL_AVX2 = 2,
    DB_HASH_X86_KERNEL_AVX512 = 3,
};

uint64_t db_fnv1a64_tree_multiply(uint64_t value);
uint64_t db_fnv1a64_tree_scalar(const void *data, size_t len_bytes,
                                uint32_t domain, uint64_t initial_hash);
void db_fnv1a64_tree_prefix_bytes(uint16_t prefix, uint8_t output[2]);

#if defined(__x86_64__) || defined(__i386__)
int db_hash_select_x86_kernel(void);
void db_fnv1a64_2x_sse2(const uint8_t *data0, const uint8_t *data1,
                        size_t length, uint64_t initial0, uint64_t initial1,
                        uint64_t out_hashes[DB_FNV_TREE_SSE2_LANES]);
void db_fnv1a64_4x_avx2(const uint8_t *data0, const uint8_t *data1,
                        const uint8_t *data2, const uint8_t *data3,
                        size_t length, uint64_t initial0, uint64_t initial1,
                        uint64_t initial2, uint64_t initial3,
                        uint64_t out_hashes[DB_FNV_TREE_AVX2_LANES]);
void db_fnv1a64_2x_avx512(const uint8_t *data0, const uint8_t *data1,
                          size_t length, uint64_t initial0, uint64_t initial1,
                          uint64_t out_hashes[DB_FNV_TREE_SSE2_LANES]);
void db_fnv1a64_4x_avx512(const uint8_t *data0, const uint8_t *data1,
                          const uint8_t *data2, const uint8_t *data3,
                          size_t length, uint64_t initial0, uint64_t initial1,
                          uint64_t initial2, uint64_t initial3,
                          uint64_t out_hashes[DB_FNV_TREE_AVX2_LANES]);
#endif

#endif
