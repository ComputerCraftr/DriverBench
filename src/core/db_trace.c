#include "db_trace.h"

#include <stddef.h>

static db_trace_config_t g_trace_config = {0};
static db_run_log_identity_t g_run_identity = {0};

void db_trace_configure(const db_trace_config_t *config) {
    g_trace_config = (config != NULL) ? *config : (db_trace_config_t){0};
}

db_trace_config_t db_trace_config_current(void) { return g_trace_config; }

void db_run_log_identity_configure(const db_run_log_identity_t *identity) {
    g_run_identity =
        (identity != NULL) ? *identity : (db_run_log_identity_t){0};
}

db_run_log_identity_t db_run_log_identity_current(void) {
    return g_run_identity;
}
