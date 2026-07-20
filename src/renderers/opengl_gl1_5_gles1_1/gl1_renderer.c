#include "gl1_renderer.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_hash_readback.h"
#include "../renderer_viewport_common.h"
#include "core/db_frame_contracts.h"
#include "core/db_log.h"
#include "core/db_qualification_contracts.h"
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
    g_state.diagnostics = resolved_runtime->diagnostics;
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
    if ((db_gl1_native_init() == 0) || (db_gl1_replay_init() == 0)) {
        g_state.native.strategy = GL1_STRATEGY_CPU_UPLOAD;
    }
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
        DB_LOG_TOKEN("geometry_storage", "runtime_selected"),
        DB_LOG_TOKEN("target_strategy_candidate", "persistent_fbo"),
        DB_LOG_TOKEN("fallback_target_strategy", "cpu_upload"),
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

const db_renderer_qualification_ops_t *db_gl1_qualification_ops(void) {
    return db_gl1_native_qualification_ops();
}

int db_gl1_render_frame(const db_frame_plan_t *plan,
                        const db_renderer_target_t *target,
                        int viewport_width_px, int viewport_height_px,
                        db_pixel_block_view_t presentation_damage,
                        int force_full_presentation) {
    if ((plan == NULL) || (target == NULL) || (target->valid == 0)) {
        return 0;
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
    int presentation_fbo = 0;
    db_gl_get_integerv(GL_DRAW_FRAMEBUFFER_BINDING, &presentation_fbo);
    const int native_rendered = db_gl1_native_render(
        plan, plan->pixel_width, target, plan->pixel_height, presentation_fbo,
        viewport_width_px, viewport_height_px);
    if ((native_rendered == 0) &&
        (target->strategy != DB_RENDER_TARGET_GL1_CPU_UPLOAD)) {
        return 0;
    }
    if (native_rendered == 0) {
        g_state.native.strategy = GL1_STRATEGY_CPU_UPLOAD;
        db_gl1_render_geometry_to_backing(
            plan,
            db_checked_u32_to_i32(BACKEND_NAME, "logical_raster_width",
                                  plan->pixel_width),
            db_checked_u32_to_i32(BACKEND_NAME, "logical_raster_height",
                                  plan->pixel_height));
        db_gl1_replay_prepare_boundary();
    }
    g_state.telemetry.frame.state_hash = plan->expected_state_hash;
    g_state.telemetry.frame.frame_index++;
    return 1;
}

void db_gl1_shutdown(void) {
    db_gl1_replay_shutdown();
    db_gl1_native_shutdown();
    free(g_state.backing.pixels);
    free(g_state.upload.blocks);
    db_gl_shadow_present_shutdown(&g_state.presentation.shadow);
    g_state = (renderer_state_t){0};
}

void db_gl1_replay_preflight_facts(db_render_target_strategy_t *strategy,
                                   uint64_t *target_generation,
                                   int *direct_window_lineage_valid) {
    if (strategy != NULL) {
        *strategy = g_state.replay.committed_strategy;
    }
    if (target_generation != NULL) {
        *target_generation = g_state.replay.committed_target_generation;
    }
    if (direct_window_lineage_valid != NULL) {
        *direct_window_lineage_valid =
            g_state.replay.direct_window_lineage_valid;
    }
}

void db_gl1_finalize_frame(int commit,
                           db_render_target_strategy_t target_strategy,
                           uint64_t target_generation) {
    if (commit != 0) {
        const gl1_replay_entry_t *const pending =
            &g_state.replay.entries[g_state.replay.pending_entry];
        const int lineage_valid =
            DB_BOOL((target_strategy == DB_RENDER_TARGET_GL1_DIRECT_WINDOW) &&
                    (pending->valid != 0) && (pending->replay_boundary == 0));
        db_gl1_replay_publish_pending();
        g_state.replay.committed_strategy = target_strategy;
        g_state.replay.committed_target_generation = target_generation;
        g_state.replay.direct_window_lineage_valid = lineage_valid;
    } else {
        db_gl1_replay_discard_pending();
    }
}

const char *db_gl1_capability_mode(void) {
    if (g_state.telemetry.capability_mode[0] == '\0') {
        db_gl1_refresh_capability_mode();
    }
    return g_state.telemetry.capability_mode;
}

uint64_t db_gl1_state_hash(void) { return g_state.telemetry.frame.state_hash; }

uint64_t db_gl1_working_hash(void) {
    if ((g_state.native.strategy == GL1_STRATEGY_DIRECT_WINDOW) &&
        (g_state.native.valid != 0)) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.native.direct_fbo);
        return db_gl_hash_framebuffer_rgba8_or_fail(
            BACKEND_NAME, g_state.native.width, g_state.native.height,
            &g_state.native.hash_scratch, 1);
    }
    if ((g_state.native.strategy == GL1_STRATEGY_PERSISTENT_FBO) &&
        (g_state.native.fbo != 0U)) {
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, g_state.native.fbo);
        const uint64_t hash =
            (g_state.backing.texture_format ==
             DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                ? db_gl_hash_framebuffer_rgba16f_or_fail(
                      BACKEND_NAME, g_state.native.width, g_state.native.height,
                      &g_state.native.hash_scratch, 1)
                : db_gl_hash_framebuffer_rgba8_or_fail(
                      BACKEND_NAME, g_state.native.width, g_state.native.height,
                      &g_state.native.hash_scratch, 1);
        db_gl_bind_framebuffer(GL_FRAMEBUFFER, 0U);
        return hash;
    }
    const db_pixel_surface_t surface = gl1_backing_surface(
        g_state.backing.pixel_width, g_state.backing.pixel_height);
    const size_t pixel_bytes =
        (surface.format == DB_PIXEL_FORMAT_RGBA16F) ? 8U : 4U;
    const size_t row_stride_bytes =
        db_checked_mul_size(BACKEND_NAME, "working hash row stride",
                            surface.pixel_width, pixel_bytes);
    return db_hash_working_rgba8(surface.pixels, surface.format,
                                 surface.pixel_width, surface.pixel_height,
                                 row_stride_bytes, 0);
}

uint32_t db_gl1_work_unit_count(void) {
    return g_state.runtime.work_unit_count;
}

void db_gl1_draw_stats(db_renderer_draw_path_stats_t *stats) {
    db_renderer_copy_draw_path_stats(&g_state.telemetry.frame, stats);
}

void db_gl1_execution_report(db_render_execution_report_t *report) {
    if (report != NULL) {
        *report = g_state.telemetry.execution;
    }
}
