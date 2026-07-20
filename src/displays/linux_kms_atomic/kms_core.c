#include "../../core/db_format_contract.h"
#include "../../core/db_frame_contracts.h"
#include "../../core/db_numeric.h"
#include "kms_internal.h"

#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <string.h>
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
#include "../display_dispatch.h"
#include "../display_frame_loop_common.h"

// libdrm has two distro-dependent public include paths.
// NOLINTBEGIN(misc-include-cleaner)
static const uint32_t db_drm_object_connector = DRM_MODE_OBJECT_CONNECTOR;
static const uint32_t db_drm_object_plane = DRM_MODE_OBJECT_PLANE;
static const uint32_t db_drm_object_crtc = DRM_MODE_OBJECT_CRTC;
static const uint32_t db_drm_property_enum = DRM_MODE_PROP_ENUM;
static const uint32_t db_drm_property_range = DRM_MODE_PROP_RANGE;
static const uint64_t db_drm_client_universal_planes =
    DRM_CLIENT_CAP_UNIVERSAL_PLANES;
static const uint64_t db_drm_client_atomic = DRM_CLIENT_CAP_ATOMIC;
static const uint32_t db_drm_atomic_allow_modeset =
    DRM_MODE_ATOMIC_ALLOW_MODESET;
static const uint32_t db_drm_atomic_test_only = DRM_MODE_ATOMIC_TEST_ONLY;
static const drmModeConnection db_drm_connected = DRM_MODE_CONNECTED;
// NOLINTEND(misc-include-cleaner)

static uint32_t require_property_id(int fd, uint32_t object_id,
                                    uint32_t object_type, const char *name) {
    drmModePropertyRes *const property =
        db_kms_find_object_property(fd, object_id, object_type, name, NULL);
    if (property == NULL) {
        runtime_failf("Missing DRM property '%s' on object %u type %u", name,
                      object_id, object_type);
    }
    const uint32_t property_id = property->prop_id;
    drmModeFreeProperty(property);
    return property_id;
}

static int crtc_mask_contains_index(uint32_t mask, int index) {
    return DB_BOOL((index >= 0) && (index < 32) &&
                   ((mask & (UINT32_C(1) << (uint32_t)index)) != 0U));
}
static drmModeConnector *pick_connected_connector(struct kms_atomic *kms) {
    for (int i = 0; i < kms->res->count_connectors; i++) {
        drmModeConnector *connector =
            drmModeGetConnector(kms->fd, kms->res->connectors[i]);
        if (connector == NULL) {
            continue;
        }
        if ((connector->connection == db_drm_connected) &&
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
            if (crtc_mask_contains_index(encoder->possible_crtcs, c) != 0) {
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
        if (crtc_mask_contains_index(plane->possible_crtcs, crtc_index) == 0) {
            drmModeFreePlane(plane);
            continue;
        }

        drmModeObjectProperties *props =
            drmModeObjectGetProperties(kms->fd, plane_id, db_drm_object_plane);
        if (props == NULL) {
            runtime_errno_fail("drmModeObjectGetProperties plane");
        }

        int is_primary = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes *prop =
                drmModeGetProperty(kms->fd, props->props[j]);
            if (prop == NULL) {
                continue;
            }
            if ((strcmp(prop->name, "type") == 0) &&
                ((prop->flags & db_drm_property_enum) != 0)) {
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
        runtime_errno_fail("open DRM card");
    }

    if (drmSetClientCap(kms->fd, db_drm_client_universal_planes, 1) != 0) {
        runtime_errno_fail("drmSetClientCap UNIVERSAL_PLANES");
    }
    if (drmSetClientCap(kms->fd, db_drm_client_atomic, 1) != 0) {
        runtime_errno_fail("drmSetClientCap ATOMIC");
    }

    kms->res = drmModeGetResources(kms->fd);
    if (kms->res == NULL) {
        runtime_errno_fail("drmModeGetResources");
    }

    kms->pres = drmModeGetPlaneResources(kms->fd);
    if (kms->pres == NULL) {
        runtime_errno_fail("drmModeGetPlaneResources");
    }

    kms->conn = pick_connected_connector(kms);
    if (kms->conn == NULL) {
        runtime_failf("No connected connector with modes");
    }
    kms->conn_id = kms->conn->connector_id;
    kms->mode = kms->conn->modes[0];

    kms->crtc_id = pick_crtc_for_connector(kms, kms->conn);
    if (kms->crtc_id == 0U) {
        runtime_failf("No usable CRTC for connector");
    }

    kms->plane_id = pick_primary_plane_for_crtc(kms, kms->crtc_id);
    if (kms->plane_id == 0U) {
        runtime_failf("No primary plane for chosen CRTC");
    }

    if (drmModeCreatePropertyBlob(kms->fd, &kms->mode, sizeof(kms->mode),
                                  &kms->mode_blob_id) != 0) {
        runtime_errno_fail("drmModeCreatePropertyBlob");
    }

    kms->conn_prop_crtc_id = require_property_id(
        kms->fd, kms->conn_id, db_drm_object_connector, "CRTC_ID");
    kms->conn_colorspace_bt2020_rgb = UINT64_MAX;
    drmModePropertyRes *colorspace = db_kms_find_object_property(
        kms->fd, kms->conn_id, db_drm_object_connector, "Colorspace",
        &kms->conn_initial_colorspace);
    if (colorspace != NULL) {
        kms->conn_prop_colorspace = colorspace->prop_id;
        for (int index = 0; index < colorspace->count_enums; index++) {
            if (strcmp(colorspace->enums[index].name, "BT2020_RGB") == 0) {
                kms->conn_colorspace_bt2020_rgb =
                    colorspace->enums[index].value;
                break;
            }
        }
        drmModeFreeProperty(colorspace);
    }
    drmModePropertyRes *hdr_metadata = db_kms_find_object_property(
        kms->fd, kms->conn_id, db_drm_object_connector, "HDR_OUTPUT_METADATA",
        NULL);
    if (hdr_metadata != NULL) {
        kms->conn_prop_hdr_metadata = hdr_metadata->prop_id;
        drmModeFreeProperty(hdr_metadata);
    }
    drmModePropertyRes *max_bpc = db_kms_find_object_property(
        kms->fd, kms->conn_id, db_drm_object_connector, "max bpc",
        &kms->conn_initial_max_bpc);
    if (max_bpc != NULL) {
        kms->conn_prop_max_bpc = max_bpc->prop_id;
        if (((max_bpc->flags & db_drm_property_range) != 0U) &&
            (max_bpc->count_values >= 2)) {
            kms->conn_max_bpc_supported = max_bpc->values[1];
        }
        drmModeFreeProperty(max_bpc);
    }

    kms->crtc_prop_mode_id = require_property_id(kms->fd, kms->crtc_id,
                                                 db_drm_object_crtc, "MODE_ID");
    kms->crtc_prop_active = require_property_id(kms->fd, kms->crtc_id,
                                                db_drm_object_crtc, "ACTIVE");

    kms->plane_prop_fb_id = require_property_id(kms->fd, kms->plane_id,
                                                db_drm_object_plane, "FB_ID");
    kms->plane_prop_crtc_id = require_property_id(
        kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_ID");
    kms->plane_prop_src_x = require_property_id(kms->fd, kms->plane_id,
                                                db_drm_object_plane, "SRC_X");
    kms->plane_prop_src_y = require_property_id(kms->fd, kms->plane_id,
                                                db_drm_object_plane, "SRC_Y");
    kms->plane_prop_src_w = require_property_id(kms->fd, kms->plane_id,
                                                db_drm_object_plane, "SRC_W");
    kms->plane_prop_src_h = require_property_id(kms->fd, kms->plane_id,
                                                db_drm_object_plane, "SRC_H");
    kms->plane_prop_crtc_x = require_property_id(kms->fd, kms->plane_id,
                                                 db_drm_object_plane, "CRTC_X");
    kms->plane_prop_crtc_y = require_property_id(kms->fd, kms->plane_id,
                                                 db_drm_object_plane, "CRTC_Y");
    kms->plane_prop_crtc_w = require_property_id(kms->fd, kms->plane_id,
                                                 db_drm_object_plane, "CRTC_W");
    kms->plane_prop_crtc_h = require_property_id(kms->fd, kms->plane_id,
                                                 db_drm_object_plane, "CRTC_H");
}

static void kms_atomic_add_modeset_props(drmModeAtomicReq *req,
                                         const struct kms_atomic *kms,
                                         uint32_t width, uint32_t height,
                                         uint32_t fb_id) {
    drmModeAtomicAddProperty(req, kms->conn_id, kms->conn_prop_crtc_id,
                             kms->crtc_id);
    if (kms->hdr_enabled != 0) {
        drmModeAtomicAddProperty(req, kms->conn_id, kms->conn_prop_colorspace,
                                 kms->conn_colorspace_bt2020_rgb);
        drmModeAtomicAddProperty(req, kms->conn_id, kms->conn_prop_hdr_metadata,
                                 kms->hdr_metadata_blob_id);
        drmModeAtomicAddProperty(req, kms->conn_id, kms->conn_prop_max_bpc,
                                 DB_KMS_HDR_BIT_DEPTH);
    }
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

db_native_output_capability_t
db_kms_atomic_verify_hdr_capability(struct kms_atomic *kms,
                                    struct gbm_device *gbm, uint32_t width,
                                    uint32_t height) {
    db_native_output_capability_t capability =
        db_kms_atomic_query_hdr_capability(kms);
    if ((capability.native_hdr_verified == 0) || (gbm == NULL)) {
        capability.native_hdr_verified = 0;
        return capability;
    }
    const uint32_t hdr_scanout_format = db_kms_atomic_gbm_format_or_fail(
        "display_linux_kms_atomic", DB_NATIVE_OUTPUT_XRGB2101010);
    struct gbm_bo *bo =
        gbm_bo_create(gbm, width, height, hdr_scanout_format,
                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (bo == NULL) {
        capability.native_hdr_verified = 0;
        capability.native_format_supported = 0;
        capability.unavailable_reason = "kms_hdr10_gbm_allocation_failed";
        return capability;
    }
    struct fb *framebuffer = fb_from_bo(kms->fd, bo, 0);
    db_kms_atomic_set_hdr_enabled(kms, 1);
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    int verified = 0;
    if (request != NULL) {
        kms_atomic_add_modeset_props(request, kms, width, height,
                                     framebuffer->fb_id);
        verified = DB_BOOL(drmModeAtomicCommit(kms->fd, request,
                                               db_drm_atomic_allow_modeset |
                                                   db_drm_atomic_test_only,
                                               NULL) == 0);
        drmModeAtomicFree(request);
    }
    fb_release(kms->fd, NULL, framebuffer);
    capability.commit_verified = verified;
    capability.native_hdr_verified = verified;
    capability.unavailable_reason =
        (verified != 0) ? "none" : "kms_hdr10_atomic_test_failed";
    if (verified == 0) {
        db_kms_atomic_set_hdr_enabled(kms, 0);
    }
    return capability;
}

static void kms_atomic_commit_modeset(const struct kms_atomic *kms,
                                      uint32_t width, uint32_t height,
                                      uint32_t fb_id) {
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (req == NULL) {
        runtime_failf("drmModeAtomicAlloc");
    }
    kms_atomic_add_modeset_props(req, kms, width, height, fb_id);
    if ((kms->hdr_enabled != 0) &&
        (drmModeAtomicCommit(kms->fd, req,
                             db_drm_atomic_allow_modeset |
                                 db_drm_atomic_test_only,
                             NULL) != 0)) {
        runtime_errno_fail("drmModeAtomicCommit HDR TEST_ONLY");
    }
    if (drmModeAtomicCommit(kms->fd, req, db_drm_atomic_allow_modeset, NULL) !=
        0) {
        runtime_errno_fail("drmModeAtomicCommit modeset");
    }
    drmModeAtomicFree(req);
}

void db_kms_atomic_init_core(const char *card, struct kms_atomic *kms,
                             struct gbm_device **out_gbm, uint32_t *out_width,
                             uint32_t *out_height) {
    if ((card == NULL) || (kms == NULL) || (out_gbm == NULL) ||
        (out_width == NULL) || (out_height == NULL)) {
        runtime_failf("invalid kms core init args");
    }
    kms_atomic_init(kms, card);
    *out_width = kms->mode.hdisplay;
    *out_height = kms->mode.vdisplay;
    struct gbm_device *gbm = gbm_create_device(kms->fd);
    if (gbm == NULL) {
        runtime_errno_fail("gbm_create_device");
    }
    *out_gbm = gbm;
}

void db_kms_atomic_shutdown_core(struct kms_atomic *kms,
                                 struct gbm_device *gbm) {
    if (kms == NULL) {
        return;
    }
    if ((kms->hdr_enabled != 0) && (kms->fd >= 0)) {
        drmModeAtomicReq *restore = drmModeAtomicAlloc();
        if (restore != NULL) {
            drmModeAtomicAddProperty(restore, kms->conn_id,
                                     kms->conn_prop_colorspace,
                                     kms->conn_initial_colorspace);
            drmModeAtomicAddProperty(restore, kms->conn_id,
                                     kms->conn_prop_hdr_metadata, 0U);
            drmModeAtomicAddProperty(restore, kms->conn_id,
                                     kms->conn_prop_max_bpc,
                                     kms->conn_initial_max_bpc);
            (void)drmModeAtomicCommit(kms->fd, restore, 0U, NULL);
            drmModeAtomicFree(restore);
        }
    }
    if (kms->mode_blob_id != 0U) {
        drmModeDestroyPropertyBlob(kms->fd, kms->mode_blob_id);
    }
    if (kms->hdr_metadata_blob_id != 0U) {
        drmModeDestroyPropertyBlob(kms->fd, kms->hdr_metadata_blob_id);
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
    void *producer_ctx, db_kms_frame_transaction_t *transaction,
    db_kms_atomic_frame_fn_t frame_fn) {
    if ((kms == NULL) || (transaction == NULL) || (frame_fn == NULL)) {
        runtime_failf("invalid kms prime args");
    }
    (void)width;
    (void)height;
    transaction->initial_modeset = 1;
    if (frame_fn(producer_ctx, 0U) != DB_DISPLAY_FRAME_LOOP_CONTINUE) {
        runtime_failf("failed to create first scanout framebuffer");
    }
    return *transaction->loop->cur_fb;
}

db_present_result_t
db_kms_present_pending(db_kms_frame_transaction_t *transaction,
                       const db_renderer_frame_output_t *output) {
    if ((transaction == NULL) || (transaction->loop == NULL) ||
        (transaction->pending_fb == NULL) || (output == NULL) ||
        (output->result.success == 0)) {
        return DB_PRESENT_FATAL;
    }
    const db_kms_atomic_frame_loop_t *const loop = transaction->loop;
    if (transaction->initial_modeset != 0) {
        kms_atomic_commit_modeset(loop->kms, loop->kms->mode.hdisplay,
                                  loop->kms->mode.vdisplay,
                                  transaction->pending_fb->fb_id);
        transaction->initial_modeset = 0;
    } else {
        db_kms_atomic_flip_to_fb(loop->kms, transaction->pending_fb->fb_id,
                                 loop->event_context);
        if (loop->release_previous_framebuffer != 0) {
            fb_release(loop->kms->fd, loop->release_surface, *loop->cur_fb);
        }
    }
    *loop->cur_fb = transaction->pending_fb;
    transaction->pending_fb = NULL;
    return DB_PRESENT_ACCEPTED;
}

static db_display_frame_loop_result_t
db_kms_atomic_shared_frame_step(void *user_data, uint32_t frame_index,
                                double elapsed_ms) {
    db_kms_atomic_shared_loop_ctx_t *loop_ctx =
        (db_kms_atomic_shared_loop_ctx_t *)user_data;
    if ((loop_ctx == NULL) || (loop_ctx->loop == NULL) ||
        (loop_ctx->frame_fn == NULL) ||
        (loop_ctx->next_progress_log_due_ms == NULL)) {
        return DB_DISPLAY_FRAME_LOOP_STOP;
    }
    const db_kms_atomic_frame_loop_t *loop_cfg = loop_ctx->loop;
    const db_display_frame_loop_result_t frame_result =
        loop_ctx->frame_fn(loop_ctx->producer_ctx, frame_index);
    if (frame_result != DB_DISPLAY_FRAME_LOOP_CONTINUE) {
        return frame_result;
    }
    db_log_progress_periodic(
        db_dispatch_api_name(loop_cfg->api), loop_cfg->renderer_name,
        loop_cfg->backend, (uint64_t)frame_index + 1U,
        loop_cfg->work_unit_count, elapsed_ms,
        loop_ctx->next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS);
    return DB_DISPLAY_FRAME_LOOP_CONTINUE;
}

static uint64_t
db_kms_atomic_run_frame_loop(const db_kms_atomic_frame_loop_t *loop,
                             void *producer_ctx,
                             db_kms_atomic_frame_fn_t frame_fn) {
    double next_progress_log_due_ms = 0.0;
    if ((loop == NULL) || (frame_fn == NULL)) {
        return 0U;
    }
    db_kms_atomic_shared_loop_ctx_t ctx = {
        .loop = loop,
        .producer_ctx = producer_ctx,
        .frame_fn = frame_fn,
        .next_progress_log_due_ms = &next_progress_log_due_ms,
    };
    db_display_frame_loop_t shared_loop = {
        .backend = loop->backend,
        .fps_cap = loop->fps_cap,
        .frame_limit = loop->frame_limit,
        .initial_frame_index = 1U,
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
                                   db_kms_atomic_frame_fn_t frame_fn) {
    const uint64_t bench_start = db_now_ns_monotonic();
    const uint64_t bench_frames =
        db_kms_atomic_run_frame_loop(loop, producer_ctx, frame_fn);
    const double bench_ms =
        DB_TO_F64(db_now_ns_monotonic() - bench_start) / DB_NS_PER_MS;
    return (db_kms_atomic_loop_run_result_t){
        .frames = bench_frames,
        .elapsed_ms = bench_ms,
    };
}
