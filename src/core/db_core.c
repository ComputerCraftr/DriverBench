#include "db_core.h"

#include "../config/runtime_options.h"

#include <errno.h>
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
    va_list ap;
    fputs("[", stderr);
    fputs(backend, stderr);
    fputs("][error] ", stderr);
    va_start(ap, fmt);
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void db_infof(const char *backend, const char *fmt, ...) {
    va_list ap;
    fputs("[", stdout);
    fputs(backend, stdout);
    fputs("][info] ", stdout);
    va_start(ap, fmt);
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    (void)vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

int db_vsnprintf(char *buffer, size_t buffer_size, const char *fmt,
                 va_list ap) {
#ifdef __STDC_LIB_EXT1__
    return vsnprintf_s(buffer, buffer_size, _TRUNCATE, fmt, ap);
#else
    // Fallback for platforms without Annex K bounds-checked APIs. Use a local
    // copy so the formatting boundary is explicit to the analyzer and to
    // preserve the caller's va_list state.
    va_list ap_copy;
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    va_copy(ap_copy, ap);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    const int written = vsnprintf(buffer, buffer_size, fmt, ap_copy);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    va_end(ap_copy);
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

    char *end = NULL;
    const double parsed = strtod(value, &end);
    if ((end != value) && (end != NULL) && (*end == '\0') && (parsed > 0.0)) {
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
    // NOLINTNEXTLINE(misc-include-cleaner)
    struct sigaction sa = {0};
    // NOLINTNEXTLINE(misc-include-cleaner)
    sa.sa_handler = db_signal_handler;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    // NOLINTNEXTLINE(misc-include-cleaner)
    (void)sigaction(SIGHUP, &sa, NULL);
}

int db_should_stop(void) { return db_stop_requested != 0; }

uint64_t db_now_ns_monotonic(void) {
    struct timespec ts = {0};
    // NOLINTNEXTLINE(misc-include-cleaner)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * DB_NS_PER_SECOND_U64) + (uint64_t)ts.tv_nsec;
}

void db_sleep_to_fps_cap(const char *backend, uint64_t frame_start_ns,
                         double fps_cap) {
    if (fps_cap <= 0.0) {
        return;
    }

    const double frame_budget_ns = DB_NS_PER_SECOND / fps_cap;
    double remaining_ns =
        frame_budget_ns - (double)(db_now_ns_monotonic() - frame_start_ns);
    while (remaining_ns > 0.0) {
        const double sleep_ns =
            (remaining_ns > DB_MAX_SLEEP_NS) ? DB_MAX_SLEEP_NS : remaining_ns;
        const long sleep_ns_long =
            db_checked_double_to_long(backend, "sleep_ns", sleep_ns);
        if (sleep_ns_long <= 0L) {
            break;
        }

        struct timespec request = {0};
        request.tv_nsec = sleep_ns_long;
        // NOLINTNEXTLINE(misc-include-cleaner)
        if ((nanosleep(&request, NULL) != 0) && (errno != EINTR)) {
            break;
        }
        remaining_ns =
            frame_budget_ns - (double)(db_now_ns_monotonic() - frame_start_ns);
    }
}

int db_format_benchmark_log(char *buffer, size_t buffer_size,
                            const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *tag, const char *capability_mode) {
    if (frames == 0U) {
        return 0;
    }

    const double ms_per_frame = elapsed_ms / (double)frames;
    const double fps = DB_MS_PER_SECOND / ms_per_frame;
    const char *mode = (capability_mode != NULL) ? capability_mode : "default";
    if (strcmp(tag, "progress") == 0) {
        return db_snprintf(buffer, buffer_size,
                           "%s benchmark (%s): frames=%llu total_ms=%.2f "
                           "ms_per_frame=%.3f fps=%.2f\n",
                           api_name, tag, (unsigned long long)frames,
                           elapsed_ms, ms_per_frame, fps);
    }
    return db_snprintf(buffer, buffer_size,
                       "%s benchmark (%s): renderer=%s backend=%s mode=%s "
                       "frames=%llu work_units=%u total_ms=%.2f "
                       "ms_per_frame=%.3f fps=%.2f\n",
                       api_name, tag, renderer_name, backend_name, mode,
                       (unsigned long long)frames, work_units, elapsed_ms,
                       ms_per_frame, fps);
}

static void db_benchmark_log(const char *api_name, const char *renderer_name,
                             const char *backend_name, uint64_t frames,
                             uint32_t work_units, double elapsed_ms,
                             const char *tag, const char *capability_mode) {
    enum { DB_BENCHMARK_LOG_TEXT_SIZE = 256 };
    char text[DB_BENCHMARK_LOG_TEXT_SIZE];
    if (db_format_benchmark_log(text, sizeof(text), api_name, renderer_name,
                                backend_name, frames, work_units, elapsed_ms,
                                tag, capability_mode) <= 0) {
        return;
    }
    fputs(text, stdout);
}

void db_benchmark_log_periodic(const char *api_name, const char *renderer_name,
                               const char *backend_name, uint64_t frames,
                               uint32_t work_units, double elapsed_ms,
                               const char *capability_mode,
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
                     elapsed_ms, "progress", capability_mode);
    do {
        *next_log_due_ms += interval_ms;
    } while (elapsed_ms >= *next_log_due_ms);
}

void db_benchmark_log_final(const char *api_name, const char *renderer_name,
                            const char *backend_name, uint64_t frames,
                            uint32_t work_units, double elapsed_ms,
                            const char *capability_mode) {
    db_benchmark_log(api_name, renderer_name, backend_name, frames, work_units,
                     elapsed_ms, "final", capability_mode);
}
