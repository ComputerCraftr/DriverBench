#include "core/db_log.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_support.h"
#include "gl1_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_ir.h"
#include "../damage_trace.h"
#include "../gl_common.h"

static uint64_t upload_trace_bytes(const db_gl_shadow_upload_trace_t *trace) {
    uint64_t total = 0U;
    if (trace == NULL) {
        return 0U;
    }
    for (size_t index = 0U; index < trace->upload_span_count; index++) {
        total = db_checked_add_u64(BACKEND_NAME, "executed_upload_bytes", total,
                                   trace->upload_spans[index].size_bytes);
    }
    return total;
}

static db_pixel_block_view_t upload_blocks_for_plan(const db_frame_plan_t *plan,
                                                    uint32_t pixel_width,
                                                    uint32_t pixel_height,
                                                    int rebuilt) {
    if ((plan == NULL) || (g_state.upload.blocks == NULL) ||
        (g_state.upload.capacity == 0U)) {
        return (db_pixel_block_view_t){0};
    }
    if (rebuilt != 0) {
        g_state.upload.blocks[0] = (db_damage_block_t){
            .row_start = 0U,
            .row_count = pixel_height,
            .col_start = 0U,
            .col_count = pixel_width,
        };
        return (db_pixel_block_view_t){
            .blocks = g_state.upload.blocks,
            .count = 1U,
        };
    }
    const db_render_ir_region_id_t region_id =
        db_render_ir_final_damage_region(&plan->update_ir);
    if (region_id == DB_RENDER_IR_INVALID_ID) {
        return (db_pixel_block_view_t){0};
    }
    const db_render_ir_region_t region = plan->update_ir.regions[region_id];
    size_t count = 0U;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            plan->update_ir.bands[region.first_band + band_index];
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
            if (count >= g_state.upload.capacity) {
                g_state.upload.blocks[0] = (db_damage_block_t){
                    .row_count = pixel_height, .col_count = pixel_width};
                return (db_pixel_block_view_t){.blocks = g_state.upload.blocks,
                                               .count = 1U};
            }
            const db_render_ir_span_t span =
                plan->update_ir.spans[band.first_span + span_index];
            db_grid_block_t grid_block = {0};
            if (db_render_ir_rect_to_grid_block(
                    (db_render_ir_rect_t){
                        .x = span.x_start,
                        .y = band.y_start,
                        .width = span.x_end - span.x_start,
                        .height = band.y_end - band.y_start,
                    },
                    g_state.runtime.grid_cols, g_state.runtime.grid_rows,
                    &grid_block) == 0) {
                g_state.upload.blocks[0] = (db_damage_block_t){
                    .row_count = pixel_height, .col_count = pixel_width};
                return (db_pixel_block_view_t){.blocks = g_state.upload.blocks,
                                               .count = 1U};
            }
            if (db_grid_block_to_pixel_block(
                    g_state.runtime.grid_cols, g_state.runtime.grid_rows,
                    &grid_block, pixel_width, pixel_height,
                    &g_state.upload.blocks[count]) != 0) {
                count++;
            }
        }
    }
    return (db_pixel_block_view_t){
        .blocks = g_state.upload.blocks,
        .count = count,
    };
}

static void trace_plan(const db_frame_plan_t *plan,
                       db_pixel_block_view_t pixel_blocks, uint32_t pixel_width,
                       uint32_t pixel_height) {
    if (db_damage_trace_enabled() == 0) {
        return;
    }
    const db_pixel_format_t format = db_gl1_backing_uses_rgba16f()
                                         ? DB_PIXEL_FORMAT_RGBA16F
                                         : DB_PIXEL_FORMAT_RGBA8;
    (void)db_damage_trace_emit_grid(
        &(const db_damage_trace_event_t){
            .frame_index = plan->frame_index,
            .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
            .stage = DB_DAMAGE_TRACE_STAGE_LOGICAL,
            .operation = DB_DAMAGE_TRACE_OP_COPY,
            .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .destination = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
            .space = DB_DAMAGE_TRACE_SPACE_GRID,
            .width = g_state.runtime.grid_cols,
            .height = g_state.runtime.grid_rows,
            .pixel_format = format,
            .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        },
        NULL, 0U);
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = plan->frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
        .stage = DB_DAMAGE_TRACE_STAGE_NORMALIZED,
        .operation = DB_DAMAGE_TRACE_OP_COPY,
        .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
        .destination = DB_DAMAGE_TRACE_BUFFER_GL1_SHADOW,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = pixel_width,
        .height = pixel_height,
        .pixel_format = format,
        .blocks = pixel_blocks.blocks,
        .block_count = pixel_blocks.count,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
    });
}

void db_gl1_render_geometry_to_backing(const db_frame_plan_t *plan,
                                       int viewport_width,
                                       int viewport_height) {
    if ((plan == NULL) || (viewport_width <= 0) || (viewport_height <= 0) ||
        (plan->update_metadata.status != DB_RENDER_IR_OK) ||
        (plan->rebuild_metadata.status != DB_RENDER_IR_OK)) {
        return;
    }
    const uint32_t pixel_width =
        db_checked_int_to_u32(BACKEND_NAME, "backing_width", viewport_width);
    const uint32_t pixel_height =
        db_checked_int_to_u32(BACKEND_NAME, "backing_height", viewport_height);
    const int rebuilt =
        DB_BOOL((g_state.presentation.shadow.backing_valid == 0) ||
                (plan->rebuild_required != 0));
    if (rebuilt != 0) {
        db_gl1_rebuild_backing(plan, pixel_width, pixel_height);
        g_state.telemetry.backing.backing_rebuild_frames++;
        db_damage_trace_emit_target_lifecycle(&(
            const db_target_lifecycle_event_t){
            .backend = DB_DAMAGE_TRACE_BACKEND_GL1,
            .action = DB_TARGET_LIFECYCLE_REBUILD,
            .target = "gl1_backing",
            .target_id = 1U,
            .generation = g_state.backing.generation,
            .new_width = pixel_width,
            .new_height = pixel_height,
            .format = db_gl1_backing_uses_rgba16f() ? DB_PIXEL_FORMAT_RGBA16F
                                                    : DB_PIXEL_FORMAT_RGBA8,
            .cause = "frame_plan",
            .valid_before = 0,
            .valid_after = 1,
        });
    } else {
        db_gl1_update_backing(plan, pixel_width, pixel_height);
    }
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_GL1, "gl1_backing",
                                    g_state.backing.generation, plan);
    const db_pixel_block_view_t upload_blocks =
        upload_blocks_for_plan(plan, pixel_width, pixel_height, rebuilt);
    trace_plan(plan, upload_blocks, pixel_width, pixel_height);
    if (db_gl1_present_backing(plan, upload_blocks, pixel_width,
                               pixel_height) == 0) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "failed to present persistent backing target");
    }
    g_state.telemetry.execution = (db_render_execution_report_t){
        .target_strategy = DB_RENDER_TARGET_GL1_CPU_UPLOAD,
        .solid_path = DB_RENDER_OPERATION_GL1_CPU_UPLOAD,
        .gradient_path = (plan->update_metadata.gradient_count > 0U)
                             ? DB_RENDER_OPERATION_GL1_CPU_UPLOAD
                             : DB_RENDER_OPERATION_NONE,
        .solid_commands =
            plan->update_metadata.solid_command_count +
            ((rebuilt != 0) ? plan->rebuild_metadata.solid_command_count : 0U),
        .gradient_commands =
            plan->update_metadata.gradient_count +
            ((rebuilt != 0) ? plan->rebuild_metadata.gradient_count : 0U),
        .fallback_instances =
            plan->update_metadata.exact_fallback_instance_count,
        .cpu_pixels_written =
            plan->update_metadata.damage_area +
            ((rebuilt != 0) ? plan->rebuild_metadata.damage_area : 0U),
        .uploaded_bytes =
            upload_trace_bytes(&g_state.presentation.shadow.upload_trace),
        .surface_restoration_bytes =
            g_state.presentation.shadow.last_surface_restoration_bytes,
        .encoded_span_bytes =
            g_state.presentation.shadow.last_encoded_span_bytes,
    };
    const int full_present = g_state.presentation.current_present_full;
    const uint32_t work_count =
        db_checked_size_to_u32(BACKEND_NAME, "geometry_work_count",
                               plan->update_metadata.instance_count +
                                   ((plan->external_bindings.count == 0U)
                                        ? plan->rebuild_metadata.instance_count
                                        : 0U));
    db_renderer_record_draw_stats_for_work(
        &g_state.telemetry.frame.full_draw_frames,
        &g_state.telemetry.frame.dirty_draw_frames, full_present,
        DB_BOOL(full_present == 0), work_count);
    db_renderer_record_draw_path(&g_state.telemetry.frame.draw_paths,
                                 full_present, DB_BOOL(full_present == 0), 0, 0,
                                 work_count);
}
