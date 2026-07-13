#include "core/db_hash.h"
#include "core/db_hash_simd_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    DB_HASH_CONFORMANCE_BYTES = 8193U,
    DB_HASH_CONFORMANCE_BYTE_MULTIPLIER = 37U,
    DB_HASH_CONFORMANCE_BYTE_OFFSET = 11U,
    DB_HASH_CONFORMANCE_LEAF_BYTES = 1024U,
    DB_HASH_CONFORMANCE_STRUCTURE_LEAVES = 3U,
    DB_HASH_CONFORMANCE_VECTOR_FAILURE = 4,
    DB_HASH_CONFORMANCE_UNALIGNED_FAILURE = 5,
    DB_HASH_CONFORMANCE_MULTIPLY_FAILURE = 6,
    DB_HASH_CONFORMANCE_STRUCTURE_FAILURE = 7,
};

static const char *db_hash_kernel_name(void) {
#ifdef __aarch64__
    return "neon";
#elif defined(__x86_64__) || defined(__i386__)
    switch (db_hash_select_x86_kernel()) {
    case DB_HASH_X86_KERNEL_SCALAR:
        return "scalar";
    case DB_HASH_X86_KERNEL_SSE2:
        return "sse2";
    case DB_HASH_X86_KERNEL_AVX2:
        return "avx2";
    default:
        return "unknown";
    }
#else
    return "scalar";
#endif
}

static int db_hash_check_vector(const uint8_t *bytes, size_t length,
                                uint64_t expected) {
    const uint64_t scalar = db_fnv1a64_tree_scalar(
        bytes, length, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    const uint64_t automatic =
        db_fnv1a64_tree(bytes, length, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    if ((scalar != expected) || (automatic != scalar)) {
        (void)fprintf(stderr,
                      "hash mismatch length=%zu expected=%016llx "
                      "scalar=%016llx automatic=%016llx\n",
                      length, (unsigned long long)expected,
                      (unsigned long long)scalar,
                      (unsigned long long)automatic);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s EXPECTED_KERNEL\n", argv[0]);
        return 2;
    }
    const char *const selected_kernel = db_hash_kernel_name();
    if ((strcmp(argv[1], "auto") != 0) &&
        (strcmp(selected_kernel, argv[1]) != 0)) {
        (void)fprintf(stderr, "expected kernel=%s selected=%s\n", argv[1],
                      selected_kernel);
        return 3;
    }
    static uint8_t bytes[DB_HASH_CONFORMANCE_BYTES + 1U];
    for (size_t index = 0U; index < sizeof(bytes); index++) {
        bytes[index] =
            (uint8_t)(((index * DB_HASH_CONFORMANCE_BYTE_MULTIPLIER) +
                       DB_HASH_CONFORMANCE_BYTE_OFFSET) &
                      UINT8_MAX);
    }
    static const struct {
        size_t length;
        uint64_t expected;
    } vectors[] = {
        {0U, UINT64_C(0x5024adbefa180b37)},
        {1U, UINT64_C(0x4455f295bb0071f6)},
        {1023U, UINT64_C(0xf51f887efdc5b787)},
        {1024U, UINT64_C(0xda4c14f15d6b176b)},
        {1025U, UINT64_C(0xbd7524afa23b4444)},
        {2048U, UINT64_C(0x69f4329a1af57001)},
        {3073U, UINT64_C(0x044cf272b55cb1f6)},
        {8193U, UINT64_C(0xe5d05eaee0df9165)},
    };
    for (size_t index = 0U; index < (sizeof(vectors) / sizeof(vectors[0]));
         index++) {
        if (db_hash_check_vector(bytes, vectors[index].length,
                                 vectors[index].expected) == 0) {
            return DB_HASH_CONFORMANCE_VECTOR_FAILURE;
        }
    }
    if (db_fnv1a64_tree(bytes, DB_HASH_CONFORMANCE_BYTES, DB_U32_SALT_PALETTE,
                        DB_FNV1A64_OFFSET) ==
        db_fnv1a64_tree(bytes + 1U, DB_HASH_CONFORMANCE_BYTES,
                        DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET)) {
        (void)fprintf(stderr,
                      "unaligned shifted input was not distinguished\n");
        return DB_HASH_CONFORMANCE_UNALIGNED_FAILURE;
    }
    const uint64_t multiply_inputs[] = {
        0U,
        1U,
        UINT64_MAX,
        UINT64_C(0x0123456789abcdef),
    };
    for (size_t index = 0U;
         index < (sizeof(multiply_inputs) / sizeof(multiply_inputs[0]));
         index++) {
        if (db_fnv1a64_tree_multiply(multiply_inputs[index]) !=
            (multiply_inputs[index] * DB_FNV1A64_PRIME)) {
            (void)fprintf(stderr, "shift-add multiplication mismatch\n");
            return DB_HASH_CONFORMANCE_MULTIPLY_FAILURE;
        }
    }
    static uint8_t original[DB_HASH_CONFORMANCE_LEAF_BYTES *
                            DB_HASH_CONFORMANCE_STRUCTURE_LEAVES];
    static uint8_t reordered[sizeof(original)];
    static uint8_t duplicated[sizeof(original)];
    memcpy(original, bytes, sizeof(original));
    const size_t second_leaf_offset = DB_HASH_CONFORMANCE_LEAF_BYTES;
    const size_t third_leaf_offset = 2U * second_leaf_offset;
    for (size_t index = 0U; index < DB_HASH_CONFORMANCE_LEAF_BYTES; index++) {
        original[second_leaf_offset + index] ^= UINT8_C(0x5a);
        original[third_leaf_offset + index] ^= UINT8_C(0xa5);
    }
    memcpy(reordered, original + second_leaf_offset,
           DB_HASH_CONFORMANCE_LEAF_BYTES);
    memcpy(reordered + second_leaf_offset, original,
           DB_HASH_CONFORMANCE_LEAF_BYTES);
    memcpy(reordered + third_leaf_offset, original + third_leaf_offset,
           DB_HASH_CONFORMANCE_LEAF_BYTES);
    memcpy(duplicated, original, DB_HASH_CONFORMANCE_LEAF_BYTES);
    memcpy(duplicated + second_leaf_offset, original,
           DB_HASH_CONFORMANCE_LEAF_BYTES);
    memcpy(duplicated + third_leaf_offset, original + third_leaf_offset,
           DB_HASH_CONFORMANCE_LEAF_BYTES);
    const uint64_t original_hash = db_fnv1a64_tree(
        original, sizeof(original), DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    if ((original_hash == db_fnv1a64_tree(reordered, sizeof(reordered),
                                          DB_U32_SALT_PALETTE,
                                          DB_FNV1A64_OFFSET)) ||
        (original_hash == db_fnv1a64_tree(duplicated, sizeof(duplicated),
                                          DB_U32_SALT_PALETTE,
                                          DB_FNV1A64_OFFSET)) ||
        (original_hash == db_fnv1a64_tree(original, sizeof(original) - 1U,
                                          DB_U32_SALT_PALETTE,
                                          DB_FNV1A64_OFFSET))) {
        (void)fprintf(stderr, "tree structure was not distinguished\n");
        return DB_HASH_CONFORMANCE_STRUCTURE_FAILURE;
    }
    (void)printf("hash conformance passed kernel=%s algorithm=%s\n",
                 selected_kernel, DB_FNV1A64_TREE_ALGORITHM);
    return 0;
}
