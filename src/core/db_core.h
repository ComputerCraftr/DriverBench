#ifndef DRIVERBENCH_DB_CORE_H
#define DRIVERBENCH_DB_CORE_H

#include "db_numeric.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
#include <stdckdint.h>
#endif
#define DB_MS_PER_SECOND 1000.0
#define DB_NS_PER_MS 1000000.0
#define DB_NS_PER_SECOND 1000000000.0
#define DB_NS_PER_SECOND_U64 UINT64_C(1000000000)
#define DB_CACHELINE_ALIGNMENT_BYTES 64U
#define DB_PARSE_BASE_AUTODETECT 0
#define DB_PARSE_BASE_MIN 2
#define DB_PARSE_BASE_DECIMAL 10
#define DB_PARSE_BASE_MAX 36
#define DB_U32_MIX_SHIFT_A 16U
#define DB_U32_MIX_SHIFT_B 15U
#define DB_U32_MIX_MUL_A 0x7FEB352DU
#define DB_U32_MIX_MUL_B 0x846CA68BU

#if defined(__GNUC__) || defined(__clang__)
#define DB_ASSUME_ALIGNED(ptr, alignment)                                      \
    __builtin_assume_aligned((ptr), (alignment))
#else
#define DB_ASSUME_ALIGNED(ptr, alignment) (ptr)
#endif

// Logging and diagnostics.
void db_failf(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
void db_errorf(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void db_infof(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list ap)
    __attribute__((format(printf, 3, 0)));
int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Runtime option parsing and process lifecycle.
int db_parse_bool_text(const char *value, int *out_value);
int db_parse_int_text(const char *value, int *out_value);
int db_parse_fps_cap_text(const char *value, double *out_value);
int db_parse_u32_prefix(const char *value, int base, uint32_t *out_value,
                        const char **out_end);
int db_parse_long_prefix(const char *value, int base, long *out_value,
                         const char **out_end);
int db_parse_double_prefix(const char *value, double *out_value,
                           const char **out_end);
uint32_t db_fold_u64_to_u32(uint64_t value);
uint32_t db_mix_u32(uint32_t value);
int db_runtime_is_linux_x11(void);
void db_validate_runtime_environment(const char *backend,
                                     const char *remote_override_option);
void db_install_signal_handlers(void);
int db_should_stop(void);
uint64_t db_now_ns_monotonic(void);
void db_sleep_to_fps_cap(const char *backend, uint64_t frame_start_ns,
                         double fps_cap);

// Benchmark logging contract:
// - progress logs contain throughput fields only
// - final logs may include static mode/config context
int db_format_benchmark_log(char *buffer, size_t buffer_size,
                            const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *tag);
void db_log_progress_periodic(const char *api_name, const char *renderer_name,
                              const char *backend_name, uint64_t frames,
                              uint32_t work_units, double elapsed_ms,
                              double *next_log_due_ms, double interval_ms);
void db_benchmark_log_final(const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif

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

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// Helper naming contract:
// - db_checked_*: checked arithmetic, conversion, or allocation helpers only.
// - db_*_or_fail: domain or precondition wrappers that may fail for semantic,
//   object-level, or contract reasons beyond a single numeric check.
// - plain db_*: total or otherwise non-failing utility helpers.
static inline int db_checked_long_to_int(const char *backend,
                                         const char *field_name, long value) {
    if ((value < (long)INT_MIN) || (value > (long)INT_MAX)) {
        db_failf(backend, "%s out of int range: %ld", field_name, value);
    }
    return (int)value;
}

static inline int db_checked_u32_to_int(const char *backend,
                                        const char *field_name,
                                        uint32_t value) {
    if (value > (uint32_t)INT_MAX) {
        db_failf(backend, "%s out of int range: %u", field_name, value);
    }
    return (int)value;
}

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

static inline uint32_t db_checked_i32_to_u32(const char *backend,
                                             const char *field_name,
                                             int32_t value) {
    if (value < 0) {
        db_failf(backend, "%s out of u32 range: %d", field_name, value);
    }
    return (uint32_t)value;
}

static inline uint32_t db_nonnegative_int_to_u32_or_zero(int value) {
    if (value <= 0) {
        return 0U;
    }
    return (uint32_t)value;
}

static inline uint32_t db_u64_to_u32_saturating(uint64_t value) {
    if (value > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)value;
}

static inline uint32_t db_size_to_u32_or_zero(size_t value) {
    if (value > UINT32_MAX) {
        return 0U;
    }
    return (uint32_t)value;
}

static inline size_t db_checked_u32_to_size(const char *backend,
                                            const char *field_name,
                                            uint32_t value) {
    (void)backend;
    (void)field_name;
    return (size_t)value;
}

static inline size_t db_checked_int_to_size(const char *backend,
                                            const char *field_name, int value) {
    if (value < 0) {
        db_failf(backend, "%s out of size_t range: %d", field_name, value);
    }
    return (size_t)value;
}

static inline uint32_t db_checked_size_to_u32(const char *backend,
                                              const char *field_name,
                                              size_t value) {
    if (value > (size_t)UINT32_MAX) {
        db_failf(backend, "%s out of u32 range: %zu", field_name, value);
    }
    return (uint32_t)value;
}

static inline int32_t db_checked_size_to_i32(const char *backend,
                                             const char *field_name,
                                             size_t value) {
    if (value > (size_t)INT32_MAX) {
        db_failf(backend, "%s out of i32 range: %zu", field_name, value);
    }
    return (int32_t)value;
}

static inline int db_checked_size_to_int(const char *backend,
                                         const char *field_name, size_t value) {
    if (value > (size_t)INT_MAX) {
        db_failf(backend, "%s out of int range: %zu", field_name, value);
    }
    return (int)value;
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

static inline uint32_t db_checked_ulong_to_u32(const char *backend,
                                               const char *field_name,
                                               unsigned long value) {
    if (value > (unsigned long)UINT32_MAX) {
        db_failf(backend, "%s out of u32 range: %lu", field_name, value);
    }
    return (uint32_t)value;
}

static inline int32_t db_checked_u64_to_i32(const char *backend,
                                            const char *field_name,
                                            uint64_t value) {
    if (value > (uint64_t)INT32_MAX) {
        db_failf(backend, "%s out of i32 range: %llu", field_name,
                 (unsigned long long)value);
    }
    return (int32_t)value;
}

static inline size_t db_checked_u64_to_size(const char *backend,
                                            const char *field_name,
                                            uint64_t value) {
    if (value > SIZE_MAX) {
        db_failf(backend, "%s out of size_t range: %llu", field_name,
                 (unsigned long long)value);
    }
    return (size_t)value;
}

static inline size_t db_checked_ptrdiff_to_size(const char *backend,
                                                const char *field_name,
                                                ptrdiff_t value) {
    if (value < 0) {
        db_failf(backend, "%s is negative: %td", field_name, value);
    }
    return (size_t)value;
}

static inline void *db_malloc_or_fail(const char *backend,
                                      const char *field_name,
                                      size_t element_count,
                                      size_t element_size) {
    if ((element_count == 0U) || (element_size == 0U)) {
        db_failf(backend, "%s allocation extent is zero", field_name);
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

static inline void *db_calloc_or_fail(const char *backend,
                                      const char *field_name,
                                      size_t element_count, size_t element_size,
                                      size_t alignment) {
    if ((element_count == 0U) || (element_size == 0U)) {
        db_failf(backend, "%s allocation extent is zero", field_name);
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
    memset(memory, 0, aligned_bytes);
    return memory;
}

static inline void *db_calloc_array_or_fail(const char *backend,
                                            const char *field_name,
                                            size_t element_count,
                                            size_t element_size) {
    void *const memory =
        db_malloc_or_fail(backend, field_name, element_count, element_size);
    const size_t allocation_bytes = element_count * element_size;
    memset(memory, 0, allocation_bytes);
    return memory;
}

// Vulkan and similar APIs typedef handles as pointers, making an array a T **.
// Keep the explicit multilevel-to-void conversion at the ownership boundary.
static inline void db_free_opaque_handle_array(void *memory) { free(memory); }

static inline int db_try_double_to_long(double value, long *out) {
    const double lower_bound = DB_TO_F64(LONG_MIN);
    const double upper_bound_exclusive = -lower_bound;
    if ((out == NULL) || !isfinite(value)) {
        return 0;
    }
    const double integral_value = trunc(value);
    if ((integral_value < lower_bound) ||
        (integral_value >= upper_bound_exclusive)) {
        return 0;
    }
    *out = (long)value;
    return 1;
}

static inline long db_checked_double_to_long(const char *backend,
                                             const char *field_name,
                                             double value) {
    long result = 0;
    if (db_try_double_to_long(value, &result) == 0) {
        db_failf(backend, "%s out of long range: %.3f", field_name, value);
    }
    return result;
}

static inline uint32_t db_checked_double_to_u32(const char *backend,
                                                const char *field_name,
                                                double value) {
    if (!isfinite(value) || (value < 0.0) || (value > UINT32_MAX)) {
        db_failf(backend, "%s out of u32 range: %.3f", field_name, value);
    }
    return (uint32_t)value;
}

static inline uint64_t db_checked_double_to_u64(const char *backend,
                                                const char *field_name,
                                                double value) {
    if (!isfinite(value) || (value < 0.0) || (value >= DB_TO_F64(UINT64_MAX))) {
        db_failf(backend, "%s out of u64 range: %.3f", field_name, value);
    }
    return (uint64_t)value;
}

static inline uint32_t db_checked_add_u32(const char *backend,
                                          const char *field_name, uint32_t lhs,
                                          uint32_t rhs) {
    uint32_t out = 0U;
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
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
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
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
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
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

static inline int db_try_mul_u64(uint64_t lhs, uint64_t rhs, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
    return !ckd_mul(out, lhs, rhs);
#else
    if ((lhs != 0U) && (rhs > (UINT64_MAX / lhs))) {
        return 0;
    }
    *out = lhs * rhs;
    return 1;
#endif
}

static inline int db_try_add_u64(uint64_t lhs, uint64_t rhs, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
    return !ckd_add(out, lhs, rhs);
#else
    if (rhs > (UINT64_MAX - lhs)) {
        return 0;
    }
    *out = lhs + rhs;
    return 1;
#endif
}

static inline uint64_t db_checked_add_u64(const char *backend,
                                          const char *field_name, uint64_t lhs,
                                          uint64_t rhs) {
    uint64_t out = 0U;
    if (db_try_add_u64(lhs, rhs, &out) == 0) {
        db_failf(backend, "%s u64 add overflow", field_name);
    }
    return out;
}

static inline int db_try_mul_size(size_t lhs, size_t rhs, size_t *out) {
    if (out == NULL) {
        return 0;
    }
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
    return !ckd_mul(out, lhs, rhs);
#else
    if ((lhs != 0U) && (rhs > (SIZE_MAX / lhs))) {
        return 0;
    }
    *out = lhs * rhs;
    return 1;
#endif
}

static inline int db_try_add_size(size_t lhs, size_t rhs, size_t *out) {
    if (out == NULL) {
        return 0;
    }
#if defined(DB_HAVE_STDCKDINT) && DB_HAVE_STDCKDINT
    return !ckd_add(out, lhs, rhs);
#else
    if (rhs > (SIZE_MAX - lhs)) {
        return 0;
    }
    *out = lhs + rhs;
    return 1;
#endif
}

static inline int db_size_range_fits(size_t total_size, size_t offset,
                                     size_t length) {
    return (offset <= total_size) && (length <= (total_size - offset));
}

static inline int db_try_strided_size(size_t row_count, size_t row_stride,
                                      size_t row_bytes, size_t *out_size) {
    if ((out_size == NULL) || (row_count == 0U) || (row_stride < row_bytes)) {
        return 0;
    }
    size_t last_row_offset = 0U;
    return db_try_mul_size(row_count - 1U, row_stride, &last_row_offset) &&
           db_try_add_size(last_row_offset, row_bytes, out_size);
}

static inline size_t db_checked_mul_size(const char *backend,
                                         const char *field_name, size_t lhs,
                                         size_t rhs) {
    size_t out = 0U;
    if (db_try_mul_size(lhs, rhs, &out) == 0) {
        db_failf(backend, "%s size mul overflow: %zu * %zu", field_name, lhs,
                 rhs);
    }
    return out;
}

static inline size_t db_checked_add_size(const char *backend,
                                         const char *field_name, size_t lhs,
                                         size_t rhs) {
    size_t out = 0U;
    if (db_try_add_size(lhs, rhs, &out) == 0) {
        db_failf(backend, "%s size add overflow: %zu + %zu", field_name, lhs,
                 rhs);
    }
    return out;
}

#endif
