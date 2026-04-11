#ifndef DRIVERBENCH_CORE_DB_TRACE_H
#define DRIVERBENCH_CORE_DB_TRACE_H

typedef struct {
    int damage;
    int shadow_upload;
    int gl_errors;
    int vulkan;
} db_trace_config_t;

typedef struct {
    const char *benchmark_mode;
    const char *presenter;
    const char *execution_strategy;
    const char *working_format;
    const char *native_format;
    const char *present_method;
} db_run_log_identity_t;

void db_trace_configure(const db_trace_config_t *config);
db_trace_config_t db_trace_config_current(void);
void db_run_log_identity_configure(const db_run_log_identity_t *identity);
db_run_log_identity_t db_run_log_identity_current(void);

#endif
