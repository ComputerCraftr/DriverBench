#include "gl1_renderer.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../gl_common.h"
#include "../renderer_viewport_common.h"
#include "core/db_log.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_runtime_contract.h"
#include "core/db_renderer_support.h"
#include "gl1_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

renderer_state_t g_gl1_state = {0};

void db_gl1_init(const db_renderer_runtime_contract_t *resolved_runtime) {
    g_state = (renderer_state_t){0};
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_dither_enabled(0);

    if (resolved_runtime == NULL) {
        runtime_failf("missing resolved runtime");
    }
    const db_renderer_execution_config_t runtime_state =
        resolved_runtime->execution;
    const uint32_t default_preserved_framebuffer_count =
        resolved_runtime->preserved_framebuffer_count;
    if (db_gl1_init_runtime(&runtime_state) == 0) {
        runtime_failf("failed to initialize renderer metadata");
    }

    g_state.backing.format = resolved_runtime->format;
    db_gl_shadow_present_init_runtime(&g_state.presentation.shadow, 1, 0,
                                      &g_state.backing.format,
                                      default_preserved_framebuffer_count);
    g_state.backing.texture_format =
        g_state.presentation.shadow.selected_texture_format;
    g_state.upload.capacity = DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY;
    g_state.upload.blocks = (db_damage_block_t *)db_malloc_or_fail(
        BACKEND_NAME, "backing_upload_blocks", g_state.upload.capacity,
        sizeof(*g_state.upload.blocks));

    db_gl1_refresh_capability_mode();

    const db_log_field_t capability_fields[] = {
        DB_LOG_TOKEN("draw_strategy",
                     (g_state.runtime.backbuffer_draw_full != 0)
                         ? "full_present"
                         : "dirty_present"),
        DB_LOG_TOKEN("geometry_storage", "cpu_backing"),
        DB_LOG_TOKEN(
            "present_geometry_storage",
            (g_state.presentation.shadow.presentation_quad_uses_vbo != 0)
                ? "vbo"
                : "client_arrays"),
        DB_LOG_BOOL("replay_enabled", 0),
        DB_LOG_TOKEN(
            "upload_mode",
            db_gl_stream_upload_name(
                &g_state.presentation.shadow.upload_profile.effective_partial,
                0, 1)),
        DB_LOG_BOOL("hdr_content_supported",
                    resolved_runtime->format.hdr_content_supported),
        DB_LOG_TOKEN("hdr_conversion",
                     db_hdr_conversion_implementation_name(
                         g_state.presentation.shadow.hdr_conversion)),
    };
    db_log_info(BACKEND_NAME, "renderer_capability", capability_fields,
                DB_LOG_FIELD_COUNT(capability_fields));
}

void db_gl1_render_frame(const db_frame_plan_t *plan, int viewport_width_px,
                         int viewport_height_px,
                         db_pixel_block_view_t presentation_damage,
                         int force_full_presentation) {
    if (plan == NULL) {
        return;
    }
    g_state.telemetry.frame.frame_index = plan->frame_index;
    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(
            BACKEND_NAME, plan->grid_cols, plan->grid_rows, &viewport_width_px,
            &viewport_height_px, &g_state.viewport.last_viewport_w,
            &g_state.viewport.last_viewport_h);

    if (viewport_state.viewport_changed != 0) {
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
    }
    db_gl_shadow_present_set_preserve_mode(
        &g_state.presentation.shadow,
        DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE);
    db_gl_shadow_present_set_draw_damage(&g_state.presentation.shadow,
                                         presentation_damage,
                                         force_full_presentation);
    g_state.presentation.current_present_full =
        DB_BOOL(force_full_presentation != 0);
    db_gl1_render_geometry_to_backing(
        plan,
        db_checked_u32_to_i32(BACKEND_NAME, "logical_raster_width",
                              plan->pixel_width),
        db_checked_u32_to_i32(BACKEND_NAME, "logical_raster_height",
                              plan->pixel_height));
    g_state.telemetry.frame.state_hash = plan->expected_state_hash;
    g_state.telemetry.frame.frame_index++;
}

void db_gl1_shutdown(void) {
    free(g_state.backing.pixels);
    free(g_state.upload.blocks);
    db_gl_shadow_present_shutdown(&g_state.presentation.shadow);
    g_state = (renderer_state_t){0};
}

const char *db_gl1_capability_mode(void) {
    if (g_state.telemetry.capability_mode[0] == '\0') {
        db_gl1_refresh_capability_mode();
    }
    return g_state.telemetry.capability_mode;
}

uint64_t db_gl1_state_hash(void) { return g_state.telemetry.frame.state_hash; }

uint64_t db_gl1_working_hash(void) {
    const db_pixel_surface_t surface = gl1_backing_surface(
        g_state.backing.pixel_width, g_state.backing.pixel_height);
    const size_t pixel_bytes =
        (surface.format == DB_PIXEL_FORMAT_RGBA16F) ? 8U : 4U;
    return db_hash_working_rgba8(surface.pixels, surface.format,
                                 surface.pixel_width, surface.pixel_height,
                                 (size_t)surface.pixel_width * pixel_bytes, 0);
}

uint32_t db_gl1_work_unit_count(void) {
    return g_state.runtime.work_unit_count;
}

void db_gl1_draw_stats(db_renderer_draw_path_stats_t *stats) {
    db_renderer_copy_draw_path_stats(&g_state.telemetry.frame, stats);
}
