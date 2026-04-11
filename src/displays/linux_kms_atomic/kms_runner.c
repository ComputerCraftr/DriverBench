#include "kms_runner.h"
#include "../../core/db_frame_source.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "kms_internal.h"

#include <EGL/egl.h>

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <xf86drm.h>

#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/cpu_renderer.h"
#include "../../renderers/gl_common.h"
#include "../display_dispatch.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"
#include "../gl_display_runtime.h"
#include "core/db_render_types.h"

const char *g_active_backend = BACKEND_NAME;

uint32_t db_kms_atomic_gbm_format_or_fail(const char *backend,
                                          db_native_output_format_t format) {
    switch (format) {
    case DB_NATIVE_OUTPUT_XRGB8888:
        return GBM_FORMAT_XRGB8888;
    case DB_NATIVE_OUTPUT_XRGB2101010:
#ifdef GBM_FORMAT_XRGB2101010
        return GBM_FORMAT_XRGB2101010;
#else
        break;
#endif
    case DB_NATIVE_OUTPUT_RGBA16F:
        break;
    }
    DB_RUNTIME_FAIL(
        (backend != NULL) ? backend : BACKEND_NAME,
        "resolved native output format is unavailable for KMS scanout");
}

int db_kms_atomic_run(const char *backend, const char *renderer_name,
                      const char *card, db_gl_renderer_t gl_renderer,
                      db_kms_atomic_context_profile_t context_profile,
                      const db_kms_atomic_renderer_vtable_t *renderer,
                      const db_cli_config_t *cfg) {
    if ((backend == NULL) || (renderer_name == NULL) || (card == NULL) ||
        (renderer == NULL) || (renderer->init == NULL) ||
        (renderer->render_frame == NULL) || (renderer->shutdown == NULL) ||
        (renderer->capability_mode == NULL) ||
        (renderer->work_unit_count == NULL)) {
        DB_RUNTIME_FAIL((backend != NULL) ? backend : BACKEND_NAME,
                        "Invalid KMS atomic run config");
    }

    g_active_backend = backend;
    db_install_signal_handlers();

    struct kms_atomic kms;
    struct gbm_device *gbm = NULL;
    uint32_t width = 0U;
    uint32_t height = 0U;
    db_kms_atomic_init_core(card, &kms, &gbm, &width, &height);

    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            backend, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE);
    const int req_major =
        (context_profile == DB_KMS_ATOMIC_CONTEXT_GL3_3) ? 3 : 1;
    const int req_minor =
        (context_profile == DB_KMS_ATOMIC_CONTEXT_GL3_3) ? 3 : 5;
    db_native_output_capability_t kms_capability =
        db_kms_atomic_verify_hdr_capability(&kms, gbm, width, height);
    if ((kms_capability.native_hdr_verified != 0) &&
        (db_kms_egl_hdr10_desktop_gl_supported(backend, gbm, width, height,
                                               req_major, req_minor) == 0)) {
        kms_capability.native_hdr_verified = 0;
        kms_capability.colorspace_supported = 0;
        kms_capability.unavailable_reason =
            "kms_egl_hdr10_desktop_gl_unavailable";
    }
    db_display_apply_native_output_capability_or_fail(
        backend, &resolved_runtime, &kms_capability);
    db_hdr_conversion_implementation_t hdr_conversion = DB_HDR_CONVERSION_NONE;
    if (resolved_runtime.renderer.format.native_hdr_enabled != 0) {
        hdr_conversion = (gl_renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                             ? DB_HDR_CONVERSION_CPU_FIXED_FUNCTION
                             : DB_HDR_CONVERSION_FRAGMENT_SHADER;
    }
    resolved_runtime.renderer.format.hdr_conversion = hdr_conversion;
    db_kms_atomic_set_hdr_enabled(
        &kms, resolved_runtime.renderer.format.native_hdr_enabled);
    const uint32_t gbm_format = db_kms_atomic_gbm_format_or_fail(
        backend, resolved_runtime.renderer.format.native_output_format);

    struct gbm_surface *gbm_surf =
        gbm_surface_create(gbm, width, height, gbm_format,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surf == NULL) {
        runtime_errno_fail("gbm_surface_create");
    }

    const int allow_gles1_1_fallback =
        DB_BOOL((context_profile == DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1) &&
                (resolved_runtime.renderer.format.native_hdr_enabled == 0));

    EGLConfig egl_cfg;
    EGLContext ctx;
    EGLSurface surf;
    EGLDisplay dpy = egl_init_try_gl_then_optional_gles1_1(
        backend, gbm, &egl_cfg, &ctx, &surf, gbm_surf, req_major, req_minor,
        allow_gles1_1_fallback,
        resolved_runtime.renderer.format.native_output_format);

    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)eglGetProcAddress, gl_renderer, backend, -1);

    const int viewport_width =
        db_checked_u32_to_i32(backend, "viewport_width", width);
    const int viewport_height =
        db_checked_u32_to_i32(backend, "viewport_height", height);
    db_gl_set_viewport_px(viewport_width, viewport_height);
    resolved_runtime.renderer.preserved_framebuffer_count = 0U;
    const db_presentation_transform_t presentation =
        db_display_presentation_transform(width, height);
    resolved_runtime.presentation = presentation;
    db_display_log_presentation_contract(backend, &resolved_runtime,
                                         &presentation);

    renderer->init(&resolved_runtime.renderer);
    db_frame_source_t benchmark_core = {0};
    db_frame_source_init(
        &benchmark_core,
        &(const db_frame_source_config_t){
            .benchmark_configuration = &resolved_runtime.benchmark,
            .working_format =
                resolved_runtime.renderer.format.surface_pixel_format,
        });
    const char *capability_mode = renderer->capability_mode();
    const uint32_t work_unit_count = renderer->work_unit_count();

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    struct fb *cur = NULL;

    const int debug_clear_default_framebuffer =
        resolved_runtime.display.debug_clear_default_framebuffer;
    const db_kms_atomic_frame_loop_t loop = {
        .api = DB_API_OPENGL,
        .backend = backend,
        .renderer_name = renderer_name,
        .capability_mode = capability_mode,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .work_unit_count = work_unit_count,
        .kms = &kms,
        .release_surface = gbm_surf,
        .event_context = &ev,
        .cur_fb = &cur,
        .release_previous_framebuffer = 1,
    };
    db_kms_atomic_gl_frame_producer_t producer = {
        .backend = backend,
        .debug_clear_default_framebuffer = debug_clear_default_framebuffer,
        .kms_fd = kms.fd,
        .dpy = dpy,
        .surf = surf,
        .gbm_surf = gbm_surf,
        .resolved_runtime = &resolved_runtime,
        .renderer = renderer,
        .core = &benchmark_core,
        .pixel_width = resolved_runtime.presentation.source_width,
        .pixel_height = resolved_runtime.presentation.source_height,
        .destination_width = width,
        .destination_height = height,
    };
    db_kms_egl_presentation_init(backend, dpy, surf, &producer.presentation);
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_gl_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_gl_fb);
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_OPENGL), renderer_name, backend,
        loop_result.frames, work_unit_count, loop_result.elapsed_ms,
        renderer->draw_stats);

    renderer->shutdown();
    db_frame_source_shutdown(&benchmark_core);

    fb_release(kms.fd, gbm_surf, cur);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    gbm_surface_destroy(gbm_surf);
    db_kms_atomic_shutdown_core(&kms, gbm);

    return 0;
}

int db_kms_atomic_run_cpu(const char *backend, const char *renderer_name,
                          const char *card, db_api_t api,
                          const db_cli_config_t *cfg) {
    if (api != DB_API_CPU) {
        DB_RUNTIME_FAIL((backend != NULL) ? backend : BACKEND_NAME,
                        "CPU KMS path requires cpu api");
    }
    if ((backend == NULL) || (renderer_name == NULL) || (card == NULL)) {
        DB_RUNTIME_FAIL((backend != NULL) ? backend : BACKEND_NAME,
                        "Invalid CPU KMS run config");
    }

    g_active_backend = backend;
    db_install_signal_handlers();

    struct kms_atomic kms;
    struct gbm_device *gbm = NULL;
    uint32_t width = 0U;
    uint32_t height = 0U;
    db_kms_atomic_init_core(card, &kms, &gbm, &width, &height);

    db_display_renderer_runtime_t resolved_runtime =
        db_display_renderer_runtime_from_cli(
            backend, cfg, 0U, 0, 0,
            DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE);
    const db_native_output_capability_t kms_capability =
        db_kms_atomic_verify_hdr_capability(&kms, gbm, width, height);
    db_display_apply_native_output_capability_or_fail(
        backend, &resolved_runtime, &kms_capability);
    resolved_runtime.renderer.format.hdr_conversion =
        (resolved_runtime.renderer.format.native_hdr_enabled != 0)
            ? DB_HDR_CONVERSION_CPU_SCANOUT
            : DB_HDR_CONVERSION_NONE;
    db_kms_atomic_set_hdr_enabled(
        &kms, resolved_runtime.renderer.format.native_hdr_enabled);
    resolved_runtime.presentation =
        db_display_presentation_transform(width, height);
    db_display_log_presentation_contract(backend, &resolved_runtime,
                                         &resolved_runtime.presentation);
    db_cpu_init(&resolved_runtime.renderer);
    db_frame_source_t benchmark_core = {0};
    db_frame_source_init(
        &benchmark_core,
        &(const db_frame_source_config_t){
            .benchmark_configuration = &resolved_runtime.benchmark,
            .working_format =
                resolved_runtime.renderer.format.surface_pixel_format,
        });
    const char *capability_mode = db_cpu_capability_mode();
    const uint32_t work_unit_count = db_cpu_work_unit_count();
    const uint32_t surface_width = resolved_runtime.presentation.source_width;
    const uint32_t surface_height = resolved_runtime.presentation.source_height;
    const uint64_t surface_pixel_count =
        (uint64_t)surface_width * (uint64_t)surface_height;
    db_pixel_surface_t cpu_surface = {
        .pixel_width = surface_width,
        .pixel_height = surface_height,
        .pixels = NULL,
        .format = resolved_runtime.renderer.format.surface_pixel_format,
    };
    if ((surface_width == 0U) || (surface_height == 0U) ||
        (surface_pixel_count == 0U)) {
        DB_RUNTIME_FAIL(backend, "invalid CPU KMS render surface: %ux%u",
                        surface_width, surface_height);
    }
    if (db_display_pixel_format_uses_rgba16f(cpu_surface.format) != 0) {
        cpu_surface.pixels = db_calloc_or_fail(
            backend, "kms_cpu_surface_rgba16f",
            (size_t)surface_pixel_count * DB_RGBA16F_CHANNELS_PER_PIXEL,
            sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES);
    } else {
        cpu_surface.pixels = db_calloc_or_fail(
            backend, "kms_cpu_surface_rgba8", (size_t)surface_pixel_count,
            sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    }

    struct fb *cur = NULL;

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    const db_kms_atomic_frame_loop_t loop = {
        .api = DB_API_CPU,
        .backend = backend,
        .renderer_name = renderer_name,
        .capability_mode = capability_mode,
        .fps_cap = resolved_runtime.display.fps_cap,
        .frame_limit = resolved_runtime.display.frame_limit,
        .work_unit_count = work_unit_count,
        .kms = &kms,
        .release_surface = NULL,
        .event_context = &ev,
        .cur_fb = &cur,
        .release_previous_framebuffer = 0,
    };
    db_kms_atomic_cpu_frame_producer_t producer = {
        .kms_fd = kms.fd,
        .gbm = gbm,
        .width = width,
        .height = height,
        .backend = backend,
        .surface = cpu_surface,
        .core = &benchmark_core,
        .native_output_format =
            resolved_runtime.renderer.format.native_output_format,
    };
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_cpu_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_cpu_fb);
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_CPU), renderer_name, backend,
        loop_result.frames, work_unit_count, loop_result.elapsed_ms, NULL);

    db_cpu_shutdown();
    db_frame_source_shutdown(&benchmark_core);
    free(producer.surface.pixels);
    db_kms_atomic_cpu_scanout_shutdown(&producer);
    db_kms_atomic_shutdown_core(&kms, gbm);
    return 0;
}
