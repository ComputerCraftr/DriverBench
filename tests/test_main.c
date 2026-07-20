#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/db_core.h"
#include "core/db_numeric.h"
#include "support/test_harness.h"

static int verify_failure_propagation(void) {
    db_test_state_t state = {0U};
    DB_TEST_EXPECT_TRUE(&state, 0);
    return DB_BOOL(state.failures);
}

static int verify_failure_count_propagation(void) {
    db_test_state_t state = {0U};
    DB_TEST_EXPECT_TRUE(&state, 0);
    DB_TEST_EXPECT_TRUE(&state, 0);
    return (int)state.failures;
}

static int verify_nan_failure_propagation(void) {
    db_test_state_t state = {0U};
    DB_TEST_EXPECT_DOUBLE_EQUAL(&state, nan(""), 0.0);
    return DB_BOOL(state.failures);
}

int main(int argc, char **argv) {
    enum { DB_TEST_MAIN_FAILURE_TEXT_SIZE = 64 };
    if ((argc == 2) && (strcmp(argv[1], "--verify-failure-propagation") == 0)) {
        return verify_failure_propagation();
    }
    if ((argc == 2) &&
        (strcmp(argv[1], "--verify-nan-failure-propagation") == 0)) {
        return verify_nan_failure_propagation();
    }
    if ((argc == 2) &&
        (strcmp(argv[1], "--verify-failure-count-propagation") == 0)) {
        return verify_failure_count_propagation();
    }
    if (argc != 1) {
        return 2;
    }
    unsigned failures = 0U;
    failures += db_benchmark_checkpoint_transaction_test_run_all();
    failures += db_benchmark_emitters_test_run_all();
    failures += db_benchmark_seeding_test_run_all();
    failures += db_cli_test_run_all();
    failures += db_core_logging_test_run_all();
    failures += db_damage_trace_test_run_all();
    failures += db_display_gl_runtime_test_run_all();
    failures += db_display_hdr_test_run_all();
    failures += db_frame_coordinator_test_run_all();
    failures += db_gl_shadow_present_test_run_all();
    failures += db_gl1_replay_test_run_all();
    failures += db_gradient_divergence_test_run_all();
    failures += db_hash_test_run_all();
    failures += db_metrics_policy_test_run_all();
    failures += db_numeric_test_run_all();
    failures += db_progress_policy_test_run_all();
    failures += db_render_ir_clip_test_run_all();
    failures += db_render_ir_malformed_test_run_all();
    failures += db_render_ir_policy_test_run_all();
    failures += db_render_ir_ranges_test_run_all();
    failures += db_render_ir_snapshot_test_run_all();
    failures += db_render_ir_test_run_all();
    failures += db_render_ir_upload_test_run_all();
    failures += db_run_session_test_run_all();
    failures += db_sort_test_run_all();
#ifdef DB_HAS_VULKAN_API
    failures += db_vk_scheduler_test_run_all();
#endif
    if (DB_BOOL(failures)) {
        char failure_text[DB_TEST_MAIN_FAILURE_TEXT_SIZE];
        (void)db_snprintf(failure_text, sizeof(failure_text),
                          "driverbench_unit_tests: %u failure(s)\n", failures);
        fputs(failure_text, stderr);
        return 1;
    }
    return 0;
}
