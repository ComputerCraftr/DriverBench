#include "support/test_harness.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/db_buffer_convert.h"
#include "core/db_core.h"
#include "core/db_numeric.h"

static const uint32_t db_test_maximum_rhs = 7U;
static const double db_test_quarter = 0.25;
static const double db_test_three_quarters = 0.75;
static const float db_test_widen_input = 1.25F;
static const double db_test_widen_incremented = 2.25;

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
    DB_TEST_EXPECT_EQ_U32(state, db_f32_to_bits_u32(db_double_to_f32(nan(""))),
                          DB_F32_CANONICAL_NAN_BITS);
    DB_TEST_EXPECT_EQ_U64(
        state, db_f64_to_bits_u64(db_f32_to_double(nanf("-payload"))),
        DB_F64_CANONICAL_NAN_BITS);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(-1.0), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(0.5), 128U);
    DB_TEST_EXPECT_EQ_U32(state, db_double01_to_u8_clamped(2.0), UINT8_MAX);
}

static void db_test_to_f64_dispatch_is_canonical_and_single_evaluation(
    db_test_state_t *state) {
    float value = db_test_widen_input;
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, DB_TO_F64(value++),
                                DB_TO_F64(db_test_widen_input));
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f32_to_double(value),
                                db_test_widen_incremented);
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(DB_TO_F64(-0.0F)),
                          db_f64_to_bits_u64(0.0));
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(DB_TO_F64(nanf("payload"))),
                          DB_F64_CANONICAL_NAN_BITS);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, DB_TO_F64((int16_t)-17), -17.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, DB_TO_F64(UINT32_MAX), 4294967295.0);
}

static void db_test_min_max_helpers_evaluate_once(db_test_state_t *state) {
    uint32_t lhs = 4U;
    uint32_t rhs = db_test_maximum_rhs;
    DB_TEST_EXPECT_EQ_U32(state, DB_MIN(lhs++, rhs++), 4U);
    DB_TEST_EXPECT_EQ_U32(state, lhs, 5U);
    DB_TEST_EXPECT_EQ_U32(state, rhs, 8U);
    DB_TEST_EXPECT_EQ_U32(state, DB_MAX(lhs++, rhs++), 8U);
    DB_TEST_EXPECT_EQ_U32(state, lhs, 6U);
    DB_TEST_EXPECT_EQ_U32(state, rhs, 9U);

    uint32_t value = 5U;
    DB_TEST_EXPECT_EQ_U32(state, DB_CLAMP(value++, 0U, 10U), 5U);
    DB_TEST_EXPECT_EQ_U32(state, value, 6U);

    const uint32_t narrow = UINT32_MAX;
    const uint64_t wide = (uint64_t)UINT32_MAX + 1U;
    DB_TEST_EXPECT_EQ_U64(state, DB_MIN(narrow, wide), UINT32_MAX);
    DB_TEST_EXPECT_EQ_U64(state, DB_MAX(narrow, wide), wide);
}

static void db_test_integral_boundary_helpers(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_U32(state, db_nonnegative_int_to_u32_or_zero(-1), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_nonnegative_int_to_u32_or_zero(0), 0U);
    DB_TEST_EXPECT_EQ_U32(state, db_nonnegative_int_to_u32_or_zero(17), 17U);

    DB_TEST_EXPECT_EQ_U32(state, db_u64_to_u32_saturating(UINT32_MAX),
                          UINT32_MAX);
    DB_TEST_EXPECT_EQ_U32(
        state, db_u64_to_u32_saturating((uint64_t)UINT32_MAX + 1U), UINT32_MAX);
    DB_TEST_EXPECT_EQ_U32(state, db_size_to_u32_or_zero((size_t)UINT32_MAX),
                          UINT32_MAX);
#if SIZE_MAX > UINT32_MAX
    DB_TEST_EXPECT_EQ_U32(state,
                          db_size_to_u32_or_zero((size_t)UINT32_MAX + 1U), 0U);
#endif
    DB_TEST_EXPECT_EQ_SIZE(state, db_positive_i32_to_size_or_zero(-1), 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_positive_i32_to_size_or_zero(0), 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_positive_i32_to_size_or_zero(23), 23U);
    DB_TEST_EXPECT_EQ_SIZE(
        state,
        db_checked_ptrdiff_to_size("test_numeric", "pointer_distance", 31),
        31U);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_saturating_sub(19U, 7U), 12U);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_saturating_sub(7U, 19U), 0U);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_saturating_sub(7U, 7U), 0U);
}

static void db_test_f64_extrema_policy(db_test_state_t *state) {
    DB_TEST_EXPECT_TRUE(
        state,
        db_equal_f64(db_min_f64(nan(""), db_test_quarter), db_test_quarter));
    DB_TEST_EXPECT_TRUE(
        state, db_equal_f64(db_max_f64(db_test_three_quarters, nan("")),
                            db_test_three_quarters));
    DB_TEST_EXPECT_EQ_U64(state,
                          db_f64_to_bits_u64(db_min_f64(nan("1"), nan("2"))),
                          DB_F64_CANONICAL_NAN_BITS);
    DB_TEST_EXPECT_EQ_U64(state,
                          db_f64_to_bits_u64(db_max_f64(nan("1"), nan("2"))),
                          DB_F64_CANONICAL_NAN_BITS);
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(db_min_f64(-0.0, 0.0)),
                          db_f64_to_bits_u64(-0.0));
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(db_min_f64(0.0, -0.0)),
                          db_f64_to_bits_u64(-0.0));
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(db_max_f64(-0.0, 0.0)),
                          db_f64_to_bits_u64(0.0));
    DB_TEST_EXPECT_EQ_U64(state, db_f64_to_bits_u64(db_max_f64(0.0, -0.0)),
                          db_f64_to_bits_u64(0.0));
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_clamp_f64_finite_or(nan(""), 0.0, 1.0, 0.25), 0.25);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_clamp_f64_finite_or(HUGE_VAL, 0.0, 1.0, 0.0), 0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_clamp_f64_finite_or(-1.0, 0.0, 1.0, 0.5), 0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_clamp_f64_finite_or(2.0, 0.0, 1.0, 0.5), 1.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f64_positive_finite_or_zero(-1.0),
                                0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f64_positive_finite_or_zero(0.0),
                                0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f64_positive_finite_or_zero(0.25),
                                0.25);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f64_positive_finite_or_zero(HUGE_VAL),
                                0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, db_f64_positive_finite_or_zero(nan("")),
                                0.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_f64_reciprocal_positive_finite_or(4.0, 7.0), 0.25);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_f64_reciprocal_positive_finite_or(0.0, 7.0), 7.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(
        state, db_f64_reciprocal_positive_finite_or(HUGE_VAL, 7.0), 7.0);
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

static void db_test_aligned_calloc_is_zero_initialized(db_test_state_t *state) {
    enum { DB_TEST_ELEMENT_COUNT = 17 };
    uint32_t *const values = (uint32_t *)db_calloc_or_fail(
        "test_numeric", "zeroed_values", DB_TEST_ELEMENT_COUNT, sizeof(*values),
        DB_CACHELINE_ALIGNMENT_BYTES);
    DB_TEST_EXPECT_TRUE(state, db_pointer_is_aligned(
                                   values, DB_CACHELINE_ALIGNMENT_BYTES) != 0);
    for (size_t index = 0U; index < DB_TEST_ELEMENT_COUNT; index++) {
        DB_TEST_EXPECT_EQ_U32(state, values[index], 0U);
    }
    free(values);

    uint32_t *const natural_values = (uint32_t *)db_calloc_array_or_fail(
        "test_numeric", "naturally_aligned_zeroed_values",
        DB_TEST_ELEMENT_COUNT, sizeof(*natural_values));
    for (size_t index = 0U; index < DB_TEST_ELEMENT_COUNT; index++) {
        DB_TEST_EXPECT_EQ_U32(state, natural_values[index], 0U);
    }
    free(natural_values);
}

static void
db_test_checked_arithmetic_reports_overflow(db_test_state_t *state) {
    size_t size_result = 0U;
    uint32_t u32_result = 0U;
    uint64_t u64_result = 0U;
    DB_TEST_EXPECT_TRUE(state, db_try_add_u32(7U, 9U, &u32_result));
    DB_TEST_EXPECT_EQ_U32(state, u32_result, 16U);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_add_u32(UINT32_MAX, 1U, &u32_result) == 0);
    DB_TEST_EXPECT_TRUE(state, db_try_add_u32(1U, 1U, NULL) == 0);
    DB_TEST_EXPECT_TRUE(state, db_try_mul_size(7U, 9U, &size_result));
    DB_TEST_EXPECT_EQ_SIZE(state, size_result, 63U);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_mul_size(SIZE_MAX, 2U, &size_result) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_add_size(SIZE_MAX, 1U, &size_result) == 0);
    DB_TEST_EXPECT_TRUE(state, db_try_mul_size(1U, 1U, NULL) == 0);
    DB_TEST_EXPECT_TRUE(state, db_try_mul_u64(11U, 13U, &u64_result));
    DB_TEST_EXPECT_EQ_U64(state, u64_result, 143U);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_mul_u64(UINT64_MAX, 2U, &u64_result) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_add_u64(UINT64_MAX, 1U, &u64_result) == 0);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_saturating_add(7U, 11U), 18U);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_saturating_add(UINT64_MAX, 1U),
                          UINT64_MAX);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_abs_diff(7U, 19U), 12U);
    DB_TEST_EXPECT_EQ_U64(state, db_u64_abs_diff(19U, 7U), 12U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_size_add_or_zero(7U, 11U), 18U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_size_add_or_zero(SIZE_MAX, 1U), 0U);
}

static void
db_test_double_to_long_uses_half_open_bounds(db_test_state_t *state) {
    const int value_bits = (int)(sizeof(long) * CHAR_BIT);
    const double upper_exclusive = ldexp(1.0, value_bits - 1);
    const double lower_inclusive = -upper_exclusive;
    long result = 0;

    DB_TEST_EXPECT_TRUE(state,
                        db_try_double_to_long(lower_inclusive, &result) != 0);
    DB_TEST_EXPECT_TRUE(state, result == LONG_MIN);
    const double below_upper = nextafter(upper_exclusive, 0.0);
    const long expected_below_upper = (long)below_upper;
    DB_TEST_EXPECT_TRUE(state,
                        db_try_double_to_long(below_upper, &result) != 0);
    DB_TEST_EXPECT_TRUE(state, result == expected_below_upper);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_double_to_long(upper_exclusive, &result) == 0);
    const double below_lower = nextafter(lower_inclusive, -HUGE_VAL);
    const int below_lower_is_representable =
        trunc(below_lower) >= lower_inclusive;
    DB_TEST_EXPECT_EQ_INT(state, db_try_double_to_long(below_lower, &result),
                          below_lower_is_representable);
    if (below_lower_is_representable != 0) {
        DB_TEST_EXPECT_TRUE(state, result == (long)below_lower);
    }
    DB_TEST_EXPECT_TRUE(
        state, db_try_double_to_long(lower_inclusive - db_test_three_quarters,
                                     &result) != 0);
    DB_TEST_EXPECT_TRUE(state, result == LONG_MIN);
    DB_TEST_EXPECT_TRUE(state, db_try_double_to_long(nan(""), &result) == 0);
    DB_TEST_EXPECT_TRUE(state, db_try_double_to_long(0.0, NULL) == 0);
}

static void db_test_byte_ranges_reject_wraparound(db_test_state_t *state) {
    size_t strided_size = 0U;
    DB_TEST_EXPECT_TRUE(state, db_size_range_fits(16U, 4U, 12U));
    DB_TEST_EXPECT_TRUE(state, db_size_range_fits(16U, 16U, 0U));
    DB_TEST_EXPECT_TRUE(state, db_size_range_fits(16U, 17U, 0U) == 0);
    DB_TEST_EXPECT_TRUE(state, db_size_range_fits(16U, 8U, 9U) == 0);
    DB_TEST_EXPECT_TRUE(state, db_size_range_fits(SIZE_MAX, SIZE_MAX, 1U) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_strided_size(3U, 16U, 12U, &strided_size));
    DB_TEST_EXPECT_EQ_SIZE(state, strided_size, 44U);
    DB_TEST_EXPECT_TRUE(
        state, db_try_strided_size(2U, SIZE_MAX, 4U, &strided_size) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_try_strided_size(2U, 3U, 4U, &strided_size) == 0);

    uint8_t storage[8] = {0};
    int overlap = -1;
    DB_TEST_EXPECT_TRUE(state, db_memory_ranges_overlap(
                                   storage, 4U, storage + 3U, 4U, &overlap));
    DB_TEST_EXPECT_EQ_INT(state, overlap, 1);
    DB_TEST_EXPECT_TRUE(state, db_memory_ranges_overlap(
                                   storage, 2U, storage + 4U, 2U, &overlap));
    DB_TEST_EXPECT_EQ_INT(state, overlap, 0);
    DB_TEST_EXPECT_TRUE(
        state,
        db_memory_ranges_overlap(NULL, 0U, storage, sizeof(storage), &overlap));
    DB_TEST_EXPECT_EQ_INT(state, overlap, 0);
    DB_TEST_EXPECT_TRUE(
        state,
        db_memory_ranges_overlap(db_test_pointer_from_uintptr(UINTPTR_MAX - 1U),
                                 4U, storage, sizeof(storage), &overlap) == 0);
}

static void
db_test_strided_row_copy_is_tight_and_overlap_safe(db_test_state_t *state) {
    enum {
        ROW_COUNT = 3U,
        ROW_BYTES = 4U,
        SOURCE_STRIDE = 6U,
        SOURCE_BYTES = 16U,
        PACKED_BYTES = ROW_COUNT * ROW_BYTES,
    };
    const uint8_t padded[SOURCE_BYTES] = {
        1U, 2U, 3U, 4U, 90U, 91U, 5U, 6U, 7U, 8U, 92U, 93U, 9U, 10U, 11U, 12U,
    };
    const uint8_t expected[PACKED_BYTES] = {1U, 2U, 3U, 4U,  5U,  6U,
                                            7U, 8U, 9U, 10U, 11U, 12U};
    uint8_t packed[PACKED_BYTES] = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_copy_strided_rows_tight(packed, sizeof(packed), padded,
                                          sizeof(padded), ROW_COUNT,
                                          SOURCE_STRIDE, ROW_BYTES) != 0);
    DB_TEST_EXPECT_TRUE(state, memcmp(packed, expected, sizeof(expected)) == 0);

    uint8_t overlap[PACKED_BYTES + 1U] = {0};
    memcpy(overlap, expected, sizeof(expected));
    DB_TEST_EXPECT_TRUE(
        state, db_copy_strided_rows_tight(overlap + 1U, PACKED_BYTES, overlap,
                                          PACKED_BYTES, ROW_COUNT, ROW_BYTES,
                                          ROW_BYTES) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        memcmp(overlap + 1U, expected, sizeof(expected)) == 0);
    uint8_t padded_overlap[32] = {0};
    memcpy(padded_overlap, expected, ROW_BYTES);
    memcpy(padded_overlap + SOURCE_STRIDE, expected + ROW_BYTES, ROW_BYTES);
    DB_TEST_EXPECT_TRUE(state,
                        db_copy_strided_rows_tight(
                            padded_overlap + 1U, sizeof(padded_overlap) - 1U,
                            padded_overlap, sizeof(padded_overlap), ROW_COUNT,
                            SOURCE_STRIDE, ROW_BYTES) == 0);
    DB_TEST_EXPECT_TRUE(
        state, db_copy_strided_rows_tight(packed, sizeof(packed) - 1U, padded,
                                          sizeof(padded), ROW_COUNT,
                                          SOURCE_STRIDE, ROW_BYTES) == 0);

    uint8_t unaligned_storage[SOURCE_BYTES + PACKED_BYTES + 3U] = {0};
    uint8_t *const unaligned_source = unaligned_storage + 1U;
    uint8_t *const unaligned_destination =
        unaligned_storage + SOURCE_BYTES + 2U;
    memcpy(unaligned_source, padded, sizeof(padded));
    DB_TEST_EXPECT_TRUE(state, db_copy_strided_rows_tight(
                                   unaligned_destination, PACKED_BYTES,
                                   unaligned_source, SOURCE_BYTES, ROW_COUNT,
                                   SOURCE_STRIDE, ROW_BYTES) != 0);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(unaligned_destination, expected, sizeof(expected)) == 0);

    uint8_t forward_overlap[SOURCE_BYTES + 2U] = {0};
    memcpy(forward_overlap + 2U, padded, sizeof(padded));
    DB_TEST_EXPECT_TRUE(state, db_copy_strided_rows_tight(
                                   forward_overlap, PACKED_BYTES,
                                   forward_overlap + 2U, SOURCE_BYTES,
                                   ROW_COUNT, SOURCE_STRIDE, ROW_BYTES) != 0);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(forward_overlap, expected, sizeof(expected)) == 0);
}

static void
db_test_pixel_conversion_rejects_malformed_layouts(db_test_state_t *state) {
    enum { TEST_CONVERSION_SENTINEL = 0x5A5A5A5A };
    const uint32_t rgba8[4] = {1U, 2U, 3U, 4U};
    const uint16_t rgba16f[16] = {0U};
    uint32_t destination[4] = {
        TEST_CONVERSION_SENTINEL, TEST_CONVERSION_SENTINEL,
        TEST_CONVERSION_SENTINEL, TEST_CONVERSION_SENTINEL};
    const uint32_t expected[4] = {
        TEST_CONVERSION_SENTINEL, TEST_CONVERSION_SENTINEL,
        TEST_CONVERSION_SENTINEL, TEST_CONVERSION_SENTINEL};

    db_convert_rgba8_to_xrgb8888_block(destination, 2U, rgba8, 2U, UINT32_MAX,
                                       2U, 0U, 1U);
    db_convert_rgba8_to_xrgb2101010_block(destination, 1U, rgba8, 1U, 0U, 1U,
                                          1U, 1U);
    db_convert_rgba16f_to_xrgb8888_block(destination, 1U, rgba16f, 1U, 0U, 1U,
                                         1U, 1U);
    db_convert_rgba16f_to_xrgb2101010_block(destination, 1U, rgba16f, 1U, 0U,
                                            1U, 1U, 1U);
    db_convert_rgba16f_to_rgba8888_block(destination, 1U, rgba16f, 1U, 0U, 1U,
                                         1U, 1U, UINT8_MAX);
    db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(destination, rgba8, 1U,
                                                UINT32_MAX, 2U, 0U, 1U);
    db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(destination, rgba16f, 1U, 0U,
                                                  1U, 1U, 1U);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(destination, expected, sizeof(destination)) == 0);

    uint32_t overlapping[5] = {1U, 2U, 3U, 4U, TEST_CONVERSION_SENTINEL};
    const uint32_t overlapping_expected[5] = {1U, 2U, 3U, 4U,
                                              TEST_CONVERSION_SENTINEL};
    db_convert_rgba8_to_xrgb8888_block(overlapping + 1U, 4U, overlapping, 4U,
                                       0U, 1U, 0U, 4U);
    DB_TEST_EXPECT_TRUE(state, memcmp(overlapping, overlapping_expected,
                                      sizeof(overlapping_expected)) == 0);

    db_convert_rgba8_to_xrgb8888_block(overlapping, 4U, overlapping, 4U, 0U, 1U,
                                       0U, 4U);
    DB_TEST_EXPECT_TRUE(state,
                        overlapping[0] == db_pack_xrgb8888_from_rgba8888(1U));
}

static void
db_test_rgba16f_fill_accepts_unaligned_output(db_test_state_t *state) {
    enum { TEST_PIXEL_COUNT = 2U };
    const uint16_t rgba[4] = {1U, 2U, 3U, 4U};
    const uint64_t expected = UINT64_C(1) | (UINT64_C(2) << 16U) |
                              (UINT64_C(3) << 32U) | (UINT64_C(4) << 48U);
    uint8_t bytes[(TEST_PIXEL_COUNT * sizeof(uint64_t)) + 1U] = {0U};
    db_fill_rgba16f_buffer(bytes + 1U, TEST_PIXEL_COUNT, rgba);
    for (size_t pixel = 0U; pixel < TEST_PIXEL_COUNT; pixel++) {
        uint64_t observed = 0U;
        memcpy(&observed, bytes + 1U + (pixel * sizeof(observed)),
               sizeof(observed));
        DB_TEST_EXPECT_EQ_U64(state, observed, expected);
    }
}

static void db_test_numeric_text_rejects_host_overflow(db_test_state_t *state) {
    const char *end = NULL;
    uint32_t u32_value = 0U;
    uint64_t u64_value = 0U;
    int int_value = 0;
    double double_value = 0.0;
    DB_TEST_EXPECT_TRUE(state,
                        db_parse_u32_prefix("4294967295", DB_PARSE_BASE_DECIMAL,
                                            &u32_value, &end));
    DB_TEST_EXPECT_EQ_U32(state, u32_value, UINT32_MAX);
    DB_TEST_EXPECT_TRUE(state, *end == '\0');
    DB_TEST_EXPECT_TRUE(state,
                        db_parse_u32_prefix("4294967296", DB_PARSE_BASE_DECIMAL,
                                            &u32_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_parse_u32_prefix("999999999999999999999999999",
                                            DB_PARSE_BASE_DECIMAL, &u32_value,
                                            &end) == 0);
    DB_TEST_EXPECT_TRUE(state, db_parse_u32_prefix("-1", DB_PARSE_BASE_DECIMAL,
                                                   &u32_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_parse_u32_prefix("1", 1, &u32_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(
        state, db_parse_u64_prefix("ffffffffffffffff!", 16, &u64_value, &end));
    DB_TEST_EXPECT_EQ_U64(state, u64_value, UINT64_MAX);
    DB_TEST_EXPECT_TRUE(state, *end == '!');
    DB_TEST_EXPECT_TRUE(state, db_parse_u64_prefix("10000000000000000", 16,
                                                   &u64_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_parse_u64_prefix("-1", 16, &u64_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(state, db_parse_int_text("2147483648", &int_value) ==
                                   (INT_MAX > INT32_MAX));
    DB_TEST_EXPECT_TRUE(state, db_parse_int_text("999999999999999999999999999",
                                                 &int_value) == 0);
    DB_TEST_EXPECT_TRUE(
        state, db_parse_double_prefix("1e9999", &double_value, &end) == 0);
    DB_TEST_EXPECT_TRUE(
        state, db_parse_double_prefix("nan", &double_value, &end) == 0);
}

unsigned db_numeric_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"u32_range_handles_full_domain",
         db_test_u32_range_handles_full_domain},
        {"next_power_of_two_overflow_is_explicit",
         db_test_next_power_of_two_overflow_is_explicit},
        {"float_boundary_canonicalization",
         db_test_float_boundary_canonicalization},
        {"to_f64_dispatch_is_canonical_and_single_evaluation",
         db_test_to_f64_dispatch_is_canonical_and_single_evaluation},
        {"min_max_helpers_evaluate_once",
         db_test_min_max_helpers_evaluate_once},
        {"integral_boundary_helpers", db_test_integral_boundary_helpers},
        {"f64_extrema_policy", db_test_f64_extrema_policy},
        {"f64_comparison_policies", db_test_f64_comparison_policies},
        {"f16_f32_conversion_is_exact", db_test_f16_f32_conversion_is_exact},
        {"f16_storage_quantization_matches_gpu_boundary",
         db_test_f16_storage_quantization_matches_gpu_boundary},
        {"aligned_calloc_is_zero_initialized",
         db_test_aligned_calloc_is_zero_initialized},
        {"checked_arithmetic_reports_overflow",
         db_test_checked_arithmetic_reports_overflow},
        {"double_to_long_uses_half_open_bounds",
         db_test_double_to_long_uses_half_open_bounds},
        {"byte_ranges_reject_wraparound",
         db_test_byte_ranges_reject_wraparound},
        {"strided_row_copy_is_tight_and_overlap_safe",
         db_test_strided_row_copy_is_tight_and_overlap_safe},
        {"pixel_conversion_rejects_malformed_layouts",
         db_test_pixel_conversion_rejects_malformed_layouts},
        {"rgba16f_fill_accepts_unaligned_output",
         db_test_rgba16f_fill_accepts_unaligned_output},
        {"numeric_text_rejects_host_overflow",
         db_test_numeric_text_rejects_host_overflow},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
