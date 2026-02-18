#ifndef DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H

#include <stdint.h>

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

static inline void db_display_log_runtime_api(const char *backend,
                                              const char *api_name,
                                              const char *version_label,
                                              const char *version_value,
                                              const char *renderer_label,
                                              const char *renderer_value) {
    db_infof(backend, "runtime API: %s, %s: %s, %s: %s",
             (api_name != NULL) ? api_name : "(null)",
             (version_label != NULL) ? version_label : "version",
             (version_value != NULL) ? version_value : "(null)",
             (renderer_label != NULL) ? renderer_label : "renderer",
             (renderer_value != NULL) ? renderer_value : "(null)");
}

static inline int db_display_log_gl_runtime_api(const char *backend,
                                                const char *runtime_version,
                                                const char *runtime_renderer) {
    const int runtime_is_gles = db_gl_is_es_context(runtime_version);
    db_display_log_runtime_api(
        backend, (runtime_is_gles != 0) ? "OpenGL ES" : "OpenGL", "GL_VERSION",
        runtime_version, "GL_RENDERER", runtime_renderer);
    return runtime_is_gles;
}

static inline void
db_display_log_vulkan_runtime_api(const char *backend,
                                  uint32_t runtime_api_version,
                                  const char *runtime_renderer) {
    char runtime_api_version_text[32];
    const unsigned int major = runtime_api_version >> 22U;
    const unsigned int minor = (runtime_api_version >> 12U) & 0x3FFU;
    const unsigned int patch = runtime_api_version & 0xFFFU;
    (void)db_snprintf(runtime_api_version_text,
                      sizeof(runtime_api_version_text), "%u.%u.%u", major,
                      minor, patch);
    db_display_log_runtime_api(backend, "Vulkan", "VK_API_VERSION",
                               runtime_api_version_text, "VK_RENDERER",
                               runtime_renderer);
}

#endif
