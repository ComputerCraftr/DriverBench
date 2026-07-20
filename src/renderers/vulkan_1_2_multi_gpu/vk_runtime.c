#include "../../core/db_core.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_result.h"
#include "core/db_progress_policy.h"
#include "core/db_renderer_log.h"
#include "core/db_renderer_support.h"
#include "vk_diagnostics.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include "vk_runtime_internal.h"
#include "vk_state_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

void db_vk_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }
    const uint64_t bench_end = db_now_ns_monotonic();
    const double bench_ms =
        DB_TO_F64(bench_end - g_state.metrics.bench_start_ns) / DB_NS_PER_MS;
    db_renderer_log_final_summary(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.metrics.bench_frames, g_state.runtime.work_unit_count, bench_ms,
        db_vk_draw_stats, db_vk_execution_report);
    const db_log_field_t metric_fields[] = {
        DB_LOG_TOKEN("percentile_scope", "recent_window"),
        DB_LOG_U64("window_capacity",
                   g_state.metrics.render_metric_window_capacity),
        DB_LOG_U64("render_window_sample_count",
                   g_state.metrics.render_metric_window_sample_count),
        DB_LOG_U64("render_total_samples",
                   g_state.metrics.render_metric_total_samples),
        DB_LOG_TOKEN("secondary_kind",
                     (g_state.presentation.no_present_mode != 0) ? "loop"
                                                                 : "present"),
        DB_LOG_DOUBLE("render_frame_ema_ms", g_state.metrics.frame_time_ema_ms),
        DB_LOG_DOUBLE("render_jitter_ema_ms",
                      g_state.metrics.frame_jitter_ema_ms),
        DB_LOG_DOUBLE("render_window_p50_ms",
                      g_state.metrics.render_frame_window_p50_ms),
        DB_LOG_DOUBLE("render_window_p95_ms",
                      g_state.metrics.render_frame_window_p95_ms),
        DB_LOG_DOUBLE("render_window_p99_ms",
                      g_state.metrics.render_frame_window_p99_ms),
        DB_LOG_DOUBLE("secondary_frame_ema_ms",
                      g_state.metrics.present_frame_ema_ms),
        DB_LOG_DOUBLE("secondary_jitter_ema_ms",
                      g_state.metrics.present_jitter_ema_ms),
        DB_LOG_U64("secondary_window_sample_count",
                   g_state.metrics.present_metric_window_sample_count),
        DB_LOG_U64("secondary_window_capacity",
                   g_state.metrics.present_metric_window_capacity),
        DB_LOG_U64("secondary_total_samples",
                   g_state.metrics.present_metric_total_samples),
        DB_LOG_DOUBLE("secondary_window_p50_ms",
                      g_state.metrics.present_frame_window_p50_ms),
        DB_LOG_DOUBLE("secondary_window_p95_ms",
                      g_state.metrics.present_frame_window_p95_ms),
        DB_LOG_DOUBLE("secondary_window_p99_ms",
                      g_state.metrics.present_frame_window_p99_ms),
        DB_LOG_U64("retries", g_state.metrics.present_retries),
    };
    db_log_info(BACKEND_NAME, "vk_metrics", metric_fields,
                DB_LOG_FIELD_COUNT(metric_fields));
    const uint32_t gpu_count =
        db_vk_normalize_gpu_count(g_state.device.selection.lane_count);
    uint64_t total_work_units = 0U;
    for (uint32_t g = 0; g < gpu_count; g++) {
        total_work_units += g_state.scheduler.cumulative_work_units[g];
    }
    for (uint32_t g = 0; g < gpu_count; g++) {
        const double share_pct =
            (total_work_units > 0U)
                ? (DB_TO_F64(g_state.scheduler.cumulative_work_units[g]) *
                   100.0) /
                      DB_TO_F64(total_work_units)
                : 0.0;
        const double ema_ms_per_unit =
            g_state.scheduler.ema_ms_per_work_unit[g];
        const double ema_ns_per_unit = ema_ms_per_unit * DB_NS_PER_MS;
        const double ema_units_per_ms =
            db_f64_reciprocal_positive_finite_or(ema_ms_per_unit, 0.0);
        const db_vk_device_lane_t *lane =
            (g < g_state.device.selection.lane_count)
                ? &g_state.device.selection.lanes[g]
                : NULL;
        const char *lane_name = (lane != NULL) ? lane->name : "unknown";
        const char *lane_reason =
            ((lane != NULL) && (lane->inactive_reason[0] != '\0'))
                ? lane->inactive_reason
                : "active";
        const db_log_field_t lane_fields[] = {
            DB_LOG_U64("lane", g),
            DB_LOG_STRING("name", lane_name),
            DB_LOG_BOOL("active",
                        (lane != NULL) ? lane->active_for_scheduler : 0),
            DB_LOG_STRING("reason", lane_reason),
            DB_LOG_U64("work_units",
                       g_state.scheduler.cumulative_work_units[g]),
            DB_LOG_DOUBLE("share_pct", share_pct),
            DB_LOG_U64("active_frames",
                       g_state.scheduler.cumulative_frames_with_work[g]),
            DB_LOG_DOUBLE("ema_ms_per_unit", ema_ms_per_unit),
            DB_LOG_DOUBLE("ema_ns_per_unit", ema_ns_per_unit),
            DB_LOG_DOUBLE("ema_units_per_ms", ema_units_per_ms),
        };
        db_log_info(BACKEND_NAME, "vk_scheduler_lane_stats", lane_fields,
                    DB_LOG_FIELD_COUNT(lane_fields));
    }
    if (g_state.presentation.in_flight != VK_NULL_HANDLE) {
        DB_VK_CHECK(BACKEND_NAME,
                    db_vk_wait_fence(
                        g_state.device.device, g_state.presentation.in_flight,
                        DB_PROGRESS_VK_PRIMARY_FENCE, "renderer_shutdown"));
    }
    db_vk_release_rebuild_upload_buffer();
    db_vk_calibration_shutdown();
    db_vk_device_group_lanes_shutdown();
    db_vk_independent_lanes_shutdown();
    const db_vk_cleanup_ctx_t cleanup = {
        .device = g_state.device.device,
        .in_flight = g_state.presentation.in_flight,
        .image_available = g_state.presentation.image_available,
        .render_done = g_state.presentation.render_done,
        .vertex_buffer = g_state.pipelines.vertex_buffer,
        .vertex_memory = g_state.pipelines.vertex_memory,
        .instance_buffer = g_state.pipelines.instance_buffer,
        .instance_memory = g_state.pipelines.instance_memory,
        .instance_mapped = g_state.pipelines.instance_mapped,
        .lookup_buffer = g_state.pipelines.lookup_buffer,
        .lookup_memory = g_state.pipelines.lookup_memory,
        .lookup_mapped = g_state.pipelines.lookup_mapped,
        .hash_readback_buffer = g_state.hash.hash_readback_buffer,
        .hash_readback_memory = g_state.hash.hash_readback_memory,
        .pipeline = g_state.pipelines.pipeline,
        .present_pipeline = g_state.presentation.present_pipeline,
        .composition_pipeline = g_state.pipelines.composition_pipeline,
        .pipeline_layout = g_state.pipelines.pipeline_layout,
        .swapchain_state = &g_state.presentation.swapchain_state,
        .backing_targets = g_state.backing.targets,
        .render_pass = g_state.presentation.render_pass,
        .backing_render_pass = g_state.backing.render_pass,
        .command_pool = g_state.device.command_pool,
        .timing_query_pool = g_state.device.timing_query_pool,
        .descriptor_set_layout = g_state.pipelines.descriptor_set_layout,
        .descriptor_pool = g_state.pipelines.descriptor_pool,
        .backing_sampler = g_state.backing.sampler,
        .instance = g_state.device.instance,
        .surface = g_state.presentation.surface,
    };
    db_vk_cleanup_runtime(&cleanup);
    free(g_state.scheduler.planner_workspace.ranges);
    free(g_state.scheduler.assignment_storage);
    free(g_state.scheduler.piece_storage);
    g_state = (renderer_state_t){0};
}

const char *db_vk_capability_mode(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : DB_CAP_MODE_VK_DRAW_TILES_FULL
               "(upload=" DB_CAP_MODE_VK_UPLOAD_NONE ",backbuffer_replay=no)";
}

uint32_t db_vk_work_unit_count(void) {
    return (g_state.initialized != 0) ? g_state.runtime.work_unit_count : 0U;
}

uint64_t db_vk_state_hash(void) { return g_state.frame.state_hash; }

uint64_t db_vk_output_hash(void) { return g_state.hash.output_hash; }

void db_vk_set_output_hash_enabled(int enabled) {
    g_state.hash.output_hash_enabled = DB_BOOL(enabled);
}

void db_vk_draw_stats(db_renderer_draw_path_stats_t *stats) {
    db_renderer_copy_draw_path_stats(&g_state.frame, stats);
}

void db_vk_execution_report(db_render_execution_report_t *report) {
    if (report != NULL) {
        *report = g_state.execution;
    }
}
