#include "core/db_renderer_support.h"
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../damage_trace.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include "vk_state_internal.h"
#include <vulkan/vulkan_core.h>

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

renderer_state_t g_vk_state = {0};

void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    g_state.initialized = 1;
    g_state.presentation.wsi_config = (db_vk_wsi_config_t){0};
    if (ctx->wsi_config != NULL) {
        g_state.presentation.wsi_config = *ctx->wsi_config;
    }
    g_state.log_backend_name = BACKEND_NAME;
    if ((ctx->wsi_config != NULL) && (ctx->wsi_config->backend_name != NULL)) {
        g_state.log_backend_name = ctx->wsi_config->backend_name;
    }
    g_state.device.instance = ctx->instance;
    g_state.presentation.surface = ctx->surface;
    g_state.device.selection = ctx->selection;
    g_state.device.gpu_count =
        db_vk_normalize_gpu_count(g_state.device.selection.active_lane_count);
    g_state.device.present_phys = ctx->present_phys;
    g_state.device.device = ctx->device;
    g_state.device.queue = ctx->queue;
    g_state.presentation.surface_format = ctx->surface_format;
    g_state.presentation.present_mode = ctx->present_mode;
    g_state.presentation.render_pass = ctx->render_pass;
    g_state.backing.render_pass = ctx->backing_render_pass;
    g_state.backing.format = ctx->backing_format;
    g_state.backing.pixel_format = ctx->backing_pixel_format;
    g_state.presentation.swapchain_state = ctx->swapchain_state;
    g_state.backing.targets[0] = ctx->backing_targets[0];
    g_state.runtime = ctx->runtime;
    g_state.diagnostics = ctx->diagnostics;
    g_state.backing.extent = (VkExtent2D){
        .width = g_state.runtime.grid_cols,
        .height = g_state.runtime.grid_rows,
    };
    g_state.backing.generation = 1U;
    db_damage_trace_emit_target_lifecycle(&(const db_target_lifecycle_event_t){
        .backend = DB_DAMAGE_TRACE_BACKEND_VULKAN,
        .action = DB_TARGET_LIFECYCLE_CREATE,
        .target = "vk_backing",
        .target_id = 1U,
        .generation = g_state.backing.generation,
        .new_width = g_state.backing.extent.width,
        .new_height = g_state.backing.extent.height,
        .format = g_state.backing.pixel_format,
        .cause = "initial_target",
        .valid_before = 0,
        .valid_after = 0,
    });
    g_state.backing.valid = 0;
    g_state.backing.descriptor_index = 0;
    g_state.device.device_group_mask = ctx->device_group_mask;
    g_state.pipelines.vertex_buffer = ctx->vertex_buffer;
    g_state.pipelines.vertex_memory = ctx->vertex_memory;
    g_state.pipelines.instance_buffer = ctx->instance_buffer;
    g_state.pipelines.instance_memory = ctx->instance_memory;
    g_state.pipelines.instance_mapped = ctx->instance_mapped;
    g_state.pipelines.lookup_buffer = ctx->lookup_buffer;
    g_state.pipelines.lookup_memory = ctx->lookup_memory;
    g_state.pipelines.lookup_mapped = ctx->lookup_mapped;
    g_state.pipelines.pipeline = ctx->pipeline;
    g_state.presentation.present_pipeline = ctx->present_pipeline;
    g_state.pipelines.composition_pipeline = ctx->composition_pipeline;
    g_state.pipelines.pipeline_layout = ctx->pipeline_layout;
    g_state.pipelines.descriptor_set_layout = ctx->descriptor_set_layout;
    g_state.pipelines.descriptor_pool = ctx->descriptor_pool;
    g_state.pipelines.descriptor_set = ctx->descriptor_set;
    for (uint32_t lane = 0U; lane < MAX_GPU_COUNT; lane++) {
        for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
            g_state.pipelines.lane_descriptor_sets[lane][slot] =
                ctx->lane_descriptor_sets[lane][slot];
        }
    }
    g_state.backing.sampler = ctx->backing_sampler;
    g_state.device.command_pool = ctx->command_pool;
    g_state.device.command_buffer = ctx->command_buffer;
    g_state.presentation.image_available = ctx->image_available;
    g_state.presentation.render_done = ctx->render_done;
    g_state.presentation.in_flight = ctx->in_flight;
    g_state.device.timing_query_pool = ctx->timing_query_pool;
    g_state.device.gpu_timing_enabled = ctx->gpu_timing_enabled;
    g_state.runtime.pipeline.uses_history_pipeline = 1;
    g_state.capability_mode = ctx->capability_mode;
    g_state.presentation.no_present_mode = ctx->no_present_mode;
    for (uint32_t i = 0; i < MAX_GPU_COUNT; i++) {
        g_state.scheduler.ema_ms_per_work_unit[i] =
            ctx->ema_ms_per_work_unit[i];
        g_state.scheduler.prev_frame_work_units[i] = 0U;
        g_state.scheduler.prev_frame_owner_used[i] = 0U;
        g_state.scheduler.cumulative_work_units[i] = 0U;
        g_state.scheduler.cumulative_frames_with_work[i] = 0U;
    }
    g_state.scheduler.have_prev_timing_frame = 0;
    g_state.device.timestamp_period_ns = ctx->timestamp_period_ns;
    g_state.metrics.bench_start_ns = db_now_ns_monotonic();
    g_state.metrics.bench_frames = 0U;
    g_state.frame.full_draw_frames = 0U;
    g_state.frame.dirty_draw_frames = 0U;
    g_state.frame.state_hash = DB_FNV1A64_OFFSET;
    g_state.hash.output_hash = DB_FNV1A64_OFFSET;
    g_state.hash.output_hash_enabled = 0;
    g_state.hash.hash_readback_buffer = VK_NULL_HANDLE;
    g_state.hash.hash_readback_memory = VK_NULL_HANDLE;
    g_state.hash.hash_readback_size_bytes = 0U;
    g_state.metrics.next_progress_log_due_ms = 0.0;
    g_state.frame.frame_index = 0U;
    g_state.backing.valid = 0;
    g_state.metrics.frame_time_ema_ms = 0.0;
    g_state.metrics.frame_jitter_ema_ms = 0.0;
    g_state.metrics.present_frame_ema_ms = 0.0;
    g_state.metrics.present_jitter_ema_ms = 0.0;
    g_state.metrics.present_frame_window_p50_ms = 0.0;
    g_state.metrics.present_frame_window_p95_ms = 0.0;
    g_state.metrics.present_frame_window_p99_ms = 0.0;
    g_state.metrics.present_metric_total_samples = 0U;
    g_state.metrics.present_metric_window_sample_count = 0U;
    g_state.metrics.present_metric_window_capacity = 0U;
    g_state.metrics.present_retries = 0U;
    g_state.metrics.last_render_critical_ms = 0.0;
    g_state.scheduler.piece_storage =
        (db_vk_present_piece_t *)db_calloc_or_fail(
            BACKEND_NAME, "piece_storage", DB_VK_MAX_PIECES_PER_FRAME,
            sizeof(*g_state.scheduler.piece_storage),
            DB_CACHELINE_ALIGNMENT_BYTES);
    g_state.scheduler.assignment_storage =
        (db_vk_lane_assignment_t *)db_calloc_or_fail(
            BACKEND_NAME, "assignment_storage", DB_VK_MAX_PIECES_PER_FRAME,
            sizeof(*g_state.scheduler.assignment_storage),
            DB_CACHELINE_ALIGNMENT_BYTES);
    g_state.scheduler.scheduling_epoch = 1U;
    g_state.scheduler.content_generation = 1U;
    g_state.scheduler.last_active_lane_count = 1U;
}

void db_vk_set_present_metrics(double frame_ema_ms, double jitter_ema_ms,
                               double p50_ms, double p95_ms, double p99_ms,
                               uint32_t window_sample_count,
                               uint32_t window_capacity, uint64_t total_samples,
                               uint64_t retries) {
    g_state.metrics.present_frame_ema_ms = frame_ema_ms;
    g_state.metrics.present_jitter_ema_ms = jitter_ema_ms;
    g_state.metrics.present_frame_window_p50_ms = p50_ms;
    g_state.metrics.present_frame_window_p95_ms = p95_ms;
    g_state.metrics.present_frame_window_p99_ms = p99_ms;
    g_state.metrics.present_metric_window_sample_count = window_sample_count;
    g_state.metrics.present_metric_window_capacity = window_capacity;
    g_state.metrics.present_metric_total_samples = total_samples;
    g_state.metrics.present_retries = retries;
}

double db_vk_last_render_critical_ms(void) {
    return g_state.metrics.last_render_critical_ms;
}

void db_vk_set_render_metrics(double p50_ms, double p95_ms, double p99_ms,
                              uint32_t window_sample_count,
                              uint32_t window_capacity,
                              uint64_t total_samples) {
    g_state.metrics.render_frame_window_p50_ms = p50_ms;
    g_state.metrics.render_frame_window_p95_ms = p95_ms;
    g_state.metrics.render_frame_window_p99_ms = p99_ms;
    g_state.metrics.render_metric_window_sample_count = window_sample_count;
    g_state.metrics.render_metric_window_capacity = window_capacity;
    g_state.metrics.render_metric_total_samples = total_samples;
}
