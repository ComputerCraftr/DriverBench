#include "kms_hdr.h"
#include "kms_internal.h"

#include "core/db_format_contract.h"
#include "core/db_numeric.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#include <xf86drmMode.h>

enum {
    DB_KMS_EDID_BLOCK_SIZE = 128U,
    DB_KMS_EDID_EXTENSION_COUNT_INDEX = 126U,
    DB_KMS_CTA_DATA_END_LIMIT = 127U,
    DB_KMS_CTA_EXTENDED_TAG = 7U,
    DB_KMS_CTA_BT2020_RGB_MASK = 0x80U,
    DB_KMS_CTA_PQ_MASK = 0x04U,
    DB_KMS_CTA_HDR_STATIC_METADATA_TAG = 0x06U,
};

// libdrm has distro-dependent public providers that include-cleaner cannot
// resolve through the conditional include above.
// NOLINTNEXTLINE(misc-include-cleaner)
static const uint32_t db_drm_object_connector = DRM_MODE_OBJECT_CONNECTOR;
// NOLINTNEXTLINE(misc-include-cleaner)
typedef struct hdr_output_metadata db_kms_hdr_output_metadata_t;

drmModePropertyRes *db_kms_find_object_property(int fd, uint32_t object_id,
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
    drmModePropertyRes *property = db_kms_find_object_property(
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
