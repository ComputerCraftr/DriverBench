#ifndef DRIVERBENCH_DB_CORE_H
#define DRIVERBENCH_DB_CORE_H

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#ifdef DB_HAVE_STDCKDINT
#if DB_HAVE_STDCKDINT
#include <stdckdint.h>
#define DB_CAN_USE_STDCKDINT 1
#endif
#endif

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

static inline uint32_t db_fold_u64_to_u32(uint64_t value) {
    return (uint32_t)(value ^ (value >> 32U));
}

static inline uint32_t db_checked_add_u32(const char *backend,
                                          const char *field_name, uint32_t lhs,
                                          uint32_t rhs) {
    uint32_t out = 0U;
#ifdef DB_CAN_USE_STDCKDINT
    if (ckd_add(&out, lhs, rhs)) {
        db_failf(backend, "%s u32 add overflow: %u + %u", field_name, lhs, rhs);
    }
#else
    if (rhs > (UINT32_MAX - lhs)) {
        db_failf(backend, "%s u32 add overflow: %u + %u", field_name, lhs, rhs);
    }
    out = lhs + rhs;
#endif
    return out;
}

static inline uint32_t db_checked_sub_u32(const char *backend,
                                          const char *field_name, uint32_t lhs,
                                          uint32_t rhs) {
    uint32_t out = 0U;
#ifdef DB_CAN_USE_STDCKDINT
    if (ckd_sub(&out, lhs, rhs)) {
        db_failf(backend, "%s u32 sub underflow: %u - %u", field_name, lhs,
                 rhs);
    }
#else
    if (lhs < rhs) {
        db_failf(backend, "%s u32 sub underflow: %u - %u", field_name, lhs,
                 rhs);
    }
    out = lhs - rhs;
#endif
    return out;
}

static inline uint32_t db_checked_mul_u32(const char *backend,
                                          const char *field_name, uint32_t lhs,
                                          uint32_t rhs) {
    uint32_t out = 0U;
#ifdef DB_CAN_USE_STDCKDINT
    if (ckd_mul(&out, lhs, rhs)) {
        db_failf(backend, "%s u32 mul overflow: %u * %u", field_name, lhs, rhs);
    }
#else
    if ((lhs != 0U) && (rhs > (UINT32_MAX / lhs))) {
        db_failf(backend, "%s u32 mul overflow: %u * %u", field_name, lhs, rhs);
    }
    out = lhs * rhs;
#endif
    return out;
}

#endif
