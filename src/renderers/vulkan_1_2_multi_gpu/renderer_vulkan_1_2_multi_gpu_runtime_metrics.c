#include "../../config/runtime_options.h"
#include "../../core/db_alloc_policy.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../renderer_benchmark_gradient.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_history_common.h"
#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_runtime_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

const float db_vk_shader_ignored_color_rgb[3] = {0.0F, 0.0F, 0.0F};

void db_vk_release_output_hash_readback_buffer(void) {
    if (g_state.hash_readback_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_state.device, g_state.hash_readback_buffer, NULL);
        g_state.hash_readback_buffer = VK_NULL_HANDLE;
    }
    if (g_state.hash_readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_state.device, g_state.hash_readback_memory, NULL);
        g_state.hash_readback_memory = VK_NULL_HANDLE;
    }
    g_state.hash_readback_size_bytes = 0U;
}

void db_vk_ensure_output_hash_readback_buffer(size_t required_bytes) {
    if (required_bytes == 0U) {
        db_failf(BACKEND_NAME, "invalid Vulkan output hash readback size=0");
    }
    if ((g_state.hash_readback_buffer != VK_NULL_HANDLE) &&
        (g_state.hash_readback_memory != VK_NULL_HANDLE) &&
        (g_state.hash_readback_size_bytes >= required_bytes)) {
        return;
    }
    db_vk_release_output_hash_readback_buffer();

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = (VkDeviceSize)required_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkCreateBuffer(g_state.device, &bci, NULL,
                                             &g_state.hash_readback_buffer));

    VkMemoryRequirements mr = {0};
    vkGetBufferMemoryRequirements(g_state.device, g_state.hash_readback_buffer,
                                  &mr);
    VkPhysicalDeviceMemoryProperties mp = {0};
    vkGetPhysicalDeviceMemoryProperties(g_state.present_phys, &mp);
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
        db_failf(BACKEND_NAME,
                 "No host-visible/coherent memory for Vulkan hash readback");
    }
    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_index;
    DB_VK_CHECK(BACKEND_NAME, vkAllocateMemory(g_state.device, &mai, NULL,
                                               &g_state.hash_readback_memory));
    DB_VK_CHECK(BACKEND_NAME,
                vkBindBufferMemory(g_state.device, g_state.hash_readback_buffer,
                                   g_state.hash_readback_memory, 0));
    g_state.hash_readback_size_bytes = required_bytes;
}

uint64_t db_vk_compute_output_hash_from_image(VkImage image,
                                              VkImageLayout old_layout,
                                              VkExtent2D extent) {
    if ((image == VK_NULL_HANDLE) || (extent.width == 0U) ||
        (extent.height == 0U)) {
        return DB_FNV1A64_OFFSET;
    }
    const size_t byte_count = db_checked_mul_size(
        BACKEND_NAME, "vk_output_hash_bytes",
        db_checked_mul_size(BACKEND_NAME, "vk_output_hash_pixels",
                            (size_t)extent.width, (size_t)extent.height),
        4U);
    db_vk_ensure_output_hash_readback_buffer(byte_count);

    vkQueueWaitIdle(g_state.queue);

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_state.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateCommandBuffers(g_state.device, &cai, &cmd));
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
                           g_state.hash_readback_buffer, 1U, &region);

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
    DB_VK_CHECK(BACKEND_NAME,
                vkQueueSubmit(g_state.queue, 1U, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(g_state.queue);

    void *mapped = NULL;
    DB_VK_CHECK(BACKEND_NAME,
                vkMapMemory(g_state.device, g_state.hash_readback_memory, 0,
                            byte_count, 0, &mapped));
    const uint64_t hash = db_fnv1a64_bytes(mapped, byte_count);
    vkUnmapMemory(g_state.device, g_state.hash_readback_memory);
    vkFreeCommandBuffers(g_state.device, g_state.command_pool, 1U, &cmd);
    return hash;
}

db_vk_grid_row_block_draw_req_t
db_vk_gradient_row_block_req(const db_grid_block_t *block, db_pattern_t pattern,
                             const db_gradient_state_t *state,
                             uint32_t frame_index) {
    if ((state == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U)) {
        return (db_vk_grid_row_block_draw_req_t){0};
    }
    return (db_vk_grid_row_block_draw_req_t){
        .span_units =
            db_grid_block_span_units_or_fail("vk_gradient_span_units", block),
        .block = *block,
        .payload =
            {
                .color = db_vk_shader_ignored_color_rgb,
                .render_mode = db_checked_pattern_enum_to_u32(
                    BACKEND_NAME, "vk_render_mode", pattern),
                .gradient_head_row = state->head_row,
                .snake_shape_index = 0U,
                .gradient_direction_flag = state->direction_down,
                .snake_phase_flag = 0,
                .snake_cursor = 0U,
                .snake_batch_size = 0U,
                .snake_phase_completed = 0,
                .palette_cycle = state->cycle_index,
                .frame_index = frame_index,
                .band_count = 0U,
            },
    };
}

void db_vk_draw_gradient_block_segments(const db_vk_owner_draw_ctx_t *draw_ctx,
                                        const db_grid_block_t *block,
                                        db_pattern_t pattern,
                                        const db_gradient_state_t *state,
                                        uint32_t frame_index) {
    if ((draw_ctx == NULL) || (block == NULL) || (state == NULL) ||
        (block->row_count == 0U) || (block->col_count == 0U)) {
        return;
    }
    db_gradient_row_segment_iter_t iter = {0};
    db_gradient_row_segment_t segment = {0};
    if (db_gradient_row_segment_iter_init(block, state->head_row,
                                          state->direction_down,
                                          state->cycle_index, &iter) == 0) {
        return;
    }
    while (db_gradient_row_segment_iter_next(&iter, &segment) != 0) {
        const db_vk_grid_row_block_draw_req_t req =
            db_vk_gradient_row_block_req(&segment.block, pattern, state,
                                         frame_index);
        db_vk_draw_owner_grid_row_block(draw_ctx, &req);
    }
}

int db_vk_dual_metrics_enabled(void) {
    const char *const metrics_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_METRICS_MODE);
    return ((metrics_mode != NULL) && (strcmp(metrics_mode, "dual") == 0)) ? 1
                                                                           : 0;
}

uint32_t db_vk_metrics_sample_capacity_hint(void) {
    const char *const frame_limit_text =
        db_runtime_option_get(DB_RUNTIME_OPT_FRAME_LIMIT);
    if ((frame_limit_text == NULL) || (frame_limit_text[0] == '\0')) {
        return DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(frame_limit_text, &end, 10);
    if ((end == frame_limit_text) || (end == NULL) || (*end != '\0')) {
        return DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY;
    }
    if (parsed == 0UL) {
        return DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY;
    }
    if (parsed > (unsigned long)UINT32_MAX) {
        return UINT32_MAX;
    }
    const uint32_t hint =
        db_checked_ulong_to_u32(BACKEND_NAME, "frame_limit_hint", parsed);
    return (hint > DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY)
               ? hint
               : DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY;
}

void db_vk_record_render_frame_sample(double frame_ms) {
    if (db_vk_dual_metrics_enabled() == 0) {
        return;
    }
    if (g_state.render_frame_samples_ms == NULL) {
        g_state.render_frame_samples_capacity =
            db_vk_metrics_sample_capacity_hint();
        g_state.render_frame_samples_ms = (double *)db_alloc_array_or_fail(
            BACKEND_NAME, "render_frame_samples",
            g_state.render_frame_samples_capacity, sizeof(double));
    }
    if (g_state.render_frame_samples_count >=
        g_state.render_frame_samples_capacity) {
        const uint32_t new_capacity =
            db_u32_grow_capacity_3_2(g_state.render_frame_samples_capacity,
                                     g_state.render_frame_samples_count + 1U,
                                     db_vk_metrics_sample_capacity_hint());
        if (new_capacity <= g_state.render_frame_samples_capacity) {
            db_failf(BACKEND_NAME, "render frame sample capacity overflow");
        }
        size_t sample_capacity = (size_t)g_state.render_frame_samples_capacity;
        db_reserve_array_capacity_or_fail(
            (void **)&g_state.render_frame_samples_ms, &sample_capacity,
            (size_t)new_capacity, db_vk_metrics_sample_capacity_hint(),
            sizeof(double), g_state.render_frame_samples_count, BACKEND_NAME,
            "render_frame_samples");
        g_state.render_frame_samples_capacity = db_checked_size_to_u32(
            BACKEND_NAME, "render_frame_samples_capacity", sample_capacity);
    }
    g_state.render_frame_samples_ms[g_state.render_frame_samples_count++] =
        frame_ms;
}

void db_vk_recreate_swapchain_and_history_targets_with_reset(void) {
    const VkExtent2D old_extent = g_state.swapchain_state.extent;
    db_vk_recreate_swapchain_state(
        &g_state.wsi_config, g_state.present_phys, g_state.device,
        g_state.surface, g_state.surface_format, g_state.present_mode,
        g_state.render_pass, &g_state.swapchain_state);
    const int preserved = db_vk_recreate_history_targets_preserve(
        g_state.present_phys, g_state.device, g_state.surface_format.format,
        g_state.swapchain_state.extent, g_state.history_render_pass,
        g_state.device_group_mask, g_state.command_pool, g_state.queue,
        old_extent, g_state.history_targets, &g_state.history_pair.read_index);
    db_vk_update_history_descriptor(
        g_state.device, g_state.descriptor_set, g_state.history_sampler,
        g_state.history_targets[g_state.history_pair.read_index].view);
    (void)db_history_pair_sync_descriptor_index_if_needed(
        1, &g_state.history_descriptor_index, &g_state.history_pair);
    const db_history_resize_preserve_policy_t resize_policy =
        db_history_resize_preserve_policy_for_pattern(g_state.runtime.pattern,
                                                      1, preserved, 0);
    db_history_apply_resize_preserve_policy(&resize_policy, &g_state.runtime,
                                            &g_state.history_pair,
                                            &g_state.gradient_prev_frame);
}
