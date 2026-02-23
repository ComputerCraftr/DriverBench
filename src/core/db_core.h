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

#define DB_HASH_MIX_SHIFT_A 16U
#define DB_HASH_MIX_SHIFT_B 15U
#define DB_HASH_MIX_MUL_A 0x7FEB352DU
#define DB_HASH_MIX_MUL_B 0x846CA68BU
#define DB_FNV1A64_OFFSET UINT64_C(1469598103934665603)
#define DB_FNV1A64_PRIME UINT64_C(1099511628211)
#define DB_MS_PER_SECOND_D 1000.0
#define DB_NS_PER_MS_D 1000000.0
#define DB_NS_PER_SECOND_D 1000000000.0
#define DB_NS_PER_SECOND_U64 UINT64_C(1000000000)
#define DB_RUNTIME_OPT_ALLOW_REMOTE_DISPLAY "DRIVERBENCH_ALLOW_REMOTE_DISPLAY"
#define DB_RUNTIME_OPT_FPS_CAP "DRIVERBENCH_FPS_CAP"
#define DB_RUNTIME_OPT_FRAMEBUFFER_HASH "DRIVERBENCH_FRAMEBUFFER_HASH"
#define DB_RUNTIME_OPT_FRAME_LIMIT "DRIVERBENCH_FRAME_LIMIT"
#define DB_RUNTIME_OPT_HASH_EVERY_FRAME "DRIVERBENCH_HASH_EVERY_FRAME"
#define DB_RUNTIME_OPT_OFFSCREEN "DRIVERBENCH_OFFSCREEN"
#define DB_RUNTIME_OPT_OFFSCREEN_FRAMES "DRIVERBENCH_OFFSCREEN_FRAMES"
#define DB_RUNTIME_OPT_VSYNC "DRIVERBENCH_VSYNC"

void db_failf(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
void db_infof(const char *backend, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt, va_list ap);
int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

int db_value_is_truthy(const char *value);
int db_parse_bool_text(const char *value, int *out_value);
int db_parse_fps_cap_text(const char *value, double *out_value);
double db_runtime_resolve_fps_cap(const char *backend, double default_fps_cap);
int db_parse_u32_nonnegative_value(const char *backend, const char *field_name,
                                   const char *value, uint32_t *out_value);
int db_parse_u32_positive_value(const char *backend, const char *field_name,
                                const char *value, uint32_t *out_value);
const char *db_runtime_option_get(const char *name);
void db_runtime_option_set(const char *name, const char *value);
int db_has_ssh_env(void);
int db_is_forwarded_x11_display(void);
void db_validate_runtime_environment(const char *backend,
                                     const char *remote_override_env);
void db_install_signal_handlers(void);
int db_should_stop(void);
uint64_t db_now_ns_monotonic(void);
void db_sleep_to_fps_cap(const char *backend, uint64_t frame_start_ns,
                         double fps_cap);

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

static inline uint32_t db_mix_u32(uint32_t value) {
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    value *= DB_HASH_MIX_MUL_A;
    value ^= value >> DB_HASH_MIX_SHIFT_B;
    value *= DB_HASH_MIX_MUL_B;
    value ^= value >> DB_HASH_MIX_SHIFT_A;
    return value;
}

static inline uint32_t db_u32_range(uint32_t seed, uint32_t min_value,
                                    uint32_t max_value) {
    if (max_value <= min_value) {
        return min_value;
    }
    return min_value + (seed % (max_value - min_value + 1U));
}

static inline void db_blend_rgb(float prior_r, float prior_g, float prior_b,
                                float target_r, float target_g, float target_b,
                                float blend_factor, float *out_r, float *out_g,
                                float *out_b) {
    if (blend_factor <= 0.0F) {
        *out_r = prior_r;
        *out_g = prior_g;
        *out_b = prior_b;
        return;
    }
    if (blend_factor >= 1.0F) {
        *out_r = target_r;
        *out_g = target_g;
        *out_b = target_b;
        return;
    }
    *out_r = prior_r + ((target_r - prior_r) * blend_factor);
    *out_g = prior_g + ((target_g - prior_g) * blend_factor);
    *out_b = prior_b + ((target_b - prior_b) * blend_factor);
}

static inline uint64_t db_fnv1a64_extend(uint64_t hash, const void *data,
                                         size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0U; i < size; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= DB_FNV1A64_PRIME;
    }
    return hash;
}

static inline uint64_t db_fnv1a64_bytes(const void *data, size_t size) {
    return db_fnv1a64_extend(DB_FNV1A64_OFFSET, data, size);
}

static inline uint64_t db_fnv1a64_mix_u64(uint64_t hash, uint64_t value) {
    return db_fnv1a64_extend(hash, &value, sizeof(value));
}

static inline uint32_t db_u32_min(uint32_t lhs, uint32_t rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

static inline uint32_t db_u32_max(uint32_t lhs, uint32_t rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline uint32_t db_u32_saturating_sub(uint32_t lhs, uint32_t rhs) {
    return (lhs > rhs) ? (lhs - rhs) : 0U;
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
