#include "support/test_harness.h"

#include <string.h>

#include "core/db_core.h"
#include "displays/display_runtime_config_common.h"
#include "renderers/renderer_gl_common.h"

static const double g_db_test_progress_elapsed_ms = 5004.59;
static const double g_db_test_final_elapsed_ms = 10004.55;

static void
db_test_progress_benchmark_log_omits_static_mode(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 256 };
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};

    DB_TEST_EXPECT_TRUE(
        state,
        db_format_benchmark_log(text, sizeof(text), "OpenGL", "gl1_5_gles1_1",
                                "display_glfw_window", 578U, 1024U,
                                g_db_test_progress_elapsed_ms, "progress",
                                "draw=full_present, geometry=map_range") > 0);
    DB_TEST_EXPECT_TRUE(
        state, strstr(text, "OpenGL benchmark (progress): frames=578") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "mode=") == NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "renderer=") == NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "backend=") == NULL);
}

static void
db_test_final_benchmark_log_keeps_static_mode_context(db_test_state_t *state) {
    enum { DB_TEST_LOG_TEXT_SIZE = 256 };
    char text[DB_TEST_LOG_TEXT_SIZE] = {0};

    DB_TEST_EXPECT_TRUE(state,
                        db_format_benchmark_log(
                            text, sizeof(text), "OpenGL", "gl1_5_gles1_1",
                            "display_glfw_window", 1178U, 1024U,
                            g_db_test_final_elapsed_ms, "final",
                            "draw=full_present, geometry=buffer_object") > 0);
    DB_TEST_EXPECT_TRUE(
        state,
        strstr(text, "OpenGL benchmark (final): renderer=gl1_5_gles1_1 "
                     "backend=display_glfw_window mode=draw=full_present, "
                     "geometry=buffer_object") != NULL);
    DB_TEST_EXPECT_TRUE(state, strstr(text, "work_units=1024") != NULL);
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
        {"final_benchmark_log_keeps_static_mode_context",
         db_test_final_benchmark_log_keeps_static_mode_context},
        {"draw_stats_log_format_is_stable",
         db_test_draw_stats_log_format_is_stable},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
