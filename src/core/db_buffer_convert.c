#include "db_buffer_convert.h"

#include "db_core.h"
#include "db_format_contract.h"
#include "db_numeric.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

// Format/layout constants used by all fill/convert helpers in this unit.
#define DB_CONVERT_CHANNEL_R 0U
#define DB_CONVERT_CHANNEL_G 1U
#define DB_CONVERT_CHANNEL_B 2U
#define DB_CONVERT_CHANNEL_A 3U
#define DB_CONVERT_F16_LUT_SIZE (UINT16_MAX + 1U)
#define DB_CONVERT_RGBA16F_PIXEL_STRIDE 4U
#define DB_CONVERT_RGBA8_PIXEL_STRIDE 4U

enum {
    DB_HDR10_CODE_MAX = 1023,
    DB_HDR10_GREEN_SHIFT = 10,
    DB_HDR10_RED_SHIFT = 20,
    DB_HDR10_ALPHA_SHIFT = 30,
    DB_HDR10_ALPHA_OPAQUE = 3,
};

static const double db_hdr10_round_to_nearest_offset = 0.5;

static double db_hdr10_finite_nonnegative(double value) {
    return db_f64_positive_finite_or_zero(value);
}

double db_hdr10_pq_encode_nits(double luminance_nits) {
    static const double m1 = 2610.0 / 16384.0;
    static const double m2 = 2523.0 / 32.0;
    static const double c1 = 3424.0 / 4096.0;
    static const double c2 = 2413.0 / 128.0;
    static const double c3 = 2392.0 / 128.0;
    const double normalized =
        db_min_f64(db_hdr10_finite_nonnegative(luminance_nits), 10000.0) /
        10000.0;
    const double powered = pow(normalized, m1);
    return pow((c1 + (c2 * powered)) / (1.0 + (c3 * powered)), m2);
}

void db_hdr10_linear_srgb_to_bt2020_pq(const double *linear_srgb,
                                       double *encoded_bt2020_pq) {
    if ((linear_srgb == NULL) || (encoded_bt2020_pq == NULL)) {
        return;
    }
    const double red = db_hdr10_finite_nonnegative(linear_srgb[0]);
    const double green = db_hdr10_finite_nonnegative(linear_srgb[1]);
    const double blue = db_hdr10_finite_nonnegative(linear_srgb[2]);
    const double bt2020[] = {
        (0.6274038959 * red) + (0.3292830384 * green) + (0.0433130657 * blue),
        (0.0690972894 * red) + (0.9195403951 * green) + (0.0113623156 * blue),
        (0.0163914389 * red) + (0.0880133079 * green) + (0.8955952532 * blue),
    };
    for (size_t channel = 0U; channel < 3U; channel++) {
        const double nits =
            db_min_f64(bt2020[channel] * DB_HDR10_REFERENCE_WHITE_NITS,
                       DB_HDR10_MASTERING_MAX_NITS);
        encoded_bt2020_pq[channel] = db_hdr10_pq_encode_nits(nits);
    }
}

static uint32_t db_hdr10_quantize_10(double value) {
    const double clamped = db_clamp_f64_finite_or(value, 0.0, 1.0, 0.0);
    return (uint32_t)((clamped * DB_TO_F64(DB_HDR10_CODE_MAX)) +
                      db_hdr10_round_to_nearest_offset);
}

uint32_t db_pack_xrgb2101010_from_linear_srgb(const double *linear_srgb) {
    double encoded[3] = {0.0, 0.0, 0.0};
    db_hdr10_linear_srgb_to_bt2020_pq(linear_srgb, encoded);
    return (db_hdr10_quantize_10(encoded[0]) << DB_HDR10_RED_SHIFT) |
           (db_hdr10_quantize_10(encoded[1]) << DB_HDR10_GREEN_SHIFT) |
           db_hdr10_quantize_10(encoded[2]);
}

uint32_t db_pack_xrgb2101010_from_rgba8888(uint32_t packed_rgba) {
    double rgb[3] = {0.0, 0.0, 0.0};
    db_unpack_rgba8888_rgb01(packed_rgba, rgb);
    return db_pack_xrgb2101010_from_linear_srgb(rgb);
}

uint32_t db_pack_xrgb2101010_from_rgb16f3(const uint16_t *rgb16f3) {
    if (rgb16f3 == NULL) {
        return 0U;
    }
    const double rgb[] = {db_f16_to_double(rgb16f3[0]),
                          db_f16_to_double(rgb16f3[1]),
                          db_f16_to_double(rgb16f3[2])};
    return db_pack_xrgb2101010_from_linear_srgb(rgb);
}

uint32_t db_pack_rgb10a2_bt2020_pq_from_rgba8888(uint32_t packed_rgba) {
    return db_pack_xrgb2101010_from_rgba8888(packed_rgba) |
           ((uint32_t)DB_HDR10_ALPHA_OPAQUE << DB_HDR10_ALPHA_SHIFT);
}

uint32_t db_pack_rgb10a2_bt2020_pq_from_rgb16f3(const uint16_t *rgb16f3) {
    return db_pack_xrgb2101010_from_rgb16f3(rgb16f3) |
           ((uint32_t)DB_HDR10_ALPHA_OPAQUE << DB_HDR10_ALPHA_SHIFT);
}

void db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(
    uint32_t *dst, const uint32_t *src, size_t src_stride_pixels,
    uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count) {
    if ((dst == NULL) || (src == NULL)) {
        return;
    }
    for (uint32_t row = 0U; row < row_count; row++) {
        const uint32_t *const src_row =
            src + (((size_t)row_start + row) * src_stride_pixels) + col_start;
        uint32_t *const dst_row = dst + ((size_t)row * col_count);
        for (uint32_t col = 0U; col < col_count; col++) {
            dst_row[col] =
                db_pack_rgb10a2_bt2020_pq_from_rgba8888(src_row[col]);
        }
    }
}

void db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(
    uint32_t *dst, const uint16_t *src, size_t src_stride_pixels,
    uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count) {
    if ((dst == NULL) || (src == NULL)) {
        return;
    }
    for (uint32_t row = 0U; row < row_count; row++) {
        const uint16_t *const src_row =
            src +
            (((((size_t)row_start + row) * src_stride_pixels) + col_start) *
             DB_CONVERT_RGBA16F_PIXEL_STRIDE);
        uint32_t *const dst_row = dst + ((size_t)row * col_count);
        for (uint32_t col = 0U; col < col_count; col++) {
            dst_row[col] = db_pack_rgb10a2_bt2020_pq_from_rgb16f3(
                src_row + ((size_t)col * DB_CONVERT_RGBA16F_PIXEL_STRIDE));
        }
    }
}

enum {
    DB_PIXEL_ILP_LANES = 8U,
    DB_PIXEL_ILP_INDEX_0 = 0U,
    DB_PIXEL_ILP_INDEX_1 = 1U,
    DB_PIXEL_ILP_INDEX_2 = 2U,
    DB_PIXEL_ILP_INDEX_3 = 3U,
    DB_PIXEL_ILP_INDEX_4 = 4U,
    DB_PIXEL_ILP_INDEX_5 = 5U,
    DB_PIXEL_ILP_INDEX_6 = 6U,
    DB_PIXEL_ILP_INDEX_7 = 7U,
};

// F16->U8 lookup table used by RGBA16F conversion entry points.
static uint8_t g_f16_to_u8_lut[DB_CONVERT_F16_LUT_SIZE];
// pthread.h is the public provider; macOS attributes the typedef to a private
// sys/_pthread header that applications must not include directly.
static pthread_once_t g_f16_to_u8_lut_once = // NOLINT(misc-include-cleaner)
    PTHREAD_ONCE_INIT;

static void db_f16_to_u8_lut_initialize(void) {
    for (uint32_t value = 0U; value < DB_CONVERT_F16_LUT_SIZE; value++) {
        const double as_double = db_f16_to_double((uint16_t)value);
        g_f16_to_u8_lut[value] = db_double01_to_u8_clamped(as_double);
    }
}

static void db_f16_to_u8_lut_init_once(void) {
    const int once_result =
        pthread_once(&g_f16_to_u8_lut_once, db_f16_to_u8_lut_initialize);
    if (once_result != 0) {
        db_failf("buffer_convert", "failed to initialize f16 conversion LUT");
    }
}

static inline uint8_t db_f16_to_u8_lut_value(uint16_t value) {
    return g_f16_to_u8_lut[(uint32_t)value];
}

uint8_t db_f16_to_u8_clamped(uint16_t value) {
    db_f16_to_u8_lut_init_once();
    return db_f16_to_u8_lut_value(value);
}

static inline uint32_t
db_pack_xrgb8888_from_rgb16f3_lut(const uint16_t *rgb16f3) {
    const uint32_t red_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_R]);
    const uint32_t green_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_G]);
    const uint32_t blue_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_B]);
    return db_pack_xrgb8888_from_rgb_u8(red_u8, green_u8, blue_u8);
}

static inline uint32_t
db_pack_rgba8888_from_rgb16f3_lut(const uint16_t *rgb16f3, uint32_t alpha_u8) {
    const uint32_t red_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_R]);
    const uint32_t green_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_G]);
    const uint32_t blue_u8 =
        db_f16_to_u8_lut_value(rgb16f3[DB_CONVERT_CHANNEL_B]);
    return db_pack_rgba8888_from_rgb_u8(red_u8, green_u8, blue_u8, alpha_u8);
}

uint32_t db_pack_xrgb8888_from_rgb16f3(const uint16_t *rgb16f3) {
    if (rgb16f3 == NULL) {
        return 0U;
    }
    db_f16_to_u8_lut_init_once();
    return db_pack_xrgb8888_from_rgb16f3_lut(rgb16f3);
}

// Shared fill helpers (used by CPU renderer and display upload paths).
void db_fill_u32_buffer(uint32_t *dst, uint32_t element_count,
                        uint32_t fill_value) {
    if ((dst == NULL) || (element_count == 0U)) {
        return;
    }
    uint32_t index = 0U;
    for (; (index + (DB_PIXEL_ILP_LANES - 1U)) < element_count;
         index += DB_PIXEL_ILP_LANES) {
        dst[index + DB_PIXEL_ILP_INDEX_0] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_1] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_2] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_3] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_4] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_5] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_6] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_7] = fill_value;
    }
    for (; index < element_count; index++) {
        dst[index] = fill_value;
    }
}

void db_fill_u64_buffer(uint64_t *dst, uint32_t element_count,
                        uint64_t fill_value) {
    if ((dst == NULL) || (element_count == 0U)) {
        return;
    }
    uint32_t index = 0U;
    for (; (index + (DB_PIXEL_ILP_LANES - 1U)) < element_count;
         index += DB_PIXEL_ILP_LANES) {
        dst[index + DB_PIXEL_ILP_INDEX_0] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_1] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_2] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_3] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_4] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_5] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_6] = fill_value;
        dst[index + DB_PIXEL_ILP_INDEX_7] = fill_value;
    }
    for (; index < element_count; index++) {
        dst[index] = fill_value;
    }
}

void db_fill_rgba8_byte_pattern(uint8_t *dst, uint32_t pixel_count,
                                const uint8_t *rgba_u8) {
    if ((dst == NULL) || (rgba_u8 == NULL) || (pixel_count == 0U)) {
        return;
    }
    uint32_t pixel = 0U;
    for (; (pixel + (DB_PIXEL_ILP_LANES - 1U)) < pixel_count;
         pixel += DB_PIXEL_ILP_LANES) {
        for (uint32_t lane = 0U; lane < DB_PIXEL_ILP_LANES; lane++) {
            const size_t base =
                ((size_t)(pixel + lane) * DB_CONVERT_RGBA8_PIXEL_STRIDE);
            dst[base + DB_CONVERT_CHANNEL_R] = rgba_u8[DB_CONVERT_CHANNEL_R];
            dst[base + DB_CONVERT_CHANNEL_G] = rgba_u8[DB_CONVERT_CHANNEL_G];
            dst[base + DB_CONVERT_CHANNEL_B] = rgba_u8[DB_CONVERT_CHANNEL_B];
            dst[base + DB_CONVERT_CHANNEL_A] = rgba_u8[DB_CONVERT_CHANNEL_A];
        }
    }
    for (; pixel < pixel_count; pixel++) {
        const size_t base = ((size_t)pixel * DB_CONVERT_RGBA8_PIXEL_STRIDE);
        dst[base + DB_CONVERT_CHANNEL_R] = rgba_u8[DB_CONVERT_CHANNEL_R];
        dst[base + DB_CONVERT_CHANNEL_G] = rgba_u8[DB_CONVERT_CHANNEL_G];
        dst[base + DB_CONVERT_CHANNEL_B] = rgba_u8[DB_CONVERT_CHANNEL_B];
        dst[base + DB_CONVERT_CHANNEL_A] = rgba_u8[DB_CONVERT_CHANNEL_A];
    }
}

void db_fill_rgba16f_buffer(void *dst, uint32_t pixel_count,
                            const uint16_t *rgba_f16) {
    if ((dst == NULL) || (rgba_f16 == NULL) || (pixel_count == 0U)) {
        return;
    }
    const uint64_t packed_pixel =
        ((uint64_t)rgba_f16[DB_CONVERT_CHANNEL_R]) |
        ((uint64_t)rgba_f16[DB_CONVERT_CHANNEL_G] << 16U) |
        ((uint64_t)rgba_f16[DB_CONVERT_CHANNEL_B] << 32U) |
        ((uint64_t)rgba_f16[DB_CONVERT_CHANNEL_A] << 48U);
    db_fill_u64_buffer((uint64_t *)dst, pixel_count, packed_pixel);
}

void db_unpack_rgba8888_rgb_u8(uint32_t packed_rgba, uint32_t *rgb_u8_out) {
    if (rgb_u8_out == NULL) {
        return;
    }
    rgb_u8_out[0] = (packed_rgba >> DB_PACKED_RGB_SHIFT_BLUE) & UINT8_MAX;
    rgb_u8_out[1] = (packed_rgba >> DB_PACKED_RGB_SHIFT_GREEN) & UINT8_MAX;
    rgb_u8_out[2] = (packed_rgba >> DB_PACKED_RGB_SHIFT_RED) & UINT8_MAX;
}

void db_unpack_rgba8888_rgb01(uint32_t packed_rgba, double *rgb01_out) {
    if (rgb01_out == NULL) {
        return;
    }
    rgb01_out[0] = db_u8_to_unit_f64((packed_rgba >> DB_PACKED_RGB_SHIFT_BLUE) &
                                     UINT8_MAX);
    rgb01_out[1] = db_u8_to_unit_f64(
        (packed_rgba >> DB_PACKED_RGB_SHIFT_GREEN) & UINT8_MAX);
    rgb01_out[2] =
        db_u8_to_unit_f64((packed_rgba >> DB_PACKED_RGB_SHIFT_RED) & UINT8_MAX);
}

uint32_t db_pack_xrgb8888_from_rgba8888(uint32_t packed_rgba) {
    const uint32_t red_bits =
        ((packed_rgba >> DB_PACKED_RGB_SHIFT_BLUE) & UINT8_MAX)
        << DB_PACKED_RGB_SHIFT_RED;
    const uint32_t green_bits =
        ((packed_rgba >> DB_PACKED_RGB_SHIFT_GREEN) & UINT8_MAX)
        << DB_PACKED_RGB_SHIFT_GREEN;
    const uint32_t blue_bits =
        ((packed_rgba >> DB_PACKED_RGB_SHIFT_RED) & UINT8_MAX)
        << DB_PACKED_RGB_SHIFT_BLUE;
    return red_bits | green_bits | blue_bits;
}

// Public block/subrect pixel format conversion entry points.
void db_convert_rgba8_to_xrgb8888_block(uint32_t *dst, size_t dst_stride_pixels,
                                        const uint32_t *src,
                                        size_t src_stride_pixels,
                                        uint32_t row_start, uint32_t row_count,
                                        uint32_t col_start,
                                        uint32_t col_count) {
    if ((dst == NULL) || (src == NULL) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }

    for (uint32_t row = 0U; row < row_count; row++) {
        const uint32_t *src_row =
            src + (((size_t)(row_start + row) * src_stride_pixels) + col_start);
        uint32_t *dst_row =
            dst + (((size_t)(row_start + row) * dst_stride_pixels) + col_start);
        for (uint32_t col = 0U; col < col_count; col++) {
            dst_row[col] = db_pack_xrgb8888_from_rgba8888(src_row[col]);
        }
    }
}

void db_convert_rgba16f_to_xrgb8888_block(
    uint32_t *dst, size_t dst_stride_pixels, const uint16_t *src,
    size_t src_stride_pixels, uint32_t row_start, uint32_t row_count,
    uint32_t col_start, uint32_t col_count) {
    if ((dst == NULL) || (src == NULL) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }
    db_f16_to_u8_lut_init_once();

    for (uint32_t row = 0U; row < row_count; row++) {
        const uint16_t *src_row =
            src +
            ((((size_t)(row_start + row) * src_stride_pixels) + col_start) *
             DB_CONVERT_RGBA16F_PIXEL_STRIDE);
        uint32_t *dst_row =
            dst + (((size_t)(row_start + row) * dst_stride_pixels) + col_start);
        uint32_t col = 0U;
        for (; (col + (DB_PIXEL_ILP_LANES - 1U)) < col_count;
             col += DB_PIXEL_ILP_LANES) {
            const size_t src_base0 =
                (size_t)col * DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base1 =
                src_base0 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base2 =
                src_base1 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base3 =
                src_base2 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base4 =
                src_base3 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base5 =
                src_base4 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base6 =
                src_base5 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base7 =
                src_base6 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            dst_row[col + DB_PIXEL_ILP_INDEX_0] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base0]);
            dst_row[col + DB_PIXEL_ILP_INDEX_1] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base1]);
            dst_row[col + DB_PIXEL_ILP_INDEX_2] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base2]);
            dst_row[col + DB_PIXEL_ILP_INDEX_3] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base3]);
            dst_row[col + DB_PIXEL_ILP_INDEX_4] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base4]);
            dst_row[col + DB_PIXEL_ILP_INDEX_5] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base5]);
            dst_row[col + DB_PIXEL_ILP_INDEX_6] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base6]);
            dst_row[col + DB_PIXEL_ILP_INDEX_7] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base7]);
        }
        for (; col < col_count; col++) {
            const size_t src_base =
                (size_t)col * DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            dst_row[col] =
                db_pack_xrgb8888_from_rgb16f3_lut(&src_row[src_base]);
        }
    }
}

void db_convert_rgba16f_to_rgba8888_block(
    uint32_t *dst, size_t dst_stride_pixels, const uint16_t *src,
    size_t src_stride_pixels, uint32_t row_start, uint32_t row_count,
    uint32_t col_start, uint32_t col_count, uint32_t alpha_u8) {
    if ((dst == NULL) || (src == NULL) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }
    db_f16_to_u8_lut_init_once();

    for (uint32_t row = 0U; row < row_count; row++) {
        const uint16_t *src_row =
            src +
            ((((size_t)(row_start + row) * src_stride_pixels) + col_start) *
             DB_CONVERT_RGBA16F_PIXEL_STRIDE);
        uint32_t *dst_row =
            dst + (((size_t)(row_start + row) * dst_stride_pixels) + col_start);
        uint32_t col = 0U;
        for (; (col + (DB_PIXEL_ILP_LANES - 1U)) < col_count;
             col += DB_PIXEL_ILP_LANES) {
            const size_t src_base0 =
                (size_t)col * DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base1 =
                src_base0 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base2 =
                src_base1 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base3 =
                src_base2 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base4 =
                src_base3 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base5 =
                src_base4 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base6 =
                src_base5 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            const size_t src_base7 =
                src_base6 + DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            dst_row[col + DB_PIXEL_ILP_INDEX_0] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base0],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_1] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base1],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_2] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base2],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_3] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base3],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_4] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base4],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_5] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base5],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_6] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base6],
                                                  alpha_u8);
            dst_row[col + DB_PIXEL_ILP_INDEX_7] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base7],
                                                  alpha_u8);
        }
        for (; col < col_count; col++) {
            const size_t src_base =
                (size_t)col * DB_CONVERT_RGBA16F_PIXEL_STRIDE;
            dst_row[col] =
                db_pack_rgba8888_from_rgb16f3_lut(&src_row[src_base], alpha_u8);
        }
    }
}
