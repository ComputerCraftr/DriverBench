#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/db_hash.h"
#include "core/db_hash_simd_internal.h"

enum {
    DB_TEST_TREE_MAX_BYTES = 8193U,
    DB_TEST_TREE_BYTE_MULTIPLIER = 37U,
    DB_TEST_TREE_BYTE_OFFSET = 11U,
};

static void db_test_hash_tree_fill(uint8_t *bytes, size_t length) {
    for (size_t index = 0U; index < length; index++) {
        bytes[index] = (uint8_t)(((index * DB_TEST_TREE_BYTE_MULTIPLIER) +
                                  DB_TEST_TREE_BYTE_OFFSET) &
                                 UINT8_MAX);
    }
}

static void db_test_hash_tree_vectors(db_test_state_t *state) {
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
    uint8_t bytes[DB_TEST_TREE_MAX_BYTES + 1U];
    db_test_hash_tree_fill(bytes, sizeof(bytes));
    for (size_t index = 0U; index < (sizeof(vectors) / sizeof(vectors[0]));
         index++) {
        const uint64_t scalar =
            db_fnv1a64_tree_scalar(bytes, vectors[index].length,
                                   DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
        const uint64_t automatic =
            db_fnv1a64_tree(bytes, vectors[index].length, DB_U32_SALT_PALETTE,
                            DB_FNV1A64_OFFSET);
        DB_TEST_EXPECT_EQ_U64(state, scalar, vectors[index].expected);
        DB_TEST_EXPECT_EQ_U64(state, automatic, scalar);
    }
    const uint64_t aligned = db_fnv1a64_tree(
        bytes, DB_TEST_TREE_MAX_BYTES, DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    const uint64_t unaligned =
        db_fnv1a64_tree(bytes + 1U, DB_TEST_TREE_MAX_BYTES, DB_U32_SALT_PALETTE,
                        DB_FNV1A64_OFFSET);
    DB_TEST_EXPECT_TRUE(state, aligned != unaligned);
}

static void db_test_hash_tree_domain_and_seed(db_test_state_t *state) {
    uint8_t bytes[32U];
    for (size_t index = 0U; index < sizeof(bytes); index++) {
        bytes[index] = (uint8_t)index;
    }
    DB_TEST_EXPECT_EQ_U64(state,
                          db_fnv1a64_tree(bytes, sizeof(bytes),
                                          UINT32_C(0x12345678),
                                          DB_FNV1A64_OFFSET),
                          UINT64_C(0x23f932bf28057d81));
    DB_TEST_EXPECT_EQ_U64(state,
                          db_fnv1a64_tree(bytes, sizeof(bytes),
                                          DB_U32_SALT_PALETTE,
                                          UINT64_C(0x0123456789abcdef)),
                          UINT64_C(0x4eb452a64530c415));
}

static void db_test_hash_tree_structure_is_bound(db_test_state_t *state) {
    enum {
        LEAF_BYTES = 1024U,
        LEAF_COUNT = 3U,
    };
    uint8_t original[LEAF_BYTES * LEAF_COUNT];
    uint8_t reordered[sizeof(original)];
    uint8_t duplicated[sizeof(original)];
    db_test_hash_tree_fill(original, sizeof(original));
    const size_t third_leaf_offset = (size_t)2U * LEAF_BYTES;
    for (size_t index = 0U; index < LEAF_BYTES; index++) {
        original[LEAF_BYTES + index] ^= UINT8_C(0x5a);
        original[third_leaf_offset + index] ^= UINT8_C(0xa5);
    }
    memcpy(reordered, original + LEAF_BYTES, LEAF_BYTES);
    memcpy(reordered + LEAF_BYTES, original, LEAF_BYTES);
    memcpy(reordered + third_leaf_offset, original + third_leaf_offset,
           LEAF_BYTES);
    memcpy(duplicated, original, LEAF_BYTES);
    memcpy(duplicated + LEAF_BYTES, original, LEAF_BYTES);
    memcpy(duplicated + third_leaf_offset, original + third_leaf_offset,
           LEAF_BYTES);

    const uint64_t original_hash = db_fnv1a64_tree(
        original, sizeof(original), DB_U32_SALT_PALETTE, DB_FNV1A64_OFFSET);
    DB_TEST_EXPECT_TRUE(state, original_hash !=
                                   db_fnv1a64_tree(reordered, sizeof(reordered),
                                                   DB_U32_SALT_PALETTE,
                                                   DB_FNV1A64_OFFSET));
    DB_TEST_EXPECT_TRUE(
        state, original_hash != db_fnv1a64_tree(duplicated, sizeof(duplicated),
                                                DB_U32_SALT_PALETTE,
                                                DB_FNV1A64_OFFSET));
    DB_TEST_EXPECT_TRUE(
        state, original_hash != db_fnv1a64_tree(original, sizeof(original) - 1U,
                                                DB_U32_SALT_PALETTE,
                                                DB_FNV1A64_OFFSET));
}

static void db_test_hash_retina_normalization(db_test_state_t *state) {
    static const uint8_t colors[4][4] = {
        {10U, 20U, 30U, UINT8_MAX},
        {40U, 50U, 60U, UINT8_MAX},
        {70U, 80U, 90U, UINT8_MAX},
        {100U, 110U, 120U, UINT8_MAX},
    };
    uint8_t retina[4U * 4U * 4U] = {0};
    uint8_t canonical[2U * 2U * 4U] = {0};
    for (uint32_t y = 0U; y < 4U; y++) {
        const uint32_t logical_y = y / 2U;
        const uint32_t storage_y = 3U - y;
        for (uint32_t x = 0U; x < 4U; x++) {
            const uint32_t logical_x = x / 2U;
            const size_t source = ((size_t)logical_y * 2U) + (size_t)logical_x;
            const size_t destination = (((size_t)storage_y * 4U) + x) * 4U;
            retina[destination] = colors[source][0];
            retina[destination + 1U] = colors[source][1];
            retina[destination + 2U] = colors[source][2];
            retina[destination + 3U] = (uint8_t)(source + 1U);
        }
    }
    for (size_t pixel = 0U; pixel < 4U; pixel++) {
        canonical[pixel * 4U] = colors[pixel][0];
        canonical[(pixel * 4U) + 1U] = colors[pixel][1];
        canonical[(pixel * 4U) + 2U] = colors[pixel][2];
        canonical[(pixel * 4U) + 3U] = UINT8_MAX;
    }
    const uint64_t expected =
        db_hash_rgba8_pixels_canonical(canonical, 2U, 2U, 8U, 0);
    const uint64_t actual =
        db_hash_sdr_framebuffer_rgba8_canonical(retina, 4U, 4U, 16U, 1, 2U, 2U);
    DB_TEST_EXPECT_EQ_U64(state, actual, expected);
}

static void
db_test_hash_canonicalizes_alpha_and_origin(db_test_state_t *state) {
    const uint8_t bottom_up[] = {
        7U, 8U, 9U, 0U, 10U, 11U, 12U, 1U, 1U, 2U, 3U, 2U, 4U, 5U, 6U, 3U,
    };
    const uint8_t top_down[] = {
        1U, 2U, 3U, UINT8_MAX, 4U,  5U,  6U,  UINT8_MAX,
        7U, 8U, 9U, UINT8_MAX, 10U, 11U, 12U, UINT8_MAX,
    };
    const uint64_t expected =
        db_hash_rgba8_pixels_canonical(top_down, 2U, 2U, 8U, 0);
    const uint64_t actual = db_hash_sdr_framebuffer_rgba8_canonical(
        bottom_up, 2U, 2U, 8U, 1, 2U, 2U);
    DB_TEST_EXPECT_EQ_U64(state, actual, expected);
}

static void db_test_hash_rejects_invalid_framebuffers(db_test_state_t *state) {
    const uint8_t pixel[4] = {0};
    DB_TEST_EXPECT_EQ_U64(
        state,
        db_hash_sdr_framebuffer_rgba8_canonical(NULL, 1U, 1U, 4U, 0, 1U, 1U),
        0U);
    DB_TEST_EXPECT_EQ_U64(
        state,
        db_hash_sdr_framebuffer_rgba8_canonical(pixel, 1U, 1U, 3U, 0, 1U, 1U),
        0U);
}

unsigned db_hash_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"hash_retina_normalization", db_test_hash_retina_normalization},
        {"hash_canonicalizes_alpha_and_origin",
         db_test_hash_canonicalizes_alpha_and_origin},
        {"hash_rejects_invalid_framebuffers",
         db_test_hash_rejects_invalid_framebuffers},
        {"hash_tree_vectors", db_test_hash_tree_vectors},
        {"hash_tree_domain_and_seed", db_test_hash_tree_domain_and_seed},
        {"hash_tree_structure_is_bound", db_test_hash_tree_structure_is_bound},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
