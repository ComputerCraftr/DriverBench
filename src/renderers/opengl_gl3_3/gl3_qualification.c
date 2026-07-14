#include "gl3_qualification.h"

#include "core/db_conformance.h"
#include "core/db_conformance_service.h"
#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_gradient_divergence.h"
#include "core/db_hash.h"
#include "core/db_numeric.h"
#include "core/db_probe_protocol.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "core/db_render_types.h"
#include "core/db_renderer_diagnostics.h"
#include "renderers/gl_common.h"
#include "renderers/gl_gradient_qualification.h"
#include "renderers/gl_hash_readback.h"

#include "db_embedded_shaders.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BACKEND_NAME "renderer_opengl_gl3_3"
#define DB_GL3_PROBE_SHADER_DOMAIN UINT32_C(0x47335348)

db_conformance_decision_t db_gl3_qualify_implementation(
    db_pixel_format_t format, uint32_t logical_width, uint32_t logical_height,
    const db_renderer_diagnostic_config_t *diagnostics,
    db_gradient_implementation_t implementation) {
    uint64_t implementation_hash = db_fnv1a64_tree(
        db_gl3_ir_execute_vert_source, strlen(db_gl3_ir_execute_vert_source),
        DB_GL3_PROBE_SHADER_DOMAIN, DB_FNV1A64_OFFSET);
    implementation_hash = db_fnv1a64_tree(
        db_gl3_ir_execute_frag_source, strlen(db_gl3_ir_execute_frag_source),
        DB_GL3_PROBE_SHADER_DOMAIN, implementation_hash);
    db_conformance_key_t key = {
        .schema_version = 1U,
        .evaluator_version = 3U,
        .domain_version = 1U,
        .build_version = 1U,
        .backend = DB_PROBE_BACKEND_GL3,
        .implementation = implementation,
        .working_format = format,
        .logical_width = logical_width,
        .logical_height = logical_height,
        .gradient_window_rows = 32U,
        .implementation_hash = implementation_hash,
        .provider = "gl_context",
        .strategy = "persistent_fbo",
        .driver_name = "OpenGL",
    };
    const char *const renderer = db_gl_get_renderer_string();
    const char *const version = db_gl_get_version_string();
    (void)db_snprintf(key.driver_name, sizeof(key.driver_name), "%s",
                      (renderer != NULL) ? renderer : "unknown");
    (void)db_snprintf(key.driver_info, sizeof(key.driver_info), "%s",
                      (version != NULL) ? version : "unknown");
    return db_conformance_qualify(
        &key, &(const db_conformance_query_t){
                  .ignore_cache = diagnostics->ignore_conformance_cache,
                  .rerun_probe = diagnostics->rerun_conformance_probe,
              });
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
