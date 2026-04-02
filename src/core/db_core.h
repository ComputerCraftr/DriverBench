#ifndef DRIVERBENCH_DB_CORE_H
#define DRIVERBENCH_DB_CORE_H

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef DB_HAVE_STDCKDINT
#if DB_HAVE_STDCKDINT
#include <stdckdint.h>
#define DB_CAN_USE_STDCKDINT 1
#endif
#endif
#define DB_MS_PER_SECOND 1000.0
#define DB_NS_PER_MS 1000000.0
#define DB_NS_PER_SECOND 1000000000.0
#define DB_NS_PER_SECOND_U64 UINT64_C(1000000000)
#define DB_CACHELINE_ALIGNMENT_BYTES 64U
#define DB_U32_MIX_SHIFT_A 16U
#define DB_U32_MIX_SHIFT_B 15U
#define DB_U32_MIX_MUL_A 0x7FEB352DU
#define DB_U32_MIX_MUL_B 0x846CA68BU

// Logging and diagnostics.
void db_failf(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
void db_infof(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list ap);
int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Runtime option parsing and process lifecycle.
int db_parse_bool_text(const char *value, int *out_value);
int db_parse_fps_cap_text(const char *value, double *out_value);
uint32_t db_fold_u64_to_u32(uint64_t value);
uint32_t db_mix_u32(uint32_t value);
void db_validate_runtime_environment(const char *backend,
                                     const char *remote_override_option);
void db_install_signal_handlers(void);
int db_should_stop(void);
uint64_t db_now_ns_monotonic(void);
void db_sleep_to_fps_cap(const char *backend, uint64_t frame_start_ns,
                         double fps_cap);

// Benchmark logging.
void db_benchmark_log_periodic(const char *api_name, const char *renderer_name,
                               const char *backend_name, uint64_t frames,
                               uint32_t work_units, double elapsed_ms,
                               const char *capability_mode,
                               double *next_log_due_ms, double interval_ms);
void db_benchmark_log_final(const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *capability_mode);

#define DB_LOG_CAPACITY_EXCEEDED_ONCE(backend, tag, required, capacity)        \
    do {                                                                       \
        static int db_capacity_warned_once_ = 0;                               \
        const size_t db_required_ = (size_t)(required);                        \
        const size_t db_capacity_ = (size_t)(capacity);                        \
        if ((db_capacity_warned_once_ == 0) &&                                 \
            (db_required_ > db_capacity_)) {                                   \
            db_infof((backend),                                                \
                     "capacity exceeded for %s: required=%zu capacity=%zu "    \
                     "(truncating)",                                           \
                     (tag), db_required_, db_capacity_);                       \
            db_capacity_warned_once_ = 1;                                      \
        }                                                                      \
    } while (0)

// Checked conversion and allocation helpers.
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

static inline uint32_t
db_checked_int_to_u32(const char *backend, const char *field_name, int value) {
    if (value < 0) {
        db_failf(backend, "%s out of u32 range: %d", field_name, value);
    }
    return (uint32_t)value;
}

static inline uint32_t db_checked_size_to_u32(const char *backend,
                                              const char *field_name,
                                              size_t value) {
    if (value > (size_t)UINT32_MAX) {
        db_failf(backend, "%s out of u32 range: %zu", field_name, value);
    }
    return (uint32_t)value;
}

static inline uint32_t db_checked_u64_to_u32(const char *backend,
                                             const char *field_name,
                                             uint64_t value) {
    if (value > (uint64_t)UINT32_MAX) {
        db_failf(backend, "%s out of u32 range: %llu", field_name,
                 (unsigned long long)value);
    }
    return (uint32_t)value;
}

static inline size_t db_checked_u64_to_size(const char *backend,
                                            const char *field_name,
                                            uint64_t value) {
    if (value > (uint64_t)SIZE_MAX) {
        db_failf(backend, "%s out of size_t range: %llu", field_name,
                 (unsigned long long)value);
    }
    return (size_t)value;
}

static inline void *db_alloc_array_or_fail(const char *backend,
                                           const char *field_name,
                                           size_t element_count,
                                           size_t element_size) {
    if (element_size == 0U) {
        db_failf(backend, "%s element_size is zero", field_name);
    }
    if (element_count > (SIZE_MAX / element_size)) {
        db_failf(backend, "%s allocation overflow (%zu * %zu)", field_name,
                 element_count, element_size);
    }
    void *memory = malloc(element_count * element_size);
    if (memory == NULL) {
        db_failf(backend, "failed to allocate %s (%zu * %zu)", field_name,
                 element_count, element_size);
    }
    return memory;
}

static inline void *db_alloc_aligned_array_or_fail(const char *backend,
                                                   const char *field_name,
                                                   size_t element_count,
                                                   size_t element_size,
                                                   size_t alignment) {
    if (element_size == 0U) {
        db_failf(backend, "%s element_size is zero", field_name);
    }
    if ((alignment == 0U) || ((alignment & (alignment - 1U)) != 0U) ||
        (alignment < sizeof(void *))) {
        db_failf(backend, "%s invalid alignment: %zu", field_name, alignment);
    }
    if (element_count > (SIZE_MAX / element_size)) {
        db_failf(backend, "%s allocation overflow (%zu * %zu)", field_name,
                 element_count, element_size);
    }
    const size_t payload_bytes = element_count * element_size;
    size_t aligned_bytes = payload_bytes;
    const size_t remainder = aligned_bytes % alignment;
    if (remainder != 0U) {
        const size_t add_bytes = alignment - remainder;
        if (aligned_bytes > (SIZE_MAX - add_bytes)) {
            db_failf(backend, "%s aligned allocation size overflow",
                     field_name);
        }
        aligned_bytes += add_bytes;
    }
    void *const memory = aligned_alloc(alignment, aligned_bytes);
    if (memory == NULL) {
        db_failf(backend,
                 "failed to aligned-allocate %s (%zu bytes, align=%zu)",
                 field_name, aligned_bytes, alignment);
    }
    return memory;
}

static inline long db_checked_double_to_long(const char *backend,
                                             const char *field_name,
                                             double value) {
    if (!(value >= (double)LONG_MIN) || (value > (double)LONG_MAX)) {
        db_failf(backend, "%s out of long range: %.3f", field_name, value);
    }
    return (long)value;
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
