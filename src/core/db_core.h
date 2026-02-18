#ifndef DRIVERBENCH_DB_CORE_H
#define DRIVERBENCH_DB_CORE_H

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void db_failf(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
void db_infof(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list ap);
int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

int db_env_is_truthy(const char *name);
int db_has_ssh_env(void);
int db_is_forwarded_x11_display(void);
void db_validate_runtime_environment(const char *backend,
                                     const char *remote_override_env);
void db_install_signal_handlers(void);
int db_should_stop(void);

uint8_t *db_read_file_or_fail(const char *backend, const char *path,
                              size_t *out_sz);
char *db_read_text_file_or_fail(const char *backend, const char *path);

void db_benchmark_log_periodic(const char *api_name, const char *renderer_name,
                               const char *backend_name, uint64_t frames,
                               uint32_t work_units, double elapsed_ms,
                               const char *capability_mode,
                               double *next_log_due_ms, double interval_ms);
void db_benchmark_log_final(const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *capability_mode);

static inline int32_t db_checked_u32_to_i32(const char *backend,
                                            const char *field_name,
                                            uint32_t value) {
    if (value > (uint32_t)INT32_MAX) {
        db_failf(backend, "%s out of i32 range: %u", field_name, value);
    }
    return (int32_t)value;
}

static inline int32_t db_checked_int_to_i32(const char *backend,
                                            const char *field_name, int value) {
    if ((value < INT32_MIN) || (value > INT32_MAX)) {
        db_failf(backend, "%s out of i32 range: %d", field_name, value);
    }
    return (int32_t)value;
}

static inline long db_checked_double_to_long(const char *backend,
                                             const char *field_name,
                                             double value) {
    if (!(value >= (double)LONG_MIN) || (value > (double)LONG_MAX)) {
        db_failf(backend, "%s out of long range: %.3f", field_name, value);
    }
    return (long)value;
}

#endif
