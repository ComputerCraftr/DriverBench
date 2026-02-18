#ifndef DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H

#include "../core/db_core.h"
#include "../renderers/renderer_gl_common.h"

#define DB_DISPLAY_GLES_RUNTIME_PARSE_ERROR_FMT                                \
    "Failed to parse GLES runtime version string '%s'"
#define DB_DISPLAY_GLES_RUNTIME_UNSUPPORTED_FMT                                \
    "OpenGL ES %d.%d is unsupported for this renderer; requires OpenGL ES "    \
    "1.x fixed-function"

static inline void
db_display_validate_gles_1x_runtime_or_fail(const char *backend,
                                            const char *runtime_version) {
    int es_major = 0;
    int es_minor = 0;
    if (!db_parse_gl_version_numbers(runtime_version, &es_major, &es_minor)) {
        db_failf(backend, DB_DISPLAY_GLES_RUNTIME_PARSE_ERROR_FMT,
                 (runtime_version != NULL) ? runtime_version : "(null)");
    }
    if (es_major != 1) {
        db_failf(backend, DB_DISPLAY_GLES_RUNTIME_UNSUPPORTED_FMT, es_major,
                 es_minor);
    }
}

#endif
