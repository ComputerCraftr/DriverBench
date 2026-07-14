#ifndef DRIVERBENCH_DB_NUMERIC_H
#define DRIVERBENCH_DB_NUMERIC_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define DB_U24_MAX 16777215.0
#define DB_U8_MAX 255.0
#define DB_ROUND_HALF_UP 0.5

#define DB_F16_SIGN_SHIFT 15U
#define DB_F16_EXP_SHIFT 10U
#define DB_F16_EXP_MASK 0x1FU
#define DB_F16_MANT_BITS 10U
#define DB_F16_MANT_MASK 0x03FFU
#define DB_F16_NAN_MANT_QBIT 0x0200U
#define DB_F16_HIDDEN_BIT 0x0400U
#define DB_F16_ONE 0x3C00U
#define DB_F16_SUBNORMAL_MIN_EXP (-10)
#define DB_F16_SUBNORMAL_LDEXP (-24)
#define DB_F16_MIN_NORMAL_UNBIASED_EXP (1 - (int32_t)DB_F16_EXP_BIAS)
#define DB_F16_MAX_NORMAL_UNBIASED_EXP ((int32_t)DB_F16_EXP_BIAS)
#define DB_F16_MIN_ROUNDABLE_UNBIASED_EXP (-25)
#define DB_F16_SUBNORMAL_SCALE_EXP                                             \
    ((int32_t)DB_F16_MANT_BITS + ((int32_t)DB_F16_EXP_BIAS - 1))

#define DB_F32_SIGN_SHIFT 31U
#define DB_F32_EXP_SHIFT 23U
#define DB_F32_EXP_MASK 0xFFU
#define DB_F32_MANT_MASK 0x007FFFFFU
#define DB_F32_EXP_BIAS 127U
#define DB_F32_CANONICAL_NAN_BITS 0x7FC00000U
#define DB_F64_CANONICAL_NAN_BITS UINT64_C(0x7FF8000000000000)
#define DB_F16_EXP_BIAS 15U
#define DB_F16_FROM_F32_MANT_SHIFT 13U

#define DB_PACKED_RGB_SHIFT_RED 16U
#define DB_PACKED_RGB_SHIFT_GREEN 8U
#define DB_PACKED_RGB_SHIFT_BLUE 0U
#define DB_PACKED_RGB_SHIFT_ALPHA 24U

#define DB_MIN(a, b)                                                           \
    _Generic(((a) + (b)),                                                      \
        int: db_min_int,                                                       \
        unsigned int: db_min_uint,                                             \
        long: db_min_long,                                                     \
        unsigned long: db_min_ulong,                                           \
        long long: db_min_llong,                                               \
        unsigned long long: db_min_ullong)((a), (b))
#define DB_MAX(a, b)                                                           \
    _Generic(((a) + (b)),                                                      \
        int: db_max_int,                                                       \
        unsigned int: db_max_uint,                                             \
        long: db_max_long,                                                     \
        unsigned long: db_max_ulong,                                           \
        long long: db_max_llong,                                               \
        unsigned long long: db_max_ullong)((a), (b))
#define DB_CLAMP(val, min_v, max_v) DB_MIN(DB_MAX(val, min_v), max_v)
#define DB_TO_F64(value)                                                       \
    _Generic((value),                                                          \
        _Bool: db_bool_to_f64,                                                 \
        char: db_char_to_f64,                                                  \
        signed char: db_schar_to_f64,                                          \
        unsigned char: db_uchar_to_f64,                                        \
        short: db_short_to_f64,                                                \
        unsigned short: db_ushort_to_f64,                                      \
        int: db_int_to_f64,                                                    \
        unsigned int: db_uint_to_f64,                                          \
        long: db_long_to_f64,                                                  \
        unsigned long: db_ulong_to_f64,                                        \
        long long: db_llong_to_f64,                                            \
        unsigned long long: db_ullong_to_f64,                                  \
        float: db_f32_to_double,                                               \
        double: db_f64_identity)((value))
// Canonical boolean normalization helper for int/flag boundaries.
#define DB_BOOL(value) ((value) != 0 ? 1 : 0)

static inline int db_min_int(int lhs, int rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline int db_max_int(int lhs, int rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline unsigned int db_min_uint(unsigned int lhs, unsigned int rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline unsigned int db_max_uint(unsigned int lhs, unsigned int rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline long db_min_long(long lhs, long rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline long db_max_long(long lhs, long rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline unsigned long db_min_ulong(unsigned long lhs, unsigned long rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline unsigned long db_max_ulong(unsigned long lhs, unsigned long rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline long long db_min_llong(long long lhs, long long rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline long long db_max_llong(long long lhs, long long rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline unsigned long long db_min_ullong(unsigned long long lhs,
                                               unsigned long long rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline unsigned long long db_max_ullong(unsigned long long lhs,
                                               unsigned long long rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline double db_canonical_nan_f64(void) {
    union {
        double f64;
        uint64_t u64;
    } pun = {.u64 = DB_F64_CANONICAL_NAN_BITS};
    return pun.f64;
}

// Deterministic IEEE minimumNumber/maximumNumber behavior. A numeric operand
// wins over NaN, two NaNs canonicalize, and signed-zero ordering is explicit.
static inline double db_min_f64(double lhs, double rhs) {
    if (isnan(lhs)) {
        return isnan(rhs) ? db_canonical_nan_f64() : rhs;
    }
    if (isnan(rhs)) {
        return lhs;
    }
    if (lhs < rhs) {
        return lhs;
    }
    if (rhs < lhs) {
        return rhs;
    }
    if ((lhs <= 0.0) && (lhs >= 0.0) &&
        ((signbit(lhs) != 0) || (signbit(rhs) != 0))) {
        return -0.0;
    }
    return lhs;
}

static inline double db_max_f64(double lhs, double rhs) {
    if (isnan(lhs)) {
        return isnan(rhs) ? db_canonical_nan_f64() : rhs;
    }
    if (isnan(rhs)) {
        return lhs;
    }
    if (lhs > rhs) {
        return lhs;
    }
    if (rhs > lhs) {
        return rhs;
    }
    if ((lhs <= 0.0) && (lhs >= 0.0) &&
        ((signbit(lhs) == 0) || (signbit(rhs) == 0))) {
        return 0.0;
    }
    return lhs;
}

static inline double db_clamp_f64_finite_or(double value, double minimum,
                                            double maximum,
                                            double nonfinite_value) {
    if (!isfinite(value)) {
        return nonfinite_value;
    }
    return db_min_f64(db_max_f64(value, minimum), maximum);
}

static inline double db_f64_positive_finite_or_zero(double value) {
    if (!isfinite(value) || (value <= 0.0)) {
        return 0.0;
    }
    return value;
}

static inline double db_f64_reciprocal_positive_finite_or(double value,
                                                          double fallback) {
    if (!isfinite(value) || (value <= 0.0)) {
        return fallback;
    }
    return 1.0 / value;
}

static inline uint32_t db_u32_next_pow2(uint32_t value) {
    if (value <= 1U) {
        return 1U;
    }
    value--;
    value |= value >> 1U;
    value |= value >> 2U;
    value |= value >> 4U;
    value |= value >> 8U;
    value |= value >> 16U;
    if (value == UINT32_MAX) {
        return 0U;
    }
    return value + 1U;
}

static inline uint32_t db_u32_saturating_sub(uint32_t lhs, uint32_t rhs) {
    if (lhs <= rhs) {
        return 0U;
    }
    return lhs - rhs;
}

static inline uint64_t db_u64_saturating_sub(uint64_t lhs, uint64_t rhs) {
    if (lhs <= rhs) {
        return 0U;
    }
    return lhs - rhs;
}

static inline uint64_t db_u64_saturating_add(uint64_t lhs, uint64_t rhs) {
    if (rhs > (UINT64_MAX - lhs)) {
        return UINT64_MAX;
    }
    return lhs + rhs;
}

static inline uint64_t db_u64_abs_diff(uint64_t lhs, uint64_t rhs) {
    if (lhs > rhs) {
        return lhs - rhs;
    }
    return rhs - lhs;
}

static inline size_t db_size_add_or_zero(size_t lhs, size_t rhs) {
    if (rhs > (SIZE_MAX - lhs)) {
        return 0U;
    }
    return lhs + rhs;
}

static inline size_t db_positive_i32_to_size_or_zero(int32_t value) {
    if (value <= 0) {
        return 0U;
    }
    return (size_t)value;
}

static inline uint32_t db_u32_wrapping_sub(uint32_t lhs, uint32_t rhs) {
    return lhs - rhs;
}

static inline uint32_t db_u32_range(uint32_t seed, uint32_t min_value,
                                    uint32_t max_value) {
    if (max_value <= min_value) {
        return min_value;
    }
    const uint64_t span = (uint64_t)max_value - min_value + 1U;
    return min_value + (uint32_t)((uint64_t)seed % span);
}

// Deterministic scalar mapping helpers.
// - u32 -> unit-f64/range-f64: stable mapping for benchmark/state generation.
static inline double db_u32_to_unit_f64(uint32_t value) {
    const uint32_t value_24 = value >> 8U;
    return (double)value_24 / DB_U24_MAX;
}

static inline double db_u32_to_range_f64(uint32_t value, double min_value,
                                         double max_value) {
    if (max_value <= min_value) {
        return min_value;
    }
    return min_value + (db_u32_to_unit_f64(value) * (max_value - min_value));
}

static inline double db_u8_to_unit_f64(uint32_t value_u8) {
    return (double)(value_u8 & UINT8_MAX) / DB_U8_MAX;
}

// Deterministic narrowing/conversion policy:
// - Internal benchmark/state/color math stays in f64.
// - f64 -> f32 is only for direct GPU-facing float consumption boundaries
//   such as GL/Vulkan vertex data, uniforms, and float upload payloads.
// - f64 in [0, 1] -> u8 is the rgba8 CPU pixel export boundary.
// - RGBA16F working storage uses the explicitly named f64 -> f32 -> f16 path
//   so CPU storage matches GL/Vulkan's f32 shader-input quantization. Direct
//   f64 -> f16 remains available only as a numeric/reference conversion.
// - f32 -> f64 and f16 -> f64 are canonical CPU import/readback boundaries;
//   direct f16 <-> f32 helpers serve GPU/storage boundaries without widening.
// - Canonicalization removes representation-only variation (-0, NaN payloads)
//   while preserving meaningful sign where the destination format supports it.
static inline uint32_t db_f32_to_bits_u32(float value) {
    union {
        float f32;
        uint32_t u32;
    } pun = {.f32 = value};
    return pun.u32;
}

static inline uint64_t db_f64_to_bits_u64(double value) {
    union {
        double f64;
        uint64_t u64;
    } pun = {.f64 = value};
    return pun.u64;
}

static inline float db_bits_u32_to_f32(uint32_t value_bits) {
    union {
        float f32;
        uint32_t u32;
    } pun = {.u32 = value_bits};
    return pun.f32;
}

static inline double db_bits_u64_to_f64(uint64_t value_bits) {
    union {
        double f64;
        uint64_t u64;
    } pun = {.u64 = value_bits};
    return pun.f64;
}

static inline double db_bool_to_f64(_Bool value) { return (double)value; }

static inline double db_char_to_f64(char value) { return (double)value; }

static inline double db_schar_to_f64(signed char value) {
    return (double)value;
}

static inline double db_uchar_to_f64(unsigned char value) {
    return (double)value;
}

static inline double db_short_to_f64(short value) { return (double)value; }

static inline double db_ushort_to_f64(unsigned short value) {
    return (double)value;
}

static inline double db_int_to_f64(int value) { return (double)value; }

static inline double db_uint_to_f64(unsigned int value) {
    return (double)value;
}

static inline double db_long_to_f64(long value) { return (double)value; }

static inline double db_ulong_to_f64(unsigned long value) {
    return (double)value;
}

static inline double db_llong_to_f64(long long value) { return (double)value; }

static inline double db_ullong_to_f64(unsigned long long value) {
    return (double)value;
}

static inline double db_f64_identity(double value) { return value; }

static inline float db_double_to_f32(double value) {
    if (isnan(value) != 0) {
        return db_bits_u32_to_f32(DB_F32_CANONICAL_NAN_BITS);
    }
    if ((db_f64_to_bits_u64(value) & INT64_MAX) == 0U) {
        return 0.0F;
    }
    return (float)value;
}

static inline double db_f32_to_double(float value) {
    if (isnan(value) != 0) {
        return db_bits_u64_to_f64(DB_F64_CANONICAL_NAN_BITS);
    }
    if ((db_f32_to_bits_u32(value) & INT32_MAX) == 0U) {
        return 0.0;
    }
    return (double)value;
}

static inline float db_u32_to_f32(uint32_t value) {
    return db_double_to_f32((double)value);
}

static inline float db_i32_to_f32(int32_t value) {
    return db_double_to_f32((double)value);
}

static inline float db_u32_ratio_to_f32(uint32_t numerator,
                                        uint32_t denominator) {
    // Callers must validate denominator != 0 to keep divide-by-zero policy
    // explicit at use sites.
    return db_double_to_f32((double)numerator / (double)denominator);
}

// Exact canonical math equality: signed zero compares equal and NaNs do not.
// Use representation equality only for backend payload/cache comparisons.
static inline int db_equal_f64(double lhs, double rhs) {
    return (lhs <= rhs) && (lhs >= rhs);
}

static inline int db_equal_f64_rgb3(const double *lhs, const double *rhs) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }
    return (db_equal_f64(lhs[0], rhs[0]) != 0) &&
           (db_equal_f64(lhs[1], rhs[1]) != 0) &&
           (db_equal_f64(lhs[2], rhs[2]) != 0);
}

// Deterministic total ordering used by canonical sorting. Numeric values sort
// first, -0 precedes +0, and NaNs sort last by representation.
static inline int db_compare_f64_total(double lhs, double rhs) {
    const int lhs_nan = isnan(lhs);
    const int rhs_nan = isnan(rhs);
    if ((lhs_nan != 0) || (rhs_nan != 0)) {
        if (lhs_nan != rhs_nan) {
            return (lhs_nan != 0) ? 1 : -1;
        }
        const uint64_t lhs_bits = db_f64_to_bits_u64(lhs);
        const uint64_t rhs_bits = db_f64_to_bits_u64(rhs);
        return (lhs_bits > rhs_bits) - (lhs_bits < rhs_bits);
    }
    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    const int lhs_negative = DB_BOOL(signbit(lhs));
    const int rhs_negative = DB_BOOL(signbit(rhs));
    return (rhs_negative > lhs_negative) - (rhs_negative < lhs_negative);
}

static inline int db_compare_u32(uint32_t lhs, uint32_t rhs) {
    return (lhs > rhs) - (lhs < rhs);
}

static inline uint8_t db_double01_to_u8_clamped(double value01) {
    double clamped = value01;
    if (isnan(clamped) || (clamped < 0.0)) {
        clamped = 0.0;
    } else if (clamped > 1.0) {
        clamped = 1.0;
    }
    double scaled = (clamped * DB_U8_MAX) + DB_ROUND_HALF_UP;
    if (scaled > DB_U8_MAX) {
        scaled = DB_U8_MAX;
    }
    return (uint8_t)scaled;
}

static inline void db_rgba01_to_u8_rgba4(const double *rgba01,
                                         uint8_t *rgba_u8_out) {
    if ((rgba01 == NULL) || (rgba_u8_out == NULL)) {
        return;
    }
    rgba_u8_out[0] = db_double01_to_u8_clamped(rgba01[0]);
    rgba_u8_out[1] = db_double01_to_u8_clamped(rgba01[1]);
    rgba_u8_out[2] = db_double01_to_u8_clamped(rgba01[2]);
    rgba_u8_out[3] = db_double01_to_u8_clamped(rgba01[3]);
}

static inline void db_rgb_f64_to_f32_rgb3(const double *rgb, float *rgb_out) {
    if ((rgb == NULL) || (rgb_out == NULL)) {
        return;
    }
    rgb_out[0] = db_double_to_f32(rgb[0]);
    rgb_out[1] = db_double_to_f32(rgb[1]);
    rgb_out[2] = db_double_to_f32(rgb[2]);
}

static inline void db_rgba_f64_to_f32_rgba4(const double *rgba,
                                            float *rgba_out) {
    if ((rgba == NULL) || (rgba_out == NULL)) {
        return;
    }
    rgba_out[0] = db_double_to_f32(rgba[0]);
    rgba_out[1] = db_double_to_f32(rgba[1]);
    rgba_out[2] = db_double_to_f32(rgba[2]);
    rgba_out[3] = db_double_to_f32(rgba[3]);
}

static inline void db_rgb_f32_to_f64_rgb3(const float *rgb, double *rgb_out) {
    if ((rgb == NULL) || (rgb_out == NULL)) {
        return;
    }
    rgb_out[0] = db_f32_to_double(rgb[0]);
    rgb_out[1] = db_f32_to_double(rgb[1]);
    rgb_out[2] = db_f32_to_double(rgb[2]);
}

static inline void db_f32_rgb_to_bits_u32_rgb3(const float *rgb,
                                               uint32_t *bits_out) {
    if ((rgb == NULL) || (bits_out == NULL)) {
        return;
    }
    bits_out[0] = db_f32_to_bits_u32(rgb[0]);
    bits_out[1] = db_f32_to_bits_u32(rgb[1]);
    bits_out[2] = db_f32_to_bits_u32(rgb[2]);
}

static inline void db_bits_u32_rgb3_to_f32_rgb3(const uint32_t *bits_in,
                                                float *rgb_out) {
    if ((bits_in == NULL) || (rgb_out == NULL)) {
        return;
    }
    rgb_out[0] = db_bits_u32_to_f32(bits_in[0]);
    rgb_out[1] = db_bits_u32_to_f32(bits_in[1]);
    rgb_out[2] = db_bits_u32_to_f32(bits_in[2]);
}

static inline int db_equal_f32_rgb3(const float *lhs, const float *rhs) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }
    return (db_f32_to_bits_u32(lhs[0]) == db_f32_to_bits_u32(rhs[0])) &&
           (db_f32_to_bits_u32(lhs[1]) == db_f32_to_bits_u32(rhs[1])) &&
           (db_f32_to_bits_u32(lhs[2]) == db_f32_to_bits_u32(rhs[2]));
}

static inline int db_equal_u32_rgb3(const uint32_t *lhs, const uint32_t *rhs) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }
    return (lhs[0] == rhs[0]) && (lhs[1] == rhs[1]) && (lhs[2] == rhs[2]);
}

// Deterministic pack/unpack helpers built on narrowing policy functions.
static inline uint32_t db_pack_xrgb8888_from_rgb_u8(uint32_t red_u8,
                                                    uint32_t green_u8,
                                                    uint32_t blue_u8) {
    return (red_u8 << DB_PACKED_RGB_SHIFT_RED) |
           (green_u8 << DB_PACKED_RGB_SHIFT_GREEN) |
           (blue_u8 << DB_PACKED_RGB_SHIFT_BLUE);
}

static inline uint32_t db_pack_rgba8888_from_rgb_u8(uint32_t red_u8,
                                                    uint32_t green_u8,
                                                    uint32_t blue_u8,
                                                    uint32_t alpha_u8) {
    return (alpha_u8 << DB_PACKED_RGB_SHIFT_ALPHA) |
           (blue_u8 << DB_PACKED_RGB_SHIFT_RED) |
           (green_u8 << DB_PACKED_RGB_SHIFT_GREEN) |
           (red_u8 << DB_PACKED_RGB_SHIFT_BLUE);
}

static inline uint32_t db_pack_rgba8888_from_rgb01(double red, double green,
                                                   double blue,
                                                   uint32_t alpha_u8) {
    const uint8_t red_u8 = db_double01_to_u8_clamped(red);
    const uint8_t green_u8 = db_double01_to_u8_clamped(green);
    const uint8_t blue_u8 = db_double01_to_u8_clamped(blue);
    return db_pack_rgba8888_from_rgb_u8(red_u8, green_u8, blue_u8, alpha_u8);
}

static inline uint32_t db_round_positive_to_u32_ties_even(double value) {
    const double floor_value = floor(value);
    const double frac = value - floor_value;
    uint32_t rounded = (uint32_t)floor_value;
    const int exactly_half =
        db_f64_to_bits_u64(frac) == db_f64_to_bits_u64(DB_ROUND_HALF_UP);
    if ((frac > DB_ROUND_HALF_UP) ||
        ((exactly_half != 0) && ((rounded & 1U) != 0U))) {
        rounded++;
    }
    return rounded;
}

// f16-specific conversion details:
// - Mantissa rounding uses ties-to-even.
// - Subnormal overflow during rounding carries into the smallest normal value.
static inline uint16_t db_double_to_f16(double value) {
    const uint32_t sign = (uint32_t)DB_BOOL(signbit(value))
                          << DB_F16_SIGN_SHIFT;
    const double abs_value = fabs(value);
    if (isnan(value) != 0) {
        return (uint16_t)((DB_F16_EXP_MASK << DB_F16_EXP_SHIFT) |
                          DB_F16_NAN_MANT_QBIT);
    }
    if (isinf(value) != 0) {
        return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
    }
    if ((db_f64_to_bits_u64(abs_value) & INT64_MAX) == 0U) {
        return 0U;
    }

    int exp2 = 0;
    const double normalized = frexp(abs_value, &exp2);
    const int32_t exp_unbiased = (int32_t)exp2 - 1;
    int32_t f16_exp = exp_unbiased + (int32_t)DB_F16_EXP_BIAS;
    if (f16_exp >= (int32_t)DB_F16_EXP_MASK) {
        return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
    }

    if (exp_unbiased < DB_F16_MIN_NORMAL_UNBIASED_EXP) {
        const double scaled =
            ldexp(abs_value, DB_F16_SUBNORMAL_SCALE_EXP); // value * 2^24
        uint32_t mantissa = db_round_positive_to_u32_ties_even(scaled);
        if (mantissa > DB_F16_MANT_MASK) {
            // Subnormal rounding can carry into smallest normal value.
            f16_exp = 1;
            mantissa = 0U;
            return (uint16_t)(sign | ((uint32_t)f16_exp << DB_F16_EXP_SHIFT) |
                              mantissa);
        }
        return (uint16_t)(sign | (mantissa & DB_F16_MANT_MASK));
    }

    const double significand = ldexp(normalized, 1); // [1, 2)
    const double mantissa_value =
        ldexp(significand - 1.0, (int32_t)DB_F16_MANT_BITS);
    uint32_t mantissa = db_round_positive_to_u32_ties_even(mantissa_value);
    if (mantissa > DB_F16_MANT_MASK) {
        mantissa = 0U;
        f16_exp++;
        if (f16_exp >= (int32_t)DB_F16_EXP_MASK) {
            return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
        }
    }
    return (uint16_t)(sign | ((uint32_t)f16_exp << DB_F16_EXP_SHIFT) |
                      mantissa);
}

// Convert directly between IEEE binary32 and binary16. These helpers avoid
// widening through f64 at GPU/storage boundaries while retaining canonical
// zero and NaN representations.
static inline uint16_t db_f32_to_f16(float value) {
    const uint32_t bits = db_f32_to_bits_u32(value);
    const uint32_t sign = (bits >> DB_F32_SIGN_SHIFT) << DB_F16_SIGN_SHIFT;
    const uint32_t exp = (bits >> DB_F32_EXP_SHIFT) & DB_F32_EXP_MASK;
    const uint32_t mantissa = bits & DB_F32_MANT_MASK;
    if (exp == DB_F32_EXP_MASK) {
        if (mantissa != 0U) {
            return (uint16_t)((DB_F16_EXP_MASK << DB_F16_EXP_SHIFT) |
                              DB_F16_NAN_MANT_QBIT);
        }
        return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
    }
    if ((exp == 0U) && (mantissa == 0U)) {
        return 0U;
    }

    const int32_t exp_unbiased = (int32_t)exp - (int32_t)DB_F32_EXP_BIAS;
    if (exp_unbiased > DB_F16_MAX_NORMAL_UNBIASED_EXP) {
        return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
    }

    uint32_t rounded_mantissa = 0U;
    if (exp_unbiased >= DB_F16_MIN_NORMAL_UNBIASED_EXP) {
        uint32_t f16_exp = (uint32_t)(exp_unbiased + (int32_t)DB_F16_EXP_BIAS);
        rounded_mantissa = mantissa >> DB_F16_FROM_F32_MANT_SHIFT;
        const uint32_t discarded_mask = (1U << DB_F16_FROM_F32_MANT_SHIFT) - 1U;
        const uint32_t discarded = mantissa & discarded_mask;
        const uint32_t halfway = 1U << (DB_F16_FROM_F32_MANT_SHIFT - 1U);
        if ((discarded > halfway) ||
            ((discarded == halfway) && ((rounded_mantissa & 1U) != 0U))) {
            rounded_mantissa++;
        }
        if (rounded_mantissa > DB_F16_MANT_MASK) {
            rounded_mantissa = 0U;
            f16_exp++;
            if (f16_exp >= DB_F16_EXP_MASK) {
                return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
            }
        }
        return (uint16_t)(sign | (f16_exp << DB_F16_EXP_SHIFT) |
                          rounded_mantissa);
    }

    if (exp_unbiased < DB_F16_MIN_ROUNDABLE_UNBIASED_EXP) {
        return 0U;
    }
    const uint32_t significand =
        DB_F16_HIDDEN_BIT << DB_F16_FROM_F32_MANT_SHIFT | mantissa;
    const uint32_t shift =
        (uint32_t)(-14 - exp_unbiased) + DB_F16_FROM_F32_MANT_SHIFT;
    rounded_mantissa = significand >> shift;
    const uint32_t discarded_mask = (1U << shift) - 1U;
    const uint32_t discarded = significand & discarded_mask;
    const uint32_t halfway = 1U << (shift - 1U);
    if ((discarded > halfway) ||
        ((discarded == halfway) && ((rounded_mantissa & 1U) != 0U))) {
        rounded_mantissa++;
    }
    return (uint16_t)(sign | rounded_mantissa);
}

static inline uint16_t db_f64_to_f16_via_f32(double value) {
    return db_f32_to_f16(db_double_to_f32(value));
}

static inline double db_f16_to_double(uint16_t value) {
    const uint32_t sign = ((uint32_t)value >> DB_F16_SIGN_SHIFT) & 1U;
    const uint32_t exp =
        ((uint32_t)value >> DB_F16_EXP_SHIFT) & DB_F16_EXP_MASK;
    const uint32_t mant = (uint32_t)value & DB_F16_MANT_MASK;
    if (exp == DB_F16_EXP_MASK) {
        if (mant == 0U) {
            return (sign != 0U) ? -HUGE_VAL : HUGE_VAL;
        }
        return nan("");
    }

    if (exp == 0U) {
        if (mant == 0U) {
            return 0.0;
        }
        const double subnormal =
            ldexp((double)mant, DB_F16_SUBNORMAL_LDEXP); // mant / 2^10 * 2^-14
        return (sign != 0U) ? -subnormal : subnormal;
    }

    const int32_t exp_unbiased = (int32_t)exp - (int32_t)DB_F16_EXP_BIAS;
    const double significand =
        1.0 + ((double)mant / (double)(1U << DB_F16_MANT_BITS));
    const double normal = ldexp(significand, exp_unbiased);
    return (sign != 0U) ? -normal : normal;
}

static inline float db_f16_to_f32(uint16_t value) {
    const uint32_t sign = ((uint32_t)value >> DB_F16_SIGN_SHIFT) & 1U;
    uint32_t exp = ((uint32_t)value >> DB_F16_EXP_SHIFT) & DB_F16_EXP_MASK;
    uint32_t mantissa = (uint32_t)value & DB_F16_MANT_MASK;
    if (exp == DB_F16_EXP_MASK) {
        if (mantissa != 0U) {
            return db_bits_u32_to_f32(DB_F32_CANONICAL_NAN_BITS);
        }
        return db_bits_u32_to_f32((sign << DB_F32_SIGN_SHIFT) |
                                  (DB_F32_EXP_MASK << DB_F32_EXP_SHIFT));
    }
    if (exp == 0U) {
        if (mantissa == 0U) {
            return 0.0F;
        }
        int32_t exp_unbiased = DB_F16_MIN_NORMAL_UNBIASED_EXP;
        while ((mantissa & DB_F16_HIDDEN_BIT) == 0U) {
            mantissa <<= 1U;
            exp_unbiased--;
        }
        mantissa &= DB_F16_MANT_MASK;
        exp = (uint32_t)(exp_unbiased + (int32_t)DB_F32_EXP_BIAS);
    } else {
        exp = (uint32_t)((int32_t)exp - (int32_t)DB_F16_EXP_BIAS +
                         (int32_t)DB_F32_EXP_BIAS);
    }
    return db_bits_u32_to_f32((sign << DB_F32_SIGN_SHIFT) |
                              (exp << DB_F32_EXP_SHIFT) |
                              (mantissa << DB_F16_FROM_F32_MANT_SHIFT));
}

static inline void db_rgb_f64_quantize_f16_to_f32_rgb3(const double *rgb,
                                                       float *rgb_out) {
    if ((rgb == NULL) || (rgb_out == NULL)) {
        return;
    }
    for (uint32_t channel = 0U; channel < 3U; channel++) {
        rgb_out[channel] = db_f16_to_f32(db_f64_to_f16_via_f32(rgb[channel]));
    }
}

static inline void db_rgb_f16_to_f64_rgb3(const uint16_t *rgb_f16,
                                          double *rgb_out) {
    if ((rgb_f16 == NULL) || (rgb_out == NULL)) {
        return;
    }
    rgb_out[0] = db_f16_to_double(rgb_f16[0]);
    rgb_out[1] = db_f16_to_double(rgb_f16[1]);
    rgb_out[2] = db_f16_to_double(rgb_f16[2]);
}

static inline void db_blend_rgb3(const double *prior_rgb,
                                 const double *target_rgb, double blend_factor,
                                 double *out_rgb) {
    if ((prior_rgb == NULL) || (target_rgb == NULL) || (out_rgb == NULL)) {
        return;
    }
    if (blend_factor <= 0.0) {
        out_rgb[0] = prior_rgb[0];
        out_rgb[1] = prior_rgb[1];
        out_rgb[2] = prior_rgb[2];
        return;
    }
    if (blend_factor >= 1.0) {
        out_rgb[0] = target_rgb[0];
        out_rgb[1] = target_rgb[1];
        out_rgb[2] = target_rgb[2];
        return;
    }
    out_rgb[0] = prior_rgb[0] + ((target_rgb[0] - prior_rgb[0]) * blend_factor);
    out_rgb[1] = prior_rgb[1] + ((target_rgb[1] - prior_rgb[1]) * blend_factor);
    out_rgb[2] = prior_rgb[2] + ((target_rgb[2] - prior_rgb[2]) * blend_factor);
}

#endif
