#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "core/db_hash.h"

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
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
