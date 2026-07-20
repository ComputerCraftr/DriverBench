#include "core/db_format_contract.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "config/runtime_options.h"
#include "core/db_buffer_convert.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"

#include "displays/display_runtime_config_common.h"
#include "displays/display_types.h"
#include "driverbench_config.h"
#ifdef DB_HAS_LINUX_KMS_ATOMIC
#include "displays/linux_kms_atomic/kms_hdr.h"
#endif

enum {
    TEST_RGBA8_2X2_BYTES = 16U,
    TEST_RGBA8_FIRST_MISMATCH_BYTE = 5U,
    TEST_RGBA8_LAST_MISMATCH_BYTE = 14U,
    TEST_RGBA8_FIRST_MISMATCH_VALUE = 7U,
    TEST_RGBA8_LAST_MISMATCH_VALUE = 9U,
    TEST_PRESENTATION_EXTENT = 100U,
    TEST_STALE_SCANOUT_SERIAL = 11U,
    TEST_HDR_BLOCK_SOURCE_WIDTH = 3U,
    TEST_HDR_BLOCK_SOURCE_HEIGHT = 2U,
    TEST_HDR_BLOCK_ROW = 1U,
    TEST_HDR_BLOCK_COL = 1U,
    TEST_HDR_BLOCK_COL_COUNT = 2U,
};

static const uint32_t test_rgb10a2_alpha_mask = UINT32_C(0xC0000000);
static const uint32_t test_rgb10a2_alpha_opaque = UINT32_C(0xC0000000);

static void
db_test_working_format_is_independent_of_native_output(db_test_state_t *state) {
    const db_display_resolved_format_config_t format =
        db_display_resolve_format_config_or_fail(
            "test", DB_PIXEL_FORMAT_RGBA16F, DB_OUTPUT_FORMAT_AUTO, NULL);

    DB_TEST_EXPECT_EQ_INT(state, format.surface_pixel_format,
                          DB_PIXEL_FORMAT_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, format.present_texture_format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, format.native_output_format,
                          DB_NATIVE_OUTPUT_XRGB8888);
    DB_TEST_EXPECT_EQ_INT(state, format.native_hdr_enabled, 0);
    DB_TEST_EXPECT_EQ_INT(state, format.framebuffer_hash_format,
                          DB_PIXEL_FORMAT_RGBA8);
}

static void
db_test_auto_output_uses_verified_native_hdr(db_test_state_t *state) {
    const db_native_output_capability_t capability = {
        .native_hdr_verified = 1,
        .native_format_supported = 1,
        .colorspace_supported = 1,
        .metadata_supported = 1,
        .sink_hdr_supported = 1,
        .commit_verified = 1,
        .native_bit_depth = 10U,
        .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
        .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
        .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
        .unavailable_reason = "none",
    };
    const db_display_resolved_format_config_t supported =
        db_display_resolve_format_config_or_fail(
            "test", DB_PIXEL_FORMAT_RGBA8, DB_OUTPUT_FORMAT_AUTO, &capability);

    DB_TEST_EXPECT_EQ_INT(state, supported.surface_pixel_format,
                          DB_PIXEL_FORMAT_RGBA8);
    DB_TEST_EXPECT_EQ_INT(state, supported.present_texture_format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8);
    DB_TEST_EXPECT_EQ_INT(state, supported.native_output_format,
                          DB_NATIVE_OUTPUT_XRGB2101010);
    DB_TEST_EXPECT_EQ_INT(state, supported.native_hdr_enabled, 1);
    DB_TEST_EXPECT_EQ_INT(state, supported.output_colorspace,
                          DB_OUTPUT_COLORSPACE_BT2020);
    DB_TEST_EXPECT_EQ_INT(state, supported.output_conversion,
                          DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_BT2020_PQ);
    DB_TEST_EXPECT_EQ_INT(state, supported.encoded_present_format,
                          DB_ENCODED_PRESENT_BT2020_PQ_RGB10A2);
    DB_TEST_EXPECT_EQ_INT(state, supported.hdr_conversion,
                          DB_HDR_CONVERSION_NONE);
    DB_TEST_EXPECT_EQ_INT(state, supported.hdr_content_supported, 1);
    DB_TEST_EXPECT_EQ_U32(state, supported.native_bit_depth, 10U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, supported.hdr10.reference_white_nits,
                                203.0);
}

static void db_test_deferred_native_output_preserves_explicit_hdr_request(
    db_test_state_t *state) {
    db_runtime_options_reset_all();
    db_runtime_option_set(DB_RUNTIME_OPT_OUTPUT_FORMAT, "hdr");
    db_runtime_option_set(DB_RUNTIME_OPT_WORKING_FORMAT, "rgba16f");
    db_display_renderer_runtime_t runtime =
        db_display_renderer_runtime_from_cli(
            "test_deferred_output",
            &(const db_cli_config_t){.api = DB_API_VULKAN,
                                     .display = DB_GLFW_WINDOW_DISPLAY},
            0U, 0, 0, DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE);
    DB_TEST_EXPECT_EQ_INT(state, runtime.renderer.format.output_request,
                          DB_OUTPUT_FORMAT_HDR);
    DB_TEST_EXPECT_EQ_INT(
        state, runtime.renderer.format.native_output_resolution_pending, 1);
    DB_TEST_EXPECT_EQ_INT(state, runtime.renderer.format.surface_pixel_format,
                          DB_PIXEL_FORMAT_RGBA16F);

    const db_native_output_capability_t capability = {
        .native_hdr_verified = 1,
        .native_format_supported = 1,
        .colorspace_supported = 1,
        .metadata_supported = 1,
        .sink_hdr_supported = 1,
        .commit_verified = 1,
        .native_bit_depth = 10U,
        .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
        .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
        .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
        .unavailable_reason = "none",
    };
    db_display_apply_native_output_capability_or_fail("test_deferred_output",
                                                      &runtime, &capability);
    DB_TEST_EXPECT_EQ_INT(
        state, runtime.renderer.format.native_output_resolution_pending, 0);
    DB_TEST_EXPECT_EQ_INT(state, runtime.renderer.format.native_hdr_enabled, 1);
    DB_TEST_EXPECT_EQ_INT(state, runtime.renderer.format.output_request,
                          DB_OUTPUT_FORMAT_HDR);
    db_runtime_options_reset_all();
}

static void
db_test_hdr10_reference_conversion_and_packing(db_test_state_t *state) {
    static const double black_upper_bound = 0.000001;
    static const double reference_white_pq_lower_bound = 0.57;
    static const double reference_white_pq_upper_bound = 0.59;
    const double black[] = {0.0, 0.0, 0.0};
    const double white[] = {1.0, 1.0, 1.0};
    double black_pq[3] = {1.0, 1.0, 1.0};
    double white_pq[3] = {0.0, 0.0, 0.0};
    db_hdr10_linear_srgb_to_bt2020_pq(black, black_pq);
    db_hdr10_linear_srgb_to_bt2020_pq(white, white_pq);
    DB_TEST_EXPECT_TRUE(state, black_pq[0] < black_upper_bound);
    DB_TEST_EXPECT_TRUE(state, white_pq[0] > reference_white_pq_lower_bound);
    DB_TEST_EXPECT_TRUE(state, white_pq[0] < reference_white_pq_upper_bound);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, white_pq[0], white_pq[1]);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, white_pq[1], white_pq[2]);
    const uint32_t packed = db_pack_xrgb2101010_from_linear_srgb(white);
    DB_TEST_EXPECT_TRUE(state, (packed & 0x3FFU) != 0U);
    DB_TEST_EXPECT_EQ_U32(state, packed & 0x3FFU, (packed >> 10U) & 0x3FFU);
    DB_TEST_EXPECT_EQ_U32(state, packed & 0x3FFU, (packed >> 20U) & 0x3FFU);
}

static void db_test_hdr10_tight_block_conversion_preserves_region_and_format(
    db_test_state_t *state) {
    const uint32_t rgba8[] = {
        0x000000FFU, 0x0000FFFFU, 0x00FF00FFU,
        0xFF0000FFU, 0xFFFFFFFFU, 0x808080FFU,
    };
    uint32_t encoded_rgba8[TEST_HDR_BLOCK_COL_COUNT] = {0U};
    db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(
        encoded_rgba8, rgba8, TEST_HDR_BLOCK_SOURCE_WIDTH, TEST_HDR_BLOCK_ROW,
        1U, TEST_HDR_BLOCK_COL, TEST_HDR_BLOCK_COL_COUNT);
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba8[0],
        db_pack_rgb10a2_bt2020_pq_from_rgba8888(
            rgba8[(TEST_HDR_BLOCK_ROW * TEST_HDR_BLOCK_SOURCE_WIDTH) +
                  TEST_HDR_BLOCK_COL]));
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba8[1],
        db_pack_rgb10a2_bt2020_pq_from_rgba8888(
            rgba8[(TEST_HDR_BLOCK_ROW * TEST_HDR_BLOCK_SOURCE_WIDTH) +
                  TEST_HDR_BLOCK_COL + 1U]));
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba8[0] & test_rgb10a2_alpha_mask,
                          test_rgb10a2_alpha_opaque);

    uint16_t rgba16f[TEST_HDR_BLOCK_SOURCE_WIDTH *
                     TEST_HDR_BLOCK_SOURCE_HEIGHT *
                     DB_RGBA16F_CHANNELS_PER_PIXEL] = {0U};
    const size_t selected_pixel =
        ((size_t)TEST_HDR_BLOCK_ROW * TEST_HDR_BLOCK_SOURCE_WIDTH) +
        TEST_HDR_BLOCK_COL;
    rgba16f[(selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL) + 0U] =
        db_double_to_f16(1.0);
    rgba16f[(selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL) + 3U] = DB_F16_ONE;
    uint32_t encoded_rgba16f = 0U;
    db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(
        &encoded_rgba16f, rgba16f, TEST_HDR_BLOCK_SOURCE_WIDTH,
        TEST_HDR_BLOCK_ROW, 1U, TEST_HDR_BLOCK_COL, 1U);
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba16f,
        db_pack_rgb10a2_bt2020_pq_from_rgb16f3(
            &rgba16f[selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL]));
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba16f & test_rgb10a2_alpha_mask,
                          test_rgb10a2_alpha_opaque);
}

static void
db_test_hdr10_strided_xrgb_conversion_preserves_region(db_test_state_t *state) {
    enum {
        TEST_DESTINATION_STRIDE = 5U,
        TEST_DESTINATION_PIXELS =
            TEST_DESTINATION_STRIDE * TEST_HDR_BLOCK_SOURCE_HEIGHT,
    };
    static const uint32_t sentinel = UINT32_C(0xA5A5A5A5);
    const uint32_t rgba8[] = {
        0x000000FFU, 0x0000FFFFU, 0x00FF00FFU,
        0xFF0000FFU, 0xFFFFFFFFU, 0x808080FFU,
    };
    uint32_t encoded_rgba8[TEST_DESTINATION_PIXELS] = {0U};
    db_fill_u32_buffer(encoded_rgba8, TEST_DESTINATION_PIXELS, sentinel);
    db_convert_rgba8_to_xrgb2101010_block(
        encoded_rgba8, TEST_DESTINATION_STRIDE, rgba8,
        TEST_HDR_BLOCK_SOURCE_WIDTH, TEST_HDR_BLOCK_ROW, 1U, TEST_HDR_BLOCK_COL,
        TEST_HDR_BLOCK_COL_COUNT);
    const size_t destination_index =
        (TEST_HDR_BLOCK_ROW * TEST_DESTINATION_STRIDE) + TEST_HDR_BLOCK_COL;
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba8[destination_index],
        db_pack_xrgb2101010_from_rgba8888(
            rgba8[(TEST_HDR_BLOCK_ROW * TEST_HDR_BLOCK_SOURCE_WIDTH) +
                  TEST_HDR_BLOCK_COL]));
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba8[destination_index] & test_rgb10a2_alpha_mask, 0U);
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba8[destination_index - 1U],
                          sentinel);
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba8[destination_index + 2U],
                          sentinel);

    uint16_t rgba16f[TEST_HDR_BLOCK_SOURCE_WIDTH *
                     TEST_HDR_BLOCK_SOURCE_HEIGHT *
                     DB_RGBA16F_CHANNELS_PER_PIXEL] = {0U};
    const size_t selected_pixel =
        ((size_t)TEST_HDR_BLOCK_ROW * TEST_HDR_BLOCK_SOURCE_WIDTH) +
        TEST_HDR_BLOCK_COL;
    rgba16f[(selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL) + 0U] =
        db_double_to_f16(1.0);
    rgba16f[(selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL) + 3U] = DB_F16_ONE;
    uint32_t encoded_rgba16f[TEST_DESTINATION_PIXELS] = {0U};
    db_fill_u32_buffer(encoded_rgba16f, TEST_DESTINATION_PIXELS, sentinel);
    db_convert_rgba16f_to_xrgb2101010_block(
        encoded_rgba16f, TEST_DESTINATION_STRIDE, rgba16f,
        TEST_HDR_BLOCK_SOURCE_WIDTH, TEST_HDR_BLOCK_ROW, 1U, TEST_HDR_BLOCK_COL,
        1U);
    DB_TEST_EXPECT_EQ_U32(
        state, encoded_rgba16f[destination_index],
        db_pack_xrgb2101010_from_rgb16f3(
            &rgba16f[selected_pixel * DB_RGBA16F_CHANNELS_PER_PIXEL]));
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba16f[destination_index - 1U],
                          sentinel);
}

static void db_test_hdr10_run_conversion_preserves_color_transitions(
    db_test_state_t *state) {
    const uint32_t rgba8[] = {
        0x204060FFU, 0x204060FFU, 0x80A0C0FFU, 0x80A0C0FFU, 0x204060FFU,
    };
    uint32_t encoded_rgba8[sizeof(rgba8) / sizeof(rgba8[0])] = {0U};
    db_convert_rgba8_to_rgb10a2_bt2020_pq_tight(
        encoded_rgba8, rgba8, sizeof(rgba8) / sizeof(rgba8[0]), 0U, 1U, 0U,
        sizeof(rgba8) / sizeof(rgba8[0]));
    for (size_t index = 0U; index < sizeof(rgba8) / sizeof(rgba8[0]); index++) {
        DB_TEST_EXPECT_EQ_U32(
            state, encoded_rgba8[index],
            db_pack_rgb10a2_bt2020_pq_from_rgba8888(rgba8[index]));
    }

    uint16_t rgba16f[(sizeof(rgba8) / sizeof(rgba8[0])) *
                     DB_RGBA16F_CHANNELS_PER_PIXEL] = {0U};
    for (size_t index = 0U; index < sizeof(rgba8) / sizeof(rgba8[0]); index++) {
        const double value = ((index < 2U) || (index == 4U)) ? 0.25 : 0.75;
        const size_t base = index * DB_RGBA16F_CHANNELS_PER_PIXEL;
        rgba16f[base] = db_double_to_f16(value);
        rgba16f[base + 1U] = db_double_to_f16(value);
        rgba16f[base + 2U] = db_double_to_f16(value);
        rgba16f[base + 3U] = DB_F16_ONE;
    }
    uint32_t encoded_rgba16f[sizeof(rgba8) / sizeof(rgba8[0])] = {0U};
    db_convert_rgba16f_to_rgb10a2_bt2020_pq_tight(
        encoded_rgba16f, rgba16f, sizeof(rgba8) / sizeof(rgba8[0]), 0U, 1U, 0U,
        sizeof(rgba8) / sizeof(rgba8[0]));
    for (size_t index = 0U; index < sizeof(rgba8) / sizeof(rgba8[0]); index++) {
        DB_TEST_EXPECT_EQ_U32(
            state, encoded_rgba16f[index],
            db_pack_rgb10a2_bt2020_pq_from_rgb16f3(
                &rgba16f[index * DB_RGBA16F_CHANNELS_PER_PIXEL]));
    }
}

#ifdef DB_HAS_LINUX_KMS_ATOMIC
static void
db_test_kms_edid_hdr10_requires_pq_and_bt2020_rgb(db_test_state_t *state) {
    enum {
        TEST_EDID_SIZE = 256U,
        TEST_EXTENSION_COUNT = 126U,
        TEST_CTA_BASE = 128U,
        TEST_CTA_DATA_END = 130U,
        TEST_COLOR_BLOCK_HEADER = 132U,
        TEST_COLOR_EXTENDED_TAG = 133U,
        TEST_COLOR_FLAGS = 134U,
        TEST_HDR_BLOCK_HEADER = 135U,
        TEST_HDR_EXTENDED_TAG = 136U,
        TEST_HDR_FLAGS = 137U,
        TEST_CTA_DATA_END_VALUE = 10U,
        TEST_EXTENDED_TAG = 7U,
        TEST_BLOCK_LENGTH = 2U,
        TEST_BT2020_RGB_MASK = 0x80U,
        TEST_HDR_STATIC_METADATA_TAG = 0x06U,
        TEST_PQ_MASK = 0x04U,
    };
    uint8_t edid[TEST_EDID_SIZE] = {0U};
    edid[TEST_EXTENSION_COUNT] = 1U;
    edid[TEST_CTA_BASE] = 0x02U;
    edid[TEST_CTA_DATA_END] = TEST_CTA_DATA_END_VALUE;
    edid[TEST_COLOR_BLOCK_HEADER] =
        (uint8_t)((TEST_EXTENDED_TAG << 5U) | TEST_BLOCK_LENGTH);
    edid[TEST_COLOR_EXTENDED_TAG] = 0x05U;
    edid[TEST_COLOR_FLAGS] = TEST_BT2020_RGB_MASK;
    edid[TEST_HDR_BLOCK_HEADER] =
        (uint8_t)((TEST_EXTENDED_TAG << 5U) | TEST_BLOCK_LENGTH);
    edid[TEST_HDR_EXTENDED_TAG] = TEST_HDR_STATIC_METADATA_TAG;
    edid[TEST_HDR_FLAGS] = TEST_PQ_MASK;
    DB_TEST_EXPECT_TRUE(
        state, db_kms_edid_bytes_support_hdr10(edid, sizeof(edid)) != 0);
    edid[TEST_COLOR_FLAGS] = 0U;
    DB_TEST_EXPECT_TRUE(
        state, db_kms_edid_bytes_support_hdr10(edid, sizeof(edid)) == 0);
}
#endif

unsigned db_display_hdr_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"working_format_is_independent_of_native_output",
         db_test_working_format_is_independent_of_native_output},
        {"auto_output_uses_verified_native_hdr",
         db_test_auto_output_uses_verified_native_hdr},
        {"deferred_native_output_preserves_explicit_hdr_request",
         db_test_deferred_native_output_preserves_explicit_hdr_request},
        {"hdr10_reference_conversion_and_packing",
         db_test_hdr10_reference_conversion_and_packing},
        {"hdr10_tight_block_conversion_preserves_region_and_format",
         db_test_hdr10_tight_block_conversion_preserves_region_and_format},
        {"hdr10_strided_xrgb_conversion_preserves_region",
         db_test_hdr10_strided_xrgb_conversion_preserves_region},
        {"hdr10_run_conversion_preserves_color_transitions",
         db_test_hdr10_run_conversion_preserves_color_transitions},
#ifdef DB_HAS_LINUX_KMS_ATOMIC
        {"kms_edid_hdr10_requires_pq_and_bt2020_rgb",
         db_test_kms_edid_hdr10_requires_pq_and_bt2020_rgb},
#endif
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
