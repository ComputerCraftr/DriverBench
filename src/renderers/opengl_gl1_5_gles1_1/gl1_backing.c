#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_render_ir.h"
#include "core/db_render_ir_surface.h"
#include "gl1_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../core/db_alloc_policy.h"
#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../damage_trace.h"
#include "../gl_common.h"
#include "core/db_render_types.h"

int db_gl1_backing_uses_rgba16f(void) {
    return DB_BOOL(g_state.backing.texture_format ==
                   DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F);
}

static int has_backing_pixels(void) {
    const size_t required =
        db_checked_mul_size(BACKEND_NAME, "backing_pixel_count",
                            (size_t)g_state.backing.pixel_width,
                            (size_t)g_state.backing.pixel_height);
    return DB_BOOL((required > 0U) &&
                   (g_state.backing.pixel_capacity >= required) &&
                   (g_state.backing.pixels != NULL));
}

static void reserve_backing(size_t required) {
    const size_t capacity = db_size_grow_capacity_3_2(
        g_state.backing.pixel_capacity, required, required);
    const size_t elements =
        (db_gl1_backing_uses_rgba16f() != 0)
            ? db_checked_mul_size(BACKEND_NAME, "backing_channel_count",
                                  capacity, DB_RGBA16F_CHANNELS_PER_PIXEL)
            : capacity;
    const size_t element_size = (db_gl1_backing_uses_rgba16f() != 0)
                                    ? sizeof(uint16_t)
                                    : sizeof(uint32_t);
    db_reserve_array_capacity_or_fail(
        &g_state.backing.pixels, &g_state.backing.pixel_capacity, elements,
        elements, element_size, BACKEND_NAME, "backing_pixels");
    g_state.backing.pixel_capacity = capacity;
}

void db_gl1_ensure_backing_capacity(uint32_t pixel_width,
                                    uint32_t pixel_height) {
    const size_t required =
        db_checked_mul_size(BACKEND_NAME, "backing_pixel_count",
                            (size_t)pixel_width, (size_t)pixel_height);
    if ((pixel_width == 0U) || (pixel_height == 0U) || (required == 0U)) {
        return;
    }
    const uint32_t old_width = g_state.backing.pixel_width;
    const uint32_t old_height = g_state.backing.pixel_height;
    const int target_changed =
        DB_BOOL((old_width != pixel_width) || (old_height != pixel_height) ||
                (g_state.backing.pixels == NULL));
    db_gl_shadow_present_prepare_texture(
        &g_state.presentation.shadow, BACKEND_NAME, pixel_width, pixel_height);
    if (g_state.presentation.config_logged == 0) {
        db_gl_shadow_present_log_decision(BACKEND_NAME, "persistent backing",
                                          &g_state.backing.format,
                                          &g_state.presentation.shadow);
        g_state.presentation.config_logged = 1;
    }
    if ((g_state.backing.pixels == NULL) ||
        (g_state.backing.pixel_capacity < required)) {
        reserve_backing(required);
        g_state.presentation.shadow.backing_valid = 0;
        db_gl_shadow_present_note_shadow_change(&g_state.presentation.shadow,
                                                1);
    }
    g_state.backing.pixel_width = pixel_width;
    g_state.backing.pixel_height = pixel_height;
    if (target_changed != 0) {
        g_state.backing.generation = db_checked_add_u32(
            BACKEND_NAME, "backing_generation", g_state.backing.generation, 1U);
        db_damage_trace_emit_target_lifecycle(&(
            const db_target_lifecycle_event_t){
            .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
            .action = ((old_width == 0U) || (old_height == 0U))
                          ? DB_TARGET_LIFECYCLE_CREATE
                          : DB_TARGET_LIFECYCLE_RECREATE,
            .target = "gl1_backing",
            .target_id = 1U,
            .generation = g_state.backing.generation,
            .old_width = old_width,
            .old_height = old_height,
            .new_width = pixel_width,
            .new_height = pixel_height,
            .format = db_gl1_backing_uses_rgba16f() ? DB_PIXEL_FORMAT_RGBA16F
                                                    : DB_PIXEL_FORMAT_RGBA8,
            .cause = ((old_width == 0U) || (old_height == 0U))
                         ? "initial_target"
                         : "resize",
            .valid_before = 0,
            .valid_after = 0,
        });
    }
}

static void apply_ir(const db_frame_plan_t *plan, const db_render_ir_view_t *ir,
                     db_render_ir_external_binding_view_t bindings,
                     uint32_t pixel_width, uint32_t pixel_height) {
    if (plan == NULL) {
        return;
    }
    const db_pixel_surface_t surface =
        gl1_backing_surface(pixel_width, pixel_height);
    if (db_render_ir_rasterize_surface_with_bindings(
            ir, bindings, g_state.runtime.grid_cols, g_state.runtime.grid_rows,
            &surface) != DB_RENDER_IR_OK) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "failed to lower frame-plan render IR");
    }
}

static size_t executed_upload_bytes(const db_gl_shadow_upload_trace_t *trace) {
    if (trace == NULL) {
        return 0U;
    }
    size_t total = 0U;
    for (size_t index = 0U; index < trace->upload_span_count; index++) {
        const size_t bytes = trace->upload_spans[index].size_bytes;
        if (bytes > (SIZE_MAX - total)) {
            DB_RUNTIME_FAIL(BACKEND_NAME, "upload trace byte count overflow");
        }
        total += bytes;
    }
    return total;
}

void db_gl1_rebuild_backing(const db_frame_plan_t *plan, uint32_t pixel_width,
                            uint32_t pixel_height) {
    if ((plan == NULL) ||
        ((plan->rebuild_ir.command_count == 0U) &&
         (db_render_ir_final_damage_covers(&plan->update_ir, plan->grid_cols,
                                           plan->grid_rows) == 0))) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "missing_canonical_rebuild_source"),
            DB_LOG_U64("target_generation", g_state.backing.generation),
            DB_LOG_U64("rebuild_ir_commands",
                       (plan == NULL) ? 0U : plan->rebuild_ir.command_count),
            DB_LOG_BOOL("backing_valid",
                        g_state.presentation.shadow.backing_valid),
        };
        db_log_fail(BACKEND_NAME, "backing_rebuild_error", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    db_gl1_ensure_backing_capacity(pixel_width, pixel_height);
    if (has_backing_pixels() == 0) {
        return;
    }
    if (plan->rebuild_ir.command_count > 0U) {
        apply_ir(plan, &plan->rebuild_ir, plan->external_bindings, pixel_width,
                 pixel_height);
    }
    apply_ir(plan, &plan->update_ir, (db_render_ir_external_binding_view_t){0},
             pixel_width, pixel_height);
    g_state.presentation.shadow.backing_valid = 1;
    db_gl_shadow_present_note_shadow_change(&g_state.presentation.shadow, 1);
}

void db_gl1_update_backing(const db_frame_plan_t *plan, uint32_t pixel_width,
                           uint32_t pixel_height) {
    if (plan == NULL) {
        return;
    }
    db_gl1_ensure_backing_capacity(pixel_width, pixel_height);
    if (has_backing_pixels() == 0) {
        return;
    }
    apply_ir(plan, &plan->update_ir, (db_render_ir_external_binding_view_t){0},
             pixel_width, pixel_height);
    g_state.presentation.shadow.backing_valid = 1;
    db_gl_shadow_present_note_shadow_change(&g_state.presentation.shadow, 0);
    g_state.telemetry.backing.backing_incremental_frames++;
}

int db_gl1_present_backing(const db_frame_plan_t *plan,
                           db_pixel_block_view_t blocks_view,
                           uint32_t pixel_width, uint32_t pixel_height) {
    const db_pixel_surface_t surface =
        gl1_backing_surface(pixel_width, pixel_height);
    const uint64_t backing_hash =
        (db_damage_trace_enabled() != 0)
            ? db_damage_trace_surface_hash_oriented(&surface, 1)
            : 0U;
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = g_state.telemetry.frame.frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
        .stage = DB_DAMAGE_TRACE_STAGE_SHADOW_WRITE,
        .operation = DB_DAMAGE_TRACE_OP_DRAW,
        .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
        .destination = DB_DAMAGE_TRACE_BUFFER_GL1_SHADOW,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = pixel_width,
        .height = pixel_height,
        .pixel_format = db_gl1_backing_uses_rgba16f() ? DB_PIXEL_FORMAT_RGBA16F
                                                      : DB_PIXEL_FORMAT_RGBA8,
        .blocks = blocks_view.blocks,
        .block_count = blocks_view.count,
        .destination_hash = backing_hash,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        .target = "gl1_backing",
        .target_generation = g_state.backing.generation,
        .present_method = "none",
    });
    const db_gl_pixel_upload_payload_t payload =
        db_gl_pixel_upload_payload_from_surface(&surface);
    db_gl_shadow_upload_trace_reset(&g_state.presentation.shadow.upload_trace);
    db_gl_shadow_present_present_replace_pixels_ir(
        &g_state.presentation.shadow, BACKEND_NAME, &payload,
        blocks_view.blocks, blocks_view.count, plan);
    const db_gl_shadow_upload_trace_t *const upload_trace =
        &g_state.presentation.shadow.upload_trace;
    const size_t transfer_bytes = executed_upload_bytes(upload_trace);
    const int fallback = DB_BOOL(upload_trace->fallback_mode_label != NULL);
    db_damage_trace_result_t result = DB_DAMAGE_TRACE_RESULT_FAILED;
    if (upload_trace->full_upload_executed != 0) {
        result = (fallback != 0) ? DB_DAMAGE_TRACE_RESULT_FALLBACK
                                 : DB_DAMAGE_TRACE_RESULT_EXECUTED;
    }
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = g_state.telemetry.frame.frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
        .stage = DB_DAMAGE_TRACE_STAGE_UPLOAD,
        .operation = (fallback != 0) ? DB_DAMAGE_TRACE_OP_FALLBACK
                                     : DB_DAMAGE_TRACE_OP_UPLOAD,
        .source = DB_DAMAGE_TRACE_BUFFER_GL1_SHADOW,
        .source_index = 0U,
        .destination = DB_DAMAGE_TRACE_BUFFER_GL_TEXTURE,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = pixel_width,
        .height = pixel_height,
        .pixel_format = surface.format,
        .blocks = blocks_view.blocks,
        .block_count = blocks_view.count,
        .transfer_size_bytes = transfer_bytes,
        .source_hash = backing_hash,
        .mode = upload_trace->executed_upload_mode_label,
        .reason = upload_trace->fallback_mode_label,
        .result = result,
        .target = "gl1_backing",
        .target_generation = g_state.backing.generation,
        .present_method = "upload_texture",
    });
    return 1;
}
