#include <stdint.h>

#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

renderer_state_t g_state = {0};

void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    g_state.initialized = 1;
    g_state.wsi_config = (db_vk_wsi_config_t){0};
    if (ctx->wsi_config != NULL) {
        g_state.wsi_config = *ctx->wsi_config;
    }
    g_state.log_backend_name = BACKEND_NAME;
    if ((ctx->wsi_config != NULL) && (ctx->wsi_config->user_data != NULL)) {
        g_state.log_backend_name = (const char *)ctx->wsi_config->user_data;
    }
    g_state.instance = ctx->instance;
    g_state.surface = ctx->surface;
    g_state.selection = ctx->selection;
    g_state.gpu_count =
        db_vk_normalize_gpu_count(g_state.selection.active_lane_count);
    g_state.present_phys = ctx->present_phys;
    g_state.device = ctx->device;
    g_state.queue = ctx->queue;
    g_state.surface_format = ctx->surface_format;
    g_state.present_mode = ctx->present_mode;
    g_state.render_pass = ctx->render_pass;
    g_state.history_render_pass = ctx->history_render_pass;
    g_state.swapchain_state = ctx->swapchain_state;
    g_state.history_targets[0] = ctx->history_targets[0];
    g_state.history_targets[1] = ctx->history_targets[1];
    db_history_pair_state_seeded(&g_state.history_pair);
    g_state.history_descriptor_index = 0;
    g_state.device_group_mask = ctx->device_group_mask;
    g_state.vertex_buffer = ctx->vertex_buffer;
    g_state.vertex_memory = ctx->vertex_memory;
    g_state.pipeline = ctx->pipeline;
    g_state.pipeline_layout = ctx->pipeline_layout;
    g_state.descriptor_set_layout = ctx->descriptor_set_layout;
    g_state.descriptor_pool = ctx->descriptor_pool;
    g_state.descriptor_set = ctx->descriptor_set;
    g_state.history_sampler = ctx->history_sampler;
    g_state.command_pool = ctx->command_pool;
    g_state.command_buffer = ctx->command_buffer;
    g_state.image_available = ctx->image_available;
    g_state.render_done = ctx->render_done;
    g_state.in_flight = ctx->in_flight;
    g_state.timing_query_pool = ctx->timing_query_pool;
    g_state.gpu_timing_enabled = ctx->gpu_timing_enabled;
    g_state.runtime = ctx->runtime;
    g_state.runtime_flags = db_history_runtime_mode_flags(&g_state.runtime);
    g_state.capability_mode = ctx->capability_mode;
    g_state.no_present_mode = ctx->no_present_mode;
    for (uint32_t i = 0; i < MAX_GPU_COUNT; i++) {
        g_state.ema_ms_per_work_unit[i] = ctx->ema_ms_per_work_unit[i];
        g_state.prev_frame_work_units[i] = 0U;
        g_state.prev_frame_owner_used[i] = 0U;
        g_state.cumulative_work_units[i] = 0U;
        g_state.cumulative_frames_with_work[i] = 0U;
    }
    g_state.have_prev_timing_frame = 0;
    g_state.timestamp_period_ns = ctx->timestamp_period_ns;
    g_state.bench_start_ns = db_now_ns_monotonic();
    g_state.bench_frames = 0U;
    g_state.frame.full_draw_frames = 0U;
    g_state.frame.dirty_draw_frames = 0U;
    g_state.frame.state_hash = DB_FNV1A64_OFFSET;
    g_state.output_hash = DB_FNV1A64_OFFSET;
    g_state.output_hash_enabled = 0;
    g_state.shape_uniform_cache.valid = 0;
    g_state.hash_readback_buffer = VK_NULL_HANDLE;
    g_state.hash_readback_memory = VK_NULL_HANDLE;
    g_state.hash_readback_size_bytes = 0U;
    g_state.next_progress_log_due_ms = 0.0;
    g_state.frame.frame_index = 0U;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        g_state.runtime.snake.cursor = DB_SNAKE_CURSOR_PRE_ENTRY;
    } else {
        g_state.runtime.snake.cursor = 0U;
    }
    g_state.runtime.snake.shape_index = 0U;
    g_state.runtime.snake.prev_start = 0U;
    g_state.runtime.snake.prev_count = 0U;
    g_state.snake_scratch.damage.blocks = NULL;
    g_state.snake_scratch.damage.capacity = 0U;
    g_state.snake_scratch.compact.blocks = NULL;
    g_state.snake_scratch.shape.row_bounds = NULL;
    g_state.snake_scratch.shape.row_bounds_capacity = 0U;
    g_state.snake_scratch.compact.capacity = 0U;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        const size_t snake_compact_block_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        g_state.snake_scratch.shape.row_bounds =
            (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                sizeof(*g_state.snake_scratch.shape.row_bounds));
        g_state.snake_scratch.damage.blocks =
            (db_grid_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_damage_blocks",
                snake_compact_block_capacity,
                sizeof(*g_state.snake_scratch.damage.blocks));
        g_state.snake_scratch.compact.blocks =
            (db_snake_compact_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_compact_blocks",
                snake_compact_block_capacity,
                sizeof(*g_state.snake_scratch.compact.blocks));
        g_state.snake_scratch.shape.row_bounds_capacity =
            (size_t)db_grid_rows_effective();
        g_state.snake_scratch.damage.capacity =
            snake_compact_block_capacity;
        g_state.snake_scratch.compact.capacity =
            snake_compact_block_capacity;
    }
    g_state.gradient_window_rows = db_gradient_window_rows_effective();
    db_history_gradient_replay_state_reset(&g_state.gradient_prev_frame);
    g_state.history_pair.is_valid = 0;
    g_state.frame_time_ema_ms = 0.0;
    g_state.frame_jitter_ema_ms = 0.0;
    g_state.present_frame_ema_ms = 0.0;
    g_state.present_jitter_ema_ms = 0.0;
    g_state.present_frame_p50_ms = 0.0;
    g_state.present_frame_p95_ms = 0.0;
    g_state.present_frame_p99_ms = 0.0;
    g_state.present_retries = 0U;
    g_state.render_frame_samples_ms = NULL;
    g_state.render_frame_samples_count = 0U;
    g_state.render_frame_samples_capacity = 0U;
}

void db_renderer_vulkan_1_2_multi_gpu_set_present_metrics(
    double frame_ema_ms, double jitter_ema_ms, double p50_ms, double p95_ms,
    double p99_ms, uint64_t retries) {
    g_state.present_frame_ema_ms = frame_ema_ms;
    g_state.present_jitter_ema_ms = jitter_ema_ms;
    g_state.present_frame_p50_ms = p50_ms;
    g_state.present_frame_p95_ms = p95_ms;
    g_state.present_frame_p99_ms = p99_ms;
    g_state.present_retries = retries;
}

// NOLINTEND(misc-include-cleaner)
