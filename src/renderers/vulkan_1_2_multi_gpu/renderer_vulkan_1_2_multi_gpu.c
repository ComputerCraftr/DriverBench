#include <stdint.h>

#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

renderer_state_t g_state = {0};

void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    g_state.initialized = 1;
    g_state.wsi_config = *ctx->wsi_config;
    g_state.log_backend_name = BACKEND_NAME;
    if ((ctx->wsi_config != NULL) && (ctx->wsi_config->user_data != NULL)) {
        g_state.log_backend_name = (const char *)ctx->wsi_config->user_data;
    }
    g_state.instance = ctx->instance;
    g_state.surface = ctx->surface;
    g_state.selection = ctx->selection;
    g_state.have_group = ctx->have_group;
    g_state.gpu_count = ctx->gpu_count;
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
    g_state.history_read_index = 0;
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
    g_state.capability_mode = ctx->capability_mode;
    for (uint32_t i = 0; i < MAX_BAND_OWNER; i++) {
        g_state.work_owner[i] = ctx->work_owner[i];
    }
    for (uint32_t i = 0; i < MAX_GPU_COUNT; i++) {
        g_state.ema_ms_per_work_unit[i] = ctx->ema_ms_per_work_unit[i];
        g_state.prev_frame_work_units[i] = 0U;
        g_state.prev_frame_owner_used[i] = 0U;
    }
    g_state.have_prev_timing_frame = 0;
    g_state.timestamp_period_ns = ctx->timestamp_period_ns;
    g_state.bench_start_ns = db_now_ns_monotonic();
    g_state.bench_frames = 0U;
    g_state.state_hash = DB_FNV1A64_OFFSET;
    g_state.next_progress_log_due_ms = 0.0;
    g_state.frame_index = 0U;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        g_state.runtime.snake_cursor = DB_SNAKE_CURSOR_PRE_ENTRY;
    } else {
        g_state.runtime.snake_cursor = 0U;
    }
    g_state.runtime.snake_shape_index = 0U;
    g_state.runtime.snake_prev_start = 0U;
    g_state.runtime.snake_prev_count = 0U;
    g_state.snake_reset_pending = 1;
    g_state.snake_spans = NULL;
    g_state.snake_row_bounds = NULL;
    g_state.snake_row_bounds_capacity = 0U;
    g_state.snake_span_capacity = 0U;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const size_t scratch_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        g_state.snake_spans = (db_snake_col_span_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_spans", scratch_capacity,
            sizeof(*g_state.snake_spans));
        g_state.snake_row_bounds =
            (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                sizeof(*g_state.snake_row_bounds));
        g_state.snake_row_bounds_capacity = (size_t)db_grid_rows_effective();
        g_state.snake_span_capacity = scratch_capacity;
    }
    g_state.gradient_window_rows = db_gradient_window_rows_effective();
}

void db_renderer_vulkan_1_2_multi_gpu_init(
    const db_vk_wsi_config_t *wsi_config) {
    db_vk_init_impl(wsi_config);
}

db_vk_frame_result_t db_renderer_vulkan_1_2_multi_gpu_render_frame(void) {
    return db_vk_render_frame_impl();
}

void db_renderer_vulkan_1_2_multi_gpu_shutdown(void) { db_vk_shutdown_impl(); }

const char *db_renderer_vulkan_1_2_multi_gpu_capability_mode(void) {
    return db_vk_capability_mode_impl();
}

uint32_t db_renderer_vulkan_1_2_multi_gpu_work_unit_count(void) {
    return db_vk_work_unit_count_impl();
}

uint64_t db_renderer_vulkan_1_2_multi_gpu_state_hash(void) {
    return db_vk_state_hash_impl();
}

// NOLINTEND(misc-include-cleaner)
