#include "display_gl_runtime_common.h"

#include <stdint.h>

#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../renderers/renderer_gl_common.h"
#include "display_types.h"

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

void db_display_validate_gles_1x_runtime_or_fail(const char *backend,
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

void db_display_validate_gl3_runtime_or_fail(const char *backend,
                                             const char *runtime_version,
                                             int runtime_is_gles) {
    if (runtime_is_gles != 0) {
        db_failf(backend,
                 "OpenGL ES runtime is unsupported for renderer gl3_3; "
                 "requires desktop OpenGL 3.3+");
    }
    if (!db_gl_version_text_at_least(runtime_version, 3, 3)) {
        db_failf(backend,
                 "desktop OpenGL runtime %s is unsupported for renderer "
                 "gl3_3; requires OpenGL 3.3+",
                 (runtime_version != NULL) ? runtime_version : "(null)");
    }
}

void db_display_validate_gl_runtime_for_renderer_or_fail(
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

void db_display_log_context_gles_fallback_mismatch(const char *backend,
                                                   int context_is_gles,
                                                   int runtime_is_gles) {
    if ((context_is_gles != 0) && (runtime_is_gles == 0)) {
        db_infof(backend, "context creation reported GLES fallback, "
                          "but runtime API is OpenGL");
    }
}

int db_display_prepare_gl_runtime(db_gl_proc_resolver_fn_t resolver,
                                  const char *backend,
                                  db_display_gl_runtime_log_mode_t log_mode,
                                  const char **out_runtime_version,
                                  const char **out_runtime_renderer) {
    if (resolver == NULL) {
        db_failf((backend != NULL) ? backend : "display_gl_runtime_common",
                 "GL proc resolver is NULL");
    }

    db_gl_set_proc_resolver(resolver);
    db_gl_preload_upload_proc_table();

    const char *runtime_version = db_gl_get_version_string();
    const char *runtime_renderer = db_gl_get_renderer_string();
    if (out_runtime_version != NULL) {
        *out_runtime_version = runtime_version;
    }
    if (out_runtime_renderer != NULL) {
        *out_runtime_renderer = runtime_renderer;
    }

    if (log_mode == DB_DISPLAY_GL_RUNTIME_LOG_ENABLED) {
        return db_display_log_gl_runtime_api(backend, runtime_version,
                                             runtime_renderer);
    }
    return db_gl_is_es_context(runtime_version);
}

int db_display_prepare_and_validate_gl_runtime(
    db_gl_proc_resolver_fn_t resolver, db_gl_renderer_t renderer,
    const char *backend, db_display_gl_runtime_log_mode_t log_mode,
    int context_is_gles, const char **out_runtime_version,
    const char **out_runtime_renderer) {
    const char *runtime_version = NULL;
    const char *runtime_renderer = NULL;
    const int runtime_is_gles = db_display_prepare_gl_runtime(
        resolver, backend, log_mode, &runtime_version, &runtime_renderer);
    if (out_runtime_version != NULL) {
        *out_runtime_version = runtime_version;
    }
    if (out_runtime_renderer != NULL) {
        *out_runtime_renderer = runtime_renderer;
    }
    db_display_validate_gl_runtime_for_renderer_or_fail(
        renderer, backend, runtime_version, runtime_is_gles);
    if (context_is_gles >= 0) {
        db_display_log_context_gles_fallback_mismatch(backend, context_is_gles,
                                                      runtime_is_gles);
    }
    return runtime_is_gles;
}

void db_display_log_runtime_api(const char *backend, const char *api_name,
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

int db_display_log_gl_runtime_api(const char *backend,
                                  const char *runtime_version,
                                  const char *runtime_renderer) {
    const int runtime_is_gles = db_gl_is_es_context(runtime_version);
    db_display_log_runtime_api(
        backend, (runtime_is_gles != 0) ? "OpenGL ES" : "OpenGL", "GL_VERSION",
        runtime_version, "GL_RENDERER", runtime_renderer);
    return runtime_is_gles;
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
