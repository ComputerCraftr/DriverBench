#include "db_core.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_LOCALHOST_PREFIX "localhost:"
#define DISPLAY_LOOPBACK_PREFIX "127.0.0.1:"
#define DB_MAX_TEXT_FILE_BYTES (16U * 1024U * 1024U)

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
    (void)memcpy(text, buffer_u8, file_size);
    free(buffer_u8);
    return text;
}
