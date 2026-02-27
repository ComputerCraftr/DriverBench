#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)

#include "display_linux_kms_atomic_common.h"

#include <EGL/egl.h>
#include <EGL/eglplatform.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef DB_HAS_OPENGL_DESKTOP
#include <GL/gl.h>
#else
#include <GLES/gl.h>
#endif

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
#include "../../driverbench_cli.h"
#include "../../renderers/cpu_renderer/renderer_cpu_renderer.h"
#include "../../renderers/renderer_gl_common.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"

#define DRM_SRC_FP_SHIFT 16U
#define LOG_MSG_CAPACITY 2048U
#define BACKEND_NAME "display_linux_kms_atomic_common"

static const char *g_active_backend = BACKEND_NAME;

static db_gl_generic_proc_t db_kms_egl_resolve_proc(const char *name) {
    return (db_gl_generic_proc_t)eglGetProcAddress(name);
}

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
    while (waiting != 0) {
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

static uint64_t
db_kms_atomic_run_frame_loop(const db_kms_atomic_frame_loop_t *loop,
                             void *producer_ctx,
                             db_kms_atomic_next_fb_fn_t next_fb_fn) {
    const uint64_t bench_start = db_now_ns_monotonic();
    uint64_t bench_frames = 0U;
    double next_progress_log_due_ms = 0.0;
    while (!db_should_stop()) {
        if ((loop->frame_limit > 0U) && (bench_frames >= loop->frame_limit)) {
            break;
        }
        const uint64_t frame_start_ns = db_now_ns_monotonic();
        struct fb *next = next_fb_fn(producer_ctx, bench_frames);
        db_kms_atomic_flip_to_fb(loop->kms, next->fb_id, loop->event_context);
        fb_release(loop->kms->fd, loop->release_surface, *loop->cur_fb);
        *loop->cur_fb = next;
        bench_frames++;

        const double bench_ms =
            (double)(db_now_ns_monotonic() - bench_start) / DB_NS_PER_MS_D;
        db_benchmark_log_periodic(
            db_dispatch_api_name(loop->api), loop->renderer_name, loop->backend,
            bench_frames, loop->work_unit_count, bench_ms,
            loop->capability_mode, &next_progress_log_due_ms,
            BENCH_LOG_INTERVAL_MS_D);
        db_sleep_to_fps_cap(loop->backend, frame_start_ns, loop->fps_cap);
    }
    return bench_frames;
}

typedef struct {
    int kms_fd;
    EGLDisplay dpy;
    EGLSurface surf;
    struct gbm_surface *gbm_surf;
    const db_kms_atomic_renderer_vtable_t *renderer;
} db_kms_atomic_gl_frame_producer_t;

static struct fb *db_kms_atomic_next_gl_fb(void *user_ctx,
                                           uint32_t frame_index) {
    db_kms_atomic_gl_frame_producer_t *producer =
        (db_kms_atomic_gl_frame_producer_t *)user_ctx;
    glClearColor(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                 BENCH_CLEAR_COLOR_B_F, BENCH_CLEAR_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);
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
                    const char *ver = (const char *)glGetString(GL_VERSION);
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
                      const char *card,
                      db_kms_atomic_context_profile_t context_profile,
                      const db_kms_atomic_renderer_vtable_t *renderer,
                      db_kms_atomic_runtime_check_fn_t runtime_check,
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
    kms_atomic_init(&kms, card);

    const uint32_t width = kms.mode.hdisplay;
    const uint32_t height = kms.mode.vdisplay;

    struct gbm_device *gbm = gbm_create_device(kms.fd);
    if (gbm == NULL) {
        die("gbm_create_device");
    }

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

    const char *runtime_version = (const char *)glGetString(GL_VERSION);
    const char *runtime_renderer = (const char *)glGetString(GL_RENDERER);
    const int runtime_is_gles = db_display_log_gl_runtime_api(
        backend, runtime_version, runtime_renderer);
    if (runtime_check != NULL) {
        runtime_check(backend, runtime_version, runtime_is_gles);
    }
    db_gl_set_proc_resolver(db_kms_egl_resolve_proc);
    db_gl_preload_upload_proc_table();

    glViewport(
        0, 0, (GLint)db_checked_u32_to_i32(backend, "viewport_width", width),
        (GLint)db_checked_u32_to_i32(backend, "viewport_height", height));

    renderer->init();
    const char *capability_mode = renderer->capability_mode();
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const uint32_t work_unit_count = renderer->work_unit_count();

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    struct fb *cur = NULL;

    glClearColor(BENCH_CLEAR_COLOR_R_F, BENCH_CLEAR_COLOR_G_F,
                 BENCH_CLEAR_COLOR_B_F, BENCH_CLEAR_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);
    renderer->render_frame(0);
    eglSwapBuffers(dpy, surf);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (bo == NULL) {
        diex("gbm_surface_lock_front_buffer failed");
    }
    cur = fb_from_bo(kms.fd, bo, 1);

    db_kms_atomic_commit_modeset(&kms, width, height, cur->fb_id);

    const db_kms_atomic_frame_loop_t loop = {
        .api = DB_API_OPENGL,
        .backend = backend,
        .renderer_name = renderer_name,
        .capability_mode = capability_mode,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
        .work_unit_count = work_unit_count,
        .kms = &kms,
        .release_surface = gbm_surf,
        .event_context = &ev,
        .cur_fb = &cur,
    };
    db_kms_atomic_gl_frame_producer_t producer = {
        .kms_fd = kms.fd,
        .dpy = dpy,
        .surf = surf,
        .gbm_surf = gbm_surf,
        .renderer = renderer,
    };
    const uint64_t bench_start = db_now_ns_monotonic();
    const uint64_t bench_frames = db_kms_atomic_run_frame_loop(
        &loop, &producer, db_kms_atomic_next_gl_fb);
    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start) / DB_NS_PER_MS_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_OPENGL), renderer_name,
                           backend, bench_frames, work_unit_count, bench_ms,
                           capability_mode);

    renderer->shutdown();

    fb_release(kms.fd, gbm_surf, cur);

    drmModeDestroyPropertyBlob(kms.fd, kms.mode_blob_id);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);

    drmModeFreeConnector(kms.conn);
    drmModeFreePlaneResources(kms.pres);
    drmModeFreeResources(kms.res);
    close(kms.fd);

    return 0;
}

static struct fb *db_cpu_create_fb_from_rgba8(struct gbm_device *gbm, int fd,
                                              const uint32_t *pixels_rgba8,
                                              uint32_t width, uint32_t height) {
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
    db_convert_rgba8_to_xrgb8888_rows((uint32_t *)map_ptr, dst_stride_pixels,
                                      pixels_rgba8, (size_t)width, width,
                                      height);
    gbm_bo_unmap(bo, map_data);

    return fb_from_bo(fd, bo, 0);
}

typedef struct {
    int kms_fd;
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    const char *backend;
} db_kms_atomic_cpu_frame_producer_t;

static struct fb *db_kms_atomic_next_cpu_fb(void *user_ctx,
                                            uint32_t frame_index) {
    db_kms_atomic_cpu_frame_producer_t *producer =
        (db_kms_atomic_cpu_frame_producer_t *)user_ctx;
    db_renderer_cpu_renderer_render_frame(frame_index);
    const uint32_t *pixels = db_renderer_cpu_renderer_pixels_rgba8(NULL, NULL);
    if (pixels == NULL) {
        db_failf(producer->backend, "cpu renderer returned NULL framebuffer");
    }
    return db_cpu_create_fb_from_rgba8(producer->gbm, producer->kms_fd, pixels,
                                       producer->width, producer->height);
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
    kms_atomic_init(&kms, card);
    const uint32_t width = kms.mode.hdisplay;
    const uint32_t height = kms.mode.vdisplay;

    struct gbm_device *gbm = gbm_create_device(kms.fd);
    if (gbm == NULL) {
        die("gbm_create_device");
    }

    db_renderer_cpu_renderer_init();
    const char *capability_mode = db_renderer_cpu_renderer_capability_mode();
    const double fps_cap = (cfg != NULL) ? cfg->fps_cap : BENCH_FPS_CAP_D;
    const uint32_t frame_limit = (cfg != NULL) ? cfg->frame_limit : 0U;
    const uint32_t work_unit_count = db_renderer_cpu_renderer_work_unit_count();

    db_renderer_cpu_renderer_render_frame(0);
    const uint32_t *initial_pixels =
        db_renderer_cpu_renderer_pixels_rgba8(NULL, NULL);
    if (initial_pixels == NULL) {
        db_failf(backend, "cpu renderer returned NULL framebuffer");
    }
    struct fb *cur =
        db_cpu_create_fb_from_rgba8(gbm, kms.fd, initial_pixels, width, height);

    db_kms_atomic_commit_modeset(&kms, width, height, cur->fb_id);

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    const db_kms_atomic_frame_loop_t loop = {
        .api = DB_API_CPU,
        .backend = backend,
        .renderer_name = renderer_name,
        .capability_mode = capability_mode,
        .fps_cap = fps_cap,
        .frame_limit = frame_limit,
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
    const uint64_t bench_start = db_now_ns_monotonic();
    const uint64_t bench_frames = db_kms_atomic_run_frame_loop(
        &loop, &producer, db_kms_atomic_next_cpu_fb);
    const double bench_ms =
        (double)(db_now_ns_monotonic() - bench_start) / DB_NS_PER_MS_D;
    db_benchmark_log_final(db_dispatch_api_name(DB_API_CPU), renderer_name,
                           backend, bench_frames, work_unit_count, bench_ms,
                           capability_mode);

    db_renderer_cpu_renderer_shutdown();
    fb_release(kms.fd, NULL, cur);

    drmModeDestroyPropertyBlob(kms.fd, kms.mode_blob_id);
    gbm_device_destroy(gbm);
    drmModeFreeConnector(kms.conn);
    drmModeFreePlaneResources(kms.pres);
    drmModeFreeResources(kms.res);
    close(kms.fd);
    return 0;
}
