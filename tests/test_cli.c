#include "support/test_harness.h"
#include <stddef.h>

#include "cli/cli_parse.h"
#include "config/runtime_options.h"
#include "core/db_renderer_diagnostics.h"
#include "driverbench_config.h"
#include "renderers/gl_common.h"

enum { DB_TEST_CLI_ERROR_TEXT_SIZE = 512 };

#ifdef DB_HAS_GLFW
static void db_test_cli_valid_cpu_glfw_replace(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {
        "driverbench",           "--display", "glfw_window",   "--api", "cpu",
        "--present-buffer-mode", "replace",   "--frame-limit", "5"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) != 0);
    DB_TEST_EXPECT_EQ_U32(state, show_help, 0U);
    DB_TEST_EXPECT_EQ_U32(state, print_usage, 0U);
    DB_TEST_EXPECT_EQ_U32(state, cfg.display_is_set, 1U);
    DB_TEST_EXPECT_EQ_U32(state, cfg.frame_limit, 5U);
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE),
        "replace");
}
#endif

static void db_test_cli_invalid_api(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--display", "offscreen", "--api",
                          "nope"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "Unsupported api");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "nope");
}

static void db_test_cli_invalid_frame_limit(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--display", "offscreen",
                          "--frame-limit", "abc"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--frame-limit");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "abc");

    const char *overflow_argv[] = {"driverbench", "--display", "offscreen",
                                   "--frame-limit",
                                   "999999999999999999999999999"};
    DB_TEST_EXPECT_TRUE(
        state,
        db_cli_try_parse(sizeof(overflow_argv) / sizeof(overflow_argv[0]),
                         overflow_argv, &cfg, &show_help, &print_usage, error,
                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--frame-limit");
}

static void db_test_cli_missing_option_value(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--display"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "missing value");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--display");
}

static void db_test_cli_unknown_option(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--display", "offscreen", "--wat"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_EQ_U32(state, print_usage, 1U);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "unknown option");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--wat");
}

static void db_test_cli_help(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--help"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) != 0);
    DB_TEST_EXPECT_EQ_U32(state, show_help, 1U);
}

static void db_test_cli_invalid_present_mode_combo(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench", "--display", "offscreen",
                          "--api",       "cpu",       "--present-buffer-mode",
                          "ring"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--present-buffer-mode");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "CPU");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--display glfw_window");
}

#ifdef DB_HAS_GLFW
static void db_test_cli_invalid_gl1_replace_dirty(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *argv[] = {"driverbench",   "--display",
                          "glfw_window",   "--api",
                          "opengl",        "--renderer",
                          "gl1_5_gles1_1", "--backbuffer-draw-mode",
                          "dirty",         "--present-buffer-mode",
                          "replace"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv,
                                         &cfg, &show_help, &print_usage, error,
                                         sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--present-buffer-mode");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "replace");
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--backbuffer-draw-mode full");
}
#endif

#ifdef DB_HAS_GLFW
static void db_test_cli_present_mode_validation(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;

    // CPU with offscreen should fail for any present mode.
    {
        const char *argv[] = {
            "driverbench",           "--display", "offscreen", "--api", "cpu",
            "--present-buffer-mode", "replace"};
        DB_TEST_EXPECT_TRUE(
            state, db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv, &cfg,
                                    &show_help, &print_usage, error,
                                    sizeof(error)) == 0);
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--present-buffer-mode");
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "CPU");
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--display glfw_window");
    }

    // OpenGL with offscreen should fail for any present mode.
    {
        const char *argv[] = {"driverbench", "--display",
                              "offscreen",   "--api",
                              "opengl",      "--present-buffer-mode",
                              "ring"};
        DB_TEST_EXPECT_TRUE(
            state, db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv, &cfg,
                                    &show_help, &print_usage, error,
                                    sizeof(error)) == 0);
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--present-buffer-mode");
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--display glfw_window");
    }

    // OpenGL GL3.3 should fail for any present mode.
    {
        const char *argv[] = {"driverbench",  "--display",
                              "glfw_window",  "--api",
                              "opengl",       "--renderer",
                              "gl3_3",        "--present-buffer-mode",
                              "single_source"};
        DB_TEST_EXPECT_TRUE(
            state, db_cli_try_parse(sizeof(argv) / sizeof(argv[0]), argv, &cfg,
                                    &show_help, &print_usage, error,
                                    sizeof(error)) == 0);
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--present-buffer-mode");
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "--renderer gl1_5_gles1_1");
    }
}
#endif

static void
db_test_present_mode_resolve_auto_downgrade(db_test_state_t *state) {
    const db_gl_present_mode_request_t request = {
        .requested_backbuffer_draw_mode = DB_GL_BACKBUFFER_DRAW_FULL,
        .requested_present_buffer_mode = DB_GL_PRESENT_BUFFER_MODE_AUTO,
        .preserved_framebuffer_count = 2U,
        .prefer_ring_for_preserved_draw = 1,
        .present_upload =
            (db_gl_stream_upload_capability_t){
                .target = DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER,
                .requested_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .supported_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .effective_storage = DB_GL_STREAM_UPLOAD_STORAGE_CLIENT,
                .requested_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .supported_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
                .effective_mode = DB_GL_STREAM_UPLOAD_MODE_SUB_DATA,
            },
    };
    db_gl_present_mode_resolution_t resolved = {0};
    db_gl_present_mode_resolve(&request, &resolved);
    DB_TEST_EXPECT_EQ_U32(state, resolved.valid, 1U);
    DB_TEST_EXPECT_EQ_U32(state, resolved.downgraded, 1U);
    DB_TEST_EXPECT_EQ_U32(state, resolved.requested_preserve_mode,
                          DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT);
    DB_TEST_EXPECT_EQ_U32(state, resolved.effective_preserve_mode,
                          DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE);
    DB_TEST_EXPECT_STR_EQ(state, resolved.reason, "client upload fallback");
}

static void db_test_cli_trace_level_validation(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *damage_argv[] = {"driverbench", "--display", "offscreen",
                                 "--trace-damage", "4"};
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(damage_argv) / sizeof(damage_argv[0]),
                                damage_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "expected 0..3");

    error[0] = '\0';
    const char *vulkan_argv[] = {"driverbench", "--display", "offscreen",
                                 "--trace-vulkan", "3"};
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(vulkan_argv) / sizeof(vulkan_argv[0]),
                                vulkan_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "expected 0..2");

    error[0] = '\0';
    const char *gl_argv[] = {"driverbench", "--display", "offscreen",
                             "--trace-gl-errors", "2"};
    DB_TEST_EXPECT_TRUE(
        state,
        db_cli_try_parse(sizeof(gl_argv) / sizeof(gl_argv[0]), gl_argv, &cfg,
                         &show_help, &print_usage, error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "expected 0..1");

    error[0] = '\0';
    const char *valid_argv[] = {
        "driverbench", "--display",
        "offscreen",   "--trace-damage",
        "3",           "--trace-shadow-upload",
        "3",           "--trace-vulkan",
        "2",
    };
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(valid_argv) / sizeof(valid_argv[0]),
                                valid_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) != 0);
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_TRACE_DAMAGE), "3");
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_TRACE_SHADOW_UPLOAD), "3");
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_TRACE_VULKAN), "2");
}

static void db_test_cli_renderer_diagnostics(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *valid[] = {
        "driverbench",
        "--display",
        "offscreen",
        "--gl1-target",
        "persistent-fbo",
        "--gl1-gradient",
        "row-fill",
        "--gl1-replay-capacity",
        "3",
        "--gl3-gradient",
        "semantic",
        "--vk-gradient",
        "row-fill",
        "--ignore-conformance-cache",
        "1",
        "--rerun-conformance-probe",
        "1",
        "--dump-gradient-divergence",
        "gradient.log",
    };
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(valid) / sizeof(valid[0]),
                                         valid, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) != 0);
    const db_renderer_diagnostic_config_t diagnostics =
        db_renderer_diagnostic_config_resolve();
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.gl1_target,
                          DB_GL1_TARGET_PERSISTENT_FBO);
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.gl1_gradient,
                          DB_GL1_GRADIENT_ROW_FILL);
    DB_TEST_EXPECT_EQ_U32(state, diagnostics.gl1_replay_capacity, 3U);
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.gl3_gradient,
                          DB_GL3_GRADIENT_SEMANTIC);
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.vk_gradient,
                          DB_VK_GRADIENT_ROW_FILL);
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.ignore_conformance_cache, 1);
    DB_TEST_EXPECT_EQ_INT(state, diagnostics.rerun_conformance_probe, 1);
    DB_TEST_EXPECT_STR_EQ(state, diagnostics.gradient_divergence_path,
                          "gradient.log");

    const char *invalid_capacity[] = {"driverbench", "--display", "offscreen",
                                      "--gl1-replay-capacity", "9"};
    DB_TEST_EXPECT_TRUE(
        state,
        db_cli_try_parse(sizeof(invalid_capacity) / sizeof(invalid_capacity[0]),
                         invalid_capacity, &cfg, &show_help, &print_usage,
                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "expected 1..8");
}

static void db_test_cli_presentation_format_validation(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *valid_argv[] = {
        "driverbench", "--display",       "offscreen", "--working-format",
        "rgba16f",     "--output-format", "sdr",
    };
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(valid_argv) / sizeof(valid_argv[0]),
                                valid_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) != 0);
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_WORKING_FORMAT), "rgba16f");
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_OUTPUT_FORMAT), "sdr");

    error[0] = '\0';
    const char *invalid_argv[] = {"driverbench", "--working-format", "hdr"};
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(invalid_argv) / sizeof(invalid_argv[0]),
                                invalid_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "expected rgba8|rgba16f");

    error[0] = '\0';
    const char *removed_argv[] = {"driverbench", "--cpu-hdr", "1"};
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(removed_argv) / sizeof(removed_argv[0]),
                                removed_argv, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "unknown option: --cpu-hdr");
}

#ifdef DB_HAS_GLFW
static void db_test_cli_resize_schedule_validation(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    const char *valid[] = {"driverbench", "--display", "glfw_window",
                           "--resize-at-frame", "2:1279x719"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse(sizeof(valid) / sizeof(valid[0]),
                                         valid, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) != 0);
    db_resize_schedule_t schedule = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_resize_schedule_parse(
                   db_runtime_option_get(DB_RUNTIME_OPT_RESIZE_AT_FRAME),
                   &schedule) != 0);
    DB_TEST_EXPECT_EQ_U32(state, schedule.frame, 2U);
    DB_TEST_EXPECT_EQ_U32(state, schedule.width, 1279U);
    DB_TEST_EXPECT_EQ_U32(state, schedule.height, 719U);

    static const char *const invalid_values[] = {
        "2",       "2:0x719",          "2:1279x0",
        "2:1279x", "2:4294967296x719", "999999999999999999999999999:1279x719"};
    for (size_t i = 0U; i < sizeof(invalid_values) / sizeof(invalid_values[0]);
         i++) {
        error[0] = '\0';
        const char *invalid[] = {"driverbench", "--display", "glfw_window",
                                 "--resize-at-frame", invalid_values[i]};
        DB_TEST_EXPECT_TRUE(
            state, db_cli_try_parse(sizeof(invalid) / sizeof(invalid[0]),
                                    invalid, &cfg, &show_help, &print_usage,
                                    error, sizeof(error)) == 0);
        DB_TEST_EXPECT_STR_CONTAINS(state, error, "Invalid resize schedule");
    }

    error[0] = '\0';
    const char *unsupported[] = {"driverbench", "--display", "offscreen",
                                 "--resize-at-frame", "2:1279x719"};
    DB_TEST_EXPECT_TRUE(
        state, db_cli_try_parse(sizeof(unsupported) / sizeof(unsupported[0]),
                                unsupported, &cfg, &show_help, &print_usage,
                                error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_CONTAINS(state, error, "--display glfw_window");
}
#endif

unsigned db_cli_test_run_all(void) {
    static const db_test_case_t cases[] = {
#ifdef DB_HAS_GLFW
        {"valid_cpu_glfw_replace", db_test_cli_valid_cpu_glfw_replace},
#endif
        {"invalid_api", db_test_cli_invalid_api},
        {"invalid_frame_limit", db_test_cli_invalid_frame_limit},
        {"missing_option_value", db_test_cli_missing_option_value},
        {"unknown_option", db_test_cli_unknown_option},
        {"help", db_test_cli_help},
        {"invalid_present_mode_combo", db_test_cli_invalid_present_mode_combo},
#ifdef DB_HAS_GLFW
        {"invalid_gl1_replace_dirty", db_test_cli_invalid_gl1_replace_dirty},
        {"cli_present_mode_validation", db_test_cli_present_mode_validation},
#endif
        {"present_mode_resolve_auto_downgrade",
         db_test_present_mode_resolve_auto_downgrade},
        {"cli_trace_level_validation", db_test_cli_trace_level_validation},
        {"cli_renderer_diagnostics", db_test_cli_renderer_diagnostics},
        {"cli_presentation_format_validation",
         db_test_cli_presentation_format_validation},
#ifdef DB_HAS_GLFW
        {"cli_resize_schedule_validation",
         db_test_cli_resize_schedule_validation},
#endif
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
