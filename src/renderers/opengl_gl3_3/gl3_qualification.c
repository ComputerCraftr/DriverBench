#include "gl3_qualification.h"

#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_gradient_divergence.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_probe_protocol.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "renderers/gl_common.h"
#include "renderers/gl_gradient_qualification.h"
#include "renderers/gl_hash_readback.h"

#include "db_embedded_shaders.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define DB_GL3_PROBE_SHADER_DOMAIN UINT32_C(0x47335348)

static uint64_t gl3_implementation_hash(void) {
    uint64_t implementation_hash = db_fnv1a64_tree(
        db_gl3_ir_execute_vert_source, strlen(db_gl3_ir_execute_vert_source),
        DB_GL3_PROBE_SHADER_DOMAIN, DB_FNV1A64_OFFSET);
    implementation_hash = db_fnv1a64_tree(
        db_gl3_ir_execute_frag_source, strlen(db_gl3_ir_execute_frag_source),
        DB_GL3_PROBE_SHADER_DOMAIN, implementation_hash);
    return implementation_hash;
}

static int
append_descriptor(db_renderer_qualification_descriptor_store_t *store,
                  db_pixel_format_t format,
                  db_gradient_implementation_t implementation,
                  uint64_t implementation_hash, uint32_t logical_width,
                  uint32_t logical_height) {
    db_renderer_probe_descriptor_t descriptor = {
        .backend = DB_PROBE_BACKEND_GL3,
        .strategy = DB_RENDER_TARGET_GL3_PERSISTENT_FBO,
        .implementation = implementation,
        .lane_index = 0U,
        .is_primary = 1,
        .working_format = format,
        .implementation_hash = implementation_hash,
        .logical_width = logical_width,
        .logical_height = logical_height,
        .compatibility_validated =
            implementation == DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
    };
    const char *const renderer = db_gl_get_renderer_string();
    const char *const version = db_gl_get_version_string();
    (void)db_snprintf(descriptor.provider, sizeof(descriptor.provider), "%s",
                      "gl_context");
    (void)db_snprintf(descriptor.driver.name, sizeof(descriptor.driver.name),
                      "%s", (renderer != NULL) ? renderer : "unknown");
    (void)db_snprintf(descriptor.driver.info, sizeof(descriptor.driver.info),
                      "%s", (version != NULL) ? version : "unknown");
    return db_qualification_descriptor_store_append(store, &descriptor);
}

int db_gl3_describe_qualification(
    db_pixel_format_t format, uint32_t logical_width, uint32_t logical_height,
    db_gradient_implementation_t forced_implementation, int diagnostic_forced,
    db_renderer_qualification_descriptor_store_t *store) {
    if ((store == NULL) || (logical_width == 0U) || (logical_height == 0U)) {
        return 0;
    }
    *store = (db_renderer_qualification_descriptor_store_t){
        .generation =
            {
                .device_generation = 1U,
                .implementation_generation = gl3_implementation_hash(),
                .target_contract_generation =
                    ((uint64_t)logical_width << 32U) | logical_height,
            },
    };
    const uint64_t hash = gl3_implementation_hash();
    if (diagnostic_forced != 0) {
        return append_descriptor(store, format, forced_implementation, hash,
                                 logical_width, logical_height);
    }
    return append_descriptor(store, format, DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
                             hash, logical_width, logical_height) &&
           append_descriptor(store, format,
                             DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES, hash,
                             logical_width, logical_height);
}

int db_gl3_qualify_current_target(const db_frame_plan_t *plan,
                                  db_pixel_format_t format,
                                  db_gl_framebuffer_hash_scratch_t *scratch,
                                  const char *divergence_path) {
    if ((plan == NULL) || (plan->rebuild_required == 0)) {
        return 0;
    }
    const size_t pixel_bytes = (format == DB_PIXEL_FORMAT_RGBA16F)
                                   ? DB_RGBA16F_BYTES_PER_PIXEL
                                   : DB_RGBA8_BYTES_PER_PIXEL;
    size_t pixel_count = 0U;
    size_t byte_count = 0U;
    if ((db_try_mul_size(plan->pixel_width, plan->pixel_height, &pixel_count) ==
         0) ||
        (db_try_mul_size(pixel_count, pixel_bytes, &byte_count) == 0) ||
        (byte_count == 0U)) {
        return 0;
    }
    void *const pixels = malloc(byte_count);
    if (pixels == NULL) {
        return 0;
    }
    const db_pixel_surface_t reference = {
        .pixel_width = plan->pixel_width,
        .pixel_height = plan->pixel_height,
        .pixels = pixels,
        .format = format,
    };
    const int generated = DB_BOOL(
        db_frame_plan_rasterize_reference(plan, &reference) == DB_RENDER_IR_OK);
    const int conforming =
        generated ? db_gl_qualify_current_framebuffer(
                        BACKEND_NAME, &reference, scratch,
                        &(const db_gradient_compare_context_t){
                            .extent = {.width = (int32_t)plan->pixel_width,
                                       .height = (int32_t)plan->pixel_height}},
                        divergence_path)
                  : 0;
    free(pixels);
    return conforming;
}
