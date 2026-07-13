#include "../../core/db_format_contract.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_frame_source.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../display_presentation_policy.h"
#include "core/db_log.h"
#include "core/db_poll_policy.h"
#include "kms_hdr.h"
#include "kms_internal.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h> // IWYU pragma: keep
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
#include "../gl_display_runtime.h"

enum {
    DB_KMS_EDID_BLOCK_SIZE = 128U,
    DB_KMS_EDID_EXTENSION_COUNT_INDEX = 126U,
    DB_KMS_CTA_DATA_END_LIMIT = 127U,
    DB_KMS_CTA_EXTENDED_TAG = 7U,
    DB_KMS_CTA_BT2020_RGB_MASK = 0x80U,
    DB_KMS_CTA_PQ_MASK = 0x04U,
    DB_KMS_CTA_HDR_STATIC_METADATA_TAG = 0x06U,
    DB_KMS_HDR_BIT_DEPTH = 10U,
};

// libdrm exposes these through one of two distro-dependent public include
// paths. Keep the include-cleaner exception at this compatibility boundary.
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
static const uint32_t db_drm_atomic_nonblock = DRM_MODE_ATOMIC_NONBLOCK;
static const uint32_t db_drm_page_flip_event = DRM_MODE_PAGE_FLIP_EVENT;
static const drmModeConnection db_drm_connected = DRM_MODE_CONNECTED;
typedef struct hdr_output_metadata db_kms_hdr_output_metadata_t;
// NOLINTEND(misc-include-cleaner)

#define DB_KMS_NS_PER_SECOND 1000000000ULL
#define DB_KMS_NS_PER_MICROSECOND 1000ULL

__attribute__((noreturn)) void runtime_failf(const char *fmt, ...) {
    char message[LOG_MSG_CAPACITY];
    va_list ap;
    va_start(ap, fmt);
    (void)db_vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    DB_RUNTIME_FAIL(g_active_backend, "%s", message);
}

void runtime_errno_fail(const char *msg) {
    runtime_failf("%s: %s", msg, strerror(errno));
}

static uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type,
                            const char *name) {
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (props == NULL) {
        runtime_errno_fail("drmModeObjectGetProperties");
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
        runtime_failf("Missing DRM property '%s' on object %u type %u", name,
                      obj_id, obj_type);
    }
    return prop_id;
}

static drmModePropertyRes *find_object_property(int fd, uint32_t object_id,
                                                uint32_t object_type,
                                                const char *name,
                                                uint64_t *out_value) {
    drmModeObjectProperties *properties =
        drmModeObjectGetProperties(fd, object_id, object_type);
    if (properties == NULL) {
        return NULL;
    }
    drmModePropertyRes *found = NULL;
    for (uint32_t index = 0U; index < properties->count_props; index++) {
        drmModePropertyRes *property =
            drmModeGetProperty(fd, properties->props[index]);
        if ((property != NULL) && (strcmp(property->name, name) == 0)) {
            found = property;
            if (out_value != NULL) {
                *out_value = properties->prop_values[index];
            }
            break;
        }
        drmModeFreeProperty(property);
    }
    drmModeFreeObjectProperties(properties);
    return found;
}

static int kms_plane_supports_format(const struct kms_atomic *kms,
                                     uint32_t format) {
    drmModePlane *plane = drmModeGetPlane(kms->fd, kms->plane_id);
    if (plane == NULL) {
        return 0;
    }
    int supported = 0;
    for (uint32_t index = 0U; index < plane->count_formats; index++) {
        if (plane->formats[index] == format) {
            supported = 1;
            break;
        }
    }
    drmModeFreePlane(plane);
    return supported;
}

int db_kms_edid_bytes_support_hdr10(const uint8_t *bytes, size_t length) {
    if ((bytes == NULL) || (length < DB_KMS_EDID_BLOCK_SIZE)) {
        return 0;
    }
    int pq = 0;
    int bt2020_rgb = 0;
    const uint32_t extension_count = bytes[DB_KMS_EDID_EXTENSION_COUNT_INDEX];
    for (uint32_t extension = 0U; extension < extension_count; extension++) {
        const size_t base = ((size_t)extension + 1U) * DB_KMS_EDID_BLOCK_SIZE;
        if ((base + DB_KMS_EDID_BLOCK_SIZE > length) ||
            (bytes[base] != 0x02U)) {
            continue;
        }
        const uint8_t data_end = bytes[base + 2U];
        for (uint8_t offset = 4U;
             (offset < data_end) && (offset < DB_KMS_CTA_DATA_END_LIMIT);) {
            const uint8_t header = bytes[base + offset];
            const uint8_t block_length = header & 0x1FU;
            if ((block_length == 0U) ||
                ((uint32_t)offset + 1U + block_length > data_end)) {
                break;
            }
            const uint8_t tag = header >> 5U;
            const uint8_t *payload = &bytes[base + offset + 1U];
            if ((tag == DB_KMS_CTA_EXTENDED_TAG) && (block_length >= 2U)) {
                if ((payload[0] == 0x05U) &&
                    ((payload[1] & DB_KMS_CTA_BT2020_RGB_MASK) != 0U)) {
                    bt2020_rgb = 1;
                } else if ((payload[0] == DB_KMS_CTA_HDR_STATIC_METADATA_TAG) &&
                           ((payload[1] & DB_KMS_CTA_PQ_MASK) != 0U)) {
                    pq = 1;
                }
            }
            offset = (uint8_t)(offset + 1U + block_length);
        }
    }
    return DB_BOOL((pq != 0) && (bt2020_rgb != 0));
}

static int kms_edid_supports_hdr10(const struct kms_atomic *kms) {
    uint64_t blob_id = 0U;
    drmModePropertyRes *property = find_object_property(
        kms->fd, kms->conn_id, db_drm_object_connector, "EDID", &blob_id);
    if ((property == NULL) || (blob_id == 0U)) {
        drmModeFreeProperty(property);
        return 0;
    }
    drmModePropertyBlobRes *blob =
        drmModeGetPropertyBlob(kms->fd, (uint32_t)blob_id);
    drmModeFreeProperty(property);
    if ((blob == NULL) || (blob->data == NULL)) {
        drmModeFreePropertyBlob(blob);
        return 0;
    }
    const int supported = db_kms_edid_bytes_support_hdr10(
        (const uint8_t *)blob->data, blob->length);
    drmModeFreePropertyBlob(blob);
    return supported;
}

db_native_output_capability_t
db_kms_atomic_query_hdr_capability(const struct kms_atomic *kms) {
    db_native_output_capability_t capability = {
        .native_bit_depth = DB_KMS_HDR_BIT_DEPTH,
        .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
        .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
        .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
        .unavailable_reason = "kms_hdr10_capability_incomplete",
    };
    if (kms == NULL) {
        return capability;
    }
    const uint32_t hdr_scanout_format = db_kms_atomic_gbm_format_or_fail(
        "display_linux_kms_atomic", DB_NATIVE_OUTPUT_XRGB2101010);
    capability.native_format_supported =
        kms_plane_supports_format(kms, hdr_scanout_format);
    capability.sink_hdr_supported = kms_edid_supports_hdr10(kms);
    capability.colorspace_supported =
        DB_BOOL((kms->conn_prop_colorspace != 0U) &&
                (kms->conn_colorspace_bt2020_rgb != UINT64_MAX));
    capability.metadata_supported = DB_BOOL(kms->conn_prop_hdr_metadata != 0U);
    capability.commit_verified = 0;
    capability.native_hdr_verified =
        DB_BOOL((capability.native_format_supported != 0) &&
                (capability.sink_hdr_supported != 0) &&
                (capability.colorspace_supported != 0) &&
                (capability.metadata_supported != 0) &&
                (kms->conn_prop_max_bpc != 0U) &&
                (kms->conn_max_bpc_supported >= DB_KMS_HDR_BIT_DEPTH));
    if (capability.native_format_supported == 0) {
        capability.unavailable_reason = "kms_xrgb2101010_plane_unavailable";
    } else if (capability.sink_hdr_supported == 0) {
        capability.unavailable_reason = "kms_edid_hdr10_unavailable";
    } else if (capability.colorspace_supported == 0) {
        capability.unavailable_reason = "kms_bt2020_rgb_property_unavailable";
    } else if (capability.metadata_supported == 0) {
        capability.unavailable_reason = "kms_hdr_metadata_property_unavailable";
    } else if ((kms->conn_prop_max_bpc == 0U) ||
               (kms->conn_max_bpc_supported < DB_KMS_HDR_BIT_DEPTH)) {
        capability.unavailable_reason = "kms_10bpc_unavailable";
    }
    return capability;
}

void db_kms_atomic_set_hdr_enabled(struct kms_atomic *kms, int enabled) {
    if (kms == NULL) {
        return;
    }
    kms->hdr_enabled = DB_BOOL(enabled);
    if ((kms->hdr_enabled != 0) && (kms->hdr_metadata_blob_id == 0U)) {
        const db_kms_hdr_output_metadata_t metadata = {
            .metadata_type = 0U,
            .hdmi_metadata_type1 =
                {
                    .eotf = 2U,
                    .metadata_type = 0U,
                    .display_primaries =
                        {
                            {.x = 35400U, .y = 14600U},
                            {.x = 8500U, .y = 39850U},
                            {.x = 6550U, .y = 2300U},
                        },
                    .white_point = {.x = 15635U, .y = 16450U},
                    .max_display_mastering_luminance = 1000U,
                    .min_display_mastering_luminance = 50U,
                    .max_cll = 1000U,
                    .max_fall = 203U,
                },
        };
        if (drmModeCreatePropertyBlob(kms->fd, &metadata, sizeof(metadata),
                                      &kms->hdr_metadata_blob_id) != 0) {
            runtime_errno_fail("drmModeCreatePropertyBlob HDR metadata");
        }
    }
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

    kms->conn_prop_crtc_id =
        get_prop_id(kms->fd, kms->conn_id, db_drm_object_connector, "CRTC_ID");
    kms->conn_colorspace_bt2020_rgb = UINT64_MAX;
    drmModePropertyRes *colorspace =
        find_object_property(kms->fd, kms->conn_id, db_drm_object_connector,
                             "Colorspace", &kms->conn_initial_colorspace);
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
    drmModePropertyRes *hdr_metadata =
        find_object_property(kms->fd, kms->conn_id, db_drm_object_connector,
                             "HDR_OUTPUT_METADATA", NULL);
    if (hdr_metadata != NULL) {
        kms->conn_prop_hdr_metadata = hdr_metadata->prop_id;
        drmModeFreeProperty(hdr_metadata);
    }
    drmModePropertyRes *max_bpc =
        find_object_property(kms->fd, kms->conn_id, db_drm_object_connector,
                             "max bpc", &kms->conn_initial_max_bpc);
    if (max_bpc != NULL) {
        kms->conn_prop_max_bpc = max_bpc->prop_id;
        if (((max_bpc->flags & db_drm_property_range) != 0U) &&
            (max_bpc->count_values >= 2)) {
            kms->conn_max_bpc_supported = max_bpc->values[1];
        }
        drmModeFreeProperty(max_bpc);
    }

    kms->crtc_prop_mode_id =
        get_prop_id(kms->fd, kms->crtc_id, db_drm_object_crtc, "MODE_ID");
    kms->crtc_prop_active =
        get_prop_id(kms->fd, kms->crtc_id, db_drm_object_crtc, "ACTIVE");

    kms->plane_prop_fb_id =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "FB_ID");
    kms->plane_prop_crtc_id =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_ID");
    kms->plane_prop_src_x =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "SRC_X");
    kms->plane_prop_src_y =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "SRC_Y");
    kms->plane_prop_src_w =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "SRC_W");
    kms->plane_prop_src_h =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "SRC_H");
    kms->plane_prop_crtc_x =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_X");
    kms->plane_prop_crtc_y =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_Y");
    kms->plane_prop_crtc_w =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_W");
    kms->plane_prop_crtc_h =
        get_prop_id(kms->fd, kms->plane_id, db_drm_object_plane, "CRTC_H");
}

struct fb *fb_from_bo(int fd, struct gbm_bo *bo, int is_surface_buffer) {
    struct fb *fb = (struct fb *)calloc(1, sizeof(*fb));
    if (fb == NULL) {
        runtime_failf("calloc fb");
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
        runtime_errno_fail("drmModeAddFB2");
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

typedef struct {
    int fd;
    int *waiting;
    drmEventContext *event_context;
} db_kms_page_flip_wait_t;

static db_sync_wait_result_t
db_kms_page_flip_wait_attempt(void *user_data, uint64_t timeout_ns) {
    db_kms_page_flip_wait_t *const context =
        (db_kms_page_flip_wait_t *)user_data;
    // select(2) exposes these through <sys/time.h>, but include-cleaner maps
    // the public typedefs to libc implementation headers.
    // NOLINTNEXTLINE(misc-include-cleaner)
    struct timeval timeout = {
        // NOLINTNEXTLINE(misc-include-cleaner)
        .tv_sec = (time_t)(timeout_ns / DB_KMS_NS_PER_SECOND),
        // NOLINTNEXTLINE(misc-include-cleaner)
        .tv_usec = (suseconds_t)((timeout_ns % DB_KMS_NS_PER_SECOND) /
                                 DB_KMS_NS_PER_MICROSECOND),
    };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(context->fd, &fds);
    const int select_result =
        select(context->fd + 1, &fds, NULL, NULL, &timeout);
    if (select_result < 0) {
        // EINTR is public through <errno.h>; include-cleaner reports the
        // architecture-specific internal provider.
        // NOLINTNEXTLINE(misc-include-cleaner)
        if (errno == EINTR) {
            return db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U,
                                            (uint32_t)errno, "interrupted");
        }
        return db_sync_wait_result_make(DB_SYNC_WAIT_FAILED, 0U, 0U,
                                        (uint32_t)errno, "select_failed");
    }
    if (select_result == 0) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U, 0U,
                                        "event_pending");
    }
    if (drmHandleEvent(context->fd, context->event_context) != 0) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_FAILED, 0U, 0U,
                                        (uint32_t)errno, "drm_event_failed");
    }
    return db_sync_wait_result_make(
        (*context->waiting == 0) ? DB_SYNC_WAIT_COMPLETED
                                 : DB_SYNC_WAIT_TIMEOUT,
        0U, 0U, 0U,
        (*context->waiting == 0) ? "page_flip_complete" : "event_pending");
}

static void kms_atomic_flip_to_fb(const struct kms_atomic *kms, uint32_t fb_id,
                                  drmEventContext *ev) {
    drmModeAtomicReq *commit_req = drmModeAtomicAlloc();
    if (commit_req == NULL) {
        runtime_failf("drmModeAtomicAlloc");
    }
    drmModeAtomicAddProperty(commit_req, kms->plane_id, kms->plane_prop_fb_id,
                             fb_id);

    int waiting = 1;
    const uint32_t flip_flags = db_drm_atomic_nonblock | db_drm_page_flip_event;
    if (drmModeAtomicCommit(kms->fd, commit_req, flip_flags, &waiting) != 0) {
        runtime_errno_fail("drmModeAtomicCommit flip");
    }
    drmModeAtomicFree(commit_req);
    db_kms_page_flip_wait_t wait_context = {
        .fd = kms->fd,
        .waiting = &waiting,
        .event_context = ev,
    };
    const db_sync_wait_result_t wait_result =
        db_progress_execute(DB_PROGRESS_KMS_PAGE_FLIP,
                            db_kms_page_flip_wait_attempt, &wait_context);
    db_progress_log_outcome("display_linux_kms_atomic", "page_flip",
                            DB_PROGRESS_KMS_PAGE_FLIP, &wait_result);
    if (wait_result.status != DB_SYNC_WAIT_COMPLETED) {
        runtime_failf("timed out waiting for KMS page flip");
    }
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
    void *producer_ctx, db_kms_atomic_next_fb_fn_t next_fb_fn) {
    if ((kms == NULL) || (next_fb_fn == NULL)) {
        runtime_failf("invalid kms prime args");
    }
    struct fb *cur = next_fb_fn(producer_ctx, 0U);
    if (cur == NULL) {
        runtime_failf("failed to create first scanout framebuffer");
    }
    kms_atomic_commit_modeset(kms, width, height, cur->fb_id);
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
    kms_atomic_flip_to_fb(loop_cfg->kms, next->fb_id, loop_cfg->event_context);
    if (loop_cfg->release_previous_framebuffer != 0) {
        fb_release(loop_cfg->kms->fd, loop_cfg->release_surface,
                   *loop_cfg->cur_fb);
    }
    *loop_cfg->cur_fb = next;
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
    db_frame_plan_t plan = {0};
    db_frame_source_generate(
        producer->core, frame_index,
        &(const db_frame_plan_request_t){
            .pixel_width = producer->pixel_width,
            .pixel_height = producer->pixel_height,
            .force_rebuild = DB_BOOL(frame_index == 0U),
            .rebuild_reason = DB_FRAME_REBUILD_INITIAL_TARGET,
        },
        &plan);
    db_presentation_buffer_age_t age = db_presentation_buffer_age_resolve(
        DB_PRESENTATION_BUFFER_AGE_UNAVAILABLE, 0U,
        DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
    if (producer->presentation.buffer_age_supported != 0) {
        EGLint raw_age = 0;
        if (eglQuerySurface(producer->dpy, producer->surf, EGL_BUFFER_AGE_EXT,
                            &raw_age) == EGL_TRUE) {
            age = db_presentation_buffer_age_resolve(
                DB_PRESENTATION_BUFFER_AGE_EGL,
                (raw_age > 0) ? (uint32_t)raw_age : 0U,
                DB_PRESENTATION_DAMAGE_HISTORY_LENGTH);
        }
    }
    if ((producer->presentation.last_age_valid == 0) ||
        (producer->presentation.last_age.provider != age.provider) ||
        (producer->presentation.last_age.raw_age != age.raw_age) ||
        (producer->presentation.last_age.force_full_repair !=
         age.force_full_repair)) {
        db_presentation_log_buffer_age(producer->backend, &age);
        producer->presentation.last_age = age;
        producer->presentation.last_age_valid = 1;
    }
    int force_full = 1;
    const size_t logical_count = db_presentation_damage_history_resolve(
        &producer->presentation.damage_history, &age,
        plan.geometry.logical_damage, plan.grid_rows, plan.grid_cols,
        producer->presentation.logical_damage,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &force_full);
    int map_overflow = 0;
    size_t pixel_count = db_presentation_map_logical_damage(
        (db_grid_block_view_t){
            .blocks = producer->presentation.logical_damage,
            .count = logical_count,
        },
        plan.grid_rows, plan.grid_cols, producer->destination_width,
        producer->destination_height, producer->presentation.pixel_damage,
        DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME, &map_overflow);
    if (map_overflow != 0) {
        producer->presentation.pixel_damage[0] = (db_damage_block_t){
            .row_count = producer->destination_height,
            .col_count = producer->destination_width,
        };
        pixel_count = 1U;
        force_full = 1;
    }
    const db_gl_presentation_frame_t presentation = {
        .destination_width = producer->destination_width,
        .destination_height = producer->destination_height,
        .damage =
            (db_pixel_block_view_t){
                .blocks = producer->presentation.pixel_damage,
                .count = pixel_count,
            },
        .buffer_age = age,
        .force_full = force_full,
        .repair_reason = force_full != 0 ? age.fallback_reason : "none",
    };
    producer->renderer->render_frame(&plan, &presentation);
    db_frame_source_commit_success(producer->core, &plan);
    EGLBoolean swapped = EGL_FALSE;
    if ((producer->presentation.swap_damage_supported != 0) &&
        (producer->presentation.swap_buffers_with_damage != NULL) &&
        (pixel_count > 0U)) {
        EGLint rects[DB_PRESENTATION_DAMAGE_RECTS_PER_FRAME * 4U];
        for (size_t index = 0U; index < pixel_count; index++) {
            const db_damage_block_t *const block =
                &producer->presentation.pixel_damage[index];
            const size_t base = index * 4U;
            rects[base] = (EGLint)block->col_start;
            rects[base + 1U] = (EGLint)(producer->destination_height -
                                        block->row_start - block->row_count);
            rects[base + 2U] = (EGLint)block->col_count;
            rects[base + 3U] = (EGLint)block->row_count;
        }
        swapped = producer->presentation.swap_buffers_with_damage(
            producer->dpy, producer->surf, rects, (EGLint)pixel_count);
    } else {
        swapped = eglSwapBuffers(producer->dpy, producer->surf);
    }
    if (swapped != EGL_TRUE) {
        runtime_failf("EGL swap failed");
    }

    struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(producer->gbm_surf);
    if (next_bo == NULL) {
        runtime_failf("lock_front_buffer failed");
    }
    return fb_from_bo(producer->kms_fd, next_bo, 1);
}
