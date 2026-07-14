#include "support/test_harness.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "core/db_core.h"
#include "core/db_log.h"
#include "core/db_render_result.h"
#include "displays/display_runtime_config_common.h"

static const double g_db_test_progress_elapsed_ms = 5004.59;
static const double g_db_test_final_elapsed_ms = 10004.55;

enum {
    DB_TEST_LOG_LARGE_TEXT_SIZE = 1024,
    DB_TEST_LOG_MEDIUM_TEXT_SIZE = 256,
    DB_TEST_LOG_SMALL_TEXT_SIZE = 24,
};

static void
db_test_progress_benchmark_log_omits_static_mode(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 1024 };
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};

    DB_TEST_EXPECT_TRUE(
        state,
        db_format_benchmark_log(text, sizeof(text), "OpenGL", "gl1_5_gles1_1",
                                "display_glfw_window", 578U, 1024U,
                                g_db_test_progress_elapsed_ms, "progress") > 0);
    DB_TEST_EXPECT_TRUE(
        state, strstr(text, "event=benchmark_progress schema=2") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "frames=578") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "mode=") == NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "renderer=") == NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "backend=") == NULL);
}

static void
db_test_final_benchmark_log_uses_typed_identity(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 1024 };
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};

    DB_TEST_EXPECT_TRUE(
        state,
        db_format_benchmark_log(text, sizeof(text), "OpenGL", "gl1_5_gles1_1",
                                "display_glfw_window", 1178U, 1024U,
                                g_db_test_final_elapsed_ms, "final") > 0);
    DB_TEST_EXPECT_TRUE(
        state,
        strstr(text, "event=benchmark_final schema=2 api=OpenGL") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "renderer=gl1_5_gles1_1") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "mode=") == NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "work_units=1024") != NULL);
}

static void
db_test_structured_log_serializes_all_field_types(db_test_state_t *state) {
    char text[DB_TEST_LOG_LARGE_TEXT_SIZE] = {0};
    const db_log_field_t fields[] = {
        DB_LOG_I64("signed_value", -12),
        DB_LOG_U64("unsigned_value", 34U),
        DB_LOG_DOUBLE("ratio", 1.25),
        DB_LOG_BOOL("enabled", 1),
        DB_LOG_TOKEN("mode", "sample_fullscreen"),
        DB_LOG_STRING("message", "line one\n\"quoted\"\\tail"),
        DB_LOG_HEX64("hash", UINT64_C(0x1234)),
    };
    DB_TEST_EXPECT_TRUE(
        state, db_log_format_line(text, sizeof(text), DB_LOG_LEVEL_INFO,
                                  &(const db_log_event_t){
                                      "test_component", "test_event", fields,
                                      DB_LOG_FIELD_COUNT(fields)}) > 0);
    DB_TEST_EXPECT_TRUE(
        state,
        strstr(text, "[test_component][info] event=test_event schema=2") ==
            text);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "signed_value=-12") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "enabled=true") != NULL);
    DB_TEST_EXPECT_TRUE(
        state,
        strstr(text, "message=\"line one\\n\\\"quoted\\\"\\\\tail\"") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "hash=0x0000000000001234") != NULL);
    DB_TEST_EXPECT_TRUE(state, strchr(text, '\n') == strrchr(text, '\n'));
}

static void
db_test_structured_log_rejects_invalid_contracts(db_test_state_t *state) {
    char text[DB_TEST_LOG_MEDIUM_TEXT_SIZE] = {0};
    const db_log_field_t duplicate[] = {
        DB_LOG_U64("value", 1U),
        DB_LOG_U64("value", 2U),
    };
    const db_log_field_t invalid_key[] = {DB_LOG_U64("Bad-Key", 1U)};
    const db_log_field_t invalid_double[] = {DB_LOG_DOUBLE("value", HUGE_VAL)};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_log_format_line(
            text, sizeof(text), DB_LOG_LEVEL_INFO,
            &(const db_log_event_t){"test", "duplicate", duplicate,
                                    DB_LOG_FIELD_COUNT(duplicate)}),
        0);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_log_format_line(
            text, sizeof(text), DB_LOG_LEVEL_INFO,
            &(const db_log_event_t){"test", "invalid_key", invalid_key,
                                    DB_LOG_FIELD_COUNT(invalid_key)}),
        0);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_log_format_line(
            text, sizeof(text), DB_LOG_LEVEL_INFO,
            &(const db_log_event_t){"test", "invalid_double", invalid_double,
                                    DB_LOG_FIELD_COUNT(invalid_double)}),
        0);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_log_format_line(
            text, sizeof(text), DB_LOG_LEVEL_INFO,
            &(const db_log_event_t){"test", "BadEvent", NULL, 0U}),
        0);
}

static void
db_test_structured_log_rejects_small_output_buffer(db_test_state_t *state) {
    char text[DB_TEST_LOG_SMALL_TEXT_SIZE] = {0};
    const db_log_field_t fields[] = {
        DB_LOG_STRING("message", "this message cannot fit"),
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_log_format_line(text, sizeof(text), DB_LOG_LEVEL_ERROR,
                           &(const db_log_event_t){"test", "long_message",
                                                   fields,
                                                   DB_LOG_FIELD_COUNT(fields)}),
        0);
}

static void db_test_final_benchmark_log_is_format_safe(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 1024 };
    static const double db_test_total_ms = 1234.56;
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};

    DB_TEST_EXPECT_TRUE(state, db_format_benchmark_log(
                                   text, sizeof(text), "CPU", "renderer_cpu",
                                   "display_glfw_window_cpu_renderer", 139U,
                                   600000U, db_test_total_ms, "final") > 0);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "total_ms=1234.56") != NULL);
    DB_TEST_EXPECT_TRUE(state, strchr(text, '%') == NULL);
}

static void db_test_draw_stats_log_format_is_stable(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 256 };
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};
    const db_renderer_draw_path_stats_t stats = {
        .full_present_frames = 11U,
        .dirty_geometry_frames = 22U,
        .shadow_fallback_frames = 33U,
        .replay_only_frames = 44U,
    };

    DB_TEST_EXPECT_TRUE(state, db_display_format_draw_stats_log(
                                   text, sizeof(text), &stats) > 0);
    DB_TEST_EXPECT_STR_EQ(
        state, text,
        "draw stats: full_present_frames=11 dirty_geometry_frames=22 "
        "shadow_fallback_frames=33 replay_only_frames=44");
}

unsigned db_core_logging_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"progress_benchmark_log_omits_static_mode",
         db_test_progress_benchmark_log_omits_static_mode},
        {"final_benchmark_log_uses_typed_identity",
         db_test_final_benchmark_log_uses_typed_identity},
        {"final_benchmark_log_is_format_safe",
         db_test_final_benchmark_log_is_format_safe},
        {"draw_stats_log_format_is_stable",
         db_test_draw_stats_log_format_is_stable},
        {"structured_log_serializes_all_field_types",
         db_test_structured_log_serializes_all_field_types},
        {"structured_log_rejects_invalid_contracts",
         db_test_structured_log_rejects_invalid_contracts},
        {"structured_log_rejects_small_output_buffer",
         db_test_structured_log_rejects_small_output_buffer},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
