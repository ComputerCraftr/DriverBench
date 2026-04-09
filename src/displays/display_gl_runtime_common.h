#ifndef DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H
#define DRIVERBENCH_DISPLAY_GL_RUNTIME_COMMON_H

#include <stdint.h>

#include "../driverbench_config.h"
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

typedef struct {
    int has_probe;
    int preserve_supported;
    int preserve_stable;
    int first_reuse_distance;
} db_display_default_framebuffer_preserve_info_t;

typedef enum {
    DB_DISPLAY_GL_POLICY_REASON_NONE = 0,
    DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSUPPORTED = 1,
    DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSTABLE = 2,
} db_display_gl_policy_reason_t;

typedef struct {
    db_cli_config_t effective_cfg;
    uint32_t preserved_framebuffer_count;
    db_display_gl_policy_reason_t policy_reason_code;
    const char *policy_reason_text;
} db_display_gl_policy_resolution_t;

db_display_gl_context_policy_t
db_display_gl_context_policy_for_renderer(db_gl_renderer_t renderer);

// Init log ownership:
// - display/runtime helpers own window-system/runtime API logs
// - renderer runtime helpers own benchmark/capability/scheduler/present logs
db_display_gl_runtime_info_t
db_display_prepare_gl_runtime_info(db_gl_proc_resolver_fn_t resolver,
                                   const char *backend);
db_display_gl_runtime_info_t db_display_require_gl_runtime_for_renderer(
    db_gl_proc_resolver_fn_t resolver, db_gl_renderer_t renderer,
    const char *backend, int context_is_gles);
int db_display_should_probe_default_framebuffer_preserve(
    db_gl_renderer_t renderer, int true_offscreen_backend);
db_display_default_framebuffer_preserve_info_t
db_display_default_framebuffer_preserve_info_make(int has_probe,
                                                  int preserve_supported,
                                                  int preserve_stable,
                                                  int first_reuse_distance);
const char *
db_display_gl_policy_reason_text(db_display_gl_policy_reason_t reason_code);
void db_display_resolve_opengl_display_policy(
    db_gl_renderer_t renderer, const db_cli_config_t *cfg,
    int true_offscreen_backend, uint32_t default_preserved_framebuffer_count,
    const db_display_default_framebuffer_preserve_info_t *default_fb_preserve,
    db_display_gl_policy_resolution_t *out);
void db_display_log_vulkan_runtime_api(const char *backend,
                                       uint32_t runtime_api_version,
                                       const char *runtime_renderer);
void db_display_gl_debug_clear_default_framebuffer_if_enabled(
    int debug_clear_enabled);

#endif
