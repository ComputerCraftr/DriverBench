#include "support/test_harness.h"

#include "cli/cli_parse.h"
#include "config/runtime_options.h"
#include "driverbench_config.h"
#include "renderers/renderer_gl_common.h"

enum { DB_TEST_CLI_ERROR_TEXT_SIZE = 512 };

static void db_test_cli_valid_cpu_glfw_replace(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display",     "glfw_window",
                    "--api",       "cpu",           "--present-buffer-mode",
                    "replace",     "--frame-limit", "5"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) != 0);
    DB_TEST_EXPECT_EQ_U32(state, show_help, 0U);
    DB_TEST_EXPECT_EQ_U32(state, print_usage, 0U);
    DB_TEST_EXPECT_EQ_U32(state, cfg.display_is_set, 1U);
    DB_TEST_EXPECT_EQ_U32(state, cfg.frame_limit, 5U);
    DB_TEST_EXPECT_STR_EQ(
        state, db_runtime_option_get(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE),
        "replace");
}

static void db_test_cli_invalid_api(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display", "offscreen", "--api", "nope"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_EQ(state, error, "Unsupported api: nope");
}

static void db_test_cli_invalid_frame_limit(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display", "offscreen", "--frame-limit",
                    "abc"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_EQ(state, error, "invalid value for --frame-limit: abc");
}

static void db_test_cli_missing_option_value(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_EQ(state, error, "missing value for option: --display");
}

static void db_test_cli_unknown_option(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display", "offscreen", "--wat"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_EQ_U32(state, print_usage, 1U);
    DB_TEST_EXPECT_STR_EQ(state, error, "unknown option: --wat");
}

static void db_test_cli_help(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--help"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) != 0);
    DB_TEST_EXPECT_EQ_U32(state, show_help, 1U);
}

static void db_test_cli_invalid_present_mode_combo(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench", "--display", "offscreen",
                    "--api",       "cpu",       "--present-buffer-mode",
                    "ring"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_EQ(state, error,
                          "--present-buffer-mode is only supported for CPU "
                          "with --display glfw_window");
}

static void db_test_cli_invalid_gl1_replace_dirty(db_test_state_t *state) {
    char error[DB_TEST_CLI_ERROR_TEXT_SIZE] = {0};
    db_cli_config_t cfg = {0};
    int show_help = 0;
    int print_usage = 0;
    char *argv[] = {"driverbench",   "--display",
                    "glfw_window",   "--api",
                    "opengl",        "--renderer",
                    "gl1_5_gles1_1", "--backbuffer-draw-mode",
                    "dirty",         "--present-buffer-mode",
                    "replace"};
    DB_TEST_EXPECT_TRUE(state,
                        db_cli_try_parse((int)(sizeof(argv) / sizeof(argv[0])),
                                         argv, &cfg, &show_help, &print_usage,
                                         error, sizeof(error)) == 0);
    DB_TEST_EXPECT_STR_EQ(
        state, error,
        "--present-buffer-mode replace requires --backbuffer-draw-mode full");
}

static void db_test_present_mode_validate_request(db_test_state_t *state) {
    const char *reason = NULL;
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_present_mode_validate_request(
                            1, 1, 0, DB_GL_BACKBUFFER_DRAW_DIRTY,
                            DB_GL_PRESENT_BUFFER_MODE_REPLACE, &reason) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_gl_present_mode_validate_request(
                            1, 0, 0, DB_GL_BACKBUFFER_DRAW_DIRTY,
                            DB_GL_PRESENT_BUFFER_MODE_RING, &reason) == 0);
    DB_TEST_EXPECT_STR_EQ(state, reason,
                          "--present-buffer-mode is only supported for CPU "
                          "with --display glfw_window");
}

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

unsigned db_cli_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"valid_cpu_glfw_replace", db_test_cli_valid_cpu_glfw_replace},
        {"invalid_api", db_test_cli_invalid_api},
        {"invalid_frame_limit", db_test_cli_invalid_frame_limit},
        {"missing_option_value", db_test_cli_missing_option_value},
        {"unknown_option", db_test_cli_unknown_option},
        {"help", db_test_cli_help},
        {"invalid_present_mode_combo", db_test_cli_invalid_present_mode_combo},
        {"invalid_gl1_replace_dirty", db_test_cli_invalid_gl1_replace_dirty},
        {"present_mode_validate_request",
         db_test_present_mode_validate_request},
        {"present_mode_resolve_auto_downgrade",
         db_test_present_mode_resolve_auto_downgrade},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
