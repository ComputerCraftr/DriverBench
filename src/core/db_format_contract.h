#ifndef DRIVERBENCH_CORE_DB_FORMAT_CONTRACT_H
#define DRIVERBENCH_CORE_DB_FORMAT_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "core/db_numeric.h"
#include "core/db_render_types.h"

#define DB_SDR_NATIVE_BIT_DEPTH 8U
#define DB_HDR10_NATIVE_BIT_DEPTH 10U

typedef enum {
    DB_OUTPUT_FORMAT_AUTO = 0,
    DB_OUTPUT_FORMAT_SDR,
    DB_OUTPUT_FORMAT_HDR,
} db_output_format_request_t;

typedef enum {
    DB_NATIVE_OUTPUT_RESOLVE_IMMEDIATE = 0,
    DB_NATIVE_OUTPUT_RESOLVE_AFTER_PRESENTER_PROBE,
} db_native_output_resolution_policy_t;

typedef enum {
    DB_NATIVE_OUTPUT_XRGB8888 = 0,
    DB_NATIVE_OUTPUT_XRGB2101010,
    DB_NATIVE_OUTPUT_RGBA16F,
} db_native_output_format_t;

typedef enum {
    DB_OUTPUT_COLORSPACE_SRGB = 0,
    DB_OUTPUT_COLORSPACE_BT2020,
} db_output_colorspace_t;

typedef enum {
    DB_OUTPUT_TRANSFER_SRGB = 0,
    DB_OUTPUT_TRANSFER_PQ,
} db_output_transfer_t;

typedef enum {
    DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_SDR = 0,
    DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_BT2020_PQ,
} db_output_conversion_t;

typedef enum {
    DB_ENCODED_PRESENT_SDR_RGBA8 = 0,
    DB_ENCODED_PRESENT_BT2020_PQ_RGB10A2,
} db_encoded_present_format_t;

typedef enum {
    DB_HDR_CONVERSION_NONE = 0,
    DB_HDR_CONVERSION_CPU_SCANOUT,
    DB_HDR_CONVERSION_CPU_FIXED_FUNCTION,
    DB_HDR_CONVERSION_FRAGMENT_SHADER,
    DB_HDR_CONVERSION_VULKAN_SHADER,
} db_hdr_conversion_implementation_t;

typedef struct {
    double reference_white_nits;
    double mastering_min_nits;
    double mastering_max_nits;
    double max_cll_nits;
    double max_fall_nits;
} db_hdr10_mastering_profile_t;

#define DB_HDR10_REFERENCE_WHITE_NITS 203.0
#define DB_HDR10_MASTERING_MIN_NITS 0.005
#define DB_HDR10_MASTERING_MAX_NITS 1000.0
#define DB_HDR10_MAX_CLL_NITS 1000.0
#define DB_HDR10_MAX_FALL_NITS 203.0

typedef enum {
    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8 = 0,
    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F = 1,
    DB_GL_SHADOW_PRESENT_TEXTURE_BT2020_PQ_RGB10A2 = 2,
} db_gl_shadow_present_texture_format_t;

typedef enum {
    DB_DISPLAY_FORMAT_REASON_SDR_REQUESTED = 0,
    DB_DISPLAY_FORMAT_REASON_HDR_VERIFIED,
    DB_DISPLAY_FORMAT_REASON_HDR_UNAVAILABLE,
    DB_DISPLAY_FORMAT_REASON_AUTO_SDR_FALLBACK,
} db_display_format_reason_t;

typedef struct {
    int native_hdr_verified;
    int native_format_supported;
    int colorspace_supported;
    int metadata_supported;
    int sink_hdr_supported;
    int commit_verified;
    uint32_t native_bit_depth;
    db_native_output_format_t hdr_format;
    db_output_colorspace_t hdr_colorspace;
    db_output_transfer_t hdr_transfer;
    const char *unavailable_reason;
} db_native_output_capability_t;

typedef struct db_display_resolved_format_config_t {
    db_output_format_request_t output_request;
    int native_output_resolution_pending;
    int hdr_content_supported;
    int native_hdr_enabled;
    int native_format_supported;
    int colorspace_supported;
    int metadata_supported;
    int sink_hdr_supported;
    int commit_verified;
    db_pixel_format_t surface_pixel_format;
    db_gl_shadow_present_texture_format_t present_texture_format;
    db_native_output_format_t native_output_format;
    db_output_colorspace_t output_colorspace;
    db_output_transfer_t output_transfer;
    db_output_conversion_t output_conversion;
    db_encoded_present_format_t encoded_present_format;
    db_hdr_conversion_implementation_t hdr_conversion;
    db_hdr10_mastering_profile_t hdr10;
    uint32_t native_bit_depth;
    db_pixel_format_t framebuffer_hash_format;
    db_display_format_reason_t reason;
    const char *fallback_reason;
} db_display_resolved_format_config_t;

typedef enum {
    DB_RENDER_FORMAT_CONVERSION_NONE = 0,
    DB_RENDER_FORMAT_CONVERSION_F64_TO_RGBA8 = 1,
    DB_RENDER_FORMAT_CONVERSION_F64_TO_RGBA16F = 2,
} db_render_format_conversion_t;

typedef struct {
    db_pixel_format_t renderer_write_format;
    db_pixel_format_t upload_format;
    db_gl_shadow_present_texture_format_t presentation_format;
    db_pixel_format_t canonical_hash_format;
    db_render_format_conversion_t conversion;
    db_display_format_reason_t reason;
} db_render_format_contract_t;

static inline db_hdr10_mastering_profile_t db_hdr10_mastering_profile(void) {
    return (db_hdr10_mastering_profile_t){
        .reference_white_nits = DB_HDR10_REFERENCE_WHITE_NITS,
        .mastering_min_nits = DB_HDR10_MASTERING_MIN_NITS,
        .mastering_max_nits = DB_HDR10_MASTERING_MAX_NITS,
        .max_cll_nits = DB_HDR10_MAX_CLL_NITS,
        .max_fall_nits = DB_HDR10_MAX_FALL_NITS,
    };
}

static inline const char *
db_output_format_request_name(db_output_format_request_t request) {
    static const char *const names[] = {"auto", "sdr", "hdr"};
    return ((size_t)request < (sizeof(names) / sizeof(names[0])))
               ? names[request]
               : "unknown";
}

static inline const char *
db_output_colorspace_name(db_output_colorspace_t colorspace) {
    return (colorspace == DB_OUTPUT_COLORSPACE_BT2020) ? "bt2020" : "srgb";
}

static inline const char *
db_output_transfer_name(db_output_transfer_t transfer) {
    return (transfer == DB_OUTPUT_TRANSFER_PQ) ? "pq" : "srgb";
}

static inline const char *
db_output_conversion_name(db_output_conversion_t conversion) {
    return (conversion == DB_OUTPUT_CONVERSION_LINEAR_SRGB_TO_BT2020_PQ)
               ? "linear_srgb_to_bt2020_pq"
               : "linear_srgb_to_sdr";
}

static inline const char *
db_encoded_present_format_name(db_encoded_present_format_t format) {
    return (format == DB_ENCODED_PRESENT_BT2020_PQ_RGB10A2)
               ? "bt2020_pq_rgb10a2"
               : "sdr_rgba8";
}

static inline const char *db_hdr_conversion_implementation_name(
    db_hdr_conversion_implementation_t implementation) {
    static const char *const names[] = {"none", "cpu_scanout",
                                        "cpu_fixed_function", "fragment_shader",
                                        "vulkan_shader"};
    return ((size_t)implementation < (sizeof(names) / sizeof(names[0])))
               ? names[implementation]
               : "unknown";
}

static inline db_pixel_format_t db_gl_pixel_format_from_texture_format(
    db_gl_shadow_present_texture_format_t texture_format) {
    return (texture_format == DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
               ? DB_PIXEL_FORMAT_RGBA16F
               : DB_PIXEL_FORMAT_RGBA8;
}

static inline db_gl_shadow_present_texture_format_t
db_gl_texture_format_from_pixel_format(db_pixel_format_t pixel_format) {
    return (pixel_format == DB_PIXEL_FORMAT_RGBA16F)
               ? DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F
               : DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
}

static inline int db_pixel_format_uses_rgba16f(db_pixel_format_t format) {
    return DB_BOOL(format == DB_PIXEL_FORMAT_RGBA16F);
}

static inline const char *db_pixel_format_name(db_pixel_format_t format) {
    return (format == DB_PIXEL_FORMAT_RGBA16F) ? "rgba16f" : "rgba8";
}

static inline const char *
db_native_output_format_name(db_native_output_format_t format) {
    static const char *const names[] = {"xrgb8888", "xrgb2101010", "rgba16f"};
    return ((size_t)format < (sizeof(names) / sizeof(names[0]))) ? names[format]
                                                                 : "unknown";
}

#endif
