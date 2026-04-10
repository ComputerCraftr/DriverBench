// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _GNU_SOURCE

#include "display_linux_kms_atomic_internal.h"

#include <EGL/egl.h>
#include <EGL/eglplatform.h>

#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

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

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../renderers/renderer_gl_common.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_runtime_common.h"

__attribute__((noreturn)) void failf(const char *fmt, ...) {
    char message[LOG_MSG_CAPACITY];
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    db_failf(g_active_backend, "%s", message);
}

void die(const char *msg) { failf("%s: %s", msg, strerror(errno)); }
void diex(const char *msg) { failf("%s", msg); }

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

static void db_kms_atomic_log_egl_failure(const char *backend,
                                          const char *stage) {
    const EGLint error_code = eglGetError();
    db_infof(backend, "KMS EGL: %s failed (%s / 0x%04x)", stage,
             db_kms_atomic_egl_error_name(error_code),
             (unsigned int)error_code);
}

static uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type,
                            const char *name) {
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (props == NULL) {
        die("drmModeObjectGetProperties");
    }

    uint32_t prop_id = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        if (prop == NULL) {
            continue;
        }
        if (strcmp(prop->name, name) == 0) {
            prop_id = prop->prop_id;
            drmModeFreeProperty(prop);
            break;
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);

    if (prop_id == 0U) {
        failf("Missing DRM property '%s' on object %u type %u", name, obj_id,
              obj_type);
    }
    return prop_id;
}

static drmModeConnector *pick_connected_connector(struct kms_atomic *kms) {
    for (int i = 0; i < kms->res->count_connectors; i++) {
        drmModeConnector *connector =
            drmModeGetConnector(kms->fd, kms->res->connectors[i]);
        if (connector == NULL) {
            continue;
        }
        if ((connector->connection == DRM_MODE_CONNECTED) &&
            (connector->count_modes > 0)) {
            return connector;
        }
        drmModeFreeConnector(connector);
    }
    return NULL;
}

static uint32_t pick_crtc_for_connector(struct kms_atomic *kms,
                                        drmModeConnector *conn) {
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *encoder = drmModeGetEncoder(kms->fd, conn->encoders[i]);
        if (encoder == NULL) {
            continue;
        }

        for (int c = 0; c < kms->res->count_crtcs; c++) {
            if ((encoder->possible_crtcs & (1 << c)) != 0) {
                const uint32_t crtc_id = kms->res->crtcs[c];
                drmModeFreeEncoder(encoder);
                return crtc_id;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return 0;
}

static uint32_t pick_primary_plane_for_crtc(struct kms_atomic *kms,
                                            uint32_t crtc_id) {
    int crtc_index = -1;
    for (int i = 0; i < kms->res->count_crtcs; i++) {
        if (kms->res->crtcs[i] == crtc_id) {
            crtc_index = i;
            break;
        }
    }
    if (crtc_index < 0) {
        return 0;
    }

    for (uint32_t i = 0; i < kms->pres->count_planes; i++) {
        const uint32_t plane_id = kms->pres->planes[i];
        drmModePlane *plane = drmModeGetPlane(kms->fd, plane_id);
        if (plane == NULL) {
            continue;
        }
        if ((plane->possible_crtcs & (1 << crtc_index)) == 0) {
            drmModeFreePlane(plane);
            continue;
        }

        drmModeObjectProperties *props = drmModeObjectGetProperties(
            kms->fd, plane_id, DRM_MODE_OBJECT_PLANE);
        if (props == NULL) {
            die("drmModeObjectGetProperties plane");
        }

        int is_primary = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes *prop =
                drmModeGetProperty(kms->fd, props->props[j]);
            if (prop == NULL) {
                continue;
            }
            if ((strcmp(prop->name, "type") == 0) &&
                ((prop->flags & DRM_MODE_PROP_ENUM) != 0)) {
                for (int e = 0; e < prop->count_enums; e++) {
                    if (strcmp(prop->enums[e].name, "Primary") == 0) {
                        if (props->prop_values[j] == prop->enums[e].value) {
                            is_primary = 1;
                        }
                    }
                }
            }
            drmModeFreeProperty(prop);
            if (is_primary != 0) {
                break;
            }
        }

        drmModeFreeObjectProperties(props);
        drmModeFreePlane(plane);

        if (is_primary != 0) {
            return plane_id;
        }
    }
    return 0;
}

static void kms_atomic_init(struct kms_atomic *kms, const char *card) {
    *kms = (struct kms_atomic){0};

    kms->fd = open(card, O_RDWR | O_CLOEXEC);
    if (kms->fd < 0) {
        die("open DRM card");
    }

    if (drmSetClientCap(kms->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        die("drmSetClientCap UNIVERSAL_PLANES");
    }
    if (drmSetClientCap(kms->fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        die("drmSetClientCap ATOMIC");
    }

    kms->res = drmModeGetResources(kms->fd);
    if (kms->res == NULL) {
        die("drmModeGetResources");
    }

    kms->pres = drmModeGetPlaneResources(kms->fd);
    if (kms->pres == NULL) {
        die("drmModeGetPlaneResources");
    }

    kms->conn = pick_connected_connector(kms);
    if (kms->conn == NULL) {
        diex("No connected connector with modes");
    }
    kms->conn_id = kms->conn->connector_id;
    kms->mode = kms->conn->modes[0];

    kms->crtc_id = pick_crtc_for_connector(kms, kms->conn);
    if (kms->crtc_id == 0U) {
        diex("No usable CRTC for connector");
    }

    kms->plane_id = pick_primary_plane_for_crtc(kms, kms->crtc_id);
    if (kms->plane_id == 0U) {
        diex("No primary plane for chosen CRTC");
    }

    if (drmModeCreatePropertyBlob(kms->fd, &kms->mode, sizeof(kms->mode),
                                  &kms->mode_blob_id) != 0) {
        die("drmModeCreatePropertyBlob");
    }

    kms->conn_prop_crtc_id = get_prop_id(kms->fd, kms->conn_id,
                                         DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

    kms->crtc_prop_mode_id =
        get_prop_id(kms->fd, kms->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    kms->crtc_prop_active =
        get_prop_id(kms->fd, kms->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");

    kms->plane_prop_fb_id =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    kms->plane_prop_crtc_id =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    kms->plane_prop_src_x =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    kms->plane_prop_src_y =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    kms->plane_prop_src_w =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    kms->plane_prop_src_h =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    kms->plane_prop_crtc_x =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    kms->plane_prop_crtc_y =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    kms->plane_prop_crtc_w =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    kms->plane_prop_crtc_h =
        get_prop_id(kms->fd, kms->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
}

struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer) {
    struct fb *fb = (struct fb *)calloc(1, sizeof(*fb));
    if (fb == NULL) {
        diex("calloc fb");
    }
    fb->bo = bo;
    fb->is_surface_buffer = is_surface_buffer;

    const uint32_t width_px = gbm_bo_get_width(bo);
    const uint32_t height_px = gbm_bo_get_height(bo);
    const uint32_t stride = gbm_bo_get_stride(bo);
    const uint32_t handle = gbm_bo_get_handle(bo).u32;
    const uint32_t format = gbm_bo_get_format(bo);

    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t pitches[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(fd, width_px, height_px, format, handles, pitches,
                      offsets, &fb->fb_id, 0) != 0) {
        die("drmModeAddFB2");
    }

    return fb;
}

void fb_release(int fd, struct gbm_surface *gbm_surf, struct fb *fb) {
    if (fb == NULL) {
        return;
    }
    if (fb->fb_id != 0U) {
        drmModeRmFB(fd, fb->fb_id);
    }
    if (fb->bo != NULL) {
        if (fb->is_surface_buffer != 0) {
            if (gbm_surf != NULL) {
                gbm_surface_release_buffer(gbm_surf, fb->bo);
            }
        } else {
            gbm_bo_destroy(fb->bo);
        }
    }
    free(fb);
}

void page_flip_handler(int fd, unsigned frame, unsigned sec, unsigned usec,
                       void *data) {
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    int *waiting = (int *)data;
    *waiting = 0;
}

static void db_kms_atomic_add_modeset_props(drmModeAtomicReq *req,
                                            const struct kms_atomic *kms,
                                            uint32_t width, uint32_t height,
                                            uint32_t fb_id) {
    drmModeAtomicAddProperty(req, kms->conn_id, kms->conn_prop_crtc_id,
                             kms->crtc_id);
    drmModeAtomicAddProperty(req, kms->crtc_id, kms->crtc_prop_mode_id,
                             kms->mode_blob_id);
    drmModeAtomicAddProperty(req, kms->crtc_id, kms->crtc_prop_active, 1);

    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_fb_id, fb_id);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_crtc_id,
                             kms->crtc_id);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_src_x, 0);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_src_y, 0);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_src_w,
                             ((uint64_t)width) << DRM_SRC_FP_SHIFT);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_src_h,
                             ((uint64_t)height) << DRM_SRC_FP_SHIFT);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_crtc_x, 0);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_crtc_y, 0);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_crtc_w, width);
    drmModeAtomicAddProperty(req, kms->plane_id, kms->plane_prop_crtc_h,
                             height);
}

static void db_kms_atomic_commit_modeset(const struct kms_atomic *kms,
                                         uint32_t width, uint32_t height,
                                         uint32_t fb_id) {
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (req == NULL) {
        diex("drmModeAtomicAlloc");
    }
    db_kms_atomic_add_modeset_props(req, kms, width, height, fb_id);
    if (drmModeAtomicCommit(kms->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET,
                            NULL) != 0) {
        die("drmModeAtomicCommit modeset");
    }
    drmModeAtomicFree(req);
}

static void db_kms_atomic_flip_to_fb(const struct kms_atomic *kms,
                                     uint32_t fb_id, drmEventContext *ev) {
    drmModeAtomicReq *commit_req = drmModeAtomicAlloc();
    if (commit_req == NULL) {
        diex("drmModeAtomicAlloc");
    }
    drmModeAtomicAddProperty(commit_req, kms->plane_id, kms->plane_prop_fb_id,
                             fb_id);

    int waiting = 1;
    const uint32_t flip_flags =
        DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
    if (drmModeAtomicCommit(kms->fd, commit_req, flip_flags, &waiting) != 0) {
        die("drmModeAtomicCommit flip");
    }
    drmModeAtomicFree(commit_req);
    for (;;) {
        if (waiting == 0) {
            break;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(kms->fd, &fds);
        if (select(kms->fd + 1, &fds, NULL, NULL, NULL) < 0) {
            // NOLINTNEXTLINE(misc-include-cleaner)
            if (errno == EINTR) {
                continue;
            }
            die("select");
        }
        drmHandleEvent(kms->fd, ev);
    }
}

void db_kms_atomic_init_core(const char *card, struct kms_atomic *kms,
                             struct gbm_device **out_gbm, uint32_t *out_width,
                             uint32_t *out_height) {
    if ((card == NULL) || (kms == NULL) || (out_gbm == NULL) ||
        (out_width == NULL) || (out_height == NULL)) {
        diex("invalid kms core init args");
    }
    kms_atomic_init(kms, card);
    *out_width = kms->mode.hdisplay;
    *out_height = kms->mode.vdisplay;
    struct gbm_device *gbm = gbm_create_device(kms->fd);
    if (gbm == NULL) {
        die("gbm_create_device");
    }
    *out_gbm = gbm;
}

void db_kms_atomic_shutdown_core(struct kms_atomic *kms,
                                 struct gbm_device *gbm) {
    if (kms == NULL) {
        return;
    }
    if (kms->mode_blob_id != 0U) {
        drmModeDestroyPropertyBlob(kms->fd, kms->mode_blob_id);
    }
    if (gbm != NULL) {
        gbm_device_destroy(gbm);
    }
    if (kms->conn != NULL) {
        drmModeFreeConnector(kms->conn);
    }
    if (kms->pres != NULL) {
        drmModeFreePlaneResources(kms->pres);
    }
    if (kms->res != NULL) {
        drmModeFreeResources(kms->res);
    }
    if (kms->fd >= 0) {
        close(kms->fd);
        kms->fd = -1;
    }
}

struct fb *db_kms_atomic_prime_first_frame_and_modeset(
    const struct kms_atomic *kms, uint32_t width, uint32_t height,
    void *producer_ctx, db_kms_atomic_next_fb_fn_t next_fb_fn) {
    if ((kms == NULL) || (next_fb_fn == NULL)) {
        diex("invalid kms prime args");
    }
    struct fb *cur = next_fb_fn(producer_ctx, 0U);
    if (cur == NULL) {
        diex("failed to create first scanout framebuffer");
    }
    db_kms_atomic_commit_modeset(kms, width, height, cur->fb_id);
    return cur;
}

static db_display_frame_loop_result_t
db_kms_atomic_shared_frame_step(void *user_data, uint32_t frame_index,
                                double elapsed_ms) {
    db_kms_atomic_shared_loop_ctx_t *loop_ctx =
        (db_kms_atomic_shared_loop_ctx_t *)user_data;
    if ((loop_ctx == NULL) || (loop_ctx->loop == NULL) ||
        (loop_ctx->next_fb_fn == NULL) ||
        (loop_ctx->next_progress_log_due_ms == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    const db_kms_atomic_frame_loop_t *loop_cfg = loop_ctx->loop;
    struct fb *next = loop_ctx->next_fb_fn(loop_ctx->producer_ctx, frame_index);
    db_kms_atomic_flip_to_fb(loop_cfg->kms, next->fb_id,
                             loop_cfg->event_context);
    fb_release(loop_cfg->kms->fd, loop_cfg->release_surface, *loop_cfg->cur_fb);
    *loop_cfg->cur_fb = next;
    db_benchmark_log_periodic(
        db_dispatch_api_name(loop_cfg->api), loop_cfg->renderer_name,
        loop_cfg->backend, (uint64_t)frame_index + 1U,
        loop_cfg->work_unit_count, elapsed_ms, loop_cfg->capability_mode,
        loop_ctx->next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static uint64_t
db_kms_atomic_run_frame_loop(const db_kms_atomic_frame_loop_t *loop,
                             void *producer_ctx,
                             db_kms_atomic_next_fb_fn_t next_fb_fn) {
    double next_progress_log_due_ms = 0.0;
    if ((loop == NULL) || (next_fb_fn == NULL)) {
        return 0U;
    }
    db_kms_atomic_shared_loop_ctx_t ctx = {
        .loop = loop,
        .producer_ctx = producer_ctx,
        .next_fb_fn = next_fb_fn,
        .next_progress_log_due_ms = &next_progress_log_due_ms,
    };
    db_display_frame_loop_t shared_loop = {
        .backend = loop->backend,
        .fps_cap = loop->fps_cap,
        .frame_limit = loop->frame_limit,
        .user_data = &ctx,
        .should_continue_fn = NULL,
        .pre_frame_fn = NULL,
        .frame_fn = db_kms_atomic_shared_frame_step,
    };
    return db_display_run_frame_loop(&shared_loop).frames;
}

db_kms_atomic_loop_run_result_t
db_kms_atomic_run_frame_loop_timed(const db_kms_atomic_frame_loop_t *loop,
                                   void *producer_ctx,
                                   db_kms_atomic_next_fb_fn_t next_fb_fn) {
    const uint64_t bench_start = db_now_ns_monotonic();
    const uint64_t bench_frames =
        db_kms_atomic_run_frame_loop(loop, producer_ctx, next_fb_fn);
    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start) / DB_NS_PER_MS;
    return (db_kms_atomic_loop_run_result_t){
        .frames = bench_frames,
        .elapsed_ms = bench_ms,
    };
}

struct fb *db_kms_atomic_next_gl_fb(void *user_ctx, uint32_t frame_index) {
    db_kms_atomic_gl_frame_producer_t *producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        producer->debug_clear_default_framebuffer);
    producer->renderer->render_frame(frame_index,
                                     producer->preserved_framebuffer_count);
    eglSwapBuffers(producer->dpy, producer->surf);

    struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(producer->gbm_surf);
    if (next_bo == NULL) {
        diex("lock_front_buffer failed");
    }
    return fb_from_bo(producer->kms_fd, next_bo, 1);
}

uint32_t db_kms_atomic_enable_preserved_swap_behavior(const char *backend,
                                                      EGLDisplay dpy,
                                                      EGLSurface surf) {
    if ((backend == NULL) || (dpy == EGL_NO_DISPLAY) ||
        (surf == EGL_NO_SURFACE)) {
        return 0U;
    }

    if (!eglSurfaceAttrib(dpy, surf, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED)) {
        db_infof(
            backend,
            "EGL swap behavior preserve request rejected; using full draw");
        return 0U;
    }

    EGLint swap_behavior = EGL_BUFFER_DESTROYED;
    if (!eglQuerySurface(dpy, surf, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
        db_infof(backend,
                 "EGL swap behavior query failed after preserve request; "
                 "using full draw");
        return 0U;
    }
    if (swap_behavior != EGL_BUFFER_PRESERVED) {
        db_infof(backend,
                 "EGL swap behavior remained destroyed after preserve request; "
                 "using full draw");
        return 0U;
    }
    db_infof(backend,
             "EGL swap behavior preserved; enabling GL1 dirty backbuffer draw");
    return 2U;
}

EGLDisplay egl_init_try_gl_then_optional_gles1_1(
    const char *backend, struct gbm_device *gbm, EGLConfig *out_cfg,
    EGLContext *out_ctx, EGLSurface *out_surf, struct gbm_surface *gbm_surf,
    int req_gl_major, int req_gl_minor, int allow_gles1_1_fallback) {
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) {
        db_kms_atomic_log_egl_failure(backend, "eglGetDisplay");
        diex("eglGetDisplay failed");
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        db_kms_atomic_log_egl_failure(backend, "eglInitialize");
        diex("eglInitialize failed");
    }

    const EGLint base_cfg[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE,     8,
                               EGL_GREEN_SIZE,   8,
                               EGL_BLUE_SIZE,    8,
                               EGL_NONE};

    if (eglBindAPI(EGL_OPENGL_API)) {
        db_infof(backend, "KMS EGL: trying desktop OpenGL %d.%d window surface",
                 req_gl_major, req_gl_minor);
        EGLint cfg_attribs_gl[64];
        int idx = 0;
        for (int i = 0; base_cfg[i] != EGL_NONE; i += 2) {
            cfg_attribs_gl[idx++] = base_cfg[i];
            cfg_attribs_gl[idx++] = base_cfg[i + 1];
        }
        cfg_attribs_gl[idx++] = EGL_RENDERABLE_TYPE;
        cfg_attribs_gl[idx++] = EGL_OPENGL_BIT;
        cfg_attribs_gl[idx++] = EGL_NONE;

        EGLConfig cfg;
        EGLint config_count = 0;
        if (eglChooseConfig(dpy, cfg_attribs_gl, &cfg, 1, &config_count)) {
            db_infof(backend, "KMS EGL: desktop GL config_count=%d",
                     config_count);
        }
        if ((config_count == 1) &&
            eglChooseConfig(dpy, cfg_attribs_gl, &cfg, 1, &config_count)) {
            EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
            if (ctx != EGL_NO_CONTEXT) {
                EGLSurface surf = eglCreateWindowSurface(
                    dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
                if ((surf != EGL_NO_SURFACE) &&
                    eglMakeCurrent(dpy, surf, surf, ctx)) {
                    const db_display_gl_runtime_info_t runtime =
                        db_display_prepare_gl_runtime_info(
                            (db_gl_proc_resolver_fn_t)eglGetProcAddress,
                            BACKEND_NAME);
                    if (db_gl_version_text_at_least(
                            runtime.version, req_gl_major, req_gl_minor)) {
                        *out_cfg = cfg;
                        *out_ctx = ctx;
                        *out_surf = surf;
                        return dpy;
                    }
                    db_infof(backend,
                             "KMS EGL: desktop GL runtime version did not meet "
                             "%d.%d requirement",
                             req_gl_major, req_gl_minor);
                } else if (surf == EGL_NO_SURFACE) {
                    db_kms_atomic_log_egl_failure(
                        backend, "desktop GL eglCreateWindowSurface");
                } else {
                    db_kms_atomic_log_egl_failure(backend,
                                                  "desktop GL eglMakeCurrent");
                }
                if (surf != EGL_NO_SURFACE) {
                    eglDestroySurface(dpy, surf);
                }
                eglDestroyContext(dpy, ctx);
            } else {
                db_kms_atomic_log_egl_failure(backend,
                                              "desktop GL eglCreateContext");
            }
        } else {
            db_infof(backend,
                     "KMS EGL: no matching desktop GL window config found");
        }
    } else {
        db_kms_atomic_log_egl_failure(backend, "eglBindAPI(EGL_OPENGL_API)");
    }

    if (allow_gles1_1_fallback == 0) {
        diex("Failed to create required desktop OpenGL context");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        db_kms_atomic_log_egl_failure(backend, "eglBindAPI(EGL_OPENGL_ES_API)");
        diex("eglBindAPI ES failed");
    }
    db_infof(backend, "KMS EGL: trying OpenGL ES 1.1 fallback window surface");

    EGLint cfg_attribs_es[64];
    int idx = 0;
    for (int i = 0; base_cfg[i] != EGL_NONE; i += 2) {
        cfg_attribs_es[idx++] = base_cfg[i];
        cfg_attribs_es[idx++] = base_cfg[i + 1];
    }
    cfg_attribs_es[idx++] = EGL_RENDERABLE_TYPE;
    cfg_attribs_es[idx++] = EGL_OPENGL_ES_BIT;
    cfg_attribs_es[idx++] = EGL_NONE;

    EGLConfig cfg;
    EGLint config_count = 0;
    if (!eglChooseConfig(dpy, cfg_attribs_es, &cfg, 1, &config_count) ||
        (config_count != 1)) {
        die("eglChooseConfig ES");
    }
    db_infof(backend, "KMS EGL: GLES config_count=%d", config_count);

    const EGLint ctx_attribs_es1[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    EGLContext ctx =
        eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_es1);
    if (ctx == EGL_NO_CONTEXT) {
        db_kms_atomic_log_egl_failure(backend, "GLES eglCreateContext");
        diex("eglCreateContext ES1 failed");
    }

    EGLSurface surf =
        eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
    if (surf == EGL_NO_SURFACE) {
        db_kms_atomic_log_egl_failure(backend, "GLES eglCreateWindowSurface");
        diex("eglCreateWindowSurface failed");
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        db_kms_atomic_log_egl_failure(backend, "GLES eglMakeCurrent");
        diex("eglMakeCurrent ES1 failed");
    }

    *out_cfg = cfg;
    *out_ctx = ctx;
    *out_surf = surf;
    return dpy;
}
