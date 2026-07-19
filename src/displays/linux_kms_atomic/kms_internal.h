#ifndef DRIVERBENCH_KMS_INTERNAL_H
#define DRIVERBENCH_KMS_INTERNAL_H

#include "kms_hdr.h"
#include "kms_runner.h"

#include "../display_frame_loop_common.h"
#include "../display_presentation_policy.h"
#include "core/db_geometry.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>
#include <stdint.h>

#include "../../core/db_run_session.h"

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
    uint32_t conn_prop_colorspace;
    uint32_t conn_prop_hdr_metadata;
    uint32_t conn_prop_max_bpc;
    uint64_t conn_colorspace_bt2020_rgb;
    uint64_t conn_initial_colorspace;
    uint64_t conn_initial_max_bpc;
    uint64_t conn_max_bpc_supported;
    uint32_t hdr_metadata_blob_id;
    int hdr_enabled;
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

enum { DB_KMS_CPU_SCANOUT_SLOT_COUNT = 2U };
enum { DB_KMS_HDR_BIT_DEPTH = 10U };

typedef struct {
    struct fb *fb;
    uint64_t last_present_serial;
    uint64_t generation;
    int valid;
} db_kms_cpu_scanout_slot_t;

typedef struct {
    PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC swap_buffers_with_damage;
    db_presentation_damage_history_t damage_history;
    db_grid_block_t logical_damage[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    db_damage_block_t pixel_damage[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    db_presentation_buffer_age_t last_age;
    int buffer_age_supported;
    int swap_damage_supported;
    int last_age_valid;
} db_kms_egl_presentation_t;

typedef db_display_frame_loop_result_t (*db_kms_atomic_frame_fn_t)(
    void *user_ctx, uint32_t frame_index);

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
    int release_previous_framebuffer;
} db_kms_atomic_frame_loop_t;

typedef struct {
    db_run_session_t *session;
    const db_kms_atomic_frame_loop_t *loop;
    struct fb *pending_fb;
    uint64_t generation;
    int initial_modeset;
} db_kms_frame_transaction_t;

typedef struct {
    const char *backend;
    db_gl_renderer_t gl_renderer;
    int debug_clear_default_framebuffer;
    int kms_fd;
    EGLDisplay dpy;
    EGLSurface surf;
    struct gbm_surface *gbm_surf;
    const db_display_renderer_runtime_t *resolved_runtime;
    const db_kms_atomic_renderer_vtable_t *renderer;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t destination_width;
    uint32_t destination_height;
    db_kms_egl_presentation_t presentation;
    db_kms_egl_presentation_t pending_presentation;
    db_kms_frame_transaction_t transaction;
    int pending_presentation_valid;
} db_kms_atomic_gl_frame_producer_t;

typedef struct {
    int kms_fd;
    struct gbm_device *gbm;
    uint32_t width;
    uint32_t height;
    const char *backend;
    db_pixel_surface_t surface;
    const db_display_renderer_runtime_t *resolved_runtime;
    db_native_output_format_t native_output_format;
    db_kms_cpu_scanout_slot_t slots[DB_KMS_CPU_SCANOUT_SLOT_COUNT];
    db_presentation_damage_history_t damage_history;
    db_presentation_damage_history_t pending_damage_history;
    db_grid_block_t logical_damage[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    db_damage_block_t pixel_damage[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME];
    uint32_t next_slot;
    uint32_t pending_slot;
    uint64_t present_serial;
    int pending_slot_valid;
    int pending_damage_history_valid;
    db_kms_frame_transaction_t transaction;
} db_kms_atomic_cpu_frame_producer_t;

void db_kms_atomic_cpu_scanout_shutdown(
    db_kms_atomic_cpu_frame_producer_t *producer);

uint32_t db_kms_atomic_gbm_format_or_fail(const char *backend,
                                          db_native_output_format_t format);

typedef struct {
    uint64_t frames;
    double elapsed_ms;
} db_kms_atomic_loop_run_result_t;

typedef struct {
    const db_kms_atomic_frame_loop_t *loop;
    void *producer_ctx;
    db_kms_atomic_frame_fn_t frame_fn;
    double *next_progress_log_due_ms;
} db_kms_atomic_shared_loop_ctx_t;

void runtime_failf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2), noreturn));
void runtime_errno_fail(const char *message) __attribute__((noreturn));
drmModePropertyRes *db_kms_find_object_property(int fd, uint32_t object_id,
                                                uint32_t object_type,
                                                const char *name,
                                                uint64_t *out_value);
void page_flip_handler(int fd, unsigned frame, unsigned sec, unsigned usec,
                       void *data);
void db_kms_atomic_flip_to_fb(const struct kms_atomic *kms, uint32_t fb_id,
                              drmEventContext *event_context);
void db_kms_atomic_init_core(const char *card, struct kms_atomic *kms,
                             struct gbm_device **out_gbm, uint32_t *out_width,
                             uint32_t *out_height);
db_native_output_capability_t
db_kms_atomic_query_hdr_capability(const struct kms_atomic *kms);
db_native_output_capability_t
db_kms_atomic_verify_hdr_capability(struct kms_atomic *kms,
                                    struct gbm_device *gbm, uint32_t width,
                                    uint32_t height);
void db_kms_atomic_set_hdr_enabled(struct kms_atomic *kms, int enabled);
int db_kms_egl_hdr10_desktop_gl_supported(const char *backend,
                                          struct gbm_device *gbm,
                                          uint32_t width, uint32_t height,
                                          int req_gl_major, int req_gl_minor);
void db_kms_atomic_shutdown_core(struct kms_atomic *kms,
                                 struct gbm_device *gbm);
struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer);
void fb_release(int fd, struct gbm_surface *gbm_surf, struct fb *fb);
struct fb *db_kms_atomic_prime_first_frame_and_modeset(
    const struct kms_atomic *kms, uint32_t width, uint32_t height,
    void *producer_ctx, db_kms_frame_transaction_t *transaction,
    db_kms_atomic_frame_fn_t frame_fn);
db_kms_atomic_loop_run_result_t
db_kms_atomic_run_frame_loop_timed(const db_kms_atomic_frame_loop_t *loop,
                                   void *producer_ctx,
                                   db_kms_atomic_frame_fn_t frame_fn);
db_display_frame_loop_result_t db_kms_atomic_gl_frame(void *user_ctx,
                                                      uint32_t frame_index);
int db_kms_gl_run_session_init(db_kms_atomic_gl_frame_producer_t *producer);
db_display_frame_loop_result_t db_kms_atomic_cpu_frame(void *user_ctx,
                                                       uint32_t frame_index);
int db_kms_cpu_run_session_init(db_kms_atomic_cpu_frame_producer_t *producer);
db_present_result_t
db_kms_present_pending(db_kms_frame_transaction_t *transaction,
                       const db_renderer_frame_output_t *output);
void db_kms_egl_presentation_init(const char *backend, EGLDisplay dpy,
                                  EGLSurface surf,
                                  db_kms_egl_presentation_t *presentation);
EGLDisplay egl_init_try_gl_then_optional_gles1_1(
    const char *backend, struct gbm_device *gbm, EGLConfig *out_cfg,
    EGLContext *out_ctx, EGLSurface *out_surf, struct gbm_surface *gbm_surf,
    int req_gl_major, int req_gl_minor, int allow_gles1_1_fallback,
    db_native_output_format_t native_output_format);

#endif
