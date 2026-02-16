// kms_atomic_egl_gl15_or_gles11.c
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier)
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../bench_config.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

// We will include both headers but only use fixed-function style calls.
// On many systems <GL/gl.h> isn't compatible with EGL+GBM unless you link
// correctly. We'll use GLES 1.x header for common fixed-function entry points.
// For desktop GL 1.5, those functions still exist; we just bind the right API
// in EGL.
#include <GLES/gl.h>

#define BACKEND_NAME "display_linux_kms_atomic_opengl_gl1_5_gles1_1"
#define RENDERER_NAME "renderer_opengl_gl1_5_gles1_1"
#define NS_PER_SECOND_U64 1000000000ULL
#define USCALE_255_F 255.0F
#define BG_COLOR_R_F 0.04F
#define BG_COLOR_G_F 0.04F
#define BG_COLOR_B_F 0.07F
#define BG_COLOR_A_F 1.0F
#define TRI_VERTS_PER_BAND 6U
#define DRM_SRC_FP_SHIFT 16U
#define NS_TO_MS_D 1e6
#define MS_TO_S_D 1000.0
#define LOG_MSG_CAPACITY 2048U
#define VERT_IDX_0 0U
#define VERT_IDX_1 1U
#define VERT_IDX_2 2U
#define VERT_IDX_3 3U
#define VERT_IDX_4 4U
#define VERT_IDX_5 5U

typedef struct {
    GLfloat x, y;
    GLubyte r, g, b, a;
} V;

static __attribute__((noreturn)) void failf(const char *fmt, ...) {
    char message[LOG_MSG_CAPACITY];
    va_list ap;
    va_start(ap, fmt);
#ifdef __STDC_LIB_EXT1__
    (void)vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, ap);
#else
    // Fallback for platforms without Annex K bounds-checked APIs.
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    (void)vsnprintf(message, sizeof(message), fmt, ap);
#endif
    va_end(ap);
    fputs("[", stderr);
    fputs(BACKEND_NAME, stderr);
    fputs("][error] ", stderr);
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void die(const char *msg) { failf("%s: %s", msg, strerror(errno)); }
static void diex(const char *msg) { failf("%s", msg); }

static int has_ext(const char *exts, const char *needle) {
    if (!exts || !needle) {
        return 0;
    }
    const char *ext_ptr = exts;
    size_t needle_len = strlen(needle);
    while ((ext_ptr = strstr(ext_ptr, needle))) {
        // ensure token boundaries
        if ((ext_ptr == exts || ext_ptr[-1] == ' ') &&
            (ext_ptr[needle_len] == '\0' || ext_ptr[needle_len] == ' ')) {
            return 1;
        }
        ext_ptr += needle_len;
    }
    return 0;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * NS_PER_SECOND_U64) + (uint64_t)ts.tv_nsec;
}

static void fill_band_vertices(V *verts, uint32_t width, uint32_t height,
                               float time_s) {
    for (uint32_t b = 0; b < BENCH_BANDS; b++) {
        float x0 = ((float)width * (float)b) / (float)BENCH_BANDS;
        float x1 = ((float)width * (float)(b + 1)) / (float)BENCH_BANDS;
        float y0 = 0.0F;
        float y1 = (float)height;

        float pulse =
            BENCH_PULSE_BASE_F +
            (BENCH_PULSE_AMP_F * sinf((time_s * BENCH_PULSE_FREQ_F) +
                                      ((float)b * BENCH_PULSE_PHASE_F)));
        float rf =
            pulse * (BENCH_COLOR_R_BASE_F +
                     BENCH_COLOR_R_SCALE_F * (float)b / (float)BENCH_BANDS);
        float gf = pulse * BENCH_COLOR_G_SCALE_F;
        float bf = 1.0F - rf;

        GLubyte red_u8 = (GLubyte)(USCALE_255_F * rf);
        GLubyte green_u8 = (GLubyte)(USCALE_255_F * gf);
        GLubyte blue_u8 = (GLubyte)(USCALE_255_F * bf);
        GLubyte alpha_u8 = 255;

        uint32_t vert_index = b * TRI_VERTS_PER_BAND;
        verts[vert_index + VERT_IDX_0] =
            (V){x0, y0, red_u8, green_u8, blue_u8, alpha_u8};
        verts[vert_index + VERT_IDX_1] =
            (V){x1, y0, red_u8, green_u8, blue_u8, alpha_u8};
        verts[vert_index + VERT_IDX_2] =
            (V){x1, y1, red_u8, green_u8, blue_u8, alpha_u8};
        verts[vert_index + VERT_IDX_3] =
            (V){x0, y0, red_u8, green_u8, blue_u8, alpha_u8};
        verts[vert_index + VERT_IDX_4] =
            (V){x1, y1, red_u8, green_u8, blue_u8, alpha_u8};
        verts[vert_index + VERT_IDX_5] =
            (V){x0, y1, red_u8, green_u8, blue_u8, alpha_u8};
    }
}

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

    // property ids
    uint32_t conn_prop_crtc_id;

    uint32_t crtc_prop_mode_id;
    uint32_t crtc_prop_active;

    uint32_t plane_prop_fb_id;
    uint32_t plane_prop_crtc_id;
    uint32_t plane_prop_src_x, plane_prop_src_y, plane_prop_src_w,
        plane_prop_src_h;
    uint32_t plane_prop_crtc_x, plane_prop_crtc_y, plane_prop_crtc_w,
        plane_prop_crtc_h;
};

static uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type,
                            const char *name) {
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) {
        die("drmModeObjectGetProperties");
    }

    uint32_t prop_id = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        if (!prop) {
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

    if (!prop_id) {
        failf("Missing DRM property '%s' on object %u type %u", name, obj_id,
              obj_type);
    }
    return prop_id;
}

static drmModeConnector *pick_connected_connector(struct kms_atomic *kms) {
    for (int i = 0; i < kms->res->count_connectors; i++) {
        drmModeConnector *connector =
            drmModeGetConnector(kms->fd, kms->res->connectors[i]);
        if (!connector) {
            continue;
        }
        if (connector->connection == DRM_MODE_CONNECTED &&
            connector->count_modes > 0) {
            return connector;
        }
        drmModeFreeConnector(connector);
    }
    return NULL;
}

static uint32_t pick_crtc_for_connector(struct kms_atomic *kms,
                                        drmModeConnector *conn) {
    // Choose the first encoder+crtc that works.
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(kms->fd, conn->encoders[i]);
        if (!enc) {
            continue;
        }

        for (int c = 0; c < kms->res->count_crtcs; c++) {
            uint32_t crtc_id = kms->res->crtcs[c];
            // Check if encoder can use this CRTC via possible_crtcs bitmask
            if (enc->possible_crtcs & (1 << c)) {
                drmModeFreeEncoder(enc);
                return crtc_id;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return 0;
}

static uint32_t pick_primary_plane_for_crtc(struct kms_atomic *kms,
                                            uint32_t crtc_id) {
    // Need to map crtc_id -> crtc index (bit position)
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
        uint32_t pid = kms->pres->planes[i];
        drmModePlane *pl = drmModeGetPlane(kms->fd, pid);
        if (!pl) {
            continue;
        }

        // Must be usable on our CRTC
        if (!(pl->possible_crtcs & (1 << crtc_index))) {
            drmModeFreePlane(pl);
            continue;
        }

        // Check plane type == "Primary"
        drmModeObjectProperties *props =
            drmModeObjectGetProperties(kms->fd, pid, DRM_MODE_OBJECT_PLANE);
        if (!props) {
            die("drmModeObjectGetProperties plane");
        }

        int is_primary = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes *prop =
                drmModeGetProperty(kms->fd, props->props[j]);
            if (!prop) {
                continue;
            }
            if (strcmp(prop->name, "type") == 0 &&
                (prop->flags & DRM_MODE_PROP_ENUM)) {
                // enum values: 0 Overlay, 1 Primary, 2 Cursor (common
                // convention)
                for (int e = 0; e < prop->count_enums; e++) {
                    if (strcmp(prop->enums[e].name, "Primary") == 0) {
                        uint64_t val = props->prop_values[j];
                        if (val == prop->enums[e].value) {
                            is_primary = 1;
                        }
                    }
                }
            }
            drmModeFreeProperty(prop);
            if (is_primary) {
                break;
            }
        }
        drmModeFreeObjectProperties(props);

        drmModeFreePlane(pl);

        if (is_primary) {
            return pid;
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

    if (drmSetClientCap(kms->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
        die("drmSetClientCap UNIVERSAL_PLANES");
    }
    if (drmSetClientCap(kms->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
        die("drmSetClientCap ATOMIC");
    }

    kms->res = drmModeGetResources(kms->fd);
    if (!kms->res) {
        die("drmModeGetResources");
    }

    kms->pres = drmModeGetPlaneResources(kms->fd);
    if (!kms->pres) {
        die("drmModeGetPlaneResources");
    }

    kms->conn = pick_connected_connector(kms);
    if (!kms->conn) {
        diex("No connected connector with modes");
    }
    kms->conn_id = kms->conn->connector_id;
    kms->mode = kms->conn->modes[0]; // prefer first (often preferred)

    kms->crtc_id = pick_crtc_for_connector(kms, kms->conn);
    if (!kms->crtc_id) {
        diex("No usable CRTC for connector");
    }

    kms->plane_id = pick_primary_plane_for_crtc(kms, kms->crtc_id);
    if (!kms->plane_id) {
        diex("No primary plane for chosen CRTC");
    }

    // Create mode blob
    if (drmModeCreatePropertyBlob(kms->fd, &kms->mode, sizeof(kms->mode),
                                  &kms->mode_blob_id)) {
        die("drmModeCreatePropertyBlob");
    }

    // Fetch prop IDs
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

struct fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
};

static struct fb *fb_from_bo(int fd, struct gbm_bo *bo) {
    struct fb *fb = calloc(1, sizeof(*fb));
    fb->bo = bo;

    uint32_t width_px = gbm_bo_get_width(bo);
    uint32_t height_px = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t format = gbm_bo_get_format(bo);

    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t pitches[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(fd, width_px, height_px, format, handles, pitches,
                      offsets, &fb->fb_id, 0)) {
        die("drmModeAddFB2");
    }

    return fb;
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

static EGLDisplay
egl_init_try_gl15_then_gles11(struct gbm_device *gbm, EGLConfig *out_cfg,
                              EGLContext *out_ctx, EGLSurface *out_surf,
                              struct gbm_surface *gbm_surf, int *out_is_gl15) {
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) {
        die("eglGetDisplay");
    }

    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor)) {
        die("eglInitialize");
    }

    // Common config: window surface, 8888, and renderable type will vary.
    // We'll attempt GL first, then ES1.
    const EGLint base_cfg[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE,     8,
                               EGL_GREEN_SIZE,   8,
                               EGL_BLUE_SIZE,    8,
                               EGL_NONE};

    // ---- Attempt desktop OpenGL (aiming for 1.5) ----
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
            config_count == 1) {
            EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
            if (ctx != EGL_NO_CONTEXT) {
                EGLSurface surf = eglCreateWindowSurface(
                    dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
                if (surf != EGL_NO_SURFACE &&
                    eglMakeCurrent(dpy, surf, surf, ctx)) {
                    // Check version string contains at least "1.5" (or higher
                    // compatibility)
                    const char *ver = (const char *)glGetString(GL_VERSION);
                    if (ver) {
                        printf("GL_VERSION (EGL_OPENGL_API): %s\n", ver);
                        *out_cfg = cfg;
                        *out_ctx = ctx;
                        *out_surf = surf;
                        *out_is_gl15 = 1;
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

    // ---- Fallback: OpenGL ES 1.x ----
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
        config_count != 1) {
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

    printf("GL_VERSION (EGL_OPENGL_ES_API): %s\n", glGetString(GL_VERSION));

    *out_cfg = cfg;
    *out_ctx = ctx;
    *out_surf = surf;
    *out_is_gl15 = 0;
    return dpy;
}

int main(int argc, char **argv) {
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

    struct kms_atomic kms;
    kms_atomic_init(&kms, card);

    uint32_t width = kms.mode.hdisplay;
    uint32_t height = kms.mode.vdisplay;

    // GBM
    struct gbm_device *gbm = gbm_create_device(kms.fd);
    if (!gbm) {
        die("gbm_create_device");
    }

    struct gbm_surface *gbm_surf =
        gbm_surface_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf) {
        die("gbm_surface_create");
    }

    // EGL: try GL first then ES1
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface surf;
    int is_gl15 = 0;
    EGLDisplay dpy = egl_init_try_gl15_then_gles11(gbm, &cfg, &ctx, &surf,
                                                   gbm_surf, &is_gl15);

    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    int has_vbo_es = has_ext(exts, "GL_OES_vertex_buffer_object");
    if (!is_gl15) {
        printf("ES1 extensions include OES VBO? %s\n",
               has_vbo_es ? "yes" : "no");
    }

    // Fixed-function 2D
    glViewport(0, 0, (GLint)width, (GLint)height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, (GLfloat)width, (GLfloat)height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    V verts[BENCH_BANDS * TRI_VERTS_PER_BAND];
    fill_band_vertices(verts, width, height, 0.0F);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(V), &verts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(V), &verts[0].r);

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    struct fb *cur = NULL;

    // First render + lock front buffer
    glClearColor(BG_COLOR_R_F, BG_COLOR_G_F, BG_COLOR_B_F, BG_COLOR_A_F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, (GLint)(BENCH_BANDS * TRI_VERTS_PER_BAND));
    eglSwapBuffers(dpy, surf);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (!bo) {
        diex("gbm_surface_lock_front_buffer failed");
    }
    cur = fb_from_bo(kms.fd, bo);

    // Atomic modeset: connector->crtc, crtc->mode+active, plane->fb+coords
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) {
        diex("drmModeAtomicAlloc");
    }

    // connector CRTC_ID
    drmModeAtomicAddProperty(req, kms.conn_id, kms.conn_prop_crtc_id,
                             kms.crtc_id);
    // crtc MODE_ID + ACTIVE
    drmModeAtomicAddProperty(req, kms.crtc_id, kms.crtc_prop_mode_id,
                             kms.mode_blob_id);
    drmModeAtomicAddProperty(req, kms.crtc_id, kms.crtc_prop_active, 1);

    // plane: FB_ID + CRTC_ID + src/crtc rects
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_fb_id,
                             cur->fb_id);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_crtc_id,
                             kms.crtc_id);

    // src_* are 16.16 fixed point
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_src_x, 0);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_src_y, 0);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_src_w,
                             ((uint64_t)width) << DRM_SRC_FP_SHIFT);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_src_h,
                             ((uint64_t)height) << DRM_SRC_FP_SHIFT);

    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_crtc_x, 0);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_crtc_y, 0);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_crtc_w, width);
    drmModeAtomicAddProperty(req, kms.plane_id, kms.plane_prop_crtc_h, height);

    // Set allow modeset for initial commit
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    if (drmModeAtomicCommit(kms.fd, req, flags, NULL)) {
        die("drmModeAtomicCommit modeset");
    }
    drmModeAtomicFree(req);

    // Loop: render new frame, swap, atomic pageflip by changing plane FB_ID.
    uint64_t bench_start = now_ns();
    uint32_t bench_frames = 0;
    for (uint32_t frame = 0; frame < BENCH_FRAMES; frame++) {
        float time_s = (float)frame / BENCH_TARGET_FPS_F;
        fill_band_vertices(verts, width, height, time_s);

        glClearColor(BG_COLOR_R_F, BG_COLOR_G_F, BG_COLOR_B_F, BG_COLOR_A_F);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0,
                     (GLint)(BENCH_BANDS * TRI_VERTS_PER_BAND));
        eglSwapBuffers(dpy, surf);

        struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(gbm_surf);
        if (!next_bo) {
            diex("lock_front_buffer failed");
        }
        struct fb *next = fb_from_bo(kms.fd, next_bo);

        drmModeAtomicReq *commit_req = drmModeAtomicAlloc();
        if (!commit_req) {
            diex("drmModeAtomicAlloc");
        }
        drmModeAtomicAddProperty(commit_req, kms.plane_id, kms.plane_prop_fb_id,
                                 next->fb_id);

        int waiting = 1;
        // Request page flip event; NONBLOCK is optional but common
        uint32_t flip_flags =
            DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
        if (drmModeAtomicCommit(kms.fd, commit_req, flip_flags, &waiting)) {
            die("drmModeAtomicCommit flip");
        }
        drmModeAtomicFree(commit_req);

        while (waiting) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(kms.fd, &fds);
            if (select(kms.fd + 1, &fds, NULL, NULL, NULL) < 0) {
                die("select");
            }
            drmHandleEvent(kms.fd, &ev);
        }

        // Release previous FB/BO
        gbm_surface_release_buffer(gbm_surf, cur->bo);
        drmModeRmFB(kms.fd, cur->fb_id);
        free(cur);
        cur = next;
        bench_frames++;
    }

    uint64_t bench_end = now_ns();
    double bench_ms = (double)(bench_end - bench_start) / NS_TO_MS_D;
    if (bench_frames > 0) {
        double ms_per_frame = bench_ms / (double)bench_frames;
        double fps = MS_TO_S_D / ms_per_frame;
        printf("OpenGL benchmark: renderer=%s backend=%s frames=%u bands=%u "
               "total_ms=%.2f ms_per_frame=%.3f fps=%.2f\n",
               RENDERER_NAME, BACKEND_NAME, bench_frames, BENCH_BANDS, bench_ms,
               ms_per_frame, fps);
    }

    // Cleanup current
    if (cur) {
        gbm_surface_release_buffer(gbm_surf, cur->bo);
        drmModeRmFB(kms.fd, cur->fb_id);
        free(cur);
    }

    // Destroy blob + resources
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
