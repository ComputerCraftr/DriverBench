#include "core/db_format_contract.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "config/runtime_options.h"
#include "core/db_buffer_convert.h"
#include "core/db_geometry.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"
#include "displays/display_cpu_present_common.h"
#include "displays/display_presentation_policy.h"

#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "displays/display_runtime_config_common.h"
#include "displays/display_types.h"
#include "displays/gl_display_runtime.h"
#include "driverbench_config.h"
#include "renderers/gl_common.h"
#ifdef DB_HAS_LINUX_KMS_ATOMIC
#include "displays/linux_kms_atomic/kms_hdr.h"
#endif

#define DB_TEST_MODE_DESC_CAPACITY 128

enum {
    TEST_RGBA8_2X2_BYTES = 16U,
    TEST_RGBA8_FIRST_MISMATCH_BYTE = 5U,
    TEST_RGBA8_LAST_MISMATCH_BYTE = 14U,
    TEST_RGBA8_FIRST_MISMATCH_VALUE = 7U,
    TEST_RGBA8_LAST_MISMATCH_VALUE = 9U,
    TEST_PRESENTATION_EXTENT = 100U,
    TEST_STALE_SCANOUT_SERIAL = 11U,
    TEST_RGB10A2_ALPHA_MASK = 0xC0000000U,
    TEST_RGB10A2_ALPHA_OPAQUE = 0xC0000000U,
    TEST_HDR_BLOCK_SOURCE_WIDTH = 3U,
    TEST_HDR_BLOCK_SOURCE_HEIGHT = 2U,
    TEST_HDR_BLOCK_ROW = 1U,
    TEST_HDR_BLOCK_COL = 1U,
    TEST_HDR_BLOCK_COL_COUNT = 2U,
};

static void
db_test_glfw_policy_forces_full_draw_on_unstable_probe(db_test_state_t *state) {
    db_cli_config_t cfg = {0};
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserved_framebuffer_count = 0U,
    };

    db_display_resolve_opengl_display_policy(
        DB_GL_RENDERER_GL1_5_GLES1_1, &cfg, 0, 0U,
        DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &preserve_info, &resolved);
    db_runtime_options_reset_all();
    db_runtime_option_set(DB_RUNTIME_OPT_BENCHMARK_MODE,
                          DB_BENCHMARK_MODE_SNAKE_SHAPES);
    const db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            "test_gl_runtime", &resolved.effective_cfg,
            resolved.preserved_framebuffer_count, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 0U);
    DB_TEST_EXPECT_TRUE(state,
                        resolved.effective_cfg.backbuffer_draw_full != 0);
    DB_TEST_EXPECT_EQ_INT(state,
                          resolved_runtime.benchmark.backbuffer_draw_full, 1);
    DB_TEST_EXPECT_TRUE(state, resolved.policy_reason_text != NULL);
    db_runtime_options_reset_all();
}

static void
db_test_glfw_policy_keeps_explicit_backbuffer_mode(db_test_state_t *state) {
    db_cli_config_t cfg = {
        .backbuffer_draw_full = 0,
        .backbuffer_draw_mode_explicit = 1,
    };
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserved_framebuffer_count = 0U,
    };

    db_display_resolve_opengl_display_policy(
        DB_GL_RENDERER_GL1_5_GLES1_1, &cfg, 0, 0U,
        DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &preserve_info, &resolved);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, resolved.effective_cfg.backbuffer_draw_full,
                          0);
    DB_TEST_EXPECT_TRUE(state, resolved.policy_reason_text == NULL);
}

static void
db_test_glfw_policy_keeps_stable_preserved_chain(db_test_state_t *state) {
    db_cli_config_t cfg = {0};
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserved_framebuffer_count = 2U,
    };

    db_display_resolve_opengl_display_policy(
        DB_GL_RENDERER_GL1_5_GLES1_1, &cfg, 0, 0U,
        DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &preserve_info, &resolved);
    db_runtime_options_reset_all();
    db_runtime_option_set(DB_RUNTIME_OPT_BENCHMARK_MODE,
                          DB_BENCHMARK_MODE_SNAKE_GRID);
    const db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            "test_gl_runtime", &resolved.effective_cfg,
            resolved.preserved_framebuffer_count, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 2U);
    DB_TEST_EXPECT_EQ_INT(state, resolved.effective_cfg.backbuffer_draw_full,
                          0);
    DB_TEST_EXPECT_EQ_INT(state,
                          resolved_runtime.benchmark.backbuffer_draw_full, 0);
    DB_TEST_EXPECT_EQ_U32(
        state, resolved_runtime.renderer.preserved_framebuffer_count, 2U);
    DB_TEST_EXPECT_TRUE(state, resolved.policy_reason_text == NULL);
    db_runtime_options_reset_all();
}

static void db_test_glfw_policy_propagates_depth_three_to_resolved_runtime(
    db_test_state_t *state) {
    db_cli_config_t cfg = {0};
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserved_framebuffer_count = 3U,
    };

    db_display_resolve_opengl_display_policy(
        DB_GL_RENDERER_GL1_5_GLES1_1, &cfg, 0, 0U,
        DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &preserve_info, &resolved);
    db_runtime_options_reset_all();
    db_runtime_option_set(DB_RUNTIME_OPT_BENCHMARK_MODE,
                          DB_BENCHMARK_MODE_SNAKE_GRID);
    const db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            "test_gl_runtime", &resolved.effective_cfg,
            resolved.preserved_framebuffer_count, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 3U);
    DB_TEST_EXPECT_EQ_INT(state, resolved.effective_cfg.backbuffer_draw_full,
                          0);
    DB_TEST_EXPECT_EQ_INT(state,
                          resolved_runtime.benchmark.backbuffer_draw_full, 0);
    DB_TEST_EXPECT_EQ_U32(
        state, resolved_runtime.renderer.preserved_framebuffer_count, 3U);
    db_runtime_options_reset_all();
}

static void
db_test_glfw_policy_clamps_preserved_chain_depth(db_test_state_t *state) {
    db_cli_config_t cfg = {0};
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserved_framebuffer_count =
            DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT + 2U,
    };

    db_display_resolve_opengl_display_policy(
        DB_GL_RENDERER_GL1_5_GLES1_1, &cfg, 0, 0U,
        DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &preserve_info, &resolved);
    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count,
                          DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT);
    DB_TEST_EXPECT_EQ_INT(state, resolved.effective_cfg.backbuffer_draw_full,
                          0);
}

static void
db_test_default_framebuffer_probe_policy_visibility(db_test_state_t *state) {
#ifdef __linux__
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 0) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 1) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL3_3, 0) == 0);
#else
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 0) == 0);
#endif
}

static void
db_test_default_framebuffer_probe_translation(db_test_state_t *state) {
    const db_display_default_framebuffer_preserve_info_t info =
        db_display_default_framebuffer_preserve_info_make(1, 1, 2U);
    const db_display_default_framebuffer_preserve_info_t single_source =
        db_display_default_framebuffer_preserve_info_make(1, 1, 1U);
    const db_display_default_framebuffer_preserve_info_t deep_chain =
        db_display_default_framebuffer_preserve_info_make(1, 1, 3U);
    const db_display_default_framebuffer_preserve_info_t unstable =
        db_display_default_framebuffer_preserve_info_make(1, 1, 0U);
    DB_TEST_EXPECT_EQ_INT(state, info.has_probe, 1);
    DB_TEST_EXPECT_EQ_INT(state, info.preserve_supported, 1);
    DB_TEST_EXPECT_EQ_U32(state, info.preserved_framebuffer_count, 2U);
    DB_TEST_EXPECT_EQ_U32(
        state, db_display_default_framebuffer_preserved_count(2U, &info), 2U);
    DB_TEST_EXPECT_EQ_U32(
        state,
        db_display_default_framebuffer_preserved_count(2U, &single_source), 1U);
    DB_TEST_EXPECT_EQ_U32(
        state, db_display_default_framebuffer_preserved_count(3U, &deep_chain),
        3U);
    DB_TEST_EXPECT_EQ_U32(
        state, db_display_default_framebuffer_preserved_count(2U, &deep_chain),
        2U);
    DB_TEST_EXPECT_EQ_U32(state,
                          db_display_default_framebuffer_preserved_count(
                              DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT, &unstable),
                          0U);
}

static void db_test_presentation_buffer_age_sequences(db_test_state_t *state) {
    db_presentation_buffer_age_t age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U, 3U);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 0);
    DB_TEST_EXPECT_EQ_INT(state, age.force_full_repair, 1);

    age = db_presentation_buffer_age_resolve(DB_PRESENTATION_BUFFER_AGE_GLX, 0U,
                                             3U);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 0);
    age = db_presentation_buffer_age_resolve(DB_PRESENTATION_BUFFER_AGE_GLX, 1U,
                                             3U);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 1);
    DB_TEST_EXPECT_EQ_U32(state, age.effective_replay_depth, 0U);
    age = db_presentation_buffer_age_resolve(DB_PRESENTATION_BUFFER_AGE_GLX, 2U,
                                             3U);
    DB_TEST_EXPECT_EQ_U32(state, age.effective_replay_depth, 1U);
    age = db_presentation_buffer_age_resolve(DB_PRESENTATION_BUFFER_AGE_EGL, 3U,
                                             3U);
    DB_TEST_EXPECT_EQ_U32(state, age.effective_replay_depth, 2U);
    age = db_presentation_buffer_age_resolve(DB_PRESENTATION_BUFFER_AGE_EGL, 5U,
                                             3U);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 0);
    DB_TEST_EXPECT_EQ_INT(state, age.force_full_repair, 1);
}

static void
db_test_scanout_serial_age_tracks_alternating_slots(db_test_state_t *state) {
    db_presentation_buffer_age_t age = db_presentation_buffer_age_from_serial(
        1U, 0U, 0, DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 0);
    age = db_presentation_buffer_age_from_serial(
        3U, 1U, 1, DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 1);
    DB_TEST_EXPECT_EQ_U32(state, age.raw_age, 2U);
    DB_TEST_EXPECT_EQ_U32(state, age.effective_replay_depth, 1U);
    age = db_presentation_buffer_age_from_serial(
        TEST_STALE_SCANOUT_SERIAL, 1U, 1,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    DB_TEST_EXPECT_EQ_INT(state, age.valid, 0);
    DB_TEST_EXPECT_EQ_INT(state, age.force_full_repair, 1);
}

static void
db_test_presentation_damage_history_follows_age(db_test_state_t *state) {
    db_presentation_damage_history_t history = {0};
    db_grid_block_t output[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME] = {{0}};
    const db_grid_block_t first = {
        .row_start = 10U, .row_count = 2U, .col_start = 20U, .col_count = 3U};
    const db_grid_block_t second = {
        .row_start = 30U, .row_count = 2U, .col_start = 40U, .col_count = 3U};
    int force_full = 0;

    db_presentation_buffer_age_t age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 1U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    size_t count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &first, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_TRUE(state, force_full != 0);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_count, TEST_PRESENTATION_EXTENT);

    db_presentation_damage_history_reset(&history);

    age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 0U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &first, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_TRUE(state, force_full != 0);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_count, TEST_PRESENTATION_EXTENT);

    age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 1U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &second, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, force_full, 0);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_start, second.row_start);

    age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 2U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &first, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 2U);
    DB_TEST_EXPECT_EQ_INT(state, force_full, 0);

    db_presentation_damage_history_reset(&history);
    count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &first, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_TRUE(state, force_full != 0);

    db_grid_block_t
        overflow_damage[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME + 1U] = {{0}};
    for (size_t index = 0U; index < DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME + 1U;
         index++) {
        overflow_damage[index] = (db_grid_block_t){
            .row_start = (uint32_t)(index % TEST_PRESENTATION_EXTENT),
            .row_count = 1U,
            .col_start = (uint32_t)(index % TEST_PRESENTATION_EXTENT),
            .col_count = 1U,
        };
    }
    age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 1U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    (void)db_presentation_damage_history_resolve(
        &history, &age,
        (db_grid_block_view_t){
            .blocks = overflow_damage,
            .count = DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME + 1U,
        },
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_GLX, 2U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    count = db_presentation_damage_history_resolve(
        &history, &age, (db_grid_block_view_t){.blocks = &first, .count = 1U},
        TEST_PRESENTATION_EXTENT, TEST_PRESENTATION_EXTENT, output,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_TRUE(state, force_full != 0);
}

static void db_test_rgba8_pixel_diff_reports_bounds(db_test_state_t *state) {
    uint8_t expected[TEST_RGBA8_2X2_BYTES] = {0};
    uint8_t actual[TEST_RGBA8_2X2_BYTES] = {0};
    actual[TEST_RGBA8_FIRST_MISMATCH_BYTE] = TEST_RGBA8_FIRST_MISMATCH_VALUE;
    actual[TEST_RGBA8_LAST_MISMATCH_BYTE] = TEST_RGBA8_LAST_MISMATCH_VALUE;
    const db_rgba8_pixel_diff_t diff =
        db_rgba8_pixel_diff(expected, actual, 2U, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, diff.mismatch_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, diff.first_x, 1U);
    DB_TEST_EXPECT_EQ_U32(state, diff.first_y, 0U);
    DB_TEST_EXPECT_EQ_U32(state, diff.min_x, 1U);
    DB_TEST_EXPECT_EQ_U32(state, diff.max_x, 1U);
    DB_TEST_EXPECT_EQ_U32(state, diff.min_y, 0U);
    DB_TEST_EXPECT_EQ_U32(state, diff.max_y, 1U);
}

static void
db_test_hidden_glfw_offscreen_full_draw_policy(db_test_state_t *state) {
    db_cli_config_t implicit_cfg = {0};

#ifdef __linux__
    db_cli_config_t explicit_cfg = {
        .backbuffer_draw_mode_explicit = 1,
    };

    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_force_hidden_glfw_offscreen_full_draw(
                            DB_GL_RENDERER_GL1_5_GLES1_1, &implicit_cfg) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_force_hidden_glfw_offscreen_full_draw(
                            DB_GL_RENDERER_GL1_5_GLES1_1, &explicit_cfg) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_force_hidden_glfw_offscreen_full_draw(
                            DB_GL_RENDERER_GL3_3, &implicit_cfg) == 0);
#else
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_force_hidden_glfw_offscreen_full_draw(
                            DB_GL_RENDERER_GL1_5_GLES1_1, &implicit_cfg) == 0);
#endif
}

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
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba8[0] & TEST_RGB10A2_ALPHA_MASK,
                          TEST_RGB10A2_ALPHA_OPAQUE);

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
    DB_TEST_EXPECT_EQ_U32(state, encoded_rgba16f & TEST_RGB10A2_ALPHA_MASK,
                          TEST_RGB10A2_ALPHA_OPAQUE);
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

static void db_test_presentation_transform_uses_fixed_logical_source(
    db_test_state_t *state) {
    const db_presentation_transform_t transform =
        db_display_presentation_transform(1279U, 719U);
    DB_TEST_EXPECT_EQ_INT(state, transform.source_width,
                          db_grid_cols_effective());
    DB_TEST_EXPECT_EQ_INT(state, transform.source_height,
                          db_grid_rows_effective());
    DB_TEST_EXPECT_EQ_INT(state, transform.destination_width, 1279U);
    DB_TEST_EXPECT_EQ_INT(state, transform.destination_height, 719U);
    DB_TEST_EXPECT_EQ_INT(state, transform.viewport_width, 1279U);
    DB_TEST_EXPECT_EQ_INT(state, transform.filter, DB_PRESENT_SCALE_NEAREST);
}

static void
db_test_cpu_presentation_scaler_uses_nearest_edges(db_test_state_t *state) {
    enum { DB_TEST_PRESENT_PIXEL_COUNT = 9 };
    static const uint32_t source_top_left = 0x102030FFU;
    static const uint32_t source_top_right = 0x405060FFU;
    static const uint32_t source_bottom_left = 0x708090FFU;
    static const uint32_t source_bottom_right = 0xA0B0C0FFU;
    uint32_t source_pixels[] = {source_top_left, source_top_right,
                                source_bottom_left, source_bottom_right};
    uint32_t destination_pixels[DB_TEST_PRESENT_PIXEL_COUNT] = {0U};
    const db_pixel_surface_t source = {
        .pixel_width = 2U,
        .pixel_height = 2U,
        .pixels = source_pixels,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    db_display_scale_surface_to_xrgb8888("test", &source,
                                         (uint8_t *)destination_pixels, 3U, 3U,
                                         3U * (uint32_t)sizeof(uint32_t));
    DB_TEST_EXPECT_EQ_U32(state, destination_pixels[0],
                          db_pack_xrgb8888_from_rgba8888(source_pixels[0]));
    DB_TEST_EXPECT_EQ_U32(state, destination_pixels[1],
                          db_pack_xrgb8888_from_rgba8888(source_pixels[0]));
    DB_TEST_EXPECT_EQ_U32(state, destination_pixels[2],
                          db_pack_xrgb8888_from_rgba8888(source_pixels[1]));
    DB_TEST_EXPECT_EQ_U32(state, destination_pixels[8],
                          db_pack_xrgb8888_from_rgba8888(source_pixels[3]));
}

static void
db_test_presentation_damage_mapping_is_conservative(db_test_state_t *state) {
    const db_grid_block_t logical = {
        .row_start = 1U,
        .row_count = 1U,
        .col_start = 1U,
        .col_count = 1U,
    };
    db_damage_block_t mapped = {0};
    int overflow = 0;
    const size_t count = db_presentation_map_logical_damage(
        (db_grid_block_view_t){.blocks = &logical, .count = 1U}, 3U, 3U, 7U, 5U,
        &mapped, 1U, &overflow);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, overflow, 0);
    DB_TEST_EXPECT_EQ_U32(state, mapped.col_start, 2U);
    DB_TEST_EXPECT_EQ_U32(state, mapped.col_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, mapped.row_start, 1U);
    DB_TEST_EXPECT_EQ_U32(state, mapped.row_count, 3U);
}

static void
db_test_cpu_dirty_scaling_matches_full_scaling(db_test_state_t *state) {
    enum {
        DB_TEST_DIRTY_DESTINATION_WIDTH = 7,
        DB_TEST_DIRTY_DESTINATION_HEIGHT = 5,
        DB_TEST_DESTINATION_PIXELS =
            DB_TEST_DIRTY_DESTINATION_WIDTH * DB_TEST_DIRTY_DESTINATION_HEIGHT,
    };
    static const uint32_t source_a = 0x102030FFU;
    static const uint32_t source_b = 0x405060FFU;
    static const uint32_t source_c = 0x708090FFU;
    static const uint32_t source_d = 0x90A0B0FFU;
    static const uint32_t source_e = 0xC0D0E0FFU;
    static const uint32_t source_f = 0x112233FFU;
    uint32_t source_pixels[] = {
        source_a, source_b, source_c, source_d, source_e, source_f,
    };
    uint32_t full[DB_TEST_DESTINATION_PIXELS] = {0U};
    uint32_t dirty[DB_TEST_DESTINATION_PIXELS] = {0U};
    const db_pixel_surface_t source = {
        .pixel_width = 3U,
        .pixel_height = 2U,
        .pixels = source_pixels,
        .format = DB_PIXEL_FORMAT_RGBA8,
    };
    db_display_scale_surface_to_xrgb8888(
        "test", &source, (uint8_t *)full, DB_TEST_DIRTY_DESTINATION_WIDTH,
        DB_TEST_DIRTY_DESTINATION_HEIGHT,
        DB_TEST_DIRTY_DESTINATION_WIDTH * (uint32_t)sizeof(uint32_t));
    const db_damage_block_t regions[] = {
        {.row_count = 2U, .col_count = DB_TEST_DIRTY_DESTINATION_WIDTH},
        {.row_start = 2U,
         .row_count = 3U,
         .col_count = DB_TEST_DIRTY_DESTINATION_WIDTH},
    };
    for (size_t index = 0U; index < sizeof(regions) / sizeof(regions[0]);
         index++) {
        (void)db_display_scale_surface_region_to_xrgb8888(
            "test", &source, (uint8_t *)dirty, DB_TEST_DIRTY_DESTINATION_WIDTH,
            DB_TEST_DIRTY_DESTINATION_HEIGHT,
            DB_TEST_DIRTY_DESTINATION_WIDTH * (uint32_t)sizeof(uint32_t),
            &regions[index]);
    }
    for (size_t index = 0U; index < DB_TEST_DESTINATION_PIXELS; index++) {
        DB_TEST_EXPECT_EQ_U32(state, dirty[index], full[index]);
    }
}

static void db_test_render_format_contract_tracks_resolved_display_format(
    db_test_state_t *state) {
    const db_display_resolved_format_config_t format =
        db_display_resolve_format_config_or_fail(
            "test", DB_PIXEL_FORMAT_RGBA16F, DB_OUTPUT_FORMAT_SDR, NULL);
    const db_render_format_contract_t contract =
        db_render_format_contract_from_display(&format);

    DB_TEST_EXPECT_EQ_INT(state, contract.renderer_write_format,
                          DB_PIXEL_FORMAT_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, contract.upload_format,
                          DB_PIXEL_FORMAT_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, contract.presentation_format,
                          DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
    DB_TEST_EXPECT_EQ_INT(state, contract.canonical_hash_format,
                          DB_PIXEL_FORMAT_RGBA8);
    DB_TEST_EXPECT_EQ_INT(state, contract.conversion,
                          DB_RENDER_FORMAT_CONVERSION_F64_TO_RGBA16F);
}

static void db_test_gl_runtime_mode_desc_reports_full_present_when_requested(
    db_test_state_t *state) {
    const db_gl_stream_upload_capability_t capability = {
        .target = DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER,
    };
    char text[DB_TEST_MODE_DESC_CAPACITY] = {0};
    const db_gl_runtime_mode_desc_t mode = db_gl_runtime_mode_desc_renderer(
        DB_GL_RUNTIME_DRAW_FULL_PRESENT, 1, &capability, 0);

    db_gl_runtime_mode_format_renderer(text, sizeof(text), &mode);

    DB_TEST_EXPECT_STR_EQ(state, text,
                          "draw=full_present, geometry=map_buffer, replay=no");
}

static void db_test_gl_runtime_mode_desc_reports_dirty_replay_when_requested(
    db_test_state_t *state) {
    const db_gl_stream_upload_capability_t capability = {
        .target = DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER,
        .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT,
        .requested_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .supported_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
        .effective_mode = DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE,
    };
    char text[DB_TEST_MODE_DESC_CAPACITY] = {0};
    const db_gl_runtime_mode_desc_t mode = db_gl_runtime_mode_desc_renderer(
        DB_GL_RUNTIME_DRAW_DIRTY_REPLAY, 1, &capability, 1);

    db_gl_runtime_mode_format_renderer(text, sizeof(text), &mode);

    DB_TEST_EXPECT_STR_EQ(state, text,
                          "draw=dirty_replay, geometry=map_range, replay=yes");
}

unsigned db_display_gl_runtime_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"glfw_policy_forces_full_draw_on_unstable_probe",
         db_test_glfw_policy_forces_full_draw_on_unstable_probe},
        {"glfw_policy_keeps_explicit_backbuffer_mode",
         db_test_glfw_policy_keeps_explicit_backbuffer_mode},
        {"glfw_policy_keeps_stable_preserved_chain",
         db_test_glfw_policy_keeps_stable_preserved_chain},
        {"glfw_policy_propagates_depth_three_to_resolved_runtime",
         db_test_glfw_policy_propagates_depth_three_to_resolved_runtime},
        {"glfw_policy_clamps_preserved_chain_depth",
         db_test_glfw_policy_clamps_preserved_chain_depth},
        {"default_framebuffer_probe_policy_visibility",
         db_test_default_framebuffer_probe_policy_visibility},
        {"default_framebuffer_probe_translation",
         db_test_default_framebuffer_probe_translation},
        {"presentation_buffer_age_sequences",
         db_test_presentation_buffer_age_sequences},
        {"scanout_serial_age_tracks_alternating_slots",
         db_test_scanout_serial_age_tracks_alternating_slots},
        {"presentation_damage_history_follows_age",
         db_test_presentation_damage_history_follows_age},
        {"rgba8_pixel_diff_reports_bounds",
         db_test_rgba8_pixel_diff_reports_bounds},
        {"hidden_glfw_offscreen_full_draw_policy",
         db_test_hidden_glfw_offscreen_full_draw_policy},
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
#ifdef DB_HAS_LINUX_KMS_ATOMIC
        {"kms_edid_hdr10_requires_pq_and_bt2020_rgb",
         db_test_kms_edid_hdr10_requires_pq_and_bt2020_rgb},
#endif
        {"presentation_transform_uses_fixed_logical_source",
         db_test_presentation_transform_uses_fixed_logical_source},
        {"cpu_presentation_scaler_uses_nearest_edges",
         db_test_cpu_presentation_scaler_uses_nearest_edges},
        {"presentation_damage_mapping_is_conservative",
         db_test_presentation_damage_mapping_is_conservative},
        {"cpu_dirty_scaling_matches_full_scaling",
         db_test_cpu_dirty_scaling_matches_full_scaling},
        {"render_format_contract_tracks_resolved_display_format",
         db_test_render_format_contract_tracks_resolved_display_format},
        {"gl_runtime_mode_desc_reports_full_present_when_requested",
         db_test_gl_runtime_mode_desc_reports_full_present_when_requested},
        {"gl_runtime_mode_desc_reports_dirty_replay_when_requested",
         db_test_gl_runtime_mode_desc_reports_dirty_replay_when_requested},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
