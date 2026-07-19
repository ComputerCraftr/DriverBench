#include "vk_frame_finalize.h"

#include "config/benchmark_config.h"
#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_frame_plan.h"
#include "core/db_numeric.h"
#include "core/db_render_result.h"
#include "core/db_renderer_support.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include "vk_runtime_internal.h"
#include "vk_state_internal.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

db_vk_frame_result_t
db_vk_finalize_frame(const db_vk_frame_finalize_input_t *input) {
    if ((input == NULL) || (input->plan == NULL) ||
        (input->execution_plan == NULL) || (input->frame_work_units == NULL)) {
        return DB_VK_FRAME_STOP;
    }
    const db_frame_plan_t *const plan = input->plan;
    if (!g_state.device.gpu_timing_enabled) {
        const uint64_t frame_end_ns = db_now_ns_monotonic();
        const double frame_ms =
            DB_TO_F64(frame_end_ns - input->frame_start_ns) / DB_NS_PER_MS;
        db_vk_update_ema_fallback(input->gpu_count, input->frame_work_units,
                                  frame_ms,
                                  g_state.scheduler.ema_ms_per_work_unit);
    }
    {
        const uint64_t frame_end_ns = db_now_ns_monotonic();
        const double frame_ms =
            DB_TO_F64(frame_end_ns - input->frame_start_ns) / DB_NS_PER_MS;
        db_vk_record_render_frame_duration(frame_ms);
        db_vk_scheduler_update_frame_pacing(
            frame_ms, &g_state.metrics.frame_time_ema_ms,
            &g_state.metrics.frame_jitter_ema_ms);
    }

    g_state.frame.state_hash = plan->expected_state_hash;
    if ((g_state.calibration.state.phase == DB_VK_MULTI_GPU_WARMING) ||
        (g_state.calibration.state.phase == DB_VK_MULTI_GPU_CALIBRATING)) {
        db_vk_calibration_run_after_live(plan);
    }
    if (g_state.hash.output_hash_enabled != 0) {
        VkImage hash_image = VK_NULL_HANDLE;
        VkImageLayout hash_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (g_state.runtime.pipeline.uses_history_pipeline != 0) {
            hash_image = g_state.backing.targets[input->backing_index].image;
            hash_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else if (input->use_offscreen_target != 0) {
            hash_image = g_state.backing.targets[0].image;
            hash_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        g_state.hash.output_hash = db_vk_compute_output_hash_from_image(
            hash_image, hash_layout, g_state.backing.extent);
    }
    db_renderer_record_draw_stats_for_work(
        &g_state.frame.full_draw_frames, &g_state.frame.dirty_draw_frames,
        input->frame_full_draw, input->frame_dirty_draw,
        input->grid_tiles_drawn);
    const uint32_t gradient_commands =
        plan->update_metadata.gradient_count +
        ((plan->rebuild_required != 0) ? plan->rebuild_metadata.gradient_count
                                       : 0U);
    const uint32_t solid_commands =
        plan->update_metadata.solid_command_count +
        ((plan->rebuild_required != 0)
             ? plan->rebuild_metadata.solid_command_count
             : 0U);
    db_render_operation_path_t gradient_path = DB_RENDER_OPERATION_NONE;
    if (gradient_commands > 0U) {
        switch (input->gradient_implementation) {
        case DB_GRADIENT_IMPLEMENTATION_SEMANTIC:
            gradient_path = DB_RENDER_OPERATION_VULKAN_SEMANTIC_GRADIENT;
            break;
        case DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP:
            gradient_path = DB_RENDER_OPERATION_VULKAN_EXACT_LOOKUP;
            break;
        case DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES:
            gradient_path = DB_RENDER_OPERATION_VULKAN_ROW_FILL;
            break;
        }
    }
    g_state.execution = (db_render_execution_report_t){
        .target_strategy = DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE,
        .solid_path = DB_RENDER_OPERATION_VULKAN_INSTANCED_SOLID,
        .gradient_path = gradient_path,
        .solid_commands = solid_commands,
        .gradient_commands = gradient_commands,
        .solid_draws = db_checked_size_to_u32(
            BACKEND_NAME, "piece_draws", input->execution_plan->piece_count),
        .gradient_draws = DB_BOOL(gradient_commands > 0U),
        .fallback_instances =
            (input->gradient_implementation !=
             DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES)
                ? 0U
                : plan->update_metadata.exact_fallback_instance_count,
        .lookup_words = db_checked_size_to_u32(
            BACKEND_NAME, "lookup_word_count", input->lookup_word_count),
        .gradient_implementation = input->gradient_implementation,
        .qualification_source = g_state.scheduler.gradient_applied.source,
        .cache_status = g_state.scheduler.gradient_applied.cache_status,
        .qualification_lane_count =
            g_state.scheduler.gradient_applied.lane_count,
        .qualification_reason = g_state.scheduler.gradient_applied.reason,
        .qualified = g_state.scheduler.gradient_applied.production_qualified,
        .diagnostic_forced =
            g_state.scheduler.gradient_applied.diagnostic_forced,
    };
    for (uint32_t gpu = 0U; gpu < input->gpu_count; gpu++) {
        g_state.scheduler.cumulative_work_units[gpu] +=
            (uint64_t)input->frame_work_units[gpu];
        if (input->frame_work_units[gpu] > 0U) {
            g_state.scheduler.cumulative_frames_with_work[gpu]++;
        }
    }
    g_state.metrics.bench_frames++;
    const double bench_ms =
        DB_TO_F64(db_now_ns_monotonic() - g_state.metrics.bench_start_ns) /
        DB_NS_PER_MS;
    db_log_progress_periodic(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.metrics.bench_frames, g_state.runtime.work_unit_count, bench_ms,
        &g_state.metrics.next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS);
    g_state.frame.state_hash = plan->expected_state_hash;
    g_state.frame.frame_index++;
    return DB_VK_FRAME_OK;
}
