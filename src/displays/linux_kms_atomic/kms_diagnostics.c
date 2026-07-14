#include "kms_internal.h"

#include "core/db_core.h"
#include "core/db_log.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

__attribute__((noreturn)) void runtime_failf(const char *fmt, ...) {
    char message[LOG_MSG_CAPACITY];
    va_list arguments;
    va_start(arguments, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, arguments);
    va_end(arguments);
    DB_RUNTIME_FAIL(g_active_backend, "%s", message);
}

void runtime_errno_fail(const char *message) {
    runtime_failf("%s: %s", message, strerror(errno));
}
