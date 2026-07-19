#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_render_ir.h"
#include "../../core/db_render_types.h"
#include "core/db_log.h"
#include "core/db_progress_policy.h"
#include "vk_diagnostics.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_runtime_internal.h"
#include "vk_state_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

void db_vk_release_rebuild_upload_buffer(void) {
    if (g_state.backing.rebuild_upload_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_state.device.device,
                        g_state.backing.rebuild_upload_buffer, NULL);
    }
    if (g_state.backing.rebuild_upload_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_state.device.device,
                     g_state.backing.rebuild_upload_memory, NULL);
    }
    g_state.backing.rebuild_upload_buffer = VK_NULL_HANDLE;
    g_state.backing.rebuild_upload_memory = VK_NULL_HANDLE;
    g_state.backing.rebuild_upload_size_bytes = 0U;
}

static uint32_t vk_host_visible_memory_type(uint32_t type_bits) {
    VkPhysicalDeviceMemoryProperties properties = {0};
    vkGetPhysicalDeviceMemoryProperties(g_state.device.present_phys,
                                        &properties);
    for (uint32_t index = 0U; index < properties.memoryTypeCount; index++) {
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (((type_bits & (1U << index)) != 0U) &&
            ((properties.memoryTypes[index].propertyFlags & required) ==
             required)) {
            return index;
        }
    }
    DB_RUNTIME_FAIL(BACKEND_NAME,
                    "no host-visible Vulkan raster seed upload memory");
}

static size_t
vk_raster_seed_size(const db_render_ir_external_binding_t *binding) {
    size_t required_bytes = 0U;
    if ((binding == NULL) || (binding->pixels == NULL) ||
        (db_try_strided_size(binding->height, binding->row_stride_bytes,
                             binding->row_stride_bytes,
                             &required_bytes) == 0) ||
        (required_bytes > binding->size_bytes)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "invalid Vulkan raster rebuild seed");
    }
    return required_bytes;
}

void db_vk_prepare_raster_seed_upload(
    const db_render_ir_external_binding_t *binding) {
    const size_t required_bytes = vk_raster_seed_size(binding);
    if (g_state.backing.rebuild_upload_size_bytes < required_bytes) {
        db_vk_release_rebuild_upload_buffer();
        const VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = required_bytes,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        DB_VK_CHECK(BACKEND_NAME,
                    vkCreateBuffer(g_state.device.device, &create_info, NULL,
                                   &g_state.backing.rebuild_upload_buffer));
        VkMemoryRequirements requirements = {0};
        vkGetBufferMemoryRequirements(g_state.device.device,
                                      g_state.backing.rebuild_upload_buffer,
                                      &requirements);
        const VkMemoryAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex =
                vk_host_visible_memory_type(requirements.memoryTypeBits),
        };
        DB_VK_CHECK(BACKEND_NAME,
                    vkAllocateMemory(g_state.device.device, &allocation, NULL,
                                     &g_state.backing.rebuild_upload_memory));
        DB_VK_CHECK(BACKEND_NAME,
                    vkBindBufferMemory(g_state.device.device,
                                       g_state.backing.rebuild_upload_buffer,
                                       g_state.backing.rebuild_upload_memory,
                                       0U));
        g_state.backing.rebuild_upload_size_bytes = required_bytes;
    }
    void *mapped = NULL;
    DB_VK_CHECK(BACKEND_NAME, vkMapMemory(g_state.device.device,
                                          g_state.backing.rebuild_upload_memory,
                                          0U, required_bytes, 0U, &mapped));
    memcpy(mapped, binding->pixels, required_bytes);
    vkUnmapMemory(g_state.device.device, g_state.backing.rebuild_upload_memory);
}

void db_vk_record_raster_seed_upload(
    VkCommandBuffer command_buffer, VkBackingTargetState *target,
    const db_render_ir_external_binding_t *binding) {
    if ((target == NULL) || (binding == NULL) ||
        (binding->width != g_state.backing.extent.width) ||
        (binding->height != g_state.backing.extent.height) ||
        (binding->format != g_state.backing.pixel_format)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "Vulkan raster rebuild seed mismatch");
    }
    const int initialized = target->layout_initialized;
    const VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = (initialized != 0) ? VK_ACCESS_SHADER_READ_BIT : 0U,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = (initialized != 0)
                         ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = target->image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1U,
                .layerCount = 1U,
            },
    };
    vkCmdPipelineBarrier(command_buffer,
                         (initialized != 0)
                             ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL,
                         1U, &to_transfer);
    const VkBufferImageCopy copy = {
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1U,
            },
        .imageExtent =
            {
                .width = binding->width,
                .height = binding->height,
                .depth = 1U,
            },
    };
    vkCmdCopyBufferToImage(command_buffer,
                           g_state.backing.rebuild_upload_buffer, target->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
    VkImageMemoryBarrier to_color = to_transfer;
    to_color.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0U, 0U,
                         NULL, 0U, NULL, 1U, &to_color);
    target->layout_initialized = 1;
}

void db_vk_release_output_hash_readback_buffer(void) {
    if (g_state.hash.hash_readback_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_state.device.device,
                        g_state.hash.hash_readback_buffer, NULL);
        g_state.hash.hash_readback_buffer = VK_NULL_HANDLE;
    }
    if (g_state.hash.hash_readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_state.device.device, g_state.hash.hash_readback_memory,
                     NULL);
        g_state.hash.hash_readback_memory = VK_NULL_HANDLE;
    }
    g_state.hash.hash_readback_size_bytes = 0U;
}

void db_vk_ensure_output_hash_readback_buffer(size_t required_bytes) {
    if (required_bytes == 0U) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "invalid Vulkan output hash readback size=0");
    }
    if ((g_state.hash.hash_readback_buffer != VK_NULL_HANDLE) &&
        (g_state.hash.hash_readback_memory != VK_NULL_HANDLE) &&
        (g_state.hash.hash_readback_size_bytes >= required_bytes)) {
        return;
    }
    db_vk_release_output_hash_readback_buffer();

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = (VkDeviceSize)required_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateBuffer(g_state.device.device, &bci, NULL,
                               &g_state.hash.hash_readback_buffer));

    VkMemoryRequirements mr = {0};
    vkGetBufferMemoryRequirements(g_state.device.device,
                                  g_state.hash.hash_readback_buffer, &mr);
    VkPhysicalDeviceMemoryProperties mp = {0};
    vkGetPhysicalDeviceMemoryProperties(g_state.device.present_phys, &mp);
    uint32_t mem_index = UINT32_MAX;
    for (uint32_t i = 0U; i < mp.memoryTypeCount; i++) {
        const uint32_t mask = MASK_GPU0 << i;
        const uint32_t flags = mp.memoryTypes[i].propertyFlags;
        if ((mr.memoryTypeBits & mask) == 0U) {
            continue;
        }
        if ((flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            mem_index = i;
            break;
        }
    }
    if (mem_index == UINT32_MAX) {
        DB_RUNTIME_FAIL(
            BACKEND_NAME,
            "No host-visible/coherent memory for Vulkan hash readback");
    }
    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_index;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateMemory(g_state.device.device, &mai, NULL,
                                 &g_state.hash.hash_readback_memory));
    DB_VK_CHECK(BACKEND_NAME,
                vkBindBufferMemory(g_state.device.device,
                                   g_state.hash.hash_readback_buffer,
                                   g_state.hash.hash_readback_memory, 0));
    g_state.hash.hash_readback_size_bytes = required_bytes;
}

uint64_t db_vk_compute_output_hash_from_image(VkImage image,
                                              VkImageLayout old_layout,
                                              VkExtent2D extent) {
    if ((image == VK_NULL_HANDLE) || (extent.width == 0U) ||
        (extent.height == 0U)) {
        return DB_FNV1A64_OFFSET;
    }
    const size_t pixel_bytes =
        db_pixel_format_bytes_per_pixel(g_state.backing.pixel_format);
    if (pixel_bytes == 0U) {
        return DB_FNV1A64_OFFSET;
    }
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "vk_output_hash_bytes",
        db_checked_mul_size(
            BACKEND_NAME, "vk_output_hash_pixels",
            db_checked_u32_to_size(BACKEND_NAME, "extent_w", extent.width),
            db_checked_u32_to_size(BACKEND_NAME, "extent_h", extent.height)),
        pixel_bytes);
    db_vk_ensure_output_hash_readback_buffer(byte_count);

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_state.device.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateCommandBuffers(g_state.device.device, &cai, &cmd));
    VkCommandBufferBeginInfo cbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DB_VK_CHECK(BACKEND_NAME, vkBeginCommandBuffer(cmd, &cbi));

    VkImageMemoryBarrier to_src = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = old_layout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0U,
                .levelCount = 1U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1U, &to_src);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1U;
    region.imageExtent.width = extent.width;
    region.imageExtent.height = extent.height;
    region.imageExtent.depth = 1U;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_state.hash.hash_readback_buffer, 1U, &region);

    VkImageMemoryBarrier back = to_src;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = old_layout;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1U, &back);

    DB_VK_CHECK(BACKEND_NAME, vkEndCommandBuffer(cmd));
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1U;
    si.pCommandBuffers = &cmd;
    VkFence readback_fence = VK_NULL_HANDLE;
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    DB_VK_CHECK(BACKEND_NAME, vkCreateFence(g_state.device.device, &fence_info,
                                            NULL, &readback_fence));
    DB_VK_CHECK(BACKEND_NAME,
                vkQueueSubmit(g_state.device.queue, 1U, &si, readback_fence));
    DB_VK_CHECK(BACKEND_NAME,
                db_vk_wait_fence(g_state.device.device, readback_fence,
                                 DB_PROGRESS_VK_PRIMARY_FENCE,
                                 "hash_readback"));

    void *mapped = NULL;
    DB_VK_CHECK(BACKEND_NAME, vkMapMemory(g_state.device.device,
                                          g_state.hash.hash_readback_memory, 0,
                                          byte_count, 0, &mapped));
    const size_t row_stride_bytes = db_checked_mul_size(
        BACKEND_NAME, "vk_output_hash_row_stride", extent.width, pixel_bytes);
    const uint64_t hash =
        db_hash_working_rgba8(mapped, g_state.backing.pixel_format,
                              extent.width, extent.height, row_stride_bytes, 0);
    vkUnmapMemory(g_state.device.device, g_state.hash.hash_readback_memory);
    vkDestroyFence(g_state.device.device, readback_fence, NULL);
    vkFreeCommandBuffers(g_state.device.device, g_state.device.command_pool, 1U,
                         &cmd);
    return hash;
}

int db_vk_dual_metrics_enabled(void) {
    return g_state.runtime.dual_metrics_enabled;
}

void db_vk_record_render_frame_duration(double frame_ms) {
    g_state.metrics.last_render_critical_ms = frame_ms;
}

void db_vk_recreate_swapchain_and_backing_targets_with_reset(void) {
    db_vk_recreate_swapchain_state(
        &g_state.presentation.wsi_config, g_state.device.present_phys,
        g_state.device.device, g_state.presentation.surface,
        g_state.presentation.surface_format, g_state.presentation.present_mode,
        g_state.presentation.render_pass,
        &g_state.presentation.swapchain_state);
}
