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
#define DB_F16_SUBNORMAL_MIN_EXP (-10)
#define DB_F16_SUBNORMAL_LDEXP (-24)
#define DB_F16_MIN_NORMAL_UNBIASED_EXP (1 - (int32_t)DB_F16_EXP_BIAS)
#define DB_F16_SUBNORMAL_SCALE_EXP                                             \
    ((int32_t)DB_F16_MANT_BITS + ((int32_t)DB_F16_EXP_BIAS - 1))

#define DB_F32_SIGN_SHIFT 31U
#define DB_F32_EXP_SHIFT 23U
#define DB_F32_EXP_MASK 0xFFU
#define DB_F32_MANT_MASK 0x007FFFFFU
#define DB_F32_EXP_BIAS 127U
#define DB_F16_EXP_BIAS 15U
#define DB_F16_FROM_F32_MANT_SHIFT 13U

#define DB_PACKED_RGB_SHIFT_RED 16U
#define DB_PACKED_RGB_SHIFT_GREEN 8U
#define DB_PACKED_RGB_SHIFT_BLUE 0U
#define DB_PACKED_RGB_SHIFT_ALPHA 24U

static inline uint32_t db_u32_min(uint32_t lhs, uint32_t rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline uint32_t db_u32_max(uint32_t lhs, uint32_t rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline uint32_t db_u32_clamp(uint32_t value, uint32_t min_value,
                                    uint32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
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
    return value + 1U;
}

static inline uint32_t db_u32_saturating_sub(uint32_t lhs, uint32_t rhs) {
    return (lhs > rhs) ? (lhs - rhs) : 0U;
}

static inline uint32_t db_u32_wrapping_sub(uint32_t lhs, uint32_t rhs) {
    return lhs - rhs;
}

static inline uint32_t db_u32_range(uint32_t seed, uint32_t min_value,
                                    uint32_t max_value) {
    if (max_value <= min_value) {
        return min_value;
    }
    return min_value + (seed % (max_value - min_value + 1U));
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
// - f64 -> f32: default IEEE cast, then canonicalize NaN and zero.
// - f32 -> f64: widen, then canonicalize NaN and zero.
// - f64 in [0, 1] -> u8: treat NaN as 0, clamp, then round-half-up.
// - f64 <-> f16: canonicalize NaN and zero in both directions.
// - Sign is preserved only for finite non-zero values and infinities where the
//   destination format can represent sign.
//
// These rules remove representation-only variation (-0, signed NaN payloads)
// from deterministic state/pixel processing while preserving meaningful sign
// where it is representable.
static inline float db_double_to_f32(double value) {
    if (isnan(value) != 0) {
        return nanf("");
    }
    if (value == 0.0) {
        return 0.0F;
    }
    return (float)value;
}

static inline double db_f32_to_double(float value) {
    if (isnan(value) != 0) {
        return nan("");
    }
    if (value == 0.0F) {
        return 0.0;
    }
    return (double)value;
}

static inline float db_u32_to_f32(uint32_t value) {
    return db_double_to_f32((double)value);
}

static inline float db_u32_ratio_to_f32(uint32_t numerator,
                                        uint32_t denominator) {
    // Callers must validate denominator != 0 to keep divide-by-zero policy
    // explicit at use sites.
    return db_double_to_f32((double)numerator / (double)denominator);
}

static inline uint32_t db_f32_to_bits_u32(float value) {
    union {
        float f32;
        uint32_t u32;
    } pun = {.f32 = value};
    return pun.u32;
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

static inline int db_equal_f32_rgb3(const float *lhs, const float *rhs) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }
    return (lhs[0] == rhs[0]) && (lhs[1] == rhs[1]) && (lhs[2] == rhs[2]);
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
    if ((frac > DB_ROUND_HALF_UP) ||
        ((frac == DB_ROUND_HALF_UP) && ((rounded & 1U) != 0U))) {
        rounded++;
    }
    return rounded;
}

// f16-specific conversion details:
// - Mantissa rounding uses ties-to-even.
// - Subnormal overflow during rounding carries into the smallest normal value.
static inline uint16_t db_double_to_f16(double value) {
    const uint32_t sign =
        (signbit(value) != 0) ? (1U << DB_F16_SIGN_SHIFT) : 0U;
    const double abs_value = fabs(value);
    if (isnan(value) != 0) {
        return (uint16_t)((DB_F16_EXP_MASK << DB_F16_EXP_SHIFT) |
                          DB_F16_NAN_MANT_QBIT);
    }
    if (isinf(value) != 0) {
        return (uint16_t)(sign | (DB_F16_EXP_MASK << DB_F16_EXP_SHIFT));
    }
    if (abs_value == 0.0) {
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

static inline double db_f16_to_double(uint16_t value) {
    const uint32_t sign = ((uint32_t)value >> DB_F16_SIGN_SHIFT) & 1U;
    const uint32_t exp =
        ((uint32_t)value >> DB_F16_EXP_SHIFT) & DB_F16_EXP_MASK;
    const uint32_t mant = (uint32_t)value & DB_F16_MANT_MASK;
    if (exp == DB_F16_EXP_MASK) {
        if (mant == 0U) {
            return (sign != 0U) ? -INFINITY : INFINITY;
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
