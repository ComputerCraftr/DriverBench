#include "support/test_harness.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/db_numeric.h"
#include "core/db_sort.h"

static void db_test_sort_f64_numeric_order(db_test_state_t *state) {
    const double high_value = 3.0;
    double values[] = {high_value, -HUGE_VAL, 1.0, HUGE_VAL, -2.0, 1.0};
    const double expected[] = {-HUGE_VAL, -2.0, 1.0, 1.0, high_value, HUGE_VAL};
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(values, value_count),
                          DB_SORT_OK);
    for (size_t index = 0U; index < value_count; index++) {
        DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(values[index]),
                              db_f64_to_bits_u64(expected[index]));
    }
}

static void db_test_sort_f64_total_edge_order(db_test_state_t *state) {
    const uint64_t nan_a_bits = UINT64_C(0x7FF8000000000001);
    const uint64_t nan_b_bits = UINT64_C(0x7FF8000000000002);
    const uint64_t negative_nan_bits = UINT64_C(0xFFF8000000000001);
    double values[] = {db_bits_u64_to_f64(negative_nan_bits),
                       db_bits_u64_to_f64(nan_b_bits),
                       0.0,
                       HUGE_VAL,
                       db_bits_u64_to_f64(nan_a_bits),
                       -0.0,
                       -HUGE_VAL};
    const uint64_t expected_bits[] = {UINT64_C(0xFFF0000000000000),
                                      UINT64_C(0x8000000000000000),
                                      UINT64_C(0x0000000000000000),
                                      UINT64_C(0x7FF0000000000000),
                                      nan_a_bits,
                                      nan_b_bits,
                                      negative_nan_bits};
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(values, value_count),
                          DB_SORT_OK);
    for (size_t index = 0U; index < value_count; index++) {
        DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(values[index]),
                              expected_bits[index]);
    }
    for (size_t lhs = 0U; lhs < value_count; lhs++) {
        for (size_t rhs = 0U; rhs < value_count; rhs++) {
            const int comparison =
                db_compare_f64_total(values[lhs], values[rhs]);
            const int reverse = db_compare_f64_total(values[rhs], values[lhs]);
            DB_TEST_EXPECT_EQ_INT(state, comparison, -reverse);
            if (lhs < rhs) {
                DB_TEST_EXPECT_TRUE(state, comparison < 0);
            } else if (lhs > rhs) {
                DB_TEST_EXPECT_TRUE(state, comparison > 0);
            } else {
                DB_TEST_EXPECT_EQ_INT(state, comparison, 0);
            }
        }
    }
}

static void db_test_sort_f64_argument_contract(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(NULL, 0U), DB_SORT_OK);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(NULL, 1U),
                          DB_SORT_INVALID_ARGUMENT);
}

static void db_test_sort_u32_contract(db_test_state_t *state) {
    static const uint32_t repeated_value = 7U;
    uint32_t values[] = {UINT32_MAX, 0U, repeated_value, 1U, repeated_value};
    const uint32_t expected[] = {0U, 1U, repeated_value, repeated_value,
                                 UINT32_MAX};
    const size_t count = sizeof(values) / sizeof(values[0]);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_u32_ascending(values, count),
                          DB_SORT_OK);
    DB_TEST_EXPECT_TRUE(state, memcmp(values, expected, sizeof(values)) == 0);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_u32_ascending(NULL, 0U), DB_SORT_OK);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_u32_ascending(NULL, 1U),
                          DB_SORT_INVALID_ARGUMENT);
}

static int compare_bytes(const void *lhs, const void *rhs) {
    const uint8_t lhs_value = *(const uint8_t *)lhs;
    const uint8_t rhs_value = *(const uint8_t *)rhs;
    return (lhs_value > rhs_value) - (lhs_value < rhs_value);
}

static void db_test_sort_rejects_size_overflow(db_test_state_t *state) {
    double f64_value = 0.0;
    uint32_t u32_value = 0U;
    uint8_t record = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_sort_f64_ascending(&f64_value, (SIZE_MAX / sizeof(f64_value)) + 1U),
        DB_SORT_SIZE_OVERFLOW);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_sort_u32_ascending(&u32_value, (SIZE_MAX / sizeof(u32_value)) + 1U),
        DB_SORT_SIZE_OVERFLOW);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_sort_records(&record, 2U, (SIZE_MAX / 2U) + 1U, compare_bytes),
        DB_SORT_SIZE_OVERFLOW);
}

unsigned db_sort_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"sort_f64_numeric_order", db_test_sort_f64_numeric_order},
        {"sort_f64_total_edge_order", db_test_sort_f64_total_edge_order},
        {"sort_f64_argument_contract", db_test_sort_f64_argument_contract},
        {"sort_u32_contract", db_test_sort_u32_contract},
        {"sort_rejects_size_overflow", db_test_sort_rejects_size_overflow},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
