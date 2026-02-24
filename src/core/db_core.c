#include "db_core.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/signal.h>
#endif

#define DB_MAX_TEXT_FILE_BYTES (16U * 1024U * 1024U)
#define DB_RUNTIME_OPTION_CAPACITY 32U
#define DB_MAX_SLEEP_NS_D 100000000.0
#define DISPLAY_LOCALHOST_PREFIX "localhost:"
#define DISPLAY_LOOPBACK_PREFIX "127.0.0.1:"

static volatile sig_atomic_t db_stop_requested = 0;
static struct {
    const char *key;
    const char *value;
} db_runtime_options[DB_RUNTIME_OPTION_CAPACITY] = {0};

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
    // Fallback for platforms without Annex K bounds-checked APIs.
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,clang-analyzer-valist.Uninitialized)
    return vsnprintf(buffer, buffer_size, fmt, ap);
#endif
}

int db_snprintf(char *buffer, size_t buffer_size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int result = db_vsnprintf(buffer, buffer_size, fmt, ap);
    va_end(ap);
    return result;
}

int db_value_is_truthy(const char *value) {
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

double db_runtime_resolve_fps_cap(const char *backend, double default_fps_cap) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_FPS_CAP);
    if (value == NULL) {
        return default_fps_cap;
    }

    double parsed = 0.0;
    if (db_parse_fps_cap_text(value, &parsed) != 0) {
        return parsed;
    }

    db_infof(backend, "Invalid %s='%s'; using default fps cap %.2f",
             DB_RUNTIME_OPT_FPS_CAP, value, default_fps_cap);
    return default_fps_cap;
}

int db_parse_u32_positive_value(const char *backend, const char *field_name,
                                const char *value, uint32_t *out_value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }

    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    if ((end == value) || (end == NULL) || (*end != '\0') || (parsed == 0UL) ||
        (parsed > UINT32_MAX)) {
        db_failf(backend, "Invalid %s='%s'", field_name, value);
    }
    *out_value = (uint32_t)parsed;
    return 1;
}

int db_parse_u32_nonnegative_value(const char *backend, const char *field_name,
                                   const char *value, uint32_t *out_value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }

    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    if ((end == value) || (end == NULL) || (*end != '\0') ||
        (parsed > UINT32_MAX)) {
        db_failf(backend, "Invalid %s='%s'", field_name, value);
    }
    *out_value = (uint32_t)parsed;
    return 1;
}

const char *db_runtime_option_get(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < DB_RUNTIME_OPTION_CAPACITY; i++) {
        if ((db_runtime_options[i].key != NULL) &&
            (strcmp(db_runtime_options[i].key, name) == 0)) {
            return db_runtime_options[i].value;
        }
    }
    return NULL;
}

void db_runtime_option_set(const char *name, const char *value) {
    if ((name == NULL) || (value == NULL)) {
        return;
    }
    for (size_t i = 0U; i < DB_RUNTIME_OPTION_CAPACITY; i++) {
        if (db_runtime_options[i].key == NULL) {
            db_runtime_options[i].key = name;
            db_runtime_options[i].value = value;
            return;
        }
        if (strcmp(db_runtime_options[i].key, name) == 0) {
            db_runtime_options[i].value = value;
            return;
        }
    }
    db_failf("db_core", "Runtime option capacity exceeded");
}

int db_has_ssh_env(void) {
    return (getenv("SSH_CONNECTION") != NULL) ||
           (getenv("SSH_CLIENT") != NULL) || (getenv("SSH_TTY") != NULL);
}

int db_is_forwarded_x11_display(void) {
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
    struct sigaction sa = {0};
    sa.sa_handler = db_signal_handler;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
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

    const double frame_budget_ns_d = DB_NS_PER_SECOND_D / fps_cap;
    double remaining_ns_d =
        frame_budget_ns_d - (double)(db_now_ns_monotonic() - frame_start_ns);
    while (remaining_ns_d > 0.0) {
        const double sleep_ns_d = (remaining_ns_d > DB_MAX_SLEEP_NS_D)
                                      ? DB_MAX_SLEEP_NS_D
                                      : remaining_ns_d;
        const long sleep_ns =
            db_checked_double_to_long(backend, "sleep_ns", sleep_ns_d);
        if (sleep_ns <= 0L) {
            break;
        }

        struct timespec request = {0};
        request.tv_nsec = sleep_ns;
        // NOLINTNEXTLINE(misc-include-cleaner)
        if ((nanosleep(&request, NULL) != 0) && (errno != EINTR)) {
            break;
        }
        remaining_ns_d = frame_budget_ns_d -
                         (double)(db_now_ns_monotonic() - frame_start_ns);
    }
}

uint8_t *db_read_file_or_fail(const char *backend, const char *path,
                              size_t *out_sz) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        db_failf(backend, "Failed to open shader file: %s", path);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        db_failf(backend, "Failed to seek shader file: %s", path);
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        db_failf(backend, "Failed to stat shader file: %s", path);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        db_failf(backend, "Failed to rewind shader file: %s", path);
    }

    uint8_t *buffer = (uint8_t *)malloc((size_t)file_size);
    if (buffer == NULL) {
        fclose(file);
        db_failf(backend, "Failed to allocate %ld bytes for %s", file_size,
                 path);
    }

    size_t read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(buffer);
        db_failf(backend, "Failed to read shader file: %s", path);
    }

    *out_sz = (size_t)file_size;
    return buffer;
}

char *db_read_text_file_or_fail(const char *backend, const char *path) {
    size_t file_size = 0;
    uint8_t *buffer_u8 = db_read_file_or_fail(backend, path, &file_size);
    if ((file_size == SIZE_MAX) ||
        (file_size > (size_t)DB_MAX_TEXT_FILE_BYTES)) {
        free(buffer_u8);
        db_failf(backend, "Text file too large: %s (%zu bytes)", path,
                 file_size);
    }
    char *text = (char *)calloc(file_size + 1U, 1U);
    if (text == NULL) {
        free(buffer_u8);
        db_failf(backend, "Failed to allocate text buffer for %s", path);
    }
    for (size_t i = 0; i < file_size; i++) {
        text[i] = (char)buffer_u8[i];
    }
    free(buffer_u8);
    return text;
}

static void db_benchmark_log(const char *api_name, const char *renderer_name,
                             const char *backend_name, uint64_t frames,
                             uint32_t work_units, double elapsed_ms,
                             const char *tag, const char *capability_mode) {
    if (frames == 0U) {
        return;
    }

    double ms_per_frame = elapsed_ms / (double)frames;
    double fps = DB_MS_PER_SECOND_D / ms_per_frame;
    const char *mode = (capability_mode != NULL) ? capability_mode : "default";
    if (strcmp(tag, "progress") == 0) {
        printf("%s benchmark (%s): mode=%s frames=%llu total_ms=%.2f "
               "ms_per_frame=%.3f fps=%.2f\n",
               api_name, tag, mode, (unsigned long long)frames, elapsed_ms,
               ms_per_frame, fps);
        return;
    }
    printf("%s benchmark (%s): renderer=%s backend=%s mode=%s frames=%llu "
           "work_units=%u total_ms=%.2f ms_per_frame=%.3f fps=%.2f\n",
           api_name, tag, renderer_name, backend_name, mode,
           (unsigned long long)frames, work_units, elapsed_ms, ms_per_frame,
           fps);
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
