#include "support/test_harness.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/db_sort.h"

static double f64_from_bits(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t f64_bits(double value) {
    uint64_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void db_test_sort_f64_numeric_order(db_test_state_t *state) {
    const double high_value = 3.0;
    double values[] = {high_value, -HUGE_VAL, 1.0, HUGE_VAL, -2.0, 1.0};
    const double expected[] = {-HUGE_VAL, -2.0, 1.0, 1.0, high_value, HUGE_VAL};
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(values, value_count),
                          DB_SORT_OK);
    for (size_t index = 0U; index < value_count; index++) {
        DB_TEST_EXPECT_TRUE(state, f64_bits(values[index]) ==
                                       f64_bits(expected[index]));
    }
}

static void db_test_sort_f64_total_edge_order(db_test_state_t *state) {
    const uint64_t nan_a_bits = UINT64_C(0x7FF8000000000001);
    const uint64_t nan_b_bits = UINT64_C(0x7FF8000000000002);
    double values[] = {f64_from_bits(nan_b_bits), 0.0,
                       f64_from_bits(nan_a_bits), -0.0};
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    DB_TEST_EXPECT_EQ_INT(state, db_sort_f64_ascending(values, value_count),
                          DB_SORT_OK);
    DB_TEST_EXPECT_TRUE(state, signbit(values[0]) != 0);
    DB_TEST_EXPECT_TRUE(state, signbit(values[1]) == 0);
    DB_TEST_EXPECT_TRUE(state, f64_bits(values[2]) == nan_a_bits);
    DB_TEST_EXPECT_TRUE(state, f64_bits(values[3]) == nan_b_bits);
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

unsigned db_sort_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"sort_f64_numeric_order", db_test_sort_f64_numeric_order},
        {"sort_f64_total_edge_order", db_test_sort_f64_total_edge_order},
        {"sort_f64_argument_contract", db_test_sort_f64_argument_contract},
        {"sort_u32_contract", db_test_sort_u32_contract},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
