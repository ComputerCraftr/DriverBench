#include "core/db_format_contract.h"
#include "core/db_numeric.h"
#include "core/db_renderer_runtime_contract.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include "vk_state_internal.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>

char g_vk_capability_mode[DB_VK_CAPABILITY_MODE_MAX] = {0};

db_native_output_capability_t
db_vk_init(const db_vk_wsi_config_t *wsi_config, int vsync_enabled,
           const db_renderer_runtime_contract_t *resolved_runtime) {
    db_vk_init_instance_surface_phase_t instance_surface_phase = {0};
    db_vk_init_device_phase_t device_phase = {0};
    db_vk_init_pipeline_resources_phase_t pipeline_phase = {0};
    db_vk_init_scheduler_phase_t scheduler_phase = {0};

    db_vk_init_phase_instance_surface(wsi_config, &instance_surface_phase);
    db_vk_init_phase_device(
        instance_surface_phase.instance, instance_surface_phase.surface,
        vsync_enabled, resolved_runtime->format.output_request, &device_phase);
    db_vk_init_phase_pipeline_resources(
        wsi_config, instance_surface_phase.surface, &device_phase,
        resolved_runtime, &pipeline_phase);
    db_vk_init_phase_scheduler(&device_phase, resolved_runtime,
                               &scheduler_phase);

    db_vk_state_init_ctx_t init_ctx = {
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
        .backing_render_pass = pipeline_phase.backing_render_pass,
        .backing_format = pipeline_phase.backing_format,
        .backing_pixel_format = pipeline_phase.backing_pixel_format,
        .swapchain_state = pipeline_phase.swapchain_state,
        .backing_targets = {pipeline_phase.backing_targets[0]},
        .device_group_mask = device_phase.device_group_mask,
        .vertex_buffer = pipeline_phase.vertex_buffer,
        .vertex_memory = pipeline_phase.vertex_memory,
        .pipeline = pipeline_phase.pipeline,
        .present_pipeline = pipeline_phase.present_pipeline,
        .composition_pipeline = pipeline_phase.composition_pipeline,
        .pipeline_layout = pipeline_phase.pipeline_layout,
        .descriptor_set_layout = pipeline_phase.descriptor_set_layout,
        .descriptor_pool = pipeline_phase.descriptor_pool,
        .descriptor_set = pipeline_phase.descriptor_set,
        .backing_sampler = pipeline_phase.backing_sampler,
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
    for (uint32_t lane = 0U; lane < MAX_GPU_COUNT; lane++) {
        for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
            init_ctx.lane_descriptor_sets[lane][slot] =
                pipeline_phase.lane_descriptor_sets[lane][slot];
        }
    }
    db_vk_publish_initialized_state(&init_ctx);
    db_vk_device_group_lanes_init();
    db_vk_independent_lanes_init();
    db_native_output_capability_t capability =
        device_phase.native_output_capability;
    if (device_phase.native_hdr_enabled != 0) {
        capability.commit_verified =
            DB_BOOL(pipeline_phase.swapchain_state.swapchain != VK_NULL_HANDLE);
        capability.native_hdr_verified = capability.commit_verified;
        capability.unavailable_reason =
            (capability.native_hdr_verified != 0)
                ? "none"
                : "vulkan_hdr_swapchain_creation_unverified";
    }
    return capability;
}
