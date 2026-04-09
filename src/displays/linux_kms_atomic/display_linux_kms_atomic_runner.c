#include "display_linux_kms_atomic_runner.h"
#include "display_linux_kms_atomic_internal.h"

#include <EGL/egl.h>

#include <gbm.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <xf86drm.h>

#include "../../core/db_core.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_benchmark_runtime.h"
#include "../../renderers/renderer_gl_common.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"

const char *g_active_backend = BACKEND_NAME;

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
        db_failf((backend != NULL) ? backend : BACKEND_NAME,
                 "Invalid KMS atomic run config");
    }

    g_active_backend = backend;
    db_install_signal_handlers();

    struct kms_atomic kms;
    struct gbm_device *gbm = NULL;
    uint32_t width = 0U;
    uint32_t height = 0U;
    db_kms_atomic_init_core(card, &kms, &gbm, &width, &height);

    struct gbm_surface *gbm_surf =
        gbm_surface_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_surf == NULL) {
        die("gbm_surface_create");
    }

    const int allow_gles1_1_fallback =
        (context_profile == DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1) ? 1 : 0;
    const int req_major =
        (context_profile == DB_KMS_ATOMIC_CONTEXT_GL3_3) ? 3 : 1;
    const int req_minor =
        (context_profile == DB_KMS_ATOMIC_CONTEXT_GL3_3) ? 3 : 5;

    EGLConfig egl_cfg;
    EGLContext ctx;
    EGLSurface surf;
    EGLDisplay dpy = egl_init_try_gl_then_optional_gles1_1(
        backend, gbm, &egl_cfg, &ctx, &surf, gbm_surf, req_major, req_minor,
        allow_gles1_1_fallback);

    (void)db_display_require_gl_runtime_for_renderer(
        (db_gl_proc_resolver_fn_t)eglGetProcAddress, gl_renderer, backend, -1);

    const int viewport_width =
        db_checked_u32_to_i32(backend, "viewport_width", width);
    const int viewport_height =
        db_checked_u32_to_i32(backend, "viewport_height", height);
    db_gl_set_viewport_px(viewport_width, viewport_height);
    const uint32_t preserved_framebuffer_count =
        db_kms_atomic_enable_preserved_swap_behavior(backend, dpy, surf);

    renderer->init();
    const char *capability_mode = renderer->capability_mode();
    const db_display_runtime_config_t runtime_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0).runtime;
    const uint32_t work_unit_count = renderer->work_unit_count();

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    struct fb *cur = NULL;

    const int debug_clear_default_framebuffer =
        runtime_cfg.debug_clear_default_framebuffer;
    const db_kms_atomic_frame_loop_t loop = {
        .api = DB_API_OPENGL,
        .backend = backend,
        .renderer_name = renderer_name,
        .capability_mode = capability_mode,
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .work_unit_count = work_unit_count,
        .kms = &kms,
        .release_surface = gbm_surf,
        .event_context = &ev,
        .cur_fb = &cur,
    };
    db_kms_atomic_gl_frame_producer_t producer = {
        .debug_clear_default_framebuffer = debug_clear_default_framebuffer,
        .kms_fd = kms.fd,
        .dpy = dpy,
        .surf = surf,
        .gbm_surf = gbm_surf,
        .preserved_framebuffer_count = preserved_framebuffer_count,
        .renderer = renderer,
    };
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_gl_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_gl_fb);
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_OPENGL), renderer_name, backend,
        capability_mode, loop_result.frames, work_unit_count,
        loop_result.elapsed_ms, renderer->draw_stats);

    renderer->shutdown();

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
        db_failf((backend != NULL) ? backend : BACKEND_NAME,
                 "CPU KMS path requires cpu api");
    }
    if ((backend == NULL) || (renderer_name == NULL) || (card == NULL)) {
        db_failf((backend != NULL) ? backend : BACKEND_NAME,
                 "Invalid CPU KMS run config");
    }

    g_active_backend = backend;
    db_install_signal_handlers();

    struct kms_atomic kms;
    struct gbm_device *gbm = NULL;
    uint32_t width = 0U;
    uint32_t height = 0U;
    db_kms_atomic_init_core(card, &kms, &gbm, &width, &height);

    const int uses_rgba16f =
        db_display_cpu_hdr_option_state().option_enables_hdr;
    db_renderer_cpu_renderer_init_with_hdr_float_bo(uses_rgba16f);
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const db_display_runtime_config_t runtime_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0).runtime;
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();
    const uint32_t surface_width = db_grid_cols_effective();
    const uint32_t surface_height = db_grid_rows_effective();
    const uint64_t surface_pixel_count =
        (uint64_t)surface_width * (uint64_t)surface_height;
    db_benchmark_pixel_surface_t cpu_surface = {
        .pixel_width = surface_width,
        .pixel_height = surface_height,
        .pixels_rgba8 = NULL,
        .pixels_rgba16f = NULL,
        .uses_rgba16f = uses_rgba16f,
    };
    if ((surface_width == 0U) || (surface_height == 0U) ||
        (surface_pixel_count == 0U)) {
        db_failf(backend, "invalid CPU KMS render surface: %ux%u",
                 surface_width, surface_height);
    }
    if (uses_rgba16f != 0) {
        cpu_surface.pixels_rgba16f = (uint16_t *)db_alloc_aligned_array_or_fail(
            backend, "kms_cpu_surface_rgba16f",
            (size_t)surface_pixel_count * DB_RGBA16F_CHANNELS_PER_PIXEL,
            sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES);
    } else {
        cpu_surface.pixels_rgba8 = (uint32_t *)db_alloc_aligned_array_or_fail(
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
        .fps_cap = runtime_cfg.fps_cap,
        .frame_limit = runtime_cfg.frame_limit,
        .work_unit_count = work_unit_count,
        .kms = &kms,
        .release_surface = NULL,
        .event_context = &ev,
        .cur_fb = &cur,
    };
    db_kms_atomic_cpu_frame_producer_t producer = {
        .kms_fd = kms.fd,
        .gbm = gbm,
        .width = width,
        .height = height,
        .backend = backend,
        .surface = cpu_surface,
    };
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_cpu_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_cpu_fb);
    db_display_log_renderer_final_summary(
        db_dispatch_api_name(DB_API_CPU), renderer_name, backend,
        capability_mode, loop_result.frames, work_unit_count,
        loop_result.elapsed_ms, NULL);

    db_renderer_cpu_renderer_shutdown();
    free(producer.surface.pixels_rgba8);
    free(producer.surface.pixels_rgba16f);
    fb_release(kms.fd, NULL, cur);
    db_kms_atomic_shutdown_core(&kms, gbm);
    return 0;
}
