#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)

#include "display_linux_kms_atomic_common.h"

#include <EGL/egl.h>
#include <EGL/eglplatform.h>

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

#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/select.h>
#include <unistd.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../driverbench_config.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_gl_common.h"
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_runtime_config_common.h"
#include "../display_types.h"

#define DRM_SRC_FP_SHIFT 16U
#define LOG_MSG_CAPACITY 2048U
#define BACKEND_NAME "display_linux_kms_atomic_common"

static const char *g_active_backend = BACKEND_NAME;

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
    const db_kms_atomic_renderer_vtable_t *renderer;
} db_kms_atomic_gl_frame_producer_t;

typedef struct {
    int kms_fd;
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    const char *backend;
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

static __attribute__((noreturn)) void failf(const char *fmt, ...) {
    char message[LOG_MSG_CAPACITY];
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    db_failf(g_active_backend, "%s", message);
}

static void die(const char *msg) { failf("%s: %s", msg, strerror(errno)); }
static void diex(const char *msg) { failf("%s", msg); }

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

static struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer) {
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

static void fb_release(int fd, struct gbm_surface *gbm_surf, struct fb *fb) {
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

static void page_flip_handler(int fd, unsigned frame, unsigned sec,
                              unsigned usec, void *data) {
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

static void db_kms_atomic_init_core(const char *card, struct kms_atomic *kms,
                                    struct gbm_device **out_gbm,
                                    uint32_t *out_width, uint32_t *out_height) {
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

static void db_kms_atomic_shutdown_core(struct kms_atomic *kms,
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

static struct fb *db_kms_atomic_prime_first_frame_and_modeset(
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

static db_kms_atomic_loop_run_result_t
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

static struct fb *db_kms_atomic_next_gl_fb(void *user_ctx,
                                           uint32_t frame_index) {
    db_kms_atomic_gl_frame_producer_t *producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    db_display_gl_debug_clear_default_framebuffer_if_enabled(
        producer->debug_clear_default_framebuffer);
    producer->renderer->render_frame(frame_index);
    eglSwapBuffers(producer->dpy, producer->surf);

    struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(producer->gbm_surf);
    if (next_bo == NULL) {
        diex("lock_front_buffer failed");
    }
    return fb_from_bo(producer->kms_fd, next_bo, 1);
}

static EGLDisplay egl_init_try_gl_then_optional_gles1_1(
    struct gbm_device *gbm, EGLConfig *out_cfg, EGLContext *out_ctx,
    EGLSurface *out_surf, struct gbm_surface *gbm_surf, int req_gl_major,
    int req_gl_minor, int allow_gles1_1_fallback) {
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) {
        die("eglGetDisplay");
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        die("eglInitialize");
    }

    const EGLint base_cfg[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE,     8,
                               EGL_GREEN_SIZE,   8,
                               EGL_BLUE_SIZE,    8,
                               EGL_NONE};

    if (eglBindAPI(EGL_OPENGL_API)) {
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
        if (eglChooseConfig(dpy, cfg_attribs_gl, &cfg, 1, &config_count) &&
            (config_count == 1)) {
            EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
            if (ctx != EGL_NO_CONTEXT) {
                EGLSurface surf = eglCreateWindowSurface(
                    dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
                if ((surf != EGL_NO_SURFACE) &&
                    eglMakeCurrent(dpy, surf, surf, ctx)) {
                    const char *ver = NULL;
                    (void)db_display_prepare_gl_runtime(
                        (db_gl_proc_resolver_fn_t)eglGetProcAddress,
                        BACKEND_NAME, DB_DISPLAY_GL_RUNTIME_LOG_DISABLED, &ver,
                        NULL);
                    if (db_gl_version_text_at_least(ver, req_gl_major,
                                                    req_gl_minor)) {
                        *out_cfg = cfg;
                        *out_ctx = ctx;
                        *out_surf = surf;
                        return dpy;
                    }
                }
                if (surf != EGL_NO_SURFACE) {
                    eglDestroySurface(dpy, surf);
                }
                eglDestroyContext(dpy, ctx);
            }
        }
    }

    if (allow_gles1_1_fallback == 0) {
        diex("Failed to create required desktop OpenGL context");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        die("eglBindAPI ES");
    }

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

    const EGLint ctx_attribs_es1[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    EGLContext ctx =
        eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_es1);
    if (ctx == EGL_NO_CONTEXT) {
        die("eglCreateContext ES1");
    }

    EGLSurface surf =
        eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
    if (surf == EGL_NO_SURFACE) {
        die("eglCreateWindowSurface");
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        die("eglMakeCurrent ES1");
    }

    *out_cfg = cfg;
    *out_ctx = ctx;
    *out_surf = surf;
    return dpy;
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
        gbm, &egl_cfg, &ctx, &surf, gbm_surf, req_major, req_minor,
        allow_gles1_1_fallback);

    (void)db_display_prepare_and_validate_gl_runtime(
        (db_gl_proc_resolver_fn_t)eglGetProcAddress, gl_renderer, backend,
        DB_DISPLAY_GL_RUNTIME_LOG_ENABLED, -1, NULL, NULL);

    const int viewport_width =
        db_checked_u32_to_i32(backend, "viewport_width", width);
    const int viewport_height =
        db_checked_u32_to_i32(backend, "viewport_height", height);
    db_gl_set_viewport_px(viewport_width, viewport_height);

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
        .renderer = renderer,
    };
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_gl_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_gl_fb);
    db_display_log_draw_stats_with_fn(backend, renderer->draw_stats);
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL), renderer_name,
                           backend, loop_result.frames, work_unit_count,
                           loop_result.elapsed_ms, capability_mode);

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

static struct fb *db_cpu_create_fb_from_framebuffer(
    struct gbm_device *gbm, int fd, const uint32_t *pixels_rgba8,
    const uint16_t *pixels_rgba16f, int use_hdr_float_bo, uint32_t width,
    uint32_t height) {
    uint32_t bo_flags = GBM_BO_USE_SCANOUT;
#ifdef GBM_BO_USE_WRITE
    bo_flags |= GBM_BO_USE_WRITE;
#else
    bo_flags |= GBM_BO_USE_RENDERING;
#endif
    struct gbm_bo *bo =
        gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888, bo_flags);
    if (bo == NULL) {
        diex("gbm_bo_create failed for CPU scanout buffer");
    }

    uint32_t map_stride_bytes = 0U;
    void *map_data = NULL;
    uint8_t *map_ptr =
        gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE,
                   &map_stride_bytes, &map_data);
    if ((map_ptr == NULL) || (map_data == NULL)) {
        gbm_bo_destroy(bo);
        diex("gbm_bo_map failed for CPU scanout buffer");
    }

    const size_t dst_stride_pixels =
        (size_t)map_stride_bytes / sizeof(uint32_t);
    if (use_hdr_float_bo != 0) {
        if (pixels_rgba16f == NULL) {
            gbm_bo_unmap(bo, map_data);
            gbm_bo_destroy(bo);
            diex("cpu hdr framebuffer is NULL");
        }
        db_convert_rgba16f_to_xrgb8888_rows((uint32_t *)map_ptr,
                                            dst_stride_pixels, pixels_rgba16f,
                                            (size_t)width * 4U, width, height);
    } else {
        if (pixels_rgba8 == NULL) {
            gbm_bo_unmap(bo, map_data);
            gbm_bo_destroy(bo);
            diex("cpu rgba8 framebuffer is NULL");
        }
        db_convert_rgba8_to_xrgb8888_rows((uint32_t *)map_ptr,
                                          dst_stride_pixels, pixels_rgba8,
                                          (size_t)width, width, height);
    }
    gbm_bo_unmap(bo, map_data);

    return fb_from_bo(fd, bo, 0);
}

static struct fb *db_kms_atomic_next_cpu_fb(void *user_ctx,
                                            uint32_t frame_index) {
    db_kms_atomic_cpu_frame_producer_t *producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    db_renderer_cpu_renderer_render_frame(frame_index);
    const int use_hdr_float_bo = db_renderer_cpu_renderer_is_hdr_float_bo();
    const uint32_t *pixels_rgba8 = NULL;
    const uint16_t *pixels_rgba16f = NULL;
    if (use_hdr_float_bo != 0) {
        pixels_rgba16f = db_renderer_cpu_renderer_pixels_rgba16f(NULL, NULL);
    } else {
        pixels_rgba8 = db_renderer_cpu_renderer_pixels_rgba8(NULL, NULL);
    }
    if (((use_hdr_float_bo != 0) && (pixels_rgba16f == NULL)) ||
        ((use_hdr_float_bo == 0) && (pixels_rgba8 == NULL))) {
        db_failf(producer->backend, "cpu renderer returned NULL framebuffer");
    }
    return db_cpu_create_fb_from_framebuffer(
        producer->gbm, producer->kms_fd, pixels_rgba8, pixels_rgba16f,
        use_hdr_float_bo, producer->width, producer->height);
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

    db_renderer_cpu_renderer_init();
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const db_display_runtime_config_t runtime_cfg =
        db_display_runtime_hash_config_from_cli(cfg, 0, 0).runtime;
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();

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
    };
    cur = db_kms_atomic_prime_first_frame_and_modeset(
        &kms, width, height, &producer, db_kms_atomic_next_cpu_fb);
    const db_kms_atomic_loop_run_result_t loop_result =
        db_kms_atomic_run_frame_loop_timed(&loop, &producer,
                                           db_kms_atomic_next_cpu_fb);
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU), renderer_name,
                           backend, loop_result.frames, work_unit_count,
                           loop_result.elapsed_ms, capability_mode);

    db_renderer_cpu_renderer_shutdown();
    fb_release(kms.fd, NULL, cur);
    db_kms_atomic_shutdown_core(&kms, gbm);
    return 0;
}
