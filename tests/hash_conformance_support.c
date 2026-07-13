#include "core/db_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void db_failf(const char *backend, const char *fmt, ...) {
    (void)fprintf(stderr, "[%s] ", backend);
    va_list args;
    va_start(args, fmt);
    (void)vfprintf(stderr, fmt, args);
    va_end(args);
    (void)fputc('\n', stderr);
    exit(EXIT_FAILURE);
}
