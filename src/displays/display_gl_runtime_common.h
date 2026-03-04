#ifndef DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H

#include <stdint.h>

#include "../renderers/renderer_gl_common.h"
#include "display_types.h"

#define DB_DISPLAY_GLES_RUNTIME_PARSE_ERROR_FMT                                \
    "Failed to parse GLES runtime version string '%s'"
#define DB_DISPLAY_GLES_RUNTIME_UNSUPPORTED_FMT                                \
    "OpenGL ES %d.%d is unsupported for this renderer; requires OpenGL ES "    \
    "1.x fixed-function"

typedef enum {
    DB_DISPLAY_GL_RUNTIME_LOG_DISABLED = 0,
    DB_DISPLAY_GL_RUNTIME_LOG_ENABLED = 1,
} db_display_gl_runtime_log_mode_t;

typedef struct {
    int allow_gles1_1_fallback;
    int requested_gl_major;
    int requested_gl_minor;
} db_display_gl_context_policy_t;

db_display_gl_context_policy_t
db_display_gl_context_policy_for_renderer(db_gl_renderer_t renderer);

void db_display_validate_gles_1x_runtime_or_fail(const char *backend,
                                                 const char *runtime_version);
void db_display_validate_gl3_runtime_or_fail(const char *backend,
                                             const char *runtime_version,
                                             int runtime_is_gles);
void db_display_validate_gl_runtime_for_renderer_or_fail(
    db_gl_renderer_t renderer, const char *backend, const char *runtime_version,
    int runtime_is_gles);
void db_display_log_context_gles_fallback_mismatch(const char *backend,
                                                   int context_is_gles,
                                                   int runtime_is_gles);
int db_display_prepare_gl_runtime(db_gl_proc_resolver_fn_t resolver,
                                  const char *backend,
                                  db_display_gl_runtime_log_mode_t log_mode,
                                  const char **out_runtime_version,
                                  const char **out_runtime_renderer);
int db_display_prepare_and_validate_gl_runtime(
    db_gl_proc_resolver_fn_t resolver, db_gl_renderer_t renderer,
    const char *backend, db_display_gl_runtime_log_mode_t log_mode,
    int context_is_gles, const char **out_runtime_version,
    const char **out_runtime_renderer);
void db_display_log_runtime_api(const char *backend, const char *api_name,
                                const char *version_label,
                                const char *version_value,
                                const char *renderer_label,
                                const char *renderer_value);
int db_display_log_gl_runtime_api(const char *backend,
                                  const char *runtime_version,
                                  const char *runtime_renderer);
void db_display_log_vulkan_runtime_api(const char *backend,
                                       uint32_t runtime_api_version,
                                       const char *runtime_renderer);
void db_display_gl_debug_clear_default_framebuffer_if_enabled(
    int debug_clear_enabled);

#endif
