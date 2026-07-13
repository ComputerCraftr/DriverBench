#include "gl_display_runtime.h"
#include "core/db_log.h"

#include <stdint.h>

#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "../driverbench_config.h"
#include "../renderers/gl_common.h"
#include "../renderers/gl_proc_runtime.h"
#include "display_types.h"

static void
db_display_validate_gles_1x_runtime_or_fail(const char *backend,
                                            const char *runtime_version);
static void db_display_validate_gl3_runtime_or_fail(const char *backend,
                                                    const char *runtime_version,
                                                    int runtime_is_gles);
static void db_display_validate_gl_runtime_for_renderer_or_fail(
    db_gl_renderer_t renderer, const char *backend, const char *runtime_version,
    int runtime_is_gles);
static void db_display_log_context_gles_fallback_mismatch(const char *backend,
                                                          int context_is_gles,
                                                          int runtime_is_gles);
static void db_display_log_runtime_api(const char *backend,
                                       const char *api_name,
                                       const char *version_label,
                                       const char *version_value,
                                       const char *renderer_label,
                                       const char *renderer_value);
static void
db_display_log_gl_runtime_info(const char *backend,
                               const db_display_gl_runtime_info_t *runtime);

db_display_gl_context_policy_t
db_display_gl_context_policy_for_renderer(db_gl_renderer_t renderer) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        return (db_display_gl_context_policy_t){
            .allow_gles1_1_fallback = 1,
            .requested_gl_major = 2,
            .requested_gl_minor = 1,
        };
    }
    return (db_display_gl_context_policy_t){
        .allow_gles1_1_fallback = 0,
        .requested_gl_major = 3,
        .requested_gl_minor = 3,
    };
}

static void
db_display_validate_gles_1x_runtime_or_fail(const char *backend,
                                            const char *runtime_version) {
    int es_major = 0;
    int es_minor = 0;
    if (!db_parse_gl_version_numbers(runtime_version, &es_major, &es_minor)) {
        DB_RUNTIME_FAIL(backend, DB_DISPLAY_GLES_RUNTIME_PARSE_ERROR_FMT,
                        (runtime_version != NULL) ? runtime_version : "(null)");
    }
    if (es_major != 1) {
        DB_RUNTIME_FAIL(backend, DB_DISPLAY_GLES_RUNTIME_UNSUPPORTED_FMT,
                        es_major, es_minor);
    }
}

static void db_display_validate_gl3_runtime_or_fail(const char *backend,
                                                    const char *runtime_version,
                                                    int runtime_is_gles) {
    if (runtime_is_gles != 0) {
        DB_RUNTIME_FAIL(backend,
                        "OpenGL ES runtime is unsupported for renderer gl3_3; "
                        "requires desktop OpenGL 3.3+");
    }
    if (!db_gl_version_text_at_least(runtime_version, 3, 3)) {
        DB_RUNTIME_FAIL(backend,
                        "desktop OpenGL runtime %s is unsupported for renderer "
                        "gl3_3; requires OpenGL 3.3+",
                        (runtime_version != NULL) ? runtime_version : "(null)");
    }
}

static void db_display_validate_gl_runtime_for_renderer_or_fail(
    db_gl_renderer_t renderer, const char *backend, const char *runtime_version,
    int runtime_is_gles) {
    if (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) {
        if (runtime_is_gles != 0) {
            db_display_validate_gles_1x_runtime_or_fail(backend,
                                                        runtime_version);
        }
        return;
    }
    db_display_validate_gl3_runtime_or_fail(backend, runtime_version,
                                            runtime_is_gles);
}

static void db_display_log_context_gles_fallback_mismatch(const char *backend,
                                                          int context_is_gles,
                                                          int runtime_is_gles) {
    if ((context_is_gles != 0) && (runtime_is_gles == 0)) {
        DB_RUNTIME_STATUS(backend, "context creation reported GLES fallback, "
                                   "but runtime API is OpenGL");
    }
}

db_display_gl_runtime_info_t
db_display_prepare_gl_runtime_info(db_gl_proc_resolver_fn_t resolver,
                                   const char *backend) {
    if (resolver == NULL) {
        DB_RUNTIME_FAIL((backend != NULL) ? backend
                                          : "display_gl_runtime_common",
                        "GL proc resolver is NULL");
    }

    db_gl_set_proc_resolver(resolver);
    db_gl_load_upload_proc_table();

    const char *version = db_gl_get_version_string();
    const db_display_gl_runtime_info_t runtime = {
        .version = version,
        .renderer = db_gl_get_renderer_string(),
        .is_gles = db_gl_is_es_context(version),
    };
    return runtime;
}

db_display_gl_runtime_info_t db_display_require_gl_runtime_for_renderer(
    db_gl_proc_resolver_fn_t resolver, db_gl_renderer_t renderer,
    const char *backend, int context_is_gles) {
    const db_display_gl_runtime_info_t runtime =
        db_display_prepare_gl_runtime_info(resolver, backend);
    db_display_log_gl_runtime_info(backend, &runtime);
    db_display_validate_gl_runtime_for_renderer_or_fail(
        renderer, backend, runtime.version, runtime.is_gles);
    if (context_is_gles >= 0) {
        db_display_log_context_gles_fallback_mismatch(backend, context_is_gles,
                                                      runtime.is_gles);
    }
    return runtime;
}

int db_display_should_probe_default_framebuffer_preserve(
    db_gl_renderer_t renderer, int true_offscreen_backend) {
#ifdef __linux__
    return (true_offscreen_backend == 0) &&
           (renderer == DB_GL_RENDERER_GL1_5_GLES1_1);
#else
    (void)renderer;
    (void)true_offscreen_backend;
    return 0;
#endif
}

int db_display_gl_uses_default_framebuffer_history(db_gl_renderer_t renderer) {
    return DB_BOOL(renderer == DB_GL_RENDERER_GL1_5_GLES1_1);
}

int db_display_should_force_hidden_glfw_offscreen_full_draw(
    db_gl_renderer_t renderer, const db_cli_config_t *cfg) {
#ifdef __linux__
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) &&
           ((cfg == NULL) || (cfg->backbuffer_draw_mode_explicit == 0));
#else
    (void)renderer;
    (void)cfg;
    return 0;
#endif
}

db_display_default_framebuffer_preserve_info_t
db_display_default_framebuffer_preserve_info_make(
    int has_probe, int preserve_supported,
    uint32_t preserved_framebuffer_count) {
    return (db_display_default_framebuffer_preserve_info_t){
        .has_probe = DB_BOOL(has_probe),
        .preserve_supported = DB_BOOL(preserve_supported),
        .preserved_framebuffer_count = preserved_framebuffer_count,
    };
}

uint32_t db_display_default_framebuffer_preserved_count(
    uint32_t default_preserved_framebuffer_count,
    const db_display_default_framebuffer_preserve_info_t *preserve_info) {
    if ((default_preserved_framebuffer_count == 0U) ||
        (preserve_info == NULL) || (preserve_info->has_probe == 0) ||
        (preserve_info->preserved_framebuffer_count == 0U)) {
        return 0U;
    }
    return DB_MIN(default_preserved_framebuffer_count,
                  preserve_info->preserved_framebuffer_count);
}

const char *
db_display_gl_policy_reason_text(db_display_gl_policy_reason_t reason_code) {
    switch (reason_code) {
    case DB_DISPLAY_GL_POLICY_REASON_NONE:
        return NULL;
    case DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSUPPORTED:
        return "forcing full backbuffer draw: default framebuffer preserve is "
               "unavailable";
    case DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSTABLE:
        return "forcing full backbuffer draw: default framebuffer preserve is "
               "unstable";
    default:
        return NULL;
    }
}

void db_display_resolve_opengl_display_policy(
    db_gl_renderer_t renderer, const db_cli_config_t *cfg,
    int true_offscreen_backend, uint32_t default_preserved_framebuffer_count,
    uint32_t max_preserved_framebuffer_count,
    const db_display_default_framebuffer_preserve_info_t *default_fb_preserve,
    db_display_gl_policy_resolution_t *out) {
    if (out == NULL) {
        return;
    }
    *out = (db_display_gl_policy_resolution_t){
        .effective_cfg = (cfg != NULL) ? *cfg : (db_cli_config_t){0},
        .preserved_framebuffer_count = default_preserved_framebuffer_count,
        .policy_reason_code = DB_DISPLAY_GL_POLICY_REASON_NONE,
        .policy_reason_text = NULL,
    };

    if ((true_offscreen_backend != 0) ||
        (renderer != DB_GL_RENDERER_GL1_5_GLES1_1) ||
        (default_fb_preserve == NULL) ||
        (default_fb_preserve->has_probe == 0)) {
        return;
    }

    out->preserved_framebuffer_count =
        db_display_default_framebuffer_preserved_count(
            (max_preserved_framebuffer_count != 0U)
                ? max_preserved_framebuffer_count
                : default_preserved_framebuffer_count,
            default_fb_preserve);
    if ((out->effective_cfg.backbuffer_draw_full == 0) &&
        (out->effective_cfg.backbuffer_draw_mode_explicit == 0) &&
        (out->preserved_framebuffer_count < 2U)) {
        out->effective_cfg.backbuffer_draw_full = 1;
        out->preserved_framebuffer_count = 0U;
        out->policy_reason_code =
            (default_fb_preserve->preserve_supported != 0)
                ? DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSTABLE
                : DB_DISPLAY_GL_POLICY_REASON_DEFAULT_FB_PRESERVE_UNSUPPORTED;
        out->policy_reason_text =
            db_display_gl_policy_reason_text(out->policy_reason_code);
    }
}

static void db_display_log_runtime_api(const char *backend,
                                       const char *api_name,
                                       const char *version_label,
                                       const char *version_value,
                                       const char *renderer_label,
                                       const char *renderer_value) {
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("api", (api_name != NULL) ? api_name : "unknown"),
        DB_LOG_TOKEN("version_label",
                     (version_label != NULL) ? version_label : "version"),
        DB_LOG_STRING("version",
                      (version_value != NULL) ? version_value : "unknown"),
        DB_LOG_TOKEN("renderer_label",
                     (renderer_label != NULL) ? renderer_label : "renderer"),
        DB_LOG_STRING("renderer",
                      (renderer_value != NULL) ? renderer_value : "unknown"),
    };
    db_log_info(backend, "runtime_api", fields, DB_LOG_FIELD_COUNT(fields));
}

static void
db_display_log_gl_runtime_info(const char *backend,
                               const db_display_gl_runtime_info_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    db_display_log_runtime_api(
        backend, (runtime->is_gles != 0) ? "OpenGL ES" : "OpenGL", "GL_VERSION",
        runtime->version, "GL_RENDERER", runtime->renderer);
}

void db_display_log_vulkan_runtime_api(const char *backend,
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

void db_display_gl_debug_clear_default_framebuffer_if_enabled(
    int debug_clear_enabled) {
    if (debug_clear_enabled == 0) {
        return;
    }
    db_gl_clear_color_rgba(db_double_to_f32(BENCH_CLEAR_COLOR_R),
                           db_double_to_f32(BENCH_CLEAR_COLOR_G),
                           db_double_to_f32(BENCH_CLEAR_COLOR_B),
                           db_double_to_f32(BENCH_CLEAR_COLOR_A));
    db_gl_clear_color_buffer();
}
