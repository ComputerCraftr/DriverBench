#include "db_core.h"
#include "db_log.h"
#include "db_numeric.h"
#include "db_progress_policy.h"
#include "db_trace.h"

#include "../config/runtime_options.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DB_MAX_SLEEP_NS 100000000.0
#define DISPLAY_LOCALHOST_PREFIX "localhost:"
#define DISPLAY_LOOPBACK_PREFIX "127.0.0.1:"

static volatile sig_atomic_t db_stop_requested = 0;

enum { DB_LEGACY_LOG_MESSAGE_CAPACITY = 4096U };

int db_memory_ranges_overlap(const void *lhs, size_t lhs_size, const void *rhs,
                             size_t rhs_size, int *overlap) {
    if ((overlap == NULL) || ((lhs == NULL) && (lhs_size > 0U)) ||
        ((rhs == NULL) && (rhs_size > 0U))) {
        return 0;
    }
    *overlap = 0;
    if ((lhs_size == 0U) || (rhs_size == 0U)) {
        return 1;
    }
    const uintptr_t lhs_start = (uintptr_t)lhs;
    const uintptr_t rhs_start = (uintptr_t)rhs;
    if ((lhs_size > (UINTPTR_MAX - lhs_start)) ||
        (rhs_size > (UINTPTR_MAX - rhs_start))) {
        return 0;
    }
    *overlap = DB_BOOL((lhs_start < (rhs_start + rhs_size)) &&
                       (rhs_start < (lhs_start + lhs_size)));
    return 1;
}

int db_copy_strided_rows_tight(void *destination, size_t destination_size,
                               const void *source, size_t source_size,
                               size_t row_count, size_t source_row_stride,
                               size_t row_bytes) {
    size_t source_required = 0U;
    size_t destination_required = 0U;
    if ((destination == NULL) || (source == NULL) || (row_count == 0U) ||
        (row_bytes == 0U) ||
        (db_try_strided_size(row_count, source_row_stride, row_bytes,
                             &source_required) == 0) ||
        (db_try_mul_size(row_count, row_bytes, &destination_required) == 0) ||
        (source_required > source_size) ||
        (destination_required > destination_size)) {
        return 0;
    }
    const uintptr_t source_address = (uintptr_t)source;
    const uintptr_t destination_address = (uintptr_t)destination;
    int ranges_overlap = 0;
    if (db_memory_ranges_overlap(source, source_required, destination,
                                 destination_required, &ranges_overlap) == 0) {
        return 0;
    }
    if ((ranges_overlap != 0) && (destination_address > source_address) &&
        (source_row_stride != row_bytes)) {
        /* Packing padded rows forward can overwrite a later source row. */
        return 0;
    }
    const int copy_bottom_to_top = DB_BOOL(
        (ranges_overlap != 0) && (source_address < destination_address));
    for (size_t row_offset = 0U; row_offset < row_count; row_offset++) {
        const size_t row = (copy_bottom_to_top != 0)
                               ? row_count - 1U - row_offset
                               : row_offset;
        memmove((uint8_t *)destination + (row * row_bytes),
                (const uint8_t *)source + (row * source_row_stride), row_bytes);
    }
    return 1;
}

static int db_ascii_ieq_char(char lhs, char rhs) {
    if ((lhs >= 'A') && (lhs <= 'Z')) {
        lhs = (char)(lhs - 'A' + 'a');
    }
    if ((rhs >= 'A') && (rhs <= 'Z')) {
        rhs = (char)(rhs - 'A' + 'a');
    }
    return lhs == rhs;
}

static int db_ascii_ieq(const char *lhs, const char *rhs) {
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }
    while ((*lhs != '\0') && (*rhs != '\0')) {
        if (!db_ascii_ieq_char(*lhs, *rhs)) {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return (*lhs == '\0') && (*rhs == '\0');
}

uint32_t db_fold_u64_to_u32(uint64_t value) {
    return (uint32_t)(value ^ (value >> 32U));
}

uint32_t db_mix_u32(uint32_t value) {
    value ^= value >> DB_U32_MIX_SHIFT_A;
    value *= DB_U32_MIX_MUL_A;
    value ^= value >> DB_U32_MIX_SHIFT_B;
    value *= DB_U32_MIX_MUL_B;
    value ^= value >> DB_U32_MIX_SHIFT_A;
    return value;
}

static void db_signal_handler(int signum) {
    (void)signum;
    db_stop_requested = 1;
}

void db_failf(const char *backend, const char *fmt, ...) {
    char message[DB_LEGACY_LOG_MESSAGE_CAPACITY] = {0};
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    const db_log_field_t fields[] = {DB_LOG_STRING("message", message)};
    db_log_fail(backend, "fatal_error", fields, DB_LOG_FIELD_COUNT(fields));
}

void db_errorf(const char *backend, const char *fmt, ...) {
    char message[DB_LEGACY_LOG_MESSAGE_CAPACITY] = {0};
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    const db_log_field_t fields[] = {DB_LOG_STRING("message", message)};
    db_log_error(backend, "error_message", fields, DB_LOG_FIELD_COUNT(fields));
}

void db_infof(const char *backend, const char *fmt, ...) {
    char message[DB_LEGACY_LOG_MESSAGE_CAPACITY] = {0};
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    const db_log_field_t fields[] = {DB_LOG_STRING("message", message)};
    db_log_info(backend, "info_message", fields, DB_LOG_FIELD_COUNT(fields));
}

int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt,
                 va_list ap) {
#ifdef __STDC_LIB_EXT1__
    return vsnprintf_s(buffer, buffer_size, _TRUNCATE, fmt, ap);
#else
    const int written = vsnprintf(buffer, buffer_size, fmt, ap);
    return written;
#endif
}

int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int result = db_vsnprintf(buffer, buffer_size, fmt, ap);
    va_end(ap);
    return result;
}

static int db_value_is_truthy(const char *value) {
    int parsed = 0;
    if (db_parse_bool_text(value, &parsed) != 0) {
        return parsed;
    }
    return 0;
}

int db_parse_bool_text(const char *value, int *out_value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }
    if ((strcmp(value, "1") == 0) || db_ascii_ieq(value, "true") ||
        db_ascii_ieq(value, "yes") || db_ascii_ieq(value, "on")) {
        if (out_value != NULL) {
            *out_value = 1;
        }
        return 1;
    }
    if ((strcmp(value, "0") == 0) || db_ascii_ieq(value, "false") ||
        db_ascii_ieq(value, "no") || db_ascii_ieq(value, "off")) {
        if (out_value != NULL) {
            *out_value = 0;
        }
        return 1;
    }
    return 0;
}

int db_parse_u32_prefix(const char *value, int base, uint32_t *out_value,
                        const char **out_end) {
    if ((value == NULL) || (value[0] == '\0') || (out_value == NULL) ||
        (out_end == NULL) || (value[0] == '-') ||
        ((base != DB_PARSE_BASE_AUTODETECT) &&
         ((base < DB_PARSE_BASE_MIN) || (base > DB_PARSE_BASE_MAX)))) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, base);
    if ((end == value) || (end == NULL) || (errno == ERANGE) ||
        (parsed > UINT32_MAX)) {
        return 0;
    }
    *out_value = (uint32_t)parsed;
    *out_end = end;
    return 1;
}

int db_parse_u64_prefix(const char *value, int base, uint64_t *out_value,
                        const char **out_end) {
    if ((value == NULL) || (value[0] == '\0') || (out_value == NULL) ||
        (out_end == NULL) || (value[0] == '-') ||
        ((base != DB_PARSE_BASE_AUTODETECT) &&
         ((base < DB_PARSE_BASE_MIN) || (base > DB_PARSE_BASE_MAX)))) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    const uintmax_t parsed = strtoumax(value, &end, base);
    if ((end == value) || (end == NULL) || (errno == ERANGE) ||
        (parsed > UINT64_MAX)) {
        return 0;
    }
    *out_value = (uint64_t)parsed;
    *out_end = end;
    return 1;
}

int db_parse_long_prefix(const char *value, int base, long *out_value,
                         const char **out_end) {
    if ((value == NULL) || (value[0] == '\0') || (out_value == NULL) ||
        (out_end == NULL) ||
        ((base != DB_PARSE_BASE_AUTODETECT) &&
         ((base < DB_PARSE_BASE_MIN) || (base > DB_PARSE_BASE_MAX)))) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    const long parsed = strtol(value, &end, base);
    if ((end == value) || (end == NULL) || (errno == ERANGE)) {
        return 0;
    }
    *out_value = parsed;
    *out_end = end;
    return 1;
}

int db_parse_double_prefix(const char *value, double *out_value,
                           const char **out_end) {
    if ((value == NULL) || (value[0] == '\0') || (out_value == NULL) ||
        (out_end == NULL)) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    const double parsed = strtod(value, &end);
    if ((end == value) || (end == NULL) || (errno == ERANGE) ||
        !isfinite(parsed)) {
        return 0;
    }
    *out_value = parsed;
    *out_end = end;
    return 1;
}

int db_parse_int_text(const char *value, int *out_value) {
    long parsed = 0L;
    const char *end = NULL;
    if ((db_parse_long_prefix(value, DB_PARSE_BASE_DECIMAL, &parsed, &end) ==
         0) ||
        (*end != '\0') || (parsed < INT_MIN) || (parsed > INT_MAX)) {
        return 0;
    }
    if (out_value != NULL) {
        *out_value = (int)parsed;
    }
    return 1;
}

int db_parse_fps_cap_text(const char *value, double *out_value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }

    int parsed_bool = 0;
    if ((db_parse_bool_text(value, &parsed_bool) != 0) && (parsed_bool == 0)) {
        if (out_value != NULL) {
            *out_value = 0.0;
        }
        return 1;
    }

    if (db_ascii_ieq(value, "uncapped") || db_ascii_ieq(value, "none")) {
        if (out_value != NULL) {
            *out_value = 0.0;
        }
        return 1;
    }

    const char *end = NULL;
    double parsed = 0.0;
    if ((db_parse_double_prefix(value, &parsed, &end) != 0) && (*end == '\0') &&
        (parsed > 0.0)) {
        if (out_value != NULL) {
            *out_value = parsed;
        }
        return 1;
    }

    return 0;
}

static int db_has_ssh_env(void) {
    return (getenv("SSH_CONNECTION") != NULL) ||
           (getenv("SSH_CLIENT") != NULL) || (getenv("SSH_TTY") != NULL);
}

static int db_is_forwarded_x11_display(void) {
    const char *display = getenv("DISPLAY");
    if ((display == NULL) || !db_has_ssh_env()) {
        return 0;
    }
    return (strncmp(display, DISPLAY_LOCALHOST_PREFIX,
                    strlen(DISPLAY_LOCALHOST_PREFIX)) == 0) ||
           (strncmp(display, DISPLAY_LOOPBACK_PREFIX,
                    strlen(DISPLAY_LOOPBACK_PREFIX)) == 0);
}

int db_runtime_is_linux_x11(void) {
#ifdef __linux__
    const char *session_type = getenv("XDG_SESSION_TYPE");
    if (session_type != NULL) {
        return DB_BOOL(strcmp(session_type, "x11") == 0);
    }
    // Fallback: if DISPLAY is set but WAYLAND_DISPLAY is not, assume X11.
    return (getenv("DISPLAY") != NULL) && (getenv("WAYLAND_DISPLAY") == NULL);
#else
    return 0;
#endif
}

void db_validate_runtime_environment(const char *backend,
                                     const char *remote_override_option) {
    const char *display = getenv("DISPLAY");
    if (db_is_forwarded_x11_display() &&
        !db_value_is_truthy(db_runtime_option_get(remote_override_option))) {
        const char *display_text = (display != NULL) ? display : "(null)";
        db_failf(backend,
                 "Refusing forwarded X11 session (DISPLAY=%s). This benchmark "
                 "expects local display/GPU access. Set "
                 "--allow-remote-display 1 to override.",
                 display_text);
    }
}

void db_install_signal_handlers(void) {
    struct sigaction sa = {0};
    sa.sa_handler = db_signal_handler;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);
}

int db_should_stop(void) { return DB_BOOL(db_stop_requested); }

uint64_t db_now_ns_monotonic(void) {
    struct timespec ts = {0};
    // CLOCK_MONOTONIC is provided by <time.h>; include-cleaner reports the
    // libc internal provider instead of the public feature-test-gated header.
    // NOLINTNEXTLINE(misc-include-cleaner)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * DB_NS_PER_SECOND_U64) + (uint64_t)ts.tv_nsec;
}

static void db_sleep_until_deadline(const db_deadline_t *deadline) {
    uint64_t remaining_ns =
        db_deadline_remaining_ns(deadline, db_now_ns_monotonic());
    while (remaining_ns > 0U) {
        const double sleep_ns =
            db_min_f64(DB_TO_F64(remaining_ns), DB_MAX_SLEEP_NS);
        const long sleep_ns_long =
            db_checked_double_to_long("core", "sleep_ns", sleep_ns);
        if (sleep_ns_long <= 0L) {
            break;
        }

        const struct timespec request = {.tv_nsec = sleep_ns_long};
        struct timespec unslept = {0};
        // Recompute from the absolute deadline after EINTR so repeated signals
        // cannot extend the pacing interval.
        // NOLINTNEXTLINE(misc-include-cleaner)
        if ((nanosleep(&request, &unslept) != 0) && (errno != EINTR)) {
            break;
        }
        remaining_ns =
            db_deadline_remaining_ns(deadline, db_now_ns_monotonic());
    }
}

void db_sleep_until_ns(uint64_t deadline_ns) {
    db_sleep_until_deadline(&(const db_deadline_t){
        .expires_ns = deadline_ns,
    });
}

void db_sleep_to_fps_cap(uint64_t frame_start_ns, double fps_cap) {
    if (fps_cap <= 0.0) {
        return;
    }

    const double frame_budget_ns = DB_NS_PER_SECOND / fps_cap;
    const uint64_t budget_ns =
        db_checked_double_to_u64("core", "frame_budget_ns", frame_budget_ns);
    const db_deadline_t deadline = db_deadline_after(frame_start_ns, budget_ns);
    db_sleep_until_deadline(&deadline);
}

int db_format_benchmark_log(char *buffer, size_t buffer_size,
                            const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *tag) {
    if (frames == 0U) {
        return 0;
    }

    const double ms_per_frame = elapsed_ms / DB_TO_F64(frames);
    const double fps = DB_MS_PER_SECOND / ms_per_frame;
    enum {
        DB_BENCHMARK_LOG_BASE_FIELD_COUNT = 6,
        DB_BENCHMARK_LOG_FIELD_CAPACITY = 16,
    };
    db_log_field_t fields[DB_BENCHMARK_LOG_FIELD_CAPACITY] = {
        DB_LOG_TOKEN("api", api_name),
        DB_LOG_U64("frames", frames),
        DB_LOG_U64("work_units", work_units),
        DB_LOG_DOUBLE("total_ms", elapsed_ms),
        DB_LOG_DOUBLE("ms_per_frame", ms_per_frame),
        DB_LOG_DOUBLE("fps", fps),
    };
    size_t field_count = DB_BENCHMARK_LOG_BASE_FIELD_COUNT;
    const char *event = "benchmark_progress";
    if (strcmp(tag, "progress") == 0) {
        return db_log_format_line(
            buffer, buffer_size, DB_LOG_LEVEL_INFO,
            &(const db_log_event_t){"benchmark", event, fields, field_count});
    }
    event = "benchmark_final";
    fields[field_count++] = DB_LOG_TOKEN("renderer", renderer_name);
    fields[field_count++] = DB_LOG_TOKEN("backend", backend_name);
    const db_run_log_identity_t identity = db_run_log_identity_current();
    if (identity.benchmark_mode != NULL) {
        fields[field_count++] =
            DB_LOG_TOKEN("benchmark_mode", identity.benchmark_mode);
        fields[field_count++] = DB_LOG_TOKEN("presenter", identity.presenter);
        fields[field_count++] =
            DB_LOG_TOKEN("execution_strategy", identity.execution_strategy);
        fields[field_count++] =
            DB_LOG_TOKEN("working_format", identity.working_format);
        fields[field_count++] =
            DB_LOG_TOKEN("native_format", identity.native_format);
        fields[field_count++] =
            DB_LOG_TOKEN("present_method", identity.present_method);
    }
    return db_log_format_line(
        buffer, buffer_size, DB_LOG_LEVEL_INFO,
        &(const db_log_event_t){"benchmark", event, fields, field_count});
}

static void db_benchmark_log(const char *api_name, const char *renderer_name,
                             const char *backend_name, uint64_t frames,
                             uint32_t work_units, double elapsed_ms,
                             const char *tag) {
    enum { DB_BENCHMARK_LOG_TEXT_SIZE = 1024 };
    char text[DB_BENCHMARK_LOG_TEXT_SIZE];
    if (db_format_benchmark_log(text, sizeof(text), api_name, renderer_name,
                                backend_name, frames, work_units, elapsed_ms,
                                tag) <= 0) {
        return;
    }
    fputs(text, stdout);
}

void db_log_progress_periodic(const char *api_name, const char *renderer_name,
                              const char *backend_name, uint64_t frames,
                              uint32_t work_units, double elapsed_ms,
                              double *next_log_due_ms, double interval_ms) {
    if ((next_log_due_ms == NULL) || (interval_ms <= 0.0)) {
        return;
    }

    if (*next_log_due_ms <= 0.0) {
        *next_log_due_ms = interval_ms;
    }
    if (elapsed_ms < *next_log_due_ms) {
        return;
    }

    db_benchmark_log(api_name, renderer_name, backend_name, frames, work_units,
                     elapsed_ms, "progress");
    do {
        *next_log_due_ms += interval_ms;
    } while (elapsed_ms >= *next_log_due_ms);
}

void db_benchmark_log_final(const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms) {
    db_benchmark_log(api_name, renderer_name, backend_name, frames, work_units,
                     elapsed_ms, "final");
}
