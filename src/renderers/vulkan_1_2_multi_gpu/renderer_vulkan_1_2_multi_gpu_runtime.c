#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../../displays/display_runtime_config_common.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_runtime_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

void db_renderer_vulkan_1_2_multi_gpu_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }
    const uint64_t bench_end = db_now_ns_monotonic();
    const double bench_ms =
        (double)(bench_end - g_state.bench_start_ns) / DB_NS_PER_MS;
    db_display_log_renderer_final_summary(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.capability_mode, g_state.bench_frames,
        g_state.runtime.work_unit_count, bench_ms,
        db_renderer_vulkan_1_2_multi_gpu_draw_stats);
    double render_p50_ms = 0.0;
    double render_p95_ms = 0.0;
    double render_p99_ms = 0.0;
    if ((g_state.render_frame_samples_ms != NULL) &&
        (g_state.render_frame_samples_count > 0U)) {
        qsort(g_state.render_frame_samples_ms,
              g_state.render_frame_samples_count, sizeof(double),
              db_qsort_compare_f64);
        render_p50_ms = db_vk_scheduler_percentile_sorted(
            g_state.render_frame_samples_ms, g_state.render_frame_samples_count,
            DB_VK_PERCENTILE_P50);
        render_p95_ms = db_vk_scheduler_percentile_sorted(
            g_state.render_frame_samples_ms, g_state.render_frame_samples_count,
            DB_VK_PERCENTILE_P95);
        render_p99_ms = db_vk_scheduler_percentile_sorted(
            g_state.render_frame_samples_ms, g_state.render_frame_samples_count,
            DB_VK_PERCENTILE_P99);
    }
    if (g_state.no_present_mode != 0) {
        infof("metrics: render(frame_ema_ms=%.3f jitter_ema_ms=%.3f p50=%.3f "
              "p95=%.3f p99=%.3f) loop(frame_ema_ms=%.3f jitter_ema_ms=%.3f "
              "p50=%.3f p95=%.3f p99=%.3f retries=%llu)",
              g_state.frame_time_ema_ms, g_state.frame_jitter_ema_ms,
              render_p50_ms, render_p95_ms, render_p99_ms,
              g_state.present_frame_ema_ms, g_state.present_jitter_ema_ms,
              g_state.present_frame_p50_ms, g_state.present_frame_p95_ms,
              g_state.present_frame_p99_ms,
              (unsigned long long)g_state.present_retries);
    } else {
        infof("metrics: render(frame_ema_ms=%.3f jitter_ema_ms=%.3f p50=%.3f "
              "p95=%.3f p99=%.3f) present(frame_ema_ms=%.3f jitter_ema_ms=%.3f "
              "p50=%.3f p95=%.3f p99=%.3f retries=%llu)",
              g_state.frame_time_ema_ms, g_state.frame_jitter_ema_ms,
              render_p50_ms, render_p95_ms, render_p99_ms,
              g_state.present_frame_ema_ms, g_state.present_jitter_ema_ms,
              g_state.present_frame_p50_ms, g_state.present_frame_p95_ms,
              g_state.present_frame_p99_ms,
              (unsigned long long)g_state.present_retries);
    }
    const uint32_t gpu_count =
        db_vk_normalize_gpu_count(g_state.selection.lane_count);
    uint64_t total_work_units = 0U;
    for (uint32_t g = 0; g < gpu_count; g++) {
        total_work_units += g_state.cumulative_work_units[g];
    }
    for (uint32_t g = 0; g < gpu_count; g++) {
        const double share_pct =
            (total_work_units > 0U)
                ? ((double)g_state.cumulative_work_units[g] * 100.0) /
                      (double)total_work_units
                : 0.0;
        const double ema_ms_per_unit = g_state.ema_ms_per_work_unit[g];
        const double ema_ns_per_unit = ema_ms_per_unit * DB_NS_PER_MS;
        const double ema_units_per_ms =
            (ema_ms_per_unit > 0.0) ? (1.0 / ema_ms_per_unit) : 0.0;
        const db_vk_device_lane_t *lane = (g < g_state.selection.lane_count)
                                              ? &g_state.selection.lanes[g]
                                              : NULL;
        const char *lane_name = (lane != NULL) ? lane->name : "unknown";
        const char *lane_reason =
            ((lane != NULL) && (lane->inactive_reason[0] != '\0'))
                ? lane->inactive_reason
                : "active";
        infof("scheduler stats: lane[%u] name=%s active=%d reason=%s "
              "work_units=%llu share=%.2f%% active_frames=%llu "
              "ema_ms_per_unit=%.9g "
              "ema_ns_per_unit=%.3f ema_units_per_ms=%.3f",
              g, lane_name, (lane != NULL) ? lane->active_for_scheduler : 0,
              lane_reason, (unsigned long long)g_state.cumulative_work_units[g],
              share_pct,
              (unsigned long long)g_state.cumulative_frames_with_work[g],
              ema_ms_per_unit, ema_ns_per_unit, ema_units_per_ms);
    }
    vkDeviceWaitIdle(g_state.device);
    const db_vk_cleanup_ctx_t cleanup = {
        .device = g_state.device,
        .in_flight = g_state.in_flight,
        .image_available = g_state.image_available,
        .render_done = g_state.render_done,
        .vertex_buffer = g_state.vertex_buffer,
        .vertex_memory = g_state.vertex_memory,
        .hash_readback_buffer = g_state.hash_readback_buffer,
        .hash_readback_memory = g_state.hash_readback_memory,
        .pipeline = g_state.pipeline,
        .pipeline_layout = g_state.pipeline_layout,
        .swapchain_state = &g_state.swapchain_state,
        .history_targets = g_state.history_targets,
        .render_pass = g_state.render_pass,
        .history_render_pass = g_state.history_render_pass,
        .command_pool = g_state.command_pool,
        .timing_query_pool = g_state.timing_query_pool,
        .descriptor_set_layout = g_state.descriptor_set_layout,
        .descriptor_pool = g_state.descriptor_pool,
        .history_sampler = g_state.history_sampler,
        .instance = g_state.instance,
        .surface = g_state.surface,
    };
    db_vk_cleanup_runtime(&cleanup);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.compact.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    free(g_state.render_frame_samples_ms);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_vulkan_1_2_multi_gpu_capability_mode(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : DB_CAP_MODE_VK_DRAW_TILES_FULL
               "(upload=" DB_CAP_MODE_VK_UPLOAD_NONE ",backbuffer_replay=no)";
}

uint32_t db_renderer_vulkan_1_2_multi_gpu_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, g_state.initialized);
}

uint64_t db_renderer_vulkan_1_2_multi_gpu_state_hash(void) {
    return g_state.frame.state_hash;
}

uint64_t db_renderer_vulkan_1_2_multi_gpu_output_hash(void) {
    return g_state.output_hash;
}

void db_renderer_vulkan_1_2_multi_gpu_set_output_hash_enabled(int enabled) {
    g_state.output_hash_enabled = (enabled != 0) ? 1 : 0;
}

void db_renderer_vulkan_1_2_multi_gpu_draw_stats(
    db_renderer_draw_path_stats_t *stats) {
    db_history_copy_draw_path_stats(&g_state.frame, stats);
}
