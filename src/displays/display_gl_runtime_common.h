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

typedef struct {
    int allow_gles1_1_fallback;
    int requested_gl_major;
    int requested_gl_minor;
} db_display_gl_context_policy_t;

typedef struct {
    const char *version;
    const char *renderer;
    int is_gles;
} db_display_gl_runtime_info_t;

db_display_gl_context_policy_t
db_display_gl_context_policy_for_renderer(db_gl_renderer_t renderer);

db_display_gl_runtime_info_t
db_display_prepare_gl_runtime_info(db_gl_proc_resolver_fn_t resolver,
                                   const char *backend);
db_display_gl_runtime_info_t db_display_require_gl_runtime_for_renderer(
    db_gl_proc_resolver_fn_t resolver, db_gl_renderer_t renderer,
    const char *backend, int context_is_gles);
void db_display_log_vulkan_runtime_api(const char *backend,
                                       uint32_t runtime_api_version,
                                       const char *runtime_renderer);
void db_display_gl_debug_clear_default_framebuffer_if_enabled(
    int debug_clear_enabled);

#endif
