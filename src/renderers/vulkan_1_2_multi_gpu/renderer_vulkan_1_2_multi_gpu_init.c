#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

char g_vk_capability_mode[DB_VK_CAPABILITY_MODE_MAX] = {0};

void db_renderer_vulkan_1_2_multi_gpu_init(const db_vk_wsi_config_t *wsi_config,
                                           int vsync_enabled) {
    db_vk_init_instance_surface_phase_t instance_surface_phase = {0};
    db_vk_init_device_phase_t device_phase = {0};
    db_vk_init_pipeline_resources_phase_t pipeline_phase = {0};
    db_vk_init_scheduler_phase_t scheduler_phase = {0};

    db_vk_init_phase_instance_surface(wsi_config, &instance_surface_phase);
    db_vk_init_phase_device(instance_surface_phase.instance,
                            instance_surface_phase.surface, vsync_enabled,
                            &device_phase);
    db_vk_init_phase_pipeline_resources(wsi_config,
                                        instance_surface_phase.surface,
                                        &device_phase, &pipeline_phase);
    db_vk_init_phase_scheduler(&device_phase, &scheduler_phase);

    const db_vk_state_init_ctx_t init_ctx = {
        .wsi_config = wsi_config,
        .instance = instance_surface_phase.instance,
        .surface = instance_surface_phase.surface,
        .selection = device_phase.selection,
        .have_group = device_phase.have_group,
        .gpu_count = scheduler_phase.effective_gpu_count,
        .present_phys = device_phase.present_phys,
        .device = device_phase.device,
        .queue = device_phase.queue,
        .surface_format = device_phase.surface_format,
        .present_mode = device_phase.present_mode,
        .render_pass = pipeline_phase.render_pass,
        .history_render_pass = pipeline_phase.history_render_pass,
        .swapchain_state = pipeline_phase.swapchain_state,
        .history_targets = {pipeline_phase.history_targets[0],
                            pipeline_phase.history_targets[1]},
        .device_group_mask = device_phase.device_group_mask,
        .vertex_buffer = pipeline_phase.vertex_buffer,
        .vertex_memory = pipeline_phase.vertex_memory,
        .pipeline = pipeline_phase.pipeline,
        .pipeline_layout = pipeline_phase.pipeline_layout,
        .descriptor_set_layout = pipeline_phase.descriptor_set_layout,
        .descriptor_pool = pipeline_phase.descriptor_pool,
        .descriptor_set = pipeline_phase.descriptor_set,
        .history_sampler = pipeline_phase.history_sampler,
        .command_pool = pipeline_phase.command_pool,
        .command_buffer = pipeline_phase.command_buffer,
        .image_available = pipeline_phase.image_available,
        .render_done = pipeline_phase.render_done,
        .in_flight = pipeline_phase.in_flight,
        .timing_query_pool = pipeline_phase.timing_query_pool,
        .gpu_timing_enabled = pipeline_phase.gpu_timing_enabled,
        .runtime = scheduler_phase.runtime,
        .capability_mode = scheduler_phase.capability_mode,
        .no_present_mode = scheduler_phase.no_present_mode,
        .ema_ms_per_work_unit = scheduler_phase.ema_ms_per_work_unit,
        .timestamp_period_ns = device_phase.timestamp_period_ns,
    };
    db_vk_publish_initialized_state(&init_ctx);
}

// NOLINTEND(misc-include-cleaner)
