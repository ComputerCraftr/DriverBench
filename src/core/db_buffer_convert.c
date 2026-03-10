#include "db_buffer_convert.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "db_numeric.h"

// Format/layout constants used by all fill/convert helpers in this unit.
#define DB_CONVERT_CHANNEL_R 0U
#define DB_CONVERT_CHANNEL_G 1U
#define DB_CONVERT_CHANNEL_B 2U
#define DB_CONVERT_CHANNEL_A 3U
#define DB_CONVERT_F16_LUT_SIZE (UINT16_MAX + 1U)
#define DB_CONVERT_RGBA16F_PIXEL_STRIDE 4U
#define DB_CONVERT_RGBA8_PIXEL_STRIDE 4U
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
static atomic_bool g_f16_to_u8_lut_ready = false;
static atomic_flag g_f16_to_u8_lut_lock = ATOMIC_FLAG_INIT;

static void db_f16_to_u8_lut_init_once(void) {
    if (atomic_load_explicit(&g_f16_to_u8_lut_ready, memory_order_acquire)) {
        return;
    }

    while (atomic_flag_test_and_set_explicit(&g_f16_to_u8_lut_lock,
                                             memory_order_acquire)) {
    }
    if (!atomic_load_explicit(&g_f16_to_u8_lut_ready, memory_order_relaxed)) {
        for (uint32_t value = 0U; value < DB_CONVERT_F16_LUT_SIZE; value++) {
            const double as_double = db_f16_to_double((uint16_t)value);
            g_f16_to_u8_lut[value] = db_double01_to_u8_clamped(as_double);
        }
        atomic_store_explicit(&g_f16_to_u8_lut_ready, true,
                              memory_order_release);
    }
    atomic_flag_clear_explicit(&g_f16_to_u8_lut_lock, memory_order_release);
}

static inline uint8_t db_f16_to_u8_lut_value(uint16_t value) {
    return g_f16_to_u8_lut[(uint32_t)value];
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

// Low-level copy/move primitives.
void db_copy_bytes(void *dst, const void *src, size_t byte_count) {
    if ((dst == NULL) || (src == NULL) || (byte_count == 0U)) {
        return;
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dst, src, byte_count);
}

void db_move_bytes(void *dst, const void *src, size_t byte_count) {
    if ((dst == NULL) || (src == NULL) || (byte_count == 0U)) {
        return;
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memmove(dst, src, byte_count);
}

void db_copy_f32_vec2(float dst[2], const float src[2]) {
    db_copy_bytes(dst, src, 2U * sizeof(float));
}

void db_copy_f32_rgb3(float dst[3], const float src[3]) {
    db_copy_bytes(dst, src, 3U * sizeof(float));
}

void db_copy_f32_rgba4(float dst[4], const float src[4]) {
    db_copy_bytes(dst, src, 4U * sizeof(float));
}

void db_copy_f64_rgb3(double dst[3], const double src[3]) {
    db_copy_bytes(dst, src, 3U * sizeof(double));
}

void db_copy_u32_rgb3(uint32_t dst[3], const uint32_t src[3]) {
    db_copy_u32_buffer(dst, src, 3U);
}

void db_copy_u32_buffer(uint32_t *dst, const uint32_t *src,
                        size_t element_count) {
    db_copy_bytes(dst, src, element_count * sizeof(uint32_t));
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

void db_fill_rgba8_byte_pattern(uint8_t *dst, uint32_t pixel_count,
                                const uint8_t rgba_u8[4]) {
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

void db_fill_rgba16f_buffer(uint16_t *dst, uint32_t pixel_count,
                            const uint16_t rgba_f16[4]) {
    if ((dst == NULL) || (rgba_f16 == NULL) || (pixel_count == 0U)) {
        return;
    }
    uint32_t pixel = 0U;
    for (; (pixel + (DB_PIXEL_ILP_LANES - 1U)) < pixel_count;
         pixel += DB_PIXEL_ILP_LANES) {
        for (uint32_t lane = 0U; lane < DB_PIXEL_ILP_LANES; lane++) {
            const size_t base =
                ((size_t)(pixel + lane) * DB_CONVERT_RGBA16F_PIXEL_STRIDE);
            dst[base + DB_CONVERT_CHANNEL_R] = rgba_f16[DB_CONVERT_CHANNEL_R];
            dst[base + DB_CONVERT_CHANNEL_G] = rgba_f16[DB_CONVERT_CHANNEL_G];
            dst[base + DB_CONVERT_CHANNEL_B] = rgba_f16[DB_CONVERT_CHANNEL_B];
            dst[base + DB_CONVERT_CHANNEL_A] = rgba_f16[DB_CONVERT_CHANNEL_A];
        }
    }
    for (; pixel < pixel_count; pixel++) {
        const size_t base = ((size_t)pixel * DB_CONVERT_RGBA16F_PIXEL_STRIDE);
        dst[base + DB_CONVERT_CHANNEL_R] = rgba_f16[DB_CONVERT_CHANNEL_R];
        dst[base + DB_CONVERT_CHANNEL_G] = rgba_f16[DB_CONVERT_CHANNEL_G];
        dst[base + DB_CONVERT_CHANNEL_B] = rgba_f16[DB_CONVERT_CHANNEL_B];
        dst[base + DB_CONVERT_CHANNEL_A] = rgba_f16[DB_CONVERT_CHANNEL_A];
    }
}

void db_unpack_rgba8888_rgb_u8(uint32_t packed_rgba, uint32_t rgb_u8_out[3]) {
    if (rgb_u8_out == NULL) {
        return;
    }
    rgb_u8_out[0] = (packed_rgba >> DB_PACKED_RGB_SHIFT_BLUE) & UINT8_MAX;
    rgb_u8_out[1] = (packed_rgba >> DB_PACKED_RGB_SHIFT_GREEN) & UINT8_MAX;
    rgb_u8_out[2] = (packed_rgba >> DB_PACKED_RGB_SHIFT_RED) & UINT8_MAX;
}

void db_unpack_rgba8888_rgb01(uint32_t packed_rgba, double rgb01_out[3]) {
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
