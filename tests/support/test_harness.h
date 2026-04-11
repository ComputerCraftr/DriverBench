#ifndef DRIVERBENCH_TEST_HARNESS_H
#define DRIVERBENCH_TEST_HARNESS_H

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/db_core.h"

typedef struct {
    unsigned failures;
} db_test_state_t;

enum { DB_TEST_FAIL_PREFIX_SIZE = 128 };

static inline __attribute__((format(printf, 4, 5))) void
db_test_failf_impl(db_test_state_t *state, const char *file, int line,
                   const char *fmt, ...) {
    va_list args;
    char prefix[DB_TEST_FAIL_PREFIX_SIZE];
    state->failures++;
    (void)db_snprintf(prefix, sizeof(prefix), "%s:%d: ", file, line);
    fputs(prefix, stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

#define DB_TEST_FAILF(state, fmt, ...)                                         \
    db_test_failf_impl((state), __FILE__, __LINE__,                            \
                       (fmt)__VA_OPT__(, ) __VA_ARGS__)

#define DB_TEST_EXPECT_TRUE(state, expr)                                       \
    do {                                                                       \
        if (!(expr)) {                                                         \
            DB_TEST_FAILF((state), "expected true: %s", #expr);                \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_EQ_U32(state, a, b)                                     \
    do {                                                                       \
        const unsigned _a = (unsigned)(a);                                     \
        const unsigned _b = (unsigned)(b);                                     \
        if (_a != _b) {                                                        \
            DB_TEST_FAILF((state), "expected %s == %s (%u != %u)", #a, #b, _a, \
                          _b);                                                 \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_EQ_U64(state, a, b)                                     \
    do {                                                                       \
        const uint64_t _a = (uint64_t)(a);                                     \
        const uint64_t _b = (uint64_t)(b);                                     \
        if (_a != _b) {                                                        \
            DB_TEST_FAILF((state),                                             \
                          "expected %s == %s (0x%016llx != 0x%016llx)", #a,    \
                          #b, (unsigned long long)_a, (unsigned long long)_b); \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_EQ_INT(state, a, b)                                     \
    do {                                                                       \
        const int _a = (int)(a);                                               \
        const int _b = (int)(b);                                               \
        if (_a != _b) {                                                        \
            DB_TEST_FAILF((state), "expected %s == %s (%d != %d)", #a, #b, _a, \
                          _b);                                                 \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_EQ_SIZE(state, a, b)                                    \
    do {                                                                       \
        const size_t _a = (size_t)(a);                                         \
        const size_t _b = (size_t)(b);                                         \
        if (_a != _b) {                                                        \
            DB_TEST_FAILF((state), "expected %s == %s (%zu != %zu)", #a, #b,   \
                          _a, _b);                                             \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_DOUBLE_EQUAL(state, a, b)                               \
    do {                                                                       \
        const double _a = (double)(a);                                         \
        const double _b = (double)(b);                                         \
        if (fabs(_a - _b) > 0.000001) {                                        \
            DB_TEST_FAILF((state), "expected %s == %s (%f != %f)", #a, #b, _a, \
                          _b);                                                 \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_STR_EQ(state, a, b)                                     \
    do {                                                                       \
        const char *_a = (a);                                                  \
        const char *_b = (b);                                                  \
        if (((_a) == NULL) || ((_b) == NULL) || (strcmp(_a, _b) != 0)) {       \
            DB_TEST_FAILF((state), "expected strings equal: '%s' vs '%s'",     \
                          (_a != NULL) ? _a : "(null)",                        \
                          (_b != NULL) ? _b : "(null)");                       \
        }                                                                      \
    } while (0)

#define DB_TEST_EXPECT_STR_CONTAINS(state, haystack, needle)                   \
    do {                                                                       \
        const char *_haystack = (haystack);                                    \
        const char *_needle = (needle);                                        \
        if (((_haystack) == NULL) || ((_needle) == NULL) ||                    \
            (strstr(_haystack, _needle) == NULL)) {                            \
            DB_TEST_FAILF((state), "expected string '%s' to contain '%s'",     \
                          (_haystack != NULL) ? _haystack : "(null)",          \
                          (_needle != NULL) ? _needle : "(null)");             \
        }                                                                      \
    } while (0)

typedef void (*db_test_fn_t)(db_test_state_t *state);

typedef struct {
    const char *name;
    db_test_fn_t fn;
} db_test_case_t;

unsigned db_benchmark_seeding_test_run_all(void);
unsigned db_benchmark_emitters_test_run_all(void);
unsigned db_cli_test_run_all(void);
unsigned db_core_logging_test_run_all(void);
unsigned db_damage_trace_test_run_all(void);
unsigned db_display_gl_runtime_test_run_all(void);
unsigned db_gl_shadow_present_test_run_all(void);
unsigned db_hash_test_run_all(void);
unsigned db_numeric_test_run_all(void);
unsigned db_poll_policy_test_run_all(void);
unsigned db_sort_test_run_all(void);
unsigned db_vk_scheduler_test_run_all(void);

static inline unsigned db_test_run_cases(const db_test_case_t *cases,
                                         size_t case_count) {
    db_test_state_t state = {0U};
    for (size_t i = 0U; i < case_count; i++) {
        cases[i].fn(&state);
    }
    return state.failures;
}

#endif
