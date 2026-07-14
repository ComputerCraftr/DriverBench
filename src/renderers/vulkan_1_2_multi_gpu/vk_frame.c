#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <vulkan/vulkan_core.h>

#include "../../core/db_core.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "core/db_render_types.h"
#include "vk_internal.h"
#include "vk_renderer.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define MASK_GPU0 1U

typedef struct {
    VkCommandBuffer cmd;
    VkExtent2D extent;
    uint32_t grid_rows;
    uint32_t grid_cols;
} db_vk_grid_draw_ctx_t;

static void vk_destroy_swapchain_state(VkDevice device, SwapchainState *state) {
    if ((state == NULL) || (state->swapchain == VK_NULL_HANDLE)) {
        return;
    }
    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyFramebuffer(device, state->framebuffers[i], NULL);
    }
    db_free_opaque_handle_array((void *)state->framebuffers);
    state->framebuffers = NULL;

    free(state->image_layouts);
    state->image_layouts = NULL;

    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyImageView(device, state->views[i], NULL);
    }
    db_free_opaque_handle_array((void *)state->views);
    state->views = NULL;

    db_free_opaque_handle_array((void *)state->images);
    state->images = NULL;
    state->image_count = 0;

    vkDestroySwapchainKHR(device, state->swapchain, NULL);
    state->swapchain = VK_NULL_HANDLE;
}

void db_vk_destroy_backing_target(VkDevice device,
                                  VkBackingTargetState *target) {
    if (target == NULL) {
        return;
    }
    if (target->framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, target->framebuffer, NULL);
    }
    if (target->view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, target->view, NULL);
    }
    if (target->image != VK_NULL_HANDLE) {
        vkDestroyImage(device, target->image, NULL);
    }
    if (target->memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, target->memory, NULL);
    }
    *target = (VkBackingTargetState){0};
}

void db_vk_recreate_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                    VkPhysicalDevice present_phys,
                                    VkDevice device, VkSurfaceKHR surface,
                                    VkSurfaceFormatKHR surface_format,
                                    VkPresentModeKHR present_mode,
                                    VkRenderPass render_pass,
                                    SwapchainState *state) {
    vk_destroy_swapchain_state(device, state);
    db_vk_create_swapchain_state(wsi_config, present_phys, device, surface,
                                 surface_format, present_mode, render_pass,
                                 state);
}

void db_vk_recreate_backing_target(VkPhysicalDevice phys, VkDevice device,
                                   VkFormat format, VkExtent2D extent,
                                   VkRenderPass render_pass,
                                   uint32_t device_group_mask,
                                   VkBackingTargetState *targets) {
    if (targets == NULL) {
        return;
    }

    VkBackingTargetState old_target = targets[0];
    targets[0] = (VkBackingTargetState){0};
    db_vk_create_backing_target(phys, device, format, extent, render_pass,
                                device_group_mask, &targets[0]);
    targets[0].layout_initialized = 0;
    db_vk_destroy_backing_target(device, &old_target);
}

static void vk_draw_grid_row_block(const db_vk_grid_draw_ctx_t *ctx,
                                   const db_grid_block_t *block,
                                   uint32_t first_instance,
                                   uint32_t instance_count,
                                   const VkRect2D *piece_scissor) {
    if ((ctx == NULL) || (block == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U) || (block->row_start >= ctx->grid_rows) ||
        (ctx->grid_rows == 0U) || (ctx->grid_cols == 0U) ||
        (block->col_start >= ctx->grid_cols)) {
        return;
    }
    db_damage_block_t pixel_block = {0};
    if (db_grid_block_to_pixel_block(ctx->grid_cols, ctx->grid_rows, block,
                                     ctx->extent.width, ctx->extent.height,
                                     &pixel_block) == 0) {
        return;
    }
    VkRect2D sc =
        (piece_scissor != NULL)
            ? *piece_scissor
            : (VkRect2D){
                  .offset = {.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32",
                                                        pixel_block.col_start),
                             .y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32",
                                                        pixel_block.row_start)},
                  .extent = {.width = pixel_block.col_count,
                             .height = pixel_block.row_count}};
    vkCmdSetScissor(ctx->cmd, 0, 1, &sc);

    vkCmdDraw(ctx->cmd, DB_RECT_VERTEX_COUNT, DB_MAX(instance_count, 1U), 0U,
              first_instance);
}

void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                              VkQueryPool query_pool, uint32_t owner,
                              uint8_t *owner_started) {
    if ((!timing_enabled) || (owner_started == NULL)) {
        return;
    }
    if (owner_started[owner] == 0U) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                            owner * TIMESTAMP_QUERIES_PER_GPU);
        owner_started[owner] = 1U;
    }
}

void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                            VkQueryPool query_pool, uint32_t owner,
                            uint8_t *owner_finished) {
    if ((!timing_enabled) || (owner_finished == NULL)) {
        return;
    }
    if (owner_finished[owner] != 0U) {
        return;
    }
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool,
                        (owner * TIMESTAMP_QUERIES_PER_GPU) + 1U);
    owner_finished[owner] = 1U;
}

void db_vk_draw_owner_grid_row_block(
    const db_vk_owner_draw_ctx_t *ctx,
    const db_vk_grid_row_block_draw_req_t *req) {
    if ((ctx == NULL) || (req == NULL) || (req->span_units == 0U) ||
        (req->block.row_count == 0U) || (req->block.col_count == 0U) ||
        (ctx->grid_tiles_drawn == NULL) || (ctx->grid_tiles_per_gpu == NULL) ||
        (ctx->frame_work_units == NULL)) {
        return;
    }

    const uint32_t lane_count = DB_MAX(ctx->active_gpu_count, 1U);
    const uint32_t owner = req->owner;
    if ((owner >= lane_count) ||
        ((ctx->owner_enabled != NULL) && (ctx->owner_enabled[owner] == 0U))) {
        return;
    }
    ctx->grid_tiles_per_gpu[owner] += req->span_units;
    *ctx->grid_tiles_drawn += req->span_units;
    if (ctx->have_group) {
        vkCmdSetDeviceMask(ctx->cmd, (MASK_GPU0 << owner));
    }
    db_vk_owner_timing_begin(ctx->cmd, ctx->timing_enabled,
                             ctx->timing_query_pool, owner,
                             ctx->frame_owner_used);
    const db_vk_grid_draw_ctx_t draw_ctx = {
        .cmd = ctx->cmd,
        .extent = ctx->extent,
        .grid_rows = ctx->grid_rows,
        .grid_cols = ctx->grid_cols,
    };
    vk_draw_grid_row_block(&draw_ctx, &req->block, req->first_instance,
                           req->instance_count, &req->scissor);
    db_vk_owner_timing_end(ctx->cmd, ctx->timing_enabled,
                           ctx->timing_query_pool, owner,
                           ctx->frame_owner_finished);
    ctx->frame_work_units[owner] += req->span_units;
}

void db_vk_cleanup_runtime(const db_vk_cleanup_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    vkDestroyFence(ctx->device, ctx->in_flight, NULL);
    vkDestroySemaphore(ctx->device, ctx->image_available, NULL);
    vkDestroySemaphore(ctx->device, ctx->render_done, NULL);
    vkDestroyBuffer(ctx->device, ctx->vertex_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->vertex_memory, NULL);
    if (ctx->instance_mapped != NULL) {
        vkUnmapMemory(ctx->device, ctx->instance_memory);
    }
    vkDestroyBuffer(ctx->device, ctx->instance_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->instance_memory, NULL);
    if (ctx->lookup_mapped != NULL) {
        vkUnmapMemory(ctx->device, ctx->lookup_memory);
    }
    vkDestroyBuffer(ctx->device, ctx->lookup_buffer, NULL);
    vkFreeMemory(ctx->device, ctx->lookup_memory, NULL);
    if (ctx->hash_readback_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(ctx->device, ctx->hash_readback_buffer, NULL);
    }
    if (ctx->hash_readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, ctx->hash_readback_memory, NULL);
    }
    vkDestroyPipeline(ctx->device, ctx->pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->present_pipeline, NULL);
    vkDestroyPipeline(ctx->device, ctx->composition_pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);
    if (ctx->backing_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, ctx->backing_sampler, NULL);
    }
    if (ctx->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, NULL);
    }
    if (ctx->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout,
                                     NULL);
    }
    vk_destroy_swapchain_state(ctx->device, ctx->swapchain_state);
    db_vk_destroy_backing_target(ctx->device, &ctx->backing_targets[0]);
    vkDestroyRenderPass(ctx->device, ctx->backing_render_pass, NULL);
    vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
    vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
    if (ctx->timing_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(ctx->device, ctx->timing_query_pool, NULL);
    }
    vkDestroyDevice(ctx->device, NULL);
    vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    vkDestroyInstance(ctx->instance, NULL);
}
