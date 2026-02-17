#include "db_core.h"

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_LOCALHOST_PREFIX "localhost:"
#define DISPLAY_LOOPBACK_PREFIX "127.0.0.1:"
#define DB_MAX_TEXT_FILE_BYTES (16U * 1024U * 1024U)
#define DB_MS_PER_SEC_D 1000.0

static volatile sig_atomic_t db_stop_requested = 0;

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
    (void)vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

int db_env_is_truthy(const char *name) {
    const char *value = getenv(name);
    if (value == NULL) {
        return 0;
    }
    return (strcmp(value, "1") == 0) || (strcmp(value, "true") == 0) ||
           (strcmp(value, "TRUE") == 0) || (strcmp(value, "yes") == 0) ||
           (strcmp(value, "YES") == 0);
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
                                     const char *remote_override_env) {
    const char *display = getenv("DISPLAY");
    if (db_is_forwarded_x11_display() &&
        !db_env_is_truthy(remote_override_env)) {
        const char *display_text = (display != NULL) ? display : "(null)";
        db_failf(backend,
                 "Refusing forwarded X11 session (DISPLAY=%s). This benchmark "
                 "expects local display/GPU access. Set %s=1 to override.",
                 display_text, remote_override_env);
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
    double fps = DB_MS_PER_SEC_D / ms_per_frame;
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
