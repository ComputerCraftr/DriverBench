#include "support/test_harness.h"

#include <math.h>
#include <stdint.h>

#include "core/db_numeric.h"

static void db_test_u32_range_handles_full_domain(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_u32_range(0U, 0U, UINT32_MAX), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_range(UINT32_MAX, 0U, UINT32_MAX),
                          UINT32_MAX);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_range(17U, 10U, 20U), 16U);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_range(17U, 20U, 10U), 20U);
}

static void
db_test_next_power_of_two_overflow_is_explicit(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_u32_next_pow2(0U), 1U);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_next_pow2(1025U), 2048U);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_next_pow2(0x80000000U), 0x80000000U);
    DB_TEST_EXPECT_EQ_U32(state, db_u32_next_pow2(0x80000001U), 0U);
}

static void db_test_float_boundary_canonicalization(db_test_state_t *state) {
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_double_to_f32(-0.0), 0.0F);
    DB_TEST_EXPECT_TRUE(state, isnan(db_double_to_f32(nan(""))) != 0);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(-1.0), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(0.5), 128U);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(2.0), UINT8_MAX);
}

static void db_test_f64_comparison_policies(db_test_state_t *state) {
    const double positive_zero[3] = {0.0, 0.25, 1.0};
    const double negative_zero[3] = {-0.0, 0.25, 1.0};
    const double with_nan[3] = {nan(""), 0.25, 1.0};
    DB_TEST_EXPECT_TRUE(state,
                        db_equal_f64_rgb3(positive_zero, negative_zero) != 0);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(positive_zero, with_nan) == 0);
    DB_TEST_EXPECT_TRUE(state, db_compare_f64_total(-0.0, 0.0) < 0);
    DB_TEST_EXPECT_TRUE(state, db_compare_f64_total(nan(""), HUGE_VAL) > 0);
    DB_TEST_EXPECT_EQ_INT(state, db_compare_u32(4U, 4U), 0);
}

static void db_test_f16_f32_conversion_is_exact(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(1.0F), DB_F16_ONE);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(65504.0F), 0x7BFFU);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(ldexpf(1.0F, -14)), 0x0400U);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(ldexpf(1.0F, -24)), 0x0001U);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(ldexpf(1.0F, -25)), 0x0000U);
    DB_TEST_EXPECT_EQ_U32(
        state, db_f32_to_f16(nextafterf(ldexpf(1.0F, -25), INFINITY)), 0x0001U);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(1.0F + ldexpf(1.0F, -11)),
                          DB_F16_ONE);
    DB_TEST_EXPECT_EQ_U32(
        state, db_f32_to_f16(nextafterf(1.0F + ldexpf(1.0F, -11), INFINITY)),
        DB_F16_ONE + 1U);
    for (uint32_t bits = 0U; bits <= UINT16_MAX; bits++) {
        const uint16_t f16 = (uint16_t)bits;
        uint16_t expected = f16;
        const uint32_t magnitude = bits & 0x7FFFU;
        const uint32_t exp = (bits >> DB_F16_EXP_SHIFT) & DB_F16_EXP_MASK;
        const uint32_t mantissa = bits & DB_F16_MANT_MASK;
        if (magnitude == 0U) {
            expected = 0U;
        } else if ((exp == DB_F16_EXP_MASK) && (mantissa != 0U)) {
            expected = (uint16_t)((DB_F16_EXP_MASK << DB_F16_EXP_SHIFT) |
                                  DB_F16_NAN_MANT_QBIT);
        }
        DB_TEST_EXPECT_EQ_U32(state, db_f32_to_f16(db_f16_to_f32(f16)),
                              expected);
    }
}

static void
db_test_f16_storage_quantization_matches_gpu_boundary(db_test_state_t *state) {
    const double above_f16_halfway = 1.0 + ldexp(1.0, -11) + ldexp(1.0, -25);
    DB_TEST_EXPECT_EQ_U32(state, db_double_to_f16(above_f16_halfway),
                          DB_F16_ONE + 1U);
    DB_TEST_EXPECT_EQ_U32(state, db_f64_to_f16_via_f32(above_f16_halfway),
                          DB_F16_ONE);

    const double rgb[3] = {above_f16_halfway, 0.5, -0.0};
    float quantized[3] = {0.0F, 0.0F, 0.0F};
    db_rgb_f64_quantize_f16_to_f32_rgb3(rgb, quantized);
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_bits_u32(quantized[0]),
                          db_f32_to_bits_u32(db_f16_to_f32(DB_F16_ONE)));
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_bits_u32(quantized[1]),
                          db_f32_to_bits_u32(0.5F));
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_bits_u32(quantized[2]), 0U);
}

unsigned db_numeric_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"u32_range_handles_full_domain",
         db_test_u32_range_handles_full_domain},
        {"next_power_of_two_overflow_is_explicit",
         db_test_next_power_of_two_overflow_is_explicit},
        {"float_boundary_canonicalization",
         db_test_float_boundary_canonicalization},
        {"f64_comparison_policies", db_test_f64_comparison_policies},
        {"f16_f32_conversion_is_exact", db_test_f16_f32_conversion_is_exact},
        {"f16_storage_quantization_matches_gpu_boundary",
         db_test_f16_storage_quantization_matches_gpu_boundary},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
