#ifndef DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_INTERNAL_H
#define DRIVERBENCH_DISPLAY_LINUX_KMS_ATOMIC_INTERNAL_H

#include "display_linux_kms_atomic_runner.h"

#include <EGL/egl.h>

#ifdef __has_include
#if __has_include(<drm/drm.h>) && __has_include(<drm/drm_mode.h>)
#include <drm/drm.h>
#include <drm/drm_mode.h>
#elif __has_include(<libdrm/drm.h>) && __has_include(<libdrm/drm_mode.h>)
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#else
#error "Missing libdrm headers (drm.h/drm_mode.h)"
#endif
#else
#include <drm/drm.h>
#include <drm/drm_mode.h>
#endif
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>
#include <stdint.h>

#include "../../renderers/renderer_benchmark_runtime.h"

#define DRM_SRC_FP_SHIFT 16U
#define LOG_MSG_CAPACITY 2048U
#define BACKEND_NAME "display_linux_kms_atomic_runner"

extern const char *g_active_backend;

struct kms_atomic {
    int fd;
    drmModeConnector *conn;
    drmModeRes *res;
    drmModePlaneRes *pres;
    uint32_t conn_id;
    uint32_t crtc_id;
    uint32_t plane_id;
    drmModeModeInfo mode;
    uint32_t mode_blob_id;
    uint32_t conn_prop_crtc_id;
    uint32_t crtc_prop_mode_id;
    uint32_t crtc_prop_active;
    uint32_t plane_prop_fb_id;
    uint32_t plane_prop_crtc_id;
    uint32_t plane_prop_src_x;
    uint32_t plane_prop_src_y;
    uint32_t plane_prop_src_w;
    uint32_t plane_prop_src_h;
    uint32_t plane_prop_crtc_x;
    uint32_t plane_prop_crtc_y;
    uint32_t plane_prop_crtc_w;
    uint32_t plane_prop_crtc_h;
};

struct fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
    int is_surface_buffer;
};

typedef struct fb *(*db_kms_atomic_next_fb_fn_t)(void *user_ctx,
                                                 uint32_t frame_index);

typedef struct {
    db_api_t api;
    const char *backend;
    const char *renderer_name;
    const char *capability_mode;
    double fps_cap;
    uint32_t frame_limit;
    uint32_t work_unit_count;
    struct kms_atomic *kms;
    struct gbm_surface *release_surface;
    drmEventContext *event_context;
    struct fb **cur_fb;
} db_kms_atomic_frame_loop_t;

typedef struct {
    int debug_clear_default_framebuffer;
    int kms_fd;
    EGLDisplay dpy;
    EGLSurface surf;
    struct gbm_surface *gbm_surf;
    uint32_t preserved_framebuffer_count;
    const db_kms_atomic_renderer_vtable_t *renderer;
} db_kms_atomic_gl_frame_producer_t;

typedef struct {
    int kms_fd;
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    const char *backend;
    db_benchmark_pixel_surface_t surface;
} db_kms_atomic_cpu_frame_producer_t;

typedef struct {
    uint64_t frames;
    double elapsed_ms;
} db_kms_atomic_loop_run_result_t;

typedef struct {
    const db_kms_atomic_frame_loop_t *loop;
    void *producer_ctx;
    db_kms_atomic_next_fb_fn_t next_fb_fn;
    double *next_progress_log_due_ms;
} db_kms_atomic_shared_loop_ctx_t;

void failf(const char *fmt, ...) __attribute__((noreturn));
void die(const char *msg) __attribute__((noreturn));
void diex(const char *msg) __attribute__((noreturn));
void page_flip_handler(int fd, unsigned frame, unsigned sec, unsigned usec,
                       void *data);
void db_kms_atomic_init_core(const char *card, struct kms_atomic *kms,
                             struct gbm_device **out_gbm, uint32_t *out_width,
                             uint32_t *out_height);
void db_kms_atomic_shutdown_core(struct kms_atomic *kms,
                                 struct gbm_device *gbm);
struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer);
void fb_release(int fd, struct gbm_surface *gbm_surf, struct fb *fb);
struct fb *db_kms_atomic_prime_first_frame_and_modeset(
    const struct kms_atomic *kms, uint32_t width, uint32_t height,
    void *producer_ctx, db_kms_atomic_next_fb_fn_t next_fb_fn);
db_kms_atomic_loop_run_result_t
db_kms_atomic_run_frame_loop_timed(const db_kms_atomic_frame_loop_t *loop,
                                   void *producer_ctx,
                                   db_kms_atomic_next_fb_fn_t next_fb_fn);
struct fb *db_kms_atomic_next_gl_fb(void *user_ctx, uint32_t frame_index);
struct fb *db_kms_atomic_next_cpu_fb(void *user_ctx, uint32_t frame_index);
uint32_t db_kms_atomic_enable_preserved_swap_behavior(const char *backend,
                                                      EGLDisplay dpy,
                                                      EGLSurface surf);
EGLDisplay egl_init_try_gl_then_optional_gles1_1(
    const char *backend, struct gbm_device *gbm, EGLConfig *out_cfg,
    EGLContext *out_ctx, EGLSurface *out_surf, struct gbm_surface *gbm_surf,
    int req_gl_major, int req_gl_minor, int allow_gles1_1_fallback);

#endif
