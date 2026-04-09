#include <stdio.h>

#include "core/db_core.h"
#include "support/test_harness.h"

int main(void) {
    enum { DB_TEST_MAIN_FAILURE_TEXT_SIZE = 64 };
    unsigned failures = 0U;
    failures += db_cli_test_run_all();
    failures += db_core_logging_test_run_all();
    failures += db_display_gl_runtime_test_run_all();
    failures += db_frame_delta_test_run_all();
    failures += db_gl_shadow_present_test_run_all();
    failures += db_snake_history_test_run_all();
    failures += db_snake_optimizer_test_run_all();
    if (failures != 0U) {
        char failure_text[DB_TEST_MAIN_FAILURE_TEXT_SIZE];
        (void)db_snprintf(failure_text, sizeof(failure_text),
                          "driverbench_unit_tests: %u failure(s)\n", failures);
        fputs(failure_text, stderr);
        return 1;
    }
    return 0;
}
