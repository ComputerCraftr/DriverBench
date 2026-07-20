#include "vk_internal.h"

#include <stdint.h>

#ifdef __linux__
#include "vk_init_internal.h"
#include "vk_state_internal.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

#include "../../core/db_core.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_VK_EXTERNAL_SEMAPHORE_HANDLE                                        \
    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
#include <unistd.h>

static int vk_create_external_semaphore_pair(VkDevice exporting_device,
                                             VkDevice importing_device,
                                             VkSemaphore *exporting_semaphore,
                                             VkSemaphore *importing_semaphore) {
    const VkExportSemaphoreCreateInfo export_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = DB_VK_EXTERNAL_SEMAPHORE_HANDLE,
    };
    const VkSemaphoreCreateInfo exporting_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_info,
    };
    const VkSemaphoreCreateInfo importing_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    if ((vkCreateSemaphore(exporting_device, &exporting_info, NULL,
                           exporting_semaphore) != VK_SUCCESS) ||
        (vkCreateSemaphore(importing_device, &importing_info, NULL,
                           importing_semaphore) != VK_SUCCESS)) {
        return 0;
    }
    return 1;
}

static int vk_create_worker_device(db_vk_independent_lane_runtime_t *runtime,
                                   const db_vk_device_lane_t *lane) {
    const float priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = lane->queue_family_index,
        .queueCount = 1U,
        .pQueuePriorities = &priority,
    };
    const char *extensions[5] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                 VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    uint32_t extension_count = 2U;
    if (runtime->transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE) {
        extensions[extension_count++] =
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
        extensions[extension_count++] =
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
    }
    const db_vk_physical_device_info_t *const worker_info =
        &g_state.device.selection.phys_info[lane->physical_index];
    if (worker_info->supports_calibrated_timestamps_khr != 0) {
        extensions[extension_count++] =
            VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
        runtime->calibrated_timestamps_enabled = 1;
    } else if (worker_info->supports_calibrated_timestamps_ext != 0) {
        extensions[extension_count++] =
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
        runtime->calibrated_timestamps_enabled = 1;
    }
    const VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1U,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };
    runtime->phys = lane->phys;
    runtime->queue_family_index = lane->queue_family_index;
    if (vkCreateDevice(lane->phys, &create_info, NULL, &runtime->device) !=
        VK_SUCCESS) {
        return 0;
    }
    vkGetDeviceQueue(runtime->device, runtime->queue_family_index, 0U,
                     &runtime->queue);
    VkPhysicalDeviceProperties properties = {0};
    vkGetPhysicalDeviceProperties(runtime->phys, &properties);
    runtime->timestamp_period_ns =
        db_f32_to_double(properties.limits.timestampPeriod);
    uint32_t queue_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(runtime->phys, &queue_count, NULL);
    VkQueueFamilyProperties queue_properties[32] = {0};
    queue_count = DB_MIN(queue_count, 32U);
    vkGetPhysicalDeviceQueueFamilyProperties(runtime->phys, &queue_count,
                                             queue_properties);
    runtime->timestamp_valid_bits =
        queue_properties[runtime->queue_family_index].timestampValidBits;
    return 1;
}

static void
vk_reset_external_slot_attempt(db_vk_independent_lane_runtime_t *runtime,
                               db_vk_lane_slot_t *slot) {
    if (slot->primary_alias_view != VK_NULL_HANDLE) {
        vkDestroyImageView(g_state.device.device, slot->primary_alias_view,
                           NULL);
    }
    if (slot->primary_alias_image != VK_NULL_HANDLE) {
        vkDestroyImage(g_state.device.device, slot->primary_alias_image, NULL);
    }
    if (slot->primary_alias_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_state.device.device, slot->primary_alias_memory, NULL);
    }
    if (slot->worker_target.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(runtime->device, slot->worker_target.framebuffer,
                             NULL);
    }
    if (slot->worker_target.view != VK_NULL_HANDLE) {
        vkDestroyImageView(runtime->device, slot->worker_target.view, NULL);
    }
    if (slot->worker_target.image != VK_NULL_HANDLE) {
        vkDestroyImage(runtime->device, slot->worker_target.image, NULL);
    }
    if (slot->worker_target.memory != VK_NULL_HANDLE) {
        vkFreeMemory(runtime->device, slot->worker_target.memory, NULL);
    }
    slot->primary_alias_view = VK_NULL_HANDLE;
    slot->primary_alias_image = VK_NULL_HANDLE;
    slot->primary_alias_memory = VK_NULL_HANDLE;
    slot->worker_target = (VkBackingTargetState){0};
    slot->allocation_size = 0U;
}

static int
db_vk_create_worker_render_resources(db_vk_independent_lane_runtime_t *runtime,
                                     uint32_t lane_index) {
    const VkAttachmentDescription attachment = {
        .format = g_state.backing.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference reference = {
        .attachment = 0U,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1U,
        .pColorAttachments = &reference,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1U,
        .pAttachments = &attachment,
        .subpassCount = 1U,
        .pSubpasses = &subpass,
    };
    if (vkCreateRenderPass(runtime->device, &render_pass_info, NULL,
                           &runtime->render_pass) != VK_SUCCESS) {
        return 0;
    }
    for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
         slot_index++) {
        db_vk_lane_slot_t *const slot = &runtime->slots[slot_index];
        slot->primary_descriptor_set =
            g_state.pipelines.lane_descriptor_sets[lane_index][slot_index];
        if (runtime->transport == DB_VK_TRANSPORT_DMA_BUF_BUFFER) {
            db_vk_create_backing_target(
                runtime->phys, runtime->device, g_state.backing.format,
                g_state.backing.extent, runtime->render_pass, 0U,
                &slot->worker_target);
            if ((vk_create_external_semaphore_pair(
                     runtime->device, g_state.device.device,
                     &slot->worker_ready, &slot->primary_ready) == 0) ||
                (vk_create_external_semaphore_pair(
                     g_state.device.device, runtime->device,
                     &slot->primary_reusable, &slot->worker_reusable) == 0)) {
                return 0;
            }
            slot->phase = DB_VK_EXTERNAL_SLOT_WORKER_OWNED;
            slot->slot_generation = 1U;
            continue;
        }
        VkPhysicalDeviceMemoryProperties memory_properties = {0};
        vkGetPhysicalDeviceMemoryProperties(runtime->phys, &memory_properties);
        int imported = 0;
        for (uint32_t preference = 0U; (preference < 2U) && (imported == 0);
             preference++) {
            for (uint32_t memory_type = 0U;
                 (memory_type < memory_properties.memoryTypeCount) &&
                 (imported == 0);
                 memory_type++) {
                const int device_local = DB_BOOL(
                    (memory_properties.memoryTypes[memory_type].propertyFlags &
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U);
                if (((preference == 0U) && (device_local == 0)) ||
                    ((preference == 1U) && (device_local != 0))) {
                    continue;
                }
                vk_reset_external_slot_attempt(runtime, slot);
                if ((db_vk_create_external_image(
                         runtime->phys, runtime->device, g_state.backing.format,
                         g_state.backing.extent, runtime->render_pass,
                         runtime->transport, runtime->drm_modifier, lane_index,
                         memory_type, slot) != 0) &&
                    (db_vk_import_external_image(
                         g_state.device.present_phys, g_state.device.device,
                         runtime->device, g_state.backing.format,
                         g_state.backing.extent, lane_index, runtime->transport,
                         slot) != 0)) {
                    imported = 1;
                }
            }
        }
        if ((imported == 0) ||
            (vk_create_external_semaphore_pair(
                 runtime->device, g_state.device.device, &slot->worker_ready,
                 &slot->primary_ready) == 0) ||
            (vk_create_external_semaphore_pair(
                 g_state.device.device, runtime->device,
                 &slot->primary_reusable, &slot->worker_reusable) == 0)) {
            vk_reset_external_slot_attempt(runtime, slot);
            return 0;
        }
        db_vk_update_backing_descriptor(
            g_state.device.device, slot->primary_descriptor_set,
            g_state.backing.sampler, slot->primary_alias_view);
        slot->phase = DB_VK_EXTERNAL_SLOT_WORKER_OWNED;
        slot->slot_generation = 1U;
        slot->initialized = 0;
    }
    if ((runtime->transport == DB_VK_TRANSPORT_DMA_BUF_BUFFER) &&
        (db_vk_buffer_transport_create(runtime) == 0)) {
        return 0;
    }
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = runtime->queue_family_index,
    };
    if (vkCreateCommandPool(runtime->device, &pool_info, NULL,
                            &runtime->command_pool) != VK_SUCCESS) {
        return 0;
    }
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = runtime->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };
    if (vkAllocateCommandBuffers(runtime->device, &command_info,
                                 &runtime->command_buffer) != VK_SUCCESS) {
        return 0;
    }
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    if (vkCreateFence(runtime->device, &fence_info, NULL, &runtime->fence) !=
        VK_SUCCESS) {
        return 0;
    }
    const VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2U,
    };
    return DB_BOOL(vkCreateQueryPool(runtime->device, &query_info, NULL,
                                     &runtime->timing_query_pool) ==
                   VK_SUCCESS);
}

void db_vk_independent_lanes_init(void) {
    if (db_vk_execution_mode_uses_independent_lanes(
            g_state.device.selection.execution_mode) == 0) {
        return;
    }
    uint32_t active_count = 1U;
    for (uint32_t lane_index = 1U;
         lane_index < g_state.device.selection.lane_count; lane_index++) {
        db_vk_device_lane_t *const lane =
            &g_state.device.selection.lanes[lane_index];
        if ((lane->backend != DB_VK_LANE_BACKEND_INDEPENDENT) ||
            (lane->can_compose_to_primary == 0) ||
            (strstr(lane->inactive_reason, "cpu worker disabled") != NULL)) {
            continue;
        }
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        runtime->transport = lane->transport;
        runtime->drm_modifier = lane->drm_modifier;
        if ((vk_create_worker_device(runtime, lane) == 0) ||
            (db_vk_create_worker_render_resources(runtime, lane_index) == 0)) {
            db_vk_set_lane_reason(
                lane, (lane->transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
                          ? "dma_buf_import_profile_rejected"
                          : "opaque_fd_import_profile_rejected");
            const db_log_field_t fields[] = {
                DB_LOG_U64("lane", lane_index),
                DB_LOG_U64("transport", lane->transport),
                DB_LOG_HEX64("drm_modifier", lane->drm_modifier),
                DB_LOG_STRING("reason", lane->inactive_reason),
            };
            db_log_info(BACKEND_NAME, "vk_transport_rejected", fields,
                        DB_LOG_FIELD_COUNT(fields));
            continue;
        }
        runtime->initialized = 1;
        runtime->active = 1;
        runtime->scheduling_epoch = 1U;
        lane->active_for_scheduler = 1;
        lane->inactive_reason[0] = '\0';
        active_count++;
        const db_log_field_t fields[] = {
            DB_LOG_U64("lane", lane_index),
            DB_LOG_U64("slot_count", DB_VK_LANE_SLOT_COUNT),
            DB_LOG_U64("scheduling_epoch", runtime->scheduling_epoch),
            DB_LOG_TOKEN("handle_type",
                         (runtime->transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
                             ? "dma_buf"
                             : "opaque_fd"),
        };
        db_log_info(BACKEND_NAME, "vk_independent_lane_resources", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    g_state.device.selection.active_lane_count = active_count;
    if (active_count > 1U) {
        g_state.device.selection.execution_mode =
            DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES;
        g_state.device.gpu_count = active_count;
        const db_vk_physical_device_info_t *const primary_info =
            &g_state.device.selection
                 .phys_info[g_state.device.selection.primary_phys_index];
        const db_vk_physical_device_info_t *const worker_info =
            &g_state.device.selection
                 .phys_info[g_state.device.selection.lanes[1].physical_index];
        if (primary_info->supports_time_domain_monotonic_raw &&
            worker_info->supports_time_domain_monotonic_raw) {
            g_state.calibration.calibrated_host_domain =
                VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;
            g_state.calibration.calibrated_timestamps_enabled = 1;
        } else if (primary_info->supports_time_domain_monotonic &&
                   worker_info->supports_time_domain_monotonic) {
            g_state.calibration.calibrated_host_domain =
                VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
            g_state.calibration.calibrated_timestamps_enabled = 1;
        }
        db_vk_calibration_state_open(&g_state.calibration.state);
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("from", "closed"),
            DB_LOG_TOKEN("to", "warming"),
            DB_LOG_U64("active_lane_count", active_count),
            DB_LOG_TOKEN("reason", "transport_resources_ready"),
        };
        db_log_info(BACKEND_NAME, "vk_multi_gpu_phase", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
}

void db_vk_independent_lane_quarantine(uint32_t lane_index,
                                       const char *reason) {
    if ((lane_index == 0U) || (lane_index >= MAX_GPU_COUNT)) {
        return;
    }
    db_vk_independent_lane_runtime_t *const runtime =
        &g_state.scheduler.independent_lanes[lane_index];
    runtime->active = 0;
    runtime->scheduling_epoch =
        db_checked_add_u32(BACKEND_NAME, "independent_lane_scheduling_epoch",
                           runtime->scheduling_epoch, 1U);
    for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
         slot_index++) {
        runtime->slots[slot_index].quarantined = 1;
        runtime->slots[slot_index].phase = DB_VK_EXTERNAL_SLOT_UNUSED;
    }
    if (lane_index < g_state.device.selection.lane_count) {
        g_state.device.selection.lanes[lane_index].active_for_scheduler = 0;
        db_vk_set_lane_reason(&g_state.device.selection.lanes[lane_index],
                              reason);
    }
    g_state.scheduler.scheduling_epoch =
        db_checked_add_u32(BACKEND_NAME, "scheduling_epoch",
                           g_state.scheduler.scheduling_epoch, 1U);
    g_state.calibration.state = (db_vk_calibration_state_t){
        .phase = DB_VK_MULTI_GPU_CLOSED,
    };
    const db_log_field_t fields[] = {
        DB_LOG_U64("lane", lane_index),
        DB_LOG_U64("scheduling_epoch", g_state.scheduler.scheduling_epoch),
        DB_LOG_TOKEN("reason", reason),
    };
    db_log_info(BACKEND_NAME, "vk_lane_quarantined", fields,
                DB_LOG_FIELD_COUNT(fields));
}

#else
void db_vk_independent_lanes_init(void) {}
void db_vk_independent_lane_quarantine(uint32_t lane_index,
                                       const char *reason) {
    (void)lane_index;
    (void)reason;
}
#endif
