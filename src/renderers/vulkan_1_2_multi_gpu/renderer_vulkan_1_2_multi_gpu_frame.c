#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

#if !defined(VERT_SPV_PATH) || !defined(FRAG_SPV_PATH)
#error "Vulkan SPIR-V shader paths must be provided by the build system."
#endif

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define COLOR_CHANNEL_ALPHA 3U
#define DB_CAP_MODE_VULKAN_DEVICE_GROUP_MULTI_GPU                              \
    "vulkan_device_group_multi_gpu"
#define DB_CAP_MODE_VULKAN_SINGLE_GPU "vulkan_single_gpu"
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define FRAME_BUDGET_NS 16666666ULL
#define FRAME_SAFETY_NS 2000000ULL
#define MASK_GPU0 1U
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define WAIT_TIMEOUT_NS 100000000ULL
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    uint32_t candidate_owner;
    uint32_t span_units;
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
    const float *color;
    uint32_t render_mode;
    uint32_t gradient_head_row;
    int mode_phase_flag;
    uint32_t snake_cursor;
    uint32_t snake_batch_size;
    uint32_t snake_rect_index;
    int snake_phase_completed;
    uint32_t palette_cycle;
} db_vk_grid_span_draw_req_t;

typedef struct {
    VkCommandBuffer cmd;
    VkPipelineLayout layout;
    VkExtent2D extent;
    uint32_t grid_rows;
    uint32_t grid_cols;
} db_vk_grid_draw_ctx_t;

typedef struct {
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
    db_vk_draw_dynamic_req_t dynamic;
} db_vk_grid_span_draw_cmd_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_end;
    db_vk_draw_dynamic_req_t dynamic;
} db_vk_grid_row_block_draw_cmd_t;

static const VkShaderStageFlags DB_PC_STAGES =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

void db_vk_push_constants_frame_static(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       VkExtent2D extent, uint32_t grid_rows,
                                       uint32_t grid_cols) {
    const float base_color[4] = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                                 BENCH_GRID_PHASE0_B, 1.0F};
    const float target_color[4] = {BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G,
                                   BENCH_GRID_PHASE1_B, 1.0F};

    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, gradient_window_rows),
                       sizeof(g_state.gradient_window_rows),
                       &g_state.gradient_window_rows);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, viewport_height),
                       sizeof(extent.height), &extent.height);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, grid_cols),
                       sizeof(grid_cols), &grid_cols);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, base_color),
                       sizeof(base_color), base_color);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, target_color),
                       sizeof(target_color), target_color);

    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, grid_rows),
                       sizeof(grid_rows), &grid_rows);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, viewport_width),
                       sizeof(extent.width), &extent.width);
}

void db_vk_push_constants_draw_dynamic(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       const db_vk_draw_dynamic_req_t *req) {
    if (req == NULL) {
        return;
    }
    PushConstants pc = {0};
    pc.offset_ndc[0] = req->ndc_x0;
    pc.offset_ndc[1] = req->ndc_y0;
    pc.scale_ndc[0] = (req->ndc_x1 - req->ndc_x0);
    pc.scale_ndc[1] = (req->ndc_y1 - req->ndc_y0);
    pc.color[0] = req->color[0];
    pc.color[1] = req->color[1];
    pc.color[2] = req->color[2];
    pc.color[COLOR_CHANNEL_ALPHA] = 1.0F;
    pc.render_mode = req->render_mode;
    pc.gradient_head_row = req->gradient_head_row;
    pc.snake_rect_index = req->snake_rect_index;
    pc.mode_phase_flag = (int32_t)req->mode_phase_flag;
    pc.snake_cursor = req->snake_cursor;
    pc.snake_batch_size = req->snake_batch_size;
    pc.snake_phase_completed = (int32_t)req->snake_phase_completed;
    pc.palette_cycle = req->palette_cycle;
    pc.pattern_seed = (req->render_mode == DB_PATTERN_RECT_SNAKE)
                          ? g_state.runtime.pattern_seed
                          : 0U;
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES, 0U,
                       (uint32_t)offsetof(PushConstants, base_color), &pc);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, render_mode),
                       sizeof(pc.render_mode), &pc.render_mode);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, gradient_head_row),
                       sizeof(pc.gradient_head_row), &pc.gradient_head_row);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_rect_index),
                       sizeof(pc.snake_rect_index), &pc.snake_rect_index);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, mode_phase_flag),
                       sizeof(pc.mode_phase_flag), &pc.mode_phase_flag);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_cursor),
                       sizeof(pc.snake_cursor), &pc.snake_cursor);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_batch_size),
                       sizeof(pc.snake_batch_size), &pc.snake_batch_size);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_phase_completed),
                       sizeof(pc.snake_phase_completed),
                       &pc.snake_phase_completed);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, palette_cycle),
                       sizeof(pc.palette_cycle), &pc.palette_cycle);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, pattern_seed),
                       sizeof(pc.pattern_seed), &pc.pattern_seed);
}

static void db_vk_destroy_swapchain_state(VkDevice device,
                                          SwapchainState *state) {
    if ((state == NULL) || (state->swapchain == VK_NULL_HANDLE)) {
        return;
    }
    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyFramebuffer(device, state->framebuffers[i], NULL);
    }
    free((void *)state->framebuffers);
    state->framebuffers = NULL;

    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyImageView(device, state->views[i], NULL);
    }
    free((void *)state->views);
    state->views = NULL;

    free((void *)state->images);
    state->images = NULL;
    state->image_count = 0;

    vkDestroySwapchainKHR(device, state->swapchain, NULL);
    state->swapchain = VK_NULL_HANDLE;
}

static void db_vk_destroy_history_target(VkDevice device,
                                         HistoryTargetState *target) {
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
    *target = (HistoryTargetState){0};
}

void db_vk_recreate_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                    VkPhysicalDevice present_phys,
                                    VkDevice device, VkSurfaceKHR surface,
                                    VkSurfaceFormatKHR surface_format,
                                    VkPresentModeKHR present_mode,
                                    VkRenderPass render_pass,
                                    SwapchainState *state) {
    DB_VK_CHECK(BACKEND_NAME, vkDeviceWaitIdle(device));
    db_vk_destroy_swapchain_state(device, state);
    db_vk_create_swapchain_state(wsi_config, present_phys, device, surface,
                                 surface_format, present_mode, render_pass,
                                 state);
}

static int db_vk_copy_history_image_preserve(VkDevice device,
                                             VkCommandPool command_pool,
                                             VkQueue queue, VkImage src_image,
                                             VkExtent2D src_extent,
                                             VkImage dst_image,
                                             VkExtent2D dst_extent) {
    if ((src_image == VK_NULL_HANDLE) || (dst_image == VK_NULL_HANDLE) ||
        (src_extent.width == 0U) || (src_extent.height == 0U) ||
        (dst_extent.width == 0U) || (dst_extent.height == 0U)) {
        return 0;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1U;
    VkCommandBuffer copy_cmd = VK_NULL_HANDLE;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateCommandBuffers(device, &alloc_info, &copy_cmd));

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkBeginCommandBuffer(copy_cmd, &begin_info));

    VkImageMemoryBarrier pre_copy[2] = {{0}, {0}};
    pre_copy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_copy[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pre_copy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pre_copy[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pre_copy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre_copy[0].image = src_image;
    pre_copy[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_copy[0].subresourceRange.levelCount = 1U;
    pre_copy[0].subresourceRange.layerCount = 1U;
    pre_copy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_copy[1].srcAccessMask = 0;
    pre_copy[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre_copy[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pre_copy[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre_copy[1].image = dst_image;
    pre_copy[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_copy[1].subresourceRange.levelCount = 1U;
    pre_copy[1].subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         2U, pre_copy);

    VkImageCopy region = {0};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1U;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1U;
    region.extent.width = (src_extent.width < dst_extent.width)
                              ? src_extent.width
                              : dst_extent.width;
    region.extent.height = (src_extent.height < dst_extent.height)
                               ? src_extent.height
                               : dst_extent.height;
    region.extent.depth = 1U;
    vkCmdCopyImage(copy_cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U,
                   &region);

    VkImageMemoryBarrier post_copy[2] = {{0}, {0}};
    post_copy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_copy[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post_copy[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post_copy[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post_copy[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_copy[0].image = src_image;
    post_copy[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_copy[0].subresourceRange.levelCount = 1U;
    post_copy[0].subresourceRange.layerCount = 1U;
    post_copy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_copy[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post_copy[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post_copy[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post_copy[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_copy[1].image = dst_image;
    post_copy[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_copy[1].subresourceRange.levelCount = 1U;
    post_copy[1].subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 2U, post_copy);

    DB_VK_CHECK(BACKEND_NAME, vkEndCommandBuffer(copy_cmd));
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1U;
    submit_info.pCommandBuffers = &copy_cmd;
    DB_VK_CHECK(BACKEND_NAME,
                vkQueueSubmit(queue, 1U, &submit_info, VK_NULL_HANDLE));
    DB_VK_CHECK(BACKEND_NAME, vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, command_pool, 1U, &copy_cmd);
    return 1;
}

int db_vk_recreate_history_targets_preserve(
    VkPhysicalDevice phys, VkDevice device, VkFormat format, VkExtent2D extent,
    VkRenderPass render_pass, uint32_t device_group_mask,
    VkCommandPool command_pool, VkQueue queue, VkExtent2D old_extent,
    HistoryTargetState *targets, int *read_index) {
    if ((targets == NULL) || (read_index == NULL)) {
        return 0;
    }

    DB_VK_CHECK(BACKEND_NAME, vkDeviceWaitIdle(device));
    HistoryTargetState old_targets[2] = {targets[0], targets[1]};
    const int old_read = *read_index;
    targets[0] = (HistoryTargetState){0};
    targets[1] = (HistoryTargetState){0};
    db_vk_create_history_target(phys, device, format, extent, render_pass,
                                device_group_mask, &targets[0]);
    db_vk_create_history_target(phys, device, format, extent, render_pass,
                                device_group_mask, &targets[1]);

    int copied = 0;
    if ((old_read == 0 || old_read == 1) &&
        (old_targets[old_read].layout_initialized != 0)) {
        copied = db_vk_copy_history_image_preserve(
            device, command_pool, queue, old_targets[old_read].image,
            old_extent, targets[0].image, extent);
        copied = copied &&
                 db_vk_copy_history_image_preserve(
                     device, command_pool, queue, old_targets[old_read].image,
                     old_extent, targets[1].image, extent);
    }
    targets[0].layout_initialized = copied;
    targets[1].layout_initialized = copied;
    *read_index = 0;

    db_vk_destroy_history_target(device, &old_targets[0]);
    db_vk_destroy_history_target(device, &old_targets[1]);
    return copied;
}

static void db_vk_grid_span_bounds_ndc(uint32_t row, uint32_t col_start,
                                       uint32_t col_end, uint32_t rows,
                                       uint32_t cols, float *x0, float *y0,
                                       float *x1, float *y1) {
    const float inv_cols = 1.0F / (float)cols;
    const float inv_rows = 1.0F / (float)rows;
    *x0 = (2.0F * (float)col_start * inv_cols) - 1.0F;
    *x1 = (2.0F * (float)col_end * inv_cols) - 1.0F;
    *y0 = (2.0F * (float)row * inv_rows) - 1.0F;
    *y1 = (2.0F * (float)(row + 1U) * inv_rows) - 1.0F;
}

static void db_vk_draw_grid_span(const db_vk_grid_draw_ctx_t *ctx,
                                 const db_vk_grid_span_draw_cmd_t *req) {
    if ((ctx == NULL) || (req == NULL) || (ctx->grid_rows == 0U) ||
        (ctx->grid_cols == 0U) || (req->col_end <= req->col_start) ||
        (req->row >= ctx->grid_rows)) {
        return;
    }

    uint32_t x0 = (ctx->extent.width * req->col_start) / ctx->grid_cols;
    uint32_t x1 = (ctx->extent.width * req->col_end) / ctx->grid_cols;
    uint32_t y0 = (ctx->extent.height * req->row) / ctx->grid_rows;
    uint32_t y1 = (ctx->extent.height * (req->row + 1U)) / ctx->grid_rows;
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }

    VkRect2D sc;
    sc.offset.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
    sc.offset.y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", y0);
    sc.extent.width = x1 - x0;
    sc.extent.height = y1 - y0;
    vkCmdSetScissor(ctx->cmd, 0, 1, &sc);

    float ndc_x0 = 0.0F;
    float ndc_y0 = 0.0F;
    float ndc_x1 = 0.0F;
    float ndc_y1 = 0.0F;
    db_vk_grid_span_bounds_ndc(req->row, req->col_start, req->col_end,
                               ctx->grid_rows, ctx->grid_cols, &ndc_x0, &ndc_y0,
                               &ndc_x1, &ndc_y1);

    db_vk_grid_span_draw_cmd_t local_req = *req;
    local_req.dynamic.ndc_x0 = ndc_x0;
    local_req.dynamic.ndc_y0 = ndc_y0;
    local_req.dynamic.ndc_x1 = ndc_x1;
    local_req.dynamic.ndc_y1 = ndc_y1;
    db_vk_push_constants_draw_dynamic(ctx->cmd, ctx->layout,
                                      &local_req.dynamic);
    vkCmdDraw(ctx->cmd, DB_RECT_VERTEX_COUNT, 1, 0, 0);
}

static void
db_vk_draw_grid_row_block(const db_vk_grid_draw_ctx_t *ctx,
                          const db_vk_grid_row_block_draw_cmd_t *req) {
    if ((ctx == NULL) || (req == NULL) || (req->row_end <= req->row_start) ||
        (req->row_start >= ctx->grid_rows) || (ctx->grid_rows == 0U) ||
        (ctx->grid_cols == 0U)) {
        return;
    }
    uint32_t row_end = req->row_end;
    if (row_end > ctx->grid_rows) {
        row_end = ctx->grid_rows;
    }

    const uint32_t y0 = (ctx->extent.height * req->row_start) / ctx->grid_rows;
    const uint32_t y1 = (ctx->extent.height * row_end) / ctx->grid_rows;
    if (y1 <= y0) {
        return;
    }
    VkRect2D sc = {0};
    sc.offset.x = 0;
    sc.offset.y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", y0);
    sc.extent.width = ctx->extent.width;
    sc.extent.height = y1 - y0;
    vkCmdSetScissor(ctx->cmd, 0, 1, &sc);

    const float inv_rows = 1.0F / (float)ctx->grid_rows;
    const float ndc_y0 = (2.0F * (float)req->row_start * inv_rows) - 1.0F;
    const float ndc_y1 = (2.0F * (float)row_end * inv_rows) - 1.0F;
    db_vk_grid_row_block_draw_cmd_t local_req = *req;
    local_req.dynamic.ndc_x0 = -1.0F;
    local_req.dynamic.ndc_y0 = ndc_y0;
    local_req.dynamic.ndc_x1 = 1.0F;
    local_req.dynamic.ndc_y1 = ndc_y1;
    db_vk_push_constants_draw_dynamic(ctx->cmd, ctx->layout,
                                      &local_req.dynamic);
    vkCmdDraw(ctx->cmd, DB_RECT_VERTEX_COUNT, 1, 0, 0);
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

static void db_vk_draw_owner_grid_span(const db_vk_owner_draw_ctx_t *ctx,
                                       const db_vk_grid_span_draw_req_t *req) {
    if ((ctx == NULL) || (req == NULL) || (req->span_units == 0U) ||
        (ctx->grid_tiles_drawn == NULL) || (ctx->grid_tiles_per_gpu == NULL) ||
        (ctx->frame_work_units == NULL)) {
        return;
    }

    uint32_t owner = db_vk_select_owner_for_work(
        req->candidate_owner, ctx->active_gpu_count, req->span_units,
        ctx->frame_start_ns, ctx->budget_ns, ctx->safety_ns,
        ctx->ema_ms_per_work_unit);
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
        .layout = ctx->layout,
        .extent = ctx->extent,
        .grid_rows = ctx->grid_rows,
        .grid_cols = ctx->grid_cols,
    };
    const db_vk_grid_span_draw_cmd_t draw_req = {
        .row = req->row,
        .col_start = req->col_start,
        .col_end = req->col_end,
        .dynamic =
            {
                .ndc_x0 = 0.0F,
                .ndc_y0 = 0.0F,
                .ndc_x1 = 0.0F,
                .ndc_y1 = 0.0F,
                .color = req->color,
                .render_mode = req->render_mode,
                .gradient_head_row = req->gradient_head_row,
                .snake_rect_index = req->snake_rect_index,
                .mode_phase_flag = req->mode_phase_flag,
                .snake_cursor = req->snake_cursor,
                .snake_batch_size = req->snake_batch_size,
                .snake_phase_completed = req->snake_phase_completed,
                .palette_cycle = req->palette_cycle,
            },
    };
    db_vk_draw_grid_span(&draw_ctx, &draw_req);
    db_vk_owner_timing_end(ctx->cmd, ctx->timing_enabled,
                           ctx->timing_query_pool, owner,
                           ctx->frame_owner_finished);
    ctx->frame_work_units[owner] += req->span_units;
}

void db_vk_draw_owner_grid_row_block(
    const db_vk_owner_draw_ctx_t *ctx,
    const db_vk_grid_row_block_draw_req_t *req) {
    if ((ctx == NULL) || (req == NULL) || (req->span_units == 0U) ||
        (ctx->grid_tiles_drawn == NULL) || (ctx->grid_tiles_per_gpu == NULL) ||
        (ctx->frame_work_units == NULL)) {
        return;
    }

    uint32_t owner = db_vk_select_owner_for_work(
        req->candidate_owner, ctx->active_gpu_count, req->span_units,
        ctx->frame_start_ns, ctx->budget_ns, ctx->safety_ns,
        ctx->ema_ms_per_work_unit);
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
        .layout = ctx->layout,
        .extent = ctx->extent,
        .grid_rows = ctx->grid_rows,
        .grid_cols = ctx->grid_cols,
    };
    const db_vk_grid_row_block_draw_cmd_t draw_req = {
        .row_start = req->row_start,
        .row_end = req->row_end,
        .dynamic =
            {
                .ndc_x0 = 0.0F,
                .ndc_y0 = 0.0F,
                .ndc_x1 = 0.0F,
                .ndc_y1 = 0.0F,
                .color = req->color,
                .render_mode = req->render_mode,
                .gradient_head_row = req->gradient_head_row,
                .snake_rect_index = req->snake_rect_index,
                .mode_phase_flag = req->mode_phase_flag,
                .snake_cursor = req->snake_cursor,
                .snake_batch_size = req->snake_batch_size,
                .snake_phase_completed = req->snake_phase_completed,
                .palette_cycle = req->palette_cycle,
            },
    };
    db_vk_draw_grid_row_block(&draw_ctx, &draw_req);
    db_vk_owner_timing_end(ctx->cmd, ctx->timing_enabled,
                           ctx->timing_query_pool, owner,
                           ctx->frame_owner_finished);
    ctx->frame_work_units[owner] += req->span_units;
}

static void db_vk_draw_owner_grid_span_snake(
    const db_vk_owner_draw_ctx_t *ctx, uint32_t candidate_owner,
    uint32_t span_units, uint32_t row, uint32_t col_start, uint32_t col_end,
    const float color[3], uint32_t active_cursor, int clearing_phase,
    uint32_t batch_size, int phase_completed) {
    const db_vk_grid_span_draw_req_t req = {
        .candidate_owner = candidate_owner,
        .span_units = span_units,
        .row = row,
        .col_start = col_start,
        .col_end = col_end,
        .color = color,
        .render_mode = DB_PATTERN_SNAKE_GRID,
        .gradient_head_row = 0U,
        .snake_rect_index = 0U,
        .mode_phase_flag = clearing_phase,
        .snake_cursor = active_cursor,
        .snake_batch_size = batch_size,
        .snake_phase_completed = phase_completed,
        .palette_cycle = 0U,
    };
    db_vk_draw_owner_grid_span(ctx, &req);
}

static void db_vk_draw_owner_grid_span_rect(
    const db_vk_owner_draw_ctx_t *ctx, uint32_t candidate_owner,
    uint32_t span_units, uint32_t row, uint32_t col_start, uint32_t col_end,
    const float color[3], uint32_t rect_index, uint32_t active_cursor,
    uint32_t batch_size, int rect_completed) {
    const db_vk_grid_span_draw_req_t req = {
        .candidate_owner = candidate_owner,
        .span_units = span_units,
        .row = row,
        .col_start = col_start,
        .col_end = col_end,
        .color = color,
        .render_mode = DB_PATTERN_RECT_SNAKE,
        .gradient_head_row = 0U,
        .snake_rect_index = rect_index,
        .mode_phase_flag = 0,
        .snake_cursor = active_cursor,
        .snake_batch_size = batch_size,
        .snake_phase_completed = rect_completed,
        .palette_cycle = 0U,
    };
    db_vk_draw_owner_grid_span(ctx, &req);
}

static void db_vk_draw_owner_grid_row_block_rect(
    const db_vk_owner_draw_ctx_t *ctx, uint32_t candidate_owner,
    uint32_t span_units, uint32_t row_start, uint32_t row_end,
    const float color[3], uint32_t rect_index, uint32_t active_cursor,
    uint32_t batch_size, int rect_completed) {
    const db_vk_grid_row_block_draw_req_t req = {
        .candidate_owner = candidate_owner,
        .span_units = span_units,
        .row_start = row_start,
        .row_end = row_end,
        .color = color,
        .render_mode = DB_PATTERN_RECT_SNAKE,
        .gradient_head_row = 0U,
        .snake_rect_index = rect_index,
        .mode_phase_flag = 0,
        .snake_cursor = active_cursor,
        .snake_batch_size = batch_size,
        .snake_phase_completed = rect_completed,
        .palette_cycle = 0U,
    };
    db_vk_draw_owner_grid_row_block(ctx, &req);
}

void db_vk_draw_snake_grid_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                uint32_t work_unit_count,
                                const float color[3]) {
    const uint32_t batch_size = plan->batch_size;
    const uint32_t active_cursor = plan->active_cursor;
    const uint32_t full_rows = active_cursor / ctx->grid_cols;
    const uint32_t row_remainder = active_cursor % ctx->grid_cols;
    const uint32_t row_span_units = ctx->grid_cols;

    for (uint32_t row = 0; row < full_rows; row++) {
        db_vk_draw_owner_grid_span_snake(
            ctx, row % ctx->active_gpu_count, row_span_units, row, 0U,
            ctx->grid_cols, color, active_cursor, plan->clearing_phase,
            batch_size, plan->phase_completed);
    }

    if ((row_remainder > 0U) && (full_rows < ctx->grid_rows)) {
        const uint32_t span_units = row_remainder;
        uint32_t col_start = 0U;
        uint32_t col_end = row_remainder;
        if ((full_rows & 1U) != 0U) {
            col_start = ctx->grid_cols - row_remainder;
            col_end = ctx->grid_cols;
        }
        db_vk_draw_owner_grid_span_snake(
            ctx, full_rows % ctx->active_gpu_count, span_units, full_rows,
            col_start, col_end, color, active_cursor, plan->clearing_phase,
            batch_size, plan->phase_completed);
    }

    for (uint32_t update_index = 0; update_index < batch_size; update_index++) {
        const uint32_t tile_step = active_cursor + update_index;
        if (tile_step >= work_unit_count) {
            break;
        }
        const uint32_t tile_index = db_grid_tile_index_from_step(tile_step);
        const uint32_t row = tile_index / ctx->grid_cols;
        const uint32_t col = tile_index % ctx->grid_cols;
        db_vk_draw_owner_grid_span_snake(ctx, tile_step % ctx->active_gpu_count,
                                         1U, row, col, col + 1U, color,
                                         active_cursor, plan->clearing_phase,
                                         batch_size, plan->phase_completed);
    }
}

void db_vk_draw_rect_snake_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                uint32_t pattern_seed,
                                uint32_t snake_prev_start,
                                uint32_t snake_prev_count,
                                int snake_reset_pending, const float color[3]) {
    const db_rect_snake_rect_t rect =
        db_rect_snake_rect_from_index(pattern_seed, plan->active_rect_index);
    if (snake_reset_pending != 0) {
        const uint32_t span_units = ctx->grid_rows * ctx->grid_cols;
        db_vk_draw_owner_grid_row_block_rect(
            ctx, 0U, span_units, 0U, ctx->grid_rows, color,
            plan->active_rect_index, plan->active_cursor, plan->batch_size,
            plan->rect_completed);
        return;
    }

    const size_t max_spans =
        (size_t)snake_prev_count + (size_t)plan->batch_size;
    if (max_spans == 0U) {
        return;
    }
    if (max_spans > g_state.snake_span_capacity) {
        failf("Vulkan snake scratch overflow (required=%zu capacity=%zu)",
              max_spans, g_state.snake_span_capacity);
    }
    db_snake_col_span_t *spans = g_state.snake_spans;
    size_t span_count = 0U;
    db_snake_append_step_spans_for_rect(spans, max_spans, &span_count, rect.x,
                                        rect.y, rect.width, rect.height,
                                        snake_prev_start, snake_prev_count);
    db_snake_append_step_spans_for_rect(spans, max_spans, &span_count, rect.x,
                                        rect.y, rect.width, rect.height,
                                        plan->active_cursor, plan->batch_size);
    for (size_t i = 0U; i < span_count; i++) {
        const uint32_t span_units = spans[i].col_end - spans[i].col_start;
        db_vk_draw_owner_grid_span_rect(
            ctx, spans[i].row % ctx->active_gpu_count, span_units, spans[i].row,
            spans[i].col_start, spans[i].col_end, color,
            plan->active_rect_index, plan->active_cursor, plan->batch_size,
            plan->rect_completed);
    }
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
    vkDestroyPipeline(ctx->device, ctx->pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);
    if (ctx->history_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->device, ctx->history_sampler, NULL);
    }
    if (ctx->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, NULL);
    }
    if (ctx->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout,
                                     NULL);
    }
    db_vk_destroy_swapchain_state(ctx->device, ctx->swapchain_state);
    db_vk_destroy_history_target(ctx->device, &ctx->history_targets[0]);
    db_vk_destroy_history_target(ctx->device, &ctx->history_targets[1]);
    vkDestroyRenderPass(ctx->device, ctx->history_render_pass, NULL);
    vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
    vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
    if (ctx->timing_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(ctx->device, ctx->timing_query_pool, NULL);
    }
    vkDestroyDevice(ctx->device, NULL);
    vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    vkDestroyInstance(ctx->instance, NULL);
}

// NOLINTEND(misc-include-cleaner)
