// kms_atomic_egl_gl15_or_gles11.c
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>

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

#define BENCH_BANDS 16u
#define BENCH_FRAMES 600u
#define BACKEND_NAME "opengl"

typedef struct {
    GLfloat x, y;
    GLubyte r, g, b, a;
} V;

static void failf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s][error] ", BACKEND_NAME);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

static void die(const char *msg) { failf("%s: %s", msg, strerror(errno)); }
static void diex(const char *msg) { failf("%s", msg); }

static int has_ext(const char *exts, const char *needle) {
    if (!exts || !needle)
        return 0;
    const char *p = exts;
    size_t n = strlen(needle);
    while ((p = strstr(p, needle))) {
        // ensure token boundaries
        if ((p == exts || p[-1] == ' ') && (p[n] == '\0' || p[n] == ' '))
            return 1;
        p += n;
    }
    return 0;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fill_band_vertices(V *verts, uint32_t width, uint32_t height,
                               float t) {
    for (uint32_t b = 0; b < BENCH_BANDS; b++) {
        float x0 = (float)((width * b) / BENCH_BANDS);
        float x1 = (float)((width * (b + 1)) / BENCH_BANDS);
        float y0 = 0.0f;
        float y1 = (float)height;

        float pulse = 0.5f + 0.5f * sinf(t * 2.0f + (float)b * 0.3f);
        float rf = pulse * (0.2f + 0.8f * (float)b / (float)BENCH_BANDS);
        float gf = pulse * 0.6f;
        float bf = 1.0f - rf;

        GLubyte r = (GLubyte)(255.0f * rf);
        GLubyte g = (GLubyte)(255.0f * gf);
        GLubyte bl = (GLubyte)(255.0f * bf);
        GLubyte a = 255;

        uint32_t i = b * 6;
        verts[i + 0] = (V){x0, y0, r, g, bl, a};
        verts[i + 1] = (V){x1, y0, r, g, bl, a};
        verts[i + 2] = (V){x1, y1, r, g, bl, a};
        verts[i + 3] = (V){x0, y0, r, g, bl, a};
        verts[i + 4] = (V){x1, y1, r, g, bl, a};
        verts[i + 5] = (V){x0, y1, r, g, bl, a};
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
    if (!props)
        die("drmModeObjectGetProperties");

    uint32_t prop_id = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
            continue;
        if (strcmp(p->name, name) == 0) {
            prop_id = p->prop_id;
            drmModeFreeProperty(p);
            break;
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);

  if (!prop_id) {
    failf("Missing DRM property '%s' on object %u type %u", name, obj_id,
          obj_type);
  }
    return prop_id;
}

static drmModeConnector *pick_connected_connector(struct kms_atomic *k) {
    for (int i = 0; i < k->res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(k->fd, k->res->connectors[i]);
        if (!c)
            continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            return c;
        }
        drmModeFreeConnector(c);
    }
    return NULL;
}

static uint32_t pick_crtc_for_connector(struct kms_atomic *k,
                                        drmModeConnector *conn) {
    // Choose the first encoder+crtc that works.
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(k->fd, conn->encoders[i]);
        if (!enc)
            continue;

        for (int c = 0; c < k->res->count_crtcs; c++) {
            uint32_t crtc_id = k->res->crtcs[c];
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

static uint32_t pick_primary_plane_for_crtc(struct kms_atomic *k,
                                            uint32_t crtc_id) {
    // Need to map crtc_id -> crtc index (bit position)
    int crtc_index = -1;
    for (int i = 0; i < k->res->count_crtcs; i++) {
        if (k->res->crtcs[i] == crtc_id) {
            crtc_index = i;
            break;
        }
    }
    if (crtc_index < 0)
        return 0;

    for (uint32_t i = 0; i < k->pres->count_planes; i++) {
        uint32_t pid = k->pres->planes[i];
        drmModePlane *pl = drmModeGetPlane(k->fd, pid);
        if (!pl)
            continue;

        // Must be usable on our CRTC
        if (!(pl->possible_crtcs & (1 << crtc_index))) {
            drmModeFreePlane(pl);
            continue;
        }

        // Check plane type == "Primary"
        drmModeObjectProperties *props =
            drmModeObjectGetProperties(k->fd, pid, DRM_MODE_OBJECT_PLANE);
        if (!props)
            die("drmModeObjectGetProperties plane");

        int is_primary = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes *p = drmModeGetProperty(k->fd, props->props[j]);
            if (!p)
                continue;
            if (strcmp(p->name, "type") == 0 &&
                (p->flags & DRM_MODE_PROP_ENUM)) {
                // enum values: 0 Overlay, 1 Primary, 2 Cursor (common
                // convention)
                for (int e = 0; e < p->count_enums; e++) {
                    if (strcmp(p->enums[e].name, "Primary") == 0) {
                        uint64_t val = props->prop_values[j];
                        if (val == p->enums[e].value)
                            is_primary = 1;
                    }
                }
            }
            drmModeFreeProperty(p);
            if (is_primary)
                break;
        }
        drmModeFreeObjectProperties(props);

        drmModeFreePlane(pl);

        if (is_primary)
            return pid;
    }
    return 0;
}

static void kms_atomic_init(struct kms_atomic *k, const char *card) {
    memset(k, 0, sizeof(*k));

    k->fd = open(card, O_RDWR | O_CLOEXEC);
    if (k->fd < 0)
        die("open DRM card");

    if (drmSetClientCap(k->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
        die("drmSetClientCap UNIVERSAL_PLANES");
    if (drmSetClientCap(k->fd, DRM_CLIENT_CAP_ATOMIC, 1))
        die("drmSetClientCap ATOMIC");

    k->res = drmModeGetResources(k->fd);
    if (!k->res)
        die("drmModeGetResources");

    k->pres = drmModeGetPlaneResources(k->fd);
    if (!k->pres)
        die("drmModeGetPlaneResources");

    k->conn = pick_connected_connector(k);
    if (!k->conn)
        diex("No connected connector with modes");
    k->conn_id = k->conn->connector_id;
    k->mode = k->conn->modes[0]; // prefer first (often preferred)

    k->crtc_id = pick_crtc_for_connector(k, k->conn);
    if (!k->crtc_id)
        diex("No usable CRTC for connector");

    k->plane_id = pick_primary_plane_for_crtc(k, k->crtc_id);
    if (!k->plane_id)
        diex("No primary plane for chosen CRTC");

    // Create mode blob
    if (drmModeCreatePropertyBlob(k->fd, &k->mode, sizeof(k->mode),
                                  &k->mode_blob_id))
        die("drmModeCreatePropertyBlob");

    // Fetch prop IDs
    k->conn_prop_crtc_id =
        get_prop_id(k->fd, k->conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

    k->crtc_prop_mode_id =
        get_prop_id(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    k->crtc_prop_active =
        get_prop_id(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");

    k->plane_prop_fb_id =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    k->plane_prop_crtc_id =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    k->plane_prop_src_x =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    k->plane_prop_src_y =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    k->plane_prop_src_w =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    k->plane_prop_src_h =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    k->plane_prop_crtc_x =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    k->plane_prop_crtc_y =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    k->plane_prop_crtc_w =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    k->plane_prop_crtc_h =
        get_prop_id(k->fd, k->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
}

struct fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
};

static struct fb *fb_from_bo(int fd, struct gbm_bo *bo) {
    struct fb *fb = calloc(1, sizeof(*fb));
    fb->bo = bo;

    uint32_t w = gbm_bo_get_width(bo);
    uint32_t h = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t format = gbm_bo_get_format(bo);

    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t pitches[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    if (drmModeAddFB2(fd, w, h, format, handles, pitches, offsets, &fb->fb_id,
                      0))
        die("drmModeAddFB2");

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
    if (dpy == EGL_NO_DISPLAY)
        die("eglGetDisplay");

    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor))
        die("eglInitialize");

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
        EGLint n;
        if (eglChooseConfig(dpy, cfg_attribs_gl, &cfg, 1, &n) && n == 1) {
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
                if (surf != EGL_NO_SURFACE)
                    eglDestroySurface(dpy, surf);
                eglDestroyContext(dpy, ctx);
            }
        }
    }

    // ---- Fallback: OpenGL ES 1.x ----
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        die("eglBindAPI ES");

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
    EGLint n;
    if (!eglChooseConfig(dpy, cfg_attribs_es, &cfg, 1, &n) || n != 1)
        die("eglChooseConfig ES");

    const EGLint ctx_attribs_es1[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    EGLContext ctx =
        eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_es1);
    if (ctx == EGL_NO_CONTEXT)
        die("eglCreateContext ES1");

    EGLSurface surf =
        eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gbm_surf, NULL);
    if (surf == EGL_NO_SURFACE)
        die("eglCreateWindowSurface");

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
        die("eglMakeCurrent ES1");

    printf("GL_VERSION (EGL_OPENGL_ES_API): %s\n", glGetString(GL_VERSION));

    *out_cfg = cfg;
    *out_ctx = ctx;
    *out_surf = surf;
    *out_is_gl15 = 0;
    return dpy;
}

int main(int argc, char **argv) {
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

    struct kms_atomic k;
    kms_atomic_init(&k, card);

    uint32_t width = k.mode.hdisplay;
    uint32_t height = k.mode.vdisplay;

    // GBM
    struct gbm_device *gbm = gbm_create_device(k.fd);
    if (!gbm)
        die("gbm_create_device");

    struct gbm_surface *gbm_surf =
        gbm_surface_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf)
        die("gbm_surface_create");

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
    if (!is_gl15)
        printf("ES1 extensions include OES VBO? %s\n",
               has_vbo_es ? "yes" : "no");

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

    V verts[BENCH_BANDS * 6];
    fill_band_vertices(verts, width, height, 0.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(V), &verts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(V), &verts[0].r);

    drmEventContext ev = {0};
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_handler;

    struct fb *cur = NULL;

    // First render + lock front buffer
    glClearColor(0.04f, 0.04f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, (GLint)(BENCH_BANDS * 6));
    eglSwapBuffers(dpy, surf);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (!bo)
        diex("gbm_surface_lock_front_buffer failed");
    cur = fb_from_bo(k.fd, bo);

    // Atomic modeset: connector->crtc, crtc->mode+active, plane->fb+coords
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req)
        diex("drmModeAtomicAlloc");

    // connector CRTC_ID
    drmModeAtomicAddProperty(req, k.conn_id, k.conn_prop_crtc_id, k.crtc_id);
    // crtc MODE_ID + ACTIVE
    drmModeAtomicAddProperty(req, k.crtc_id, k.crtc_prop_mode_id,
                             k.mode_blob_id);
    drmModeAtomicAddProperty(req, k.crtc_id, k.crtc_prop_active, 1);

    // plane: FB_ID + CRTC_ID + src/crtc rects
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_fb_id, cur->fb_id);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_crtc_id, k.crtc_id);

    // src_* are 16.16 fixed point
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_src_x, 0);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_src_y, 0);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_src_w,
                             ((uint64_t)width) << 16);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_src_h,
                             ((uint64_t)height) << 16);

    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_crtc_x, 0);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_crtc_y, 0);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_crtc_w, width);
    drmModeAtomicAddProperty(req, k.plane_id, k.plane_prop_crtc_h, height);

    // Set allow modeset for initial commit
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    if (drmModeAtomicCommit(k.fd, req, flags, NULL))
        die("drmModeAtomicCommit modeset");
    drmModeAtomicFree(req);

    // Loop: render new frame, swap, atomic pageflip by changing plane FB_ID.
    uint64_t bench_start = now_ns();
    uint32_t bench_frames = 0;
    for (uint32_t frame = 0; frame < BENCH_FRAMES; frame++) {
        float t = (float)frame / 60.0f;
        fill_band_vertices(verts, width, height, t);

        glClearColor(0.04f, 0.04f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, (GLint)(BENCH_BANDS * 6));
        eglSwapBuffers(dpy, surf);

        struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(gbm_surf);
        if (!next_bo)
            diex("lock_front_buffer failed");
        struct fb *next = fb_from_bo(k.fd, next_bo);

        drmModeAtomicReq *r = drmModeAtomicAlloc();
        if (!r)
            diex("drmModeAtomicAlloc");
        drmModeAtomicAddProperty(r, k.plane_id, k.plane_prop_fb_id,
                                 next->fb_id);

        int waiting = 1;
        // Request page flip event; NONBLOCK is optional but common
        uint32_t f = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
        if (drmModeAtomicCommit(k.fd, r, f, &waiting))
            die("drmModeAtomicCommit flip");
        drmModeAtomicFree(r);

        while (waiting) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(k.fd, &fds);
            if (select(k.fd + 1, &fds, NULL, NULL, NULL) < 0)
                die("select");
            drmHandleEvent(k.fd, &ev);
        }

        // Release previous FB/BO
        gbm_surface_release_buffer(gbm_surf, cur->bo);
        drmModeRmFB(k.fd, cur->fb_id);
        free(cur);
        cur = next;
        bench_frames++;
    }

    uint64_t bench_end = now_ns();
    double bench_ms = (double)(bench_end - bench_start) / 1e6;
    if (bench_frames > 0) {
        double ms_per_frame = bench_ms / (double)bench_frames;
        double fps = 1000.0 / ms_per_frame;
        printf("OpenGL benchmark: frames=%u bands=%u total_ms=%.2f "
               "ms_per_frame=%.3f fps=%.2f\n",
               bench_frames, BENCH_BANDS, bench_ms, ms_per_frame, fps);
    }

    // Cleanup current
    if (cur) {
        gbm_surface_release_buffer(gbm_surf, cur->bo);
        drmModeRmFB(k.fd, cur->fb_id);
        free(cur);
    }

    // Destroy blob + resources
    drmModeDestroyPropertyBlob(k.fd, k.mode_blob_id);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);

    drmModeFreeConnector(k.conn);
    drmModeFreePlaneResources(k.pres);
    drmModeFreeResources(k.res);
    close(k.fd);

    return 0;
}
