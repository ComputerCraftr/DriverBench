#include <stdio.h>

#include "core/db_core.h"
#include "support/test_harness.h"

int main(void) {
    enum { DB_TEST_MAIN_FAILURE_TEXT_SIZE = 64 };
    unsigned failures = 0U;
    failures += db_benchmark_emitters_test_run_all();
    failures += db_benchmark_seeding_test_run_all();
    failures += db_cli_test_run_all();
    failures += db_core_logging_test_run_all();
    failures += db_damage_trace_test_run_all();
    failures += db_display_gl_runtime_test_run_all();
    failures += db_gl_shadow_present_test_run_all();
    failures += db_gl1_replay_test_run_all();
    failures += db_gradient_divergence_test_run_all();
    failures += db_hash_test_run_all();
    failures += db_numeric_test_run_all();
    failures += db_poll_policy_test_run_all();
    failures += db_render_ir_test_run_all();
    failures += db_sort_test_run_all();
#ifdef DB_HAS_VULKAN_API
    failures += db_vk_scheduler_test_run_all();
#endif
    if (failures != 0U) {
        char failure_text[DB_TEST_MAIN_FAILURE_TEXT_SIZE];
        (void)db_snprintf(failure_text, sizeof(failure_text),
                          "driverbench_unit_tests: %u failure(s)\n", failures);
        fputs(failure_text, stderr);
        return 1;
    }
    return 0;
}
