#include "kms_internal.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../gl_display_runtime.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "renderers/gl_common.h"

enum {
    DB_KMS_EGL_SDR_CHANNEL_BITS = 8,
    DB_KMS_EGL_HDR_CHANNEL_BITS = 10,
};

static const double db_kms_egl_metadata_scale = 50000.0;

typedef struct {
    EGLint attribute;
    double value;
} db_kms_egl_metadata_value_t;

static const char *db_kms_atomic_egl_error_name(EGLint error_code) {
    switch (error_code) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
#ifdef EGL_TIMEOUT_EXPIRED_KHR
    case EGL_TIMEOUT_EXPIRED_KHR:
        return "EGL_TIMEOUT_EXPIRED_KHR";
#endif
    default:
        return "EGL_UNKNOWN_ERROR";
    }
}

static void kms_atomic_log_egl_failure(const char *backend, const char *stage) {
    const EGLint error_code = eglGetError();
    DB_RUNTIME_STATUS(backend, "KMS EGL: %s failed (%s / 0x%04x)", stage,
                      db_kms_atomic_egl_error_name(error_code),
                      (unsigned int)error_code);
}

static int db_kms_extension_list_has(const char *extensions,
                                     const char *extension) {
    if ((extensions == NULL) || (extension == NULL)) {
        return 0;
    }
    const size_t length = strlen(extension);
    const char *cursor = extensions;
    while ((cursor = strstr(cursor, extension)) != NULL) {
        if (((cursor == extensions) || (cursor[-1] == ' ')) &&
            ((cursor[length] == '\0') || (cursor[length] == ' '))) {
            return 1;
        }
        cursor += length;
    }
    return 0;
}

static int db_kms_egl_apply_hdr10_metadata(EGLDisplay display,
                                           EGLSurface surface) {
    const char *const extensions = eglQueryString(display, EGL_EXTENSIONS);
    if ((extensions == NULL) ||
        (db_kms_extension_list_has(
             extensions, "EGL_EXT_surface_SMPTE2086_metadata") == 0) ||
        (db_kms_extension_list_has(extensions,
                                   "EGL_EXT_surface_CTA861_3_metadata") == 0)) {
        return 0;
    }
    static const db_kms_egl_metadata_value_t values[] = {
        {EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT, 0.708},
        {EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT, 0.292},
        {EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT, 0.170},
        {EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT, 0.797},
        {EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT, 0.131},
        {EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT, 0.046},
        {EGL_SMPTE2086_WHITE_POINT_X_EXT, 0.3127},
        {EGL_SMPTE2086_WHITE_POINT_Y_EXT, 0.3290},
        {EGL_SMPTE2086_MAX_LUMINANCE_EXT, DB_HDR10_MASTERING_MAX_NITS},
        {EGL_SMPTE2086_MIN_LUMINANCE_EXT, DB_HDR10_MASTERING_MIN_NITS},
        {EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT, DB_HDR10_MAX_CLL_NITS},
        {EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT, DB_HDR10_MAX_FALL_NITS},
    };
    for (size_t index = 0U; index < DB_LOG_FIELD_COUNT(values); index++) {
        const EGLint scaled =
            (EGLint)(values[index].value * db_kms_egl_metadata_scale);
        if (eglSurfaceAttrib(display, surface, values[index].attribute,
                             scaled) != EGL_TRUE) {
            return 0;
        }
    }
    return 1;
}

void db_kms_egl_presentation_init(const char *backend, EGLDisplay dpy,
                                  EGLSurface surf,
                                  db_kms_egl_presentation_t *presentation) {
    if ((backend == NULL) || (dpy == EGL_NO_DISPLAY) ||
        (surf == EGL_NO_SURFACE) || (presentation == NULL)) {
        return;
    }
    *presentation = (db_kms_egl_presentation_t){0};
    const char *const extensions = eglQueryString(dpy, EGL_EXTENSIONS);
    presentation->buffer_age_supported =
        db_kms_extension_list_has(extensions, "EGL_EXT_buffer_age");
    const int has_khr_damage = db_kms_extension_list_has(
        extensions, "EGL_KHR_swap_buffers_with_damage");
    const int has_ext_damage = db_kms_extension_list_has(
        extensions, "EGL_EXT_swap_buffers_with_damage");
    union {
        __eglMustCastToProperFunctionPointerType generic;
        PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC typed;
    } swap_damage = {0};
    if (has_khr_damage != 0) {
        swap_damage.generic = eglGetProcAddress("eglSwapBuffersWithDamageKHR");
    } else if (has_ext_damage != 0) {
        swap_damage.generic = eglGetProcAddress("eglSwapBuffersWithDamageEXT");
    }
    presentation->swap_buffers_with_damage = swap_damage.typed;
    presentation->swap_damage_supported = DB_BOOL(swap_damage.typed != NULL);

    const int preserve_requested =
        DB_BOOL(eglSurfaceAttrib(dpy, surf, EGL_SWAP_BEHAVIOR,
                                 EGL_BUFFER_PRESERVED) == EGL_TRUE);
    EGLint swap_behavior = EGL_BUFFER_DESTROYED;
    const int preserve_verified =
        DB_BOOL((preserve_requested != 0) &&
                (eglQuerySurface(dpy, surf, EGL_SWAP_BEHAVIOR,
                                 &swap_behavior) == EGL_TRUE) &&
                (swap_behavior == EGL_BUFFER_PRESERVED));
    const db_log_field_t fields[] = {
        DB_LOG_BOOL("buffer_age_supported", presentation->buffer_age_supported),
        DB_LOG_BOOL("swap_with_damage_supported",
                    presentation->swap_damage_supported),
        DB_LOG_BOOL("preserve_requested", preserve_requested),
        DB_LOG_BOOL("preserve_verified", preserve_verified),
    };
    db_log_info(backend, "kms_egl_presentation_capability", fields,
                DB_LOG_FIELD_COUNT(fields));
}

static int db_kms_egl_select_native_config(
    const char *backend, EGLDisplay dpy, EGLint renderable_type,
    db_native_output_format_t native_output_format, EGLConfig *out_config) {
    if ((backend == NULL) || (out_config == NULL)) {
        return 0;
    }
    EGLint channel_bits = 0;
    switch (native_output_format) {
    case DB_NATIVE_OUTPUT_XRGB8888:
        channel_bits = DB_KMS_EGL_SDR_CHANNEL_BITS;
        break;
    case DB_NATIVE_OUTPUT_XRGB2101010:
        channel_bits = DB_KMS_EGL_HDR_CHANNEL_BITS;
        break;
    case DB_NATIVE_OUTPUT_RGBA16F:
        return 0;
    }
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
        renderable_type,  EGL_RED_SIZE,   channel_bits,
        EGL_GREEN_SIZE,   channel_bits,   EGL_BLUE_SIZE,
        channel_bits,     EGL_NONE,
    };
    enum { DB_KMS_EGL_CONFIG_CAPACITY = 128 };
    EGLConfig configs[DB_KMS_EGL_CONFIG_CAPACITY];
    EGLint count = 0;
    if (eglChooseConfig(dpy, attributes, configs, DB_KMS_EGL_CONFIG_CAPACITY,
                        &count) != EGL_TRUE) {
        return 0;
    }
    const uint32_t expected_visual =
        db_kms_atomic_gbm_format_or_fail(backend, native_output_format);
    const EGLint inspected = DB_MIN(count, DB_KMS_EGL_CONFIG_CAPACITY);
    for (EGLint index = 0; index < inspected; index++) {
        EGLint visual = 0;
        EGLint red = 0;
        EGLint green = 0;
        EGLint blue = 0;
        if ((eglGetConfigAttrib(dpy, configs[index], EGL_NATIVE_VISUAL_ID,
                                &visual) != EGL_TRUE) ||
            (eglGetConfigAttrib(dpy, configs[index], EGL_RED_SIZE, &red) !=
             EGL_TRUE) ||
            (eglGetConfigAttrib(dpy, configs[index], EGL_GREEN_SIZE, &green) !=
             EGL_TRUE) ||
            (eglGetConfigAttrib(dpy, configs[index], EGL_BLUE_SIZE, &blue) !=
             EGL_TRUE)) {
            continue;
        }
        if (((uint32_t)visual == expected_visual) && (red == channel_bits) &&
            (green == channel_bits) && (blue == channel_bits)) {
            *out_config = configs[index];
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("native_format", db_native_output_format_name(
                                                  native_output_format)),
                DB_LOG_HEX64("native_visual", (uint64_t)(uint32_t)visual),
                DB_LOG_I64("red_bits", red),
                DB_LOG_I64("green_bits", green),
                DB_LOG_I64("blue_bits", blue),
                DB_LOG_I64("candidate_count", count),
            };
            db_log_info(backend, "kms_egl_config_selected", fields,
                        DB_LOG_FIELD_COUNT(fields));
            return 1;
        }
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("native_format",
                     db_native_output_format_name(native_output_format)),
        DB_LOG_HEX64("expected_native_visual", expected_visual),
        DB_LOG_I64("required_channel_bits", channel_bits),
        DB_LOG_I64("candidate_count", count),
        DB_LOG_TOKEN("reason", "native_visual_or_channel_mismatch"),
    };
    db_log_error(backend, "kms_egl_config_rejected", fields,
                 DB_LOG_FIELD_COUNT(fields));
    return 0;
}

int db_kms_egl_hdr10_desktop_gl_supported(const char *backend,
                                          struct gbm_device *gbm,
                                          uint32_t width, uint32_t height,
                                          int req_gl_major, int req_gl_minor) {
    if ((backend == NULL) || (gbm == NULL) || (width == 0U) || (height == 0U)) {
        return 0;
    }
#ifndef EGL_GL_COLORSPACE_BT2020_PQ_EXT
    return 0;
#else
    const uint32_t gbm_format =
        db_kms_atomic_gbm_format_or_fail(backend, DB_NATIVE_OUTPUT_XRGB2101010);
    struct gbm_surface *const gbm_surface =
        gbm_surface_create(gbm, width, height, gbm_format,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surface == NULL) {
        return 0;
    }
    EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (display == EGL_NO_DISPLAY) {
        gbm_surface_destroy(gbm_surface);
        return 0;
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(display, &major, &minor) != EGL_TRUE) {
        gbm_surface_destroy(gbm_surface);
        return 0;
    }
    const char *const extensions = eglQueryString(display, EGL_EXTENSIONS);
    const int extensions_supported = DB_BOOL(
        (extensions != NULL) &&
        (strstr(extensions, "EGL_EXT_gl_colorspace_bt2020_pq") != NULL) &&
        (strstr(extensions, "EGL_EXT_surface_SMPTE2086_metadata") != NULL) &&
        (strstr(extensions, "EGL_EXT_surface_CTA861_3_metadata") != NULL));
    int supported = extensions_supported;
    int config_supported = 0;
    int context_supported = 0;
    int surface_supported = 0;
    int metadata_supported = 0;
    int version_supported = 0;
    int packed_texture_supported = 0;
    EGLConfig config = NULL;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    if ((supported != 0) &&
        (db_kms_egl_select_native_config(backend, display, EGL_OPENGL_BIT,
                                         DB_NATIVE_OUTPUT_XRGB2101010,
                                         &config) != 0) &&
        (eglBindAPI(EGL_OPENGL_API) == EGL_TRUE)) {
        config_supported = 1;
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
        context_supported = DB_BOOL(context != EGL_NO_CONTEXT);
    }
    if (context != EGL_NO_CONTEXT) {
        const EGLint attributes[] = {EGL_GL_COLORSPACE_KHR,
                                     EGL_GL_COLORSPACE_BT2020_PQ_EXT, EGL_NONE};
        surface = eglCreateWindowSurface(
            display, config, (EGLNativeWindowType)gbm_surface, attributes);
        supported = DB_BOOL(
            (surface != EGL_NO_SURFACE) &&
            (eglMakeCurrent(display, surface, surface, context) == EGL_TRUE));
        surface_supported = supported;
    } else {
        supported = 0;
    }
    if (supported != 0) {
        supported = db_kms_egl_apply_hdr10_metadata(display, surface);
        metadata_supported = supported;
    }
    if (supported != 0) {
        const db_display_gl_runtime_info_t runtime =
            db_display_prepare_gl_runtime_info(
                (db_gl_proc_resolver_fn_t)eglGetProcAddress, backend);
        supported = db_gl_version_text_at_least(runtime.version, req_gl_major,
                                                req_gl_minor);
        version_supported = supported;
    }
    if (supported != 0) {
        unsigned int probe_texture = 0U;
        packed_texture_supported = db_gl_texture_create_rgb10a2_bt2020_pq(
            &probe_texture, 1U, 1U, NULL);
        db_gl_texture_delete_if_valid(&probe_texture);
        supported = packed_texture_supported;
    }
    const char *reason = "none";
    if (extensions_supported == 0) {
        reason = "egl_hdr_extensions_unavailable";
    } else if (config_supported == 0) {
        reason = "egl_hdr_config_unavailable";
    } else if (context_supported == 0) {
        reason = "egl_desktop_gl_context_unavailable";
    } else if (surface_supported == 0) {
        reason = "egl_bt2020_pq_surface_unavailable";
    } else if (metadata_supported == 0) {
        reason = "egl_hdr_metadata_rejected";
    } else if (version_supported == 0) {
        reason = "egl_gl_version_unavailable";
    } else if (packed_texture_supported == 0) {
        reason = "gl_rgb10a2_texture_unavailable";
    }
    const db_log_field_t fields[] = {
        DB_LOG_BOOL("extensions_supported", extensions_supported),
        DB_LOG_BOOL("config_supported", config_supported),
        DB_LOG_BOOL("context_supported", context_supported),
        DB_LOG_BOOL("surface_supported", surface_supported),
        DB_LOG_BOOL("metadata_supported", metadata_supported),
        DB_LOG_BOOL("version_supported", version_supported),
        DB_LOG_BOOL("packed_texture_supported", packed_texture_supported),
        DB_LOG_TOKEN("reason", reason),
    };
    db_log_info(backend, "kms_egl_hdr10_probe", fields,
                DB_LOG_FIELD_COUNT(fields));
    (void)eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, surface);
    }
    if (context != EGL_NO_CONTEXT) {
        eglDestroyContext(display, context);
    }
    eglTerminate(display);
    gbm_surface_destroy(gbm_surface);
    return supported;
#endif
}

EGLDisplay egl_init_try_gl_then_optional_gles1_1(
    const char *backend, struct gbm_device *gbm, EGLConfig *out_cfg,
    EGLContext *out_ctx, EGLSurface *out_surf, struct gbm_surface *gbm_surf,
    int req_gl_major, int req_gl_minor, int allow_gles1_1_fallback,
    db_native_output_format_t native_output_format) {
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) {
        kms_atomic_log_egl_failure(backend, "eglGetDisplay");
        runtime_failf("eglGetDisplay failed");
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        kms_atomic_log_egl_failure(backend, "eglInitialize");
        runtime_failf("eglInitialize failed");
    }
    const int hdr_surface_requested =
        DB_BOOL(native_output_format == DB_NATIVE_OUTPUT_XRGB2101010);
    const char *const egl_extensions = eglQueryString(dpy, EGL_EXTENSIONS);
    const int hdr_colorspace_supported = DB_BOOL(
        (egl_extensions != NULL) &&
        (strstr(egl_extensions, "EGL_EXT_gl_colorspace_bt2020_pq") != NULL));
    if ((hdr_surface_requested != 0) && (hdr_colorspace_supported == 0)) {
        runtime_failf("KMS HDR10 requires EGL_EXT_gl_colorspace_bt2020_pq");
    }
#ifdef EGL_GL_COLORSPACE_BT2020_PQ_EXT
    const EGLint hdr_surface_attributes[] = {
        EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_BT2020_PQ_EXT, EGL_NONE};
#else
    const EGLint hdr_surface_attributes[] = {EGL_NONE};
#endif
    const EGLint *const surface_attributes =
        (hdr_surface_requested != 0) ? hdr_surface_attributes : NULL;

    if (eglBindAPI(EGL_OPENGL_API)) {
        DB_RUNTIME_STATUS(backend,
                          "KMS EGL: trying desktop OpenGL %d.%d window surface",
                          req_gl_major, req_gl_minor);
        EGLConfig cfg;
        if (db_kms_egl_select_native_config(backend, dpy, EGL_OPENGL_BIT,
                                            native_output_format, &cfg) != 0) {
            EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
            if (ctx != EGL_NO_CONTEXT) {
                EGLSurface surf = eglCreateWindowSurface(
                    dpy, cfg, (EGLNativeWindowType)gbm_surf,
                    surface_attributes);
                if ((surf != EGL_NO_SURFACE) &&
                    eglMakeCurrent(dpy, surf, surf, ctx)) {
                    if ((hdr_surface_requested != 0) &&
                        (db_kms_egl_apply_hdr10_metadata(dpy, surf) == 0)) {
                        runtime_failf(
                            "KMS HDR10 EGL metadata application failed");
                    }
                    const db_display_gl_runtime_info_t runtime =
                        db_display_prepare_gl_runtime_info(
                            (db_gl_proc_resolver_fn_t)eglGetProcAddress,
                            backend);
                    if (db_gl_version_text_at_least(
                            runtime.version, req_gl_major, req_gl_minor)) {
                        *out_cfg = cfg;
                        *out_ctx = ctx;
                        *out_surf = surf;
                        return dpy;
                    }
                    DB_RUNTIME_STATUS(
                        backend,
                        "KMS EGL: desktop GL runtime version did not meet "
                        "%d.%d requirement",
                        req_gl_major, req_gl_minor);
                } else if (surf == EGL_NO_SURFACE) {
                    kms_atomic_log_egl_failure(
                        backend, "desktop GL eglCreateWindowSurface");
                } else {
                    kms_atomic_log_egl_failure(backend,
                                               "desktop GL eglMakeCurrent");
                }
                if (surf != EGL_NO_SURFACE) {
                    eglDestroySurface(dpy, surf);
                }
                eglDestroyContext(dpy, ctx);
            } else {
                kms_atomic_log_egl_failure(backend,
                                           "desktop GL eglCreateContext");
            }
        } else {
            DB_RUNTIME_STATUS(
                backend, "KMS EGL: no matching desktop GL window config found");
        }
    } else {
        kms_atomic_log_egl_failure(backend, "eglBindAPI(EGL_OPENGL_API)");
    }

    if (allow_gles1_1_fallback == 0) {
        runtime_failf("Failed to create required desktop OpenGL context");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        kms_atomic_log_egl_failure(backend, "eglBindAPI(EGL_OPENGL_ES_API)");
        runtime_failf("eglBindAPI ES failed");
    }
    DB_RUNTIME_STATUS(backend,
                      "KMS EGL: trying OpenGL ES 1.1 fallback window surface");

    EGLConfig cfg;
    if (db_kms_egl_select_native_config(backend, dpy, EGL_OPENGL_ES_BIT,
                                        native_output_format, &cfg) == 0) {
        runtime_failf("no matching native EGL config for GLES");
    }

    const EGLint ctx_attribs_es1[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    EGLContext ctx =
        eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_es1);
    if (ctx == EGL_NO_CONTEXT) {
        kms_atomic_log_egl_failure(backend, "GLES eglCreateContext");
        runtime_failf("eglCreateContext ES1 failed");
    }

    EGLSurface surf = eglCreateWindowSurface(
        dpy, cfg, (EGLNativeWindowType)gbm_surf, surface_attributes);
    if (surf == EGL_NO_SURFACE) {
        kms_atomic_log_egl_failure(backend, "GLES eglCreateWindowSurface");
        runtime_failf("eglCreateWindowSurface failed");
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        kms_atomic_log_egl_failure(backend, "GLES eglMakeCurrent");
        runtime_failf("eglMakeCurrent ES1 failed");
    }

    *out_cfg = cfg;
    *out_ctx = ctx;
    *out_surf = surf;
    return dpy;
}
