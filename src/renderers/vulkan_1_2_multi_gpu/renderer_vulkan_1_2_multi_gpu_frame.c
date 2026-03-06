#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define COLOR_CHANNEL_ALPHA 3U
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define MASK_GPU0 1U
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define WAIT_TIMEOUT_NS 100000000ULL
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    uint32_t span_units;
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
    db_vk_draw_payload_t payload;
} db_vk_grid_span_draw_req_t;

typedef struct {
    VkCommandBuffer cmd;
    VkPipelineLayout layout;
    VkExtent2D extent;
    uint32_t grid_rows;
    uint32_t grid_cols;
    db_vk_draw_payload_cache_t *payload_cache;
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
    uint32_t col_start;
    uint32_t col_end;
    db_vk_draw_dynamic_req_t dynamic;
} db_vk_grid_row_block_draw_cmd_t;

#define DB_VK_SNAKE_ROW_BLOCK_MIN_ROWS 2U

static const VkShaderStageFlags db_pc_stages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

void db_vk_push_constants_frame_static(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       VkExtent2D extent, uint32_t grid_rows,
                                       uint32_t grid_cols) {
    float base_color[4] = {BENCH_GRID_PHASE0_R_F, BENCH_GRID_PHASE0_G_F,
                           BENCH_GRID_PHASE0_B_F, BENCH_CLEAR_COLOR_A_F};
    float target_color[4] = {BENCH_GRID_PHASE1_R_F, BENCH_GRID_PHASE1_G_F,
                             BENCH_GRID_PHASE1_B_F, BENCH_CLEAR_COLOR_A_F};

    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, gradient_window_rows),
                       sizeof(g_state.gradient_window_rows),
                       &g_state.gradient_window_rows);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, viewport_height),
                       sizeof(extent.height), &extent.height);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, grid_cols),
                       sizeof(grid_cols), &grid_cols);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, base_color),
                       sizeof(base_color), base_color);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, target_color),
                       sizeof(target_color), target_color);

    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, grid_rows),
                       sizeof(grid_rows), &grid_rows);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, viewport_width),
                       sizeof(extent.width), &extent.width);
}

static void db_vk_payload_color_values(const db_vk_draw_payload_t *payload,
                                       float out_color[3]) {
    if ((payload == NULL) || (payload->color == NULL)) {
        out_color[0] = 0.0F;
        out_color[1] = 0.0F;
        out_color[2] = 0.0F;
        return;
    }
    out_color[0] = payload->color[0];
    out_color[1] = payload->color[1];
    out_color[2] = payload->color[2];
}

static int
db_vk_draw_payload_cache_matches(const db_vk_draw_payload_cache_t *cache,
                                 const db_vk_draw_payload_t *payload) {
    if ((cache == NULL) || (payload == NULL) || (cache->valid == 0)) {
        return 0;
    }
    float color[3] = {0.0F, 0.0F, 0.0F};
    db_vk_payload_color_values(payload, color);
    return (cache->payload.render_mode == payload->render_mode) &&
           (cache->payload.gradient_head_row == payload->gradient_head_row) &&
           (cache->payload.gradient_direction_flag ==
            payload->gradient_direction_flag) &&
           (cache->payload.snake_phase_flag == payload->snake_phase_flag) &&
           (cache->payload.snake_cursor == payload->snake_cursor) &&
           (cache->payload.snake_batch_size == payload->snake_batch_size) &&
           (cache->payload.snake_shape_index == payload->snake_shape_index) &&
           (cache->payload.snake_phase_completed ==
            payload->snake_phase_completed) &&
           (cache->payload.palette_cycle == payload->palette_cycle) &&
           (cache->payload.frame_index == payload->frame_index) &&
           (cache->payload.band_count == payload->band_count) &&
           (cache->color[0] == color[0]) && (cache->color[1] == color[1]) &&
           (cache->color[2] == color[2]);
}

static void
db_vk_draw_payload_cache_store(db_vk_draw_payload_cache_t *cache,
                               const db_vk_draw_payload_t *payload) {
    if ((cache == NULL) || (payload == NULL)) {
        return;
    }
    cache->payload = *payload;
    db_vk_payload_color_values(payload, cache->color);
    cache->valid = 1;
}

static void db_vk_push_constants_draw_geometry(VkCommandBuffer cmd,
                                               VkPipelineLayout layout,
                                               float ndc_x0, float ndc_y0,
                                               float ndc_x1, float ndc_y1) {
    const float offset_ndc[2] = {ndc_x0, ndc_y0};
    const float scale_ndc[2] = {(ndc_x1 - ndc_x0), (ndc_y1 - ndc_y0)};
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, offset_ndc),
                       sizeof(offset_ndc), offset_ndc);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, scale_ndc),
                       sizeof(scale_ndc), scale_ndc);
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
    float payload_color[3] = {0.0F, 0.0F, 0.0F};
    db_vk_payload_color_values(&req->payload, payload_color);
    pc.color[0] = payload_color[0];
    pc.color[1] = payload_color[1];
    pc.color[2] = payload_color[2];
    pc.color[COLOR_CHANNEL_ALPHA] = 1.0F;
    pc.render_mode = req->payload.render_mode;
    pc.gradient_head_row = req->payload.gradient_head_row;
    const uint32_t shape_index = req->payload.snake_shape_index;
    pc.gradient_direction_flag = (int32_t)req->payload.gradient_direction_flag;
    pc.snake_phase_flag = (int32_t)req->payload.snake_phase_flag;
    pc.snake_cursor = req->payload.snake_cursor;
    pc.snake_batch_size = req->payload.snake_batch_size;
    pc.snake_phase_completed = (int32_t)req->payload.snake_phase_completed;
    pc.palette_cycle = req->payload.palette_cycle;
    pc.frame_index = req->payload.frame_index;
    pc.band_count = req->payload.band_count;
    const db_history_pattern_mode_flags_t mode_flags =
        db_history_pattern_mode_flags((db_pattern_t)req->payload.render_mode);
    if (mode_flags.is_snake_region_mode != 0) {
        db_vk_shape_uniform_cache_t *const cache = &g_state.shape_uniform_cache;
        const uint32_t pattern_seed = g_state.runtime.pattern_seed;
        const uint32_t render_mode = req->payload.render_mode;
        const int is_shapes_mode =
            (((db_pattern_t)render_mode) == DB_PATTERN_SNAKE_SHAPES) ? 1 : 0;
        const int cache_hit = (cache->valid != 0) &&
                              (cache->render_mode == render_mode) &&
                              (cache->shape_index == shape_index) &&
                              (cache->pattern_seed == pattern_seed);
        if (cache_hit == 0) {
            const db_snake_shape_kind_t shape_kind =
                db_snake_shapes_kind_from_index(pattern_seed, shape_index,
                                                DB_U32_SALT_PALETTE);
            const db_snake_region_t region =
                db_snake_region_from_index(pattern_seed, shape_index);
            cache->render_mode = render_mode;
            cache->shape_index = shape_index;
            cache->pattern_seed = pattern_seed;
            cache->snake_region_height = region.height;
            cache->snake_region_width = region.width;
            cache->snake_region_x = region.x;
            cache->snake_region_y = region.y;
            db_rgb_f64_to_f32_triplet(
                region.color_r, region.color_g, region.color_b,
                &cache->snake_region_color[0], &cache->snake_region_color[1],
                &cache->snake_region_color[2]);
            cache->snake_region_color[3] = 1.0F;
            if (is_shapes_mode != 0) {
                const db_snake_shape_profile_t profile =
                    db_snake_shape_profile_from_index(pattern_seed, shape_index,
                                                      DB_U32_SALT_PALETTE,
                                                      shape_kind);
                db_snake_shape_profile_f32_t profile_f32 = {0};
                db_snake_shape_profile_to_f32(&profile, &profile_f32);
                cache->snake_shape_kind = (uint32_t)shape_kind;
                cache->snake_profile0[0] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_X];
                cache->snake_profile0[1] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_Y];
                cache->snake_profile0[2] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_DIAMOND_RADIUS];
                cache->snake_profile0[3] =
                    profile_f32
                        .values[DB_SNAKE_PROFILE_VAL_TRIANGLE_BOTTOM_WIDTH];
                cache->snake_profile1[0] =
                    profile_f32
                        .values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_TOP_WIDTH];
                cache->snake_profile1[1] =
                    profile_f32
                        .values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_BOTTOM_WIDTH];
                cache->snake_profile1[2] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_RECT_HALF_WIDTH];
                cache->snake_profile1[3] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_RECT_HALF_HEIGHT];
                cache->snake_profile2[0] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_EXTENT_X];
                cache->snake_profile2[1] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_EXTENT_Y];
                cache->snake_profile2[2] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_ROTATE_COS];
                cache->snake_profile2[3] =
                    profile_f32.values[DB_SNAKE_PROFILE_VAL_ROTATE_SIN];
                cache->snake_triangle_variant = profile_f32.triangle_variant;
            } else {
                cache->snake_shape_kind = 0U;
                for (size_t profile_index = 0U; profile_index < 4U;
                     profile_index++) {
                    cache->snake_profile0[profile_index] = 0.0F;
                    cache->snake_profile1[profile_index] = 0.0F;
                    cache->snake_profile2[profile_index] = 0.0F;
                }
                cache->snake_triangle_variant = 0U;
            }
            cache->valid = 1;
        }
        pc.snake_region_height = cache->snake_region_height;
        pc.snake_region_width = cache->snake_region_width;
        pc.snake_region_x = cache->snake_region_x;
        pc.snake_region_y = cache->snake_region_y;
        db_copy_bytes(pc.snake_region_color, cache->snake_region_color,
                      sizeof(pc.snake_region_color));
        if (is_shapes_mode != 0) {
            pc.snake_shape_kind = cache->snake_shape_kind;
            db_copy_bytes(pc.snake_profile0, cache->snake_profile0,
                          sizeof(pc.snake_profile0));
            db_copy_bytes(pc.snake_profile1, cache->snake_profile1,
                          sizeof(pc.snake_profile1));
            db_copy_bytes(pc.snake_profile2, cache->snake_profile2,
                          sizeof(pc.snake_profile2));
            pc.snake_triangle_variant = cache->snake_triangle_variant;
        }
    }
    vkCmdPushConstants(cmd, layout, db_pc_stages, 0U,
                       (uint32_t)offsetof(PushConstants, base_color), &pc);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, render_mode),
                       sizeof(pc.render_mode), &pc.render_mode);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, gradient_head_row),
                       sizeof(pc.gradient_head_row), &pc.gradient_head_row);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_region_height),
                       sizeof(pc.snake_region_height), &pc.snake_region_height);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_region_width),
                       sizeof(pc.snake_region_width), &pc.snake_region_width);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_region_x),
                       sizeof(pc.snake_region_x), &pc.snake_region_x);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_region_y),
                       sizeof(pc.snake_region_y), &pc.snake_region_y);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_region_color),
                       sizeof(pc.snake_region_color), pc.snake_region_color);
    if (mode_flags.is_snake_shapes != 0) {
        vkCmdPushConstants(cmd, layout, db_pc_stages,
                           (uint32_t)offsetof(PushConstants, snake_shape_kind),
                           sizeof(pc.snake_shape_kind), &pc.snake_shape_kind);
        vkCmdPushConstants(cmd, layout, db_pc_stages,
                           (uint32_t)offsetof(PushConstants, snake_profile0),
                           sizeof(pc.snake_profile0), pc.snake_profile0);
        vkCmdPushConstants(cmd, layout, db_pc_stages,
                           (uint32_t)offsetof(PushConstants, snake_profile1),
                           sizeof(pc.snake_profile1), pc.snake_profile1);
        vkCmdPushConstants(cmd, layout, db_pc_stages,
                           (uint32_t)offsetof(PushConstants, snake_profile2),
                           sizeof(pc.snake_profile2), pc.snake_profile2);
        vkCmdPushConstants(
            cmd, layout, db_pc_stages,
            (uint32_t)offsetof(PushConstants, snake_triangle_variant),
            sizeof(pc.snake_triangle_variant), &pc.snake_triangle_variant);
    }
    vkCmdPushConstants(
        cmd, layout, db_pc_stages,
        (uint32_t)offsetof(PushConstants, gradient_direction_flag),
        sizeof(pc.gradient_direction_flag), &pc.gradient_direction_flag);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_phase_flag),
                       sizeof(pc.snake_phase_flag), &pc.snake_phase_flag);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_cursor),
                       sizeof(pc.snake_cursor), &pc.snake_cursor);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_batch_size),
                       sizeof(pc.snake_batch_size), &pc.snake_batch_size);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, snake_phase_completed),
                       sizeof(pc.snake_phase_completed),
                       &pc.snake_phase_completed);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, palette_cycle),
                       sizeof(pc.palette_cycle), &pc.palette_cycle);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, frame_index),
                       sizeof(pc.frame_index), &pc.frame_index);
    vkCmdPushConstants(cmd, layout, db_pc_stages,
                       (uint32_t)offsetof(PushConstants, band_count),
                       sizeof(pc.band_count), &pc.band_count);
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
    HistoryTargetState *targets, int *history_pair_read_index) {
    if ((targets == NULL) || (history_pair_read_index == NULL)) {
        return 0;
    }

    DB_VK_CHECK(BACKEND_NAME, vkDeviceWaitIdle(device));
    HistoryTargetState old_targets[2] = {targets[0], targets[1]};
    const int old_read = *history_pair_read_index;
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
    *history_pair_read_index = 0;

    db_vk_destroy_history_target(device, &old_targets[0]);
    db_vk_destroy_history_target(device, &old_targets[1]);
    return copied;
}

static void db_vk_pixel_bounds_ndc(uint32_t x0_px, uint32_t y0_px,
                                   uint32_t x1_px, uint32_t y1_px,
                                   VkExtent2D extent, float *x0, float *y0,
                                   float *x1, float *y1) {
    const double inv_w = 1.0 / (double)db_u32_max(extent.width, 1U);
    const double inv_h = 1.0 / (double)db_u32_max(extent.height, 1U);
    *x0 = db_double_to_f32((2.0 * (double)x0_px * inv_w) - 1.0);
    *x1 = db_double_to_f32((2.0 * (double)x1_px * inv_w) - 1.0);
    *y0 = db_double_to_f32((2.0 * (double)y0_px * inv_h) - 1.0);
    *y1 = db_double_to_f32((2.0 * (double)y1_px * inv_h) - 1.0);
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
    db_vk_pixel_bounds_ndc(x0, y0, x1, y1, ctx->extent, &ndc_x0, &ndc_y0,
                           &ndc_x1, &ndc_y1);

    db_vk_grid_span_draw_cmd_t local_req = *req;
    local_req.dynamic.ndc_x0 = ndc_x0;
    local_req.dynamic.ndc_y0 = ndc_y0;
    local_req.dynamic.ndc_x1 = ndc_x1;
    local_req.dynamic.ndc_y1 = ndc_y1;
    if ((ctx->payload_cache == NULL) ||
        (db_vk_draw_payload_cache_matches(ctx->payload_cache,
                                          &local_req.dynamic.payload) == 0)) {
        db_vk_push_constants_draw_dynamic(ctx->cmd, ctx->layout,
                                          &local_req.dynamic);
        db_vk_draw_payload_cache_store(ctx->payload_cache,
                                       &local_req.dynamic.payload);
    } else {
        db_vk_push_constants_draw_geometry(
            ctx->cmd, ctx->layout, local_req.dynamic.ndc_x0,
            local_req.dynamic.ndc_y0, local_req.dynamic.ndc_x1,
            local_req.dynamic.ndc_y1);
    }
    vkCmdDraw(ctx->cmd, DB_RECT_VERTEX_COUNT, 1, 0, 0);
}

static void
db_vk_draw_grid_row_block(const db_vk_grid_draw_ctx_t *ctx,
                          const db_vk_grid_row_block_draw_cmd_t *req) {
    if ((ctx == NULL) || (req == NULL) || (req->row_end <= req->row_start) ||
        (req->row_start >= ctx->grid_rows) || (ctx->grid_rows == 0U) ||
        (ctx->grid_cols == 0U) || (req->col_end <= req->col_start) ||
        (req->col_start >= ctx->grid_cols)) {
        return;
    }
    uint32_t row_end = req->row_end;
    if (row_end > ctx->grid_rows) {
        row_end = ctx->grid_rows;
    }
    uint32_t col_end = req->col_end;
    if (col_end > ctx->grid_cols) {
        col_end = ctx->grid_cols;
    }

    const uint32_t x0 = (ctx->extent.width * req->col_start) / ctx->grid_cols;
    const uint32_t x1 = (ctx->extent.width * col_end) / ctx->grid_cols;
    const uint32_t y0 = (ctx->extent.height * req->row_start) / ctx->grid_rows;
    const uint32_t y1 = (ctx->extent.height * row_end) / ctx->grid_rows;
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }
    VkRect2D sc = {0};
    sc.offset.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
    sc.offset.y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", y0);
    sc.extent.width = x1 - x0;
    sc.extent.height = y1 - y0;
    vkCmdSetScissor(ctx->cmd, 0, 1, &sc);

    float ndc_x0 = 0.0F;
    float ndc_y0 = 0.0F;
    float ndc_x1 = 0.0F;
    float ndc_y1 = 0.0F;
    db_vk_pixel_bounds_ndc(x0, y0, x1, y1, ctx->extent, &ndc_x0, &ndc_y0,
                           &ndc_x1, &ndc_y1);
    db_vk_grid_row_block_draw_cmd_t local_req = *req;
    local_req.dynamic.ndc_x0 = ndc_x0;
    local_req.dynamic.ndc_y0 = ndc_y0;
    local_req.dynamic.ndc_x1 = ndc_x1;
    local_req.dynamic.ndc_y1 = ndc_y1;
    if ((ctx->payload_cache == NULL) ||
        (db_vk_draw_payload_cache_matches(ctx->payload_cache,
                                          &local_req.dynamic.payload) == 0)) {
        db_vk_push_constants_draw_dynamic(ctx->cmd, ctx->layout,
                                          &local_req.dynamic);
        db_vk_draw_payload_cache_store(ctx->payload_cache,
                                       &local_req.dynamic.payload);
    } else {
        db_vk_push_constants_draw_geometry(
            ctx->cmd, ctx->layout, local_req.dynamic.ndc_x0,
            local_req.dynamic.ndc_y0, local_req.dynamic.ndc_x1,
            local_req.dynamic.ndc_y1);
    }
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

    uint32_t owner = 0U;
    if (ctx->active_gpu_count <= 1U) {
        owner = 0U;
    } else {
        // Owner selection is fully scheduler-driven (no local hint/probe path).
        owner = db_vk_select_owner_for_work(
            ctx->active_gpu_count, req->span_units, ctx->budget_ns,
            ctx->safety_ns, ctx->ema_ms_per_work_unit, ctx->frame_work_units);
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
        .layout = ctx->layout,
        .extent = ctx->extent,
        .grid_rows = ctx->grid_rows,
        .grid_cols = ctx->grid_cols,
        .payload_cache = ctx->payload_cache,
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
                .payload = req->payload,
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

    uint32_t owner = 0U;
    if (ctx->active_gpu_count <= 1U) {
        owner = 0U;
    } else {
        // Owner selection is fully scheduler-driven (no local hint/probe path).
        owner = db_vk_select_owner_for_work(
            ctx->active_gpu_count, req->span_units, ctx->budget_ns,
            ctx->safety_ns, ctx->ema_ms_per_work_unit, ctx->frame_work_units);
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
        .layout = ctx->layout,
        .extent = ctx->extent,
        .grid_rows = ctx->grid_rows,
        .grid_cols = ctx->grid_cols,
        .payload_cache = ctx->payload_cache,
    };
    const db_vk_grid_row_block_draw_cmd_t draw_req = {
        .row_start = req->row_start,
        .row_end = req->row_end,
        .col_start = req->col_start,
        .col_end = req->col_end,
        .dynamic =
            {
                .ndc_x0 = 0.0F,
                .ndc_y0 = 0.0F,
                .ndc_x1 = 0.0F,
                .ndc_y1 = 0.0F,
                .payload = req->payload,
            },
    };
    db_vk_draw_grid_row_block(&draw_ctx, &draw_req);
    db_vk_owner_timing_end(ctx->cmd, ctx->timing_enabled,
                           ctx->timing_query_pool, owner,
                           ctx->frame_owner_finished);
    ctx->frame_work_units[owner] += req->span_units;
}

static void db_vk_draw_snake_spans_coalesced(
    const db_vk_owner_draw_ctx_t *ctx, const db_snake_col_span_t *spans,
    size_t span_count, const float color[3], uint32_t render_mode,
    uint32_t snake_shape_index, uint32_t active_cursor, int snake_phase_flag,
    uint32_t batch_size, int phase_completed) {
    if ((ctx == NULL) || (spans == NULL) || (span_count == 0U) ||
        (ctx->grid_cols == 0U) || (ctx->grid_rows == 0U)) {
        return;
    }
    const db_vk_draw_payload_t payload_base = {
        .color = color,
        .render_mode = render_mode,
        .gradient_head_row = 0U,
        .gradient_direction_flag = 0,
        .snake_phase_flag = snake_phase_flag,
        .snake_cursor = active_cursor,
        .snake_batch_size = batch_size,
        .snake_shape_index = snake_shape_index,
        .snake_phase_completed = phase_completed,
        .palette_cycle = 0U,
        .frame_index = 0U,
        .band_count = 0U,
    };

    size_t span_cursor = 0U;
    while (span_cursor < span_count) {
        const uint32_t row = spans[span_cursor].row;
        const size_t row_start = span_cursor;
        size_t row_end = row_start + 1U;
        while ((row_end < span_count) && (spans[row_end].row == row)) {
            row_end++;
        }

        if ((row_end - row_start) == 1U) {
            const db_snake_col_span_t base = spans[row_start];
            if (base.col_end > base.col_start) {
                size_t run_end = row_end;
                uint32_t run_rows = 1U;
                while (run_end < span_count) {
                    const uint32_t next_row = base.row + run_rows;
                    const size_t next_row_start = run_end;
                    if (spans[next_row_start].row != next_row) {
                        break;
                    }
                    size_t next_row_end = next_row_start + 1U;
                    while ((next_row_end < span_count) &&
                           (spans[next_row_end].row == next_row)) {
                        next_row_end++;
                    }
                    if ((next_row_end - next_row_start) != 1U) {
                        break;
                    }
                    const db_snake_col_span_t next = spans[next_row_start];
                    if ((next.col_start != base.col_start) ||
                        (next.col_end != base.col_end)) {
                        break;
                    }
                    run_rows++;
                    run_end = next_row_end;
                }

                if (run_rows >= DB_VK_SNAKE_ROW_BLOCK_MIN_ROWS) {
                    const uint32_t row_units = base.col_end - base.col_start;
                    const uint64_t span_units_u64 =
                        (uint64_t)run_rows * (uint64_t)row_units;
                    const uint32_t span_units = db_checked_u64_to_u32(
                        BACKEND_NAME, "snake_row_block_units", span_units_u64);
                    const db_vk_grid_row_block_draw_req_t req = {
                        .span_units = span_units,
                        .row_start = base.row,
                        .row_end = base.row + run_rows,
                        .col_start = base.col_start,
                        .col_end = base.col_end,
                        .payload = payload_base,
                    };
                    db_vk_draw_owner_grid_row_block(ctx, &req);
                    span_cursor = run_end;
                    continue;
                }
            }
        }

        for (size_t span_index = row_start; span_index < row_end;
             span_index++) {
            const db_snake_col_span_t span = spans[span_index];
            const uint32_t span_units = span.col_end - span.col_start;
            if (span_units == 0U) {
                continue;
            }
            const db_vk_grid_span_draw_req_t req = {
                .span_units = span_units,
                .row = span.row,
                .col_start = span.col_start,
                .col_end = span.col_end,
                .payload = payload_base,
            };
            db_vk_draw_owner_grid_span(ctx, &req);
        }
        span_cursor = row_end;
    }
}

void db_vk_draw_snake_grid_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                const float color[3]) {
    if ((ctx == NULL) || (plan == NULL) || (color == NULL)) {
        return;
    }
    const db_snake_region_t grid_region = {
        .x = 0U,
        .y = 0U,
        .width = ctx->grid_cols,
        .height = ctx->grid_rows,
        .color_r = 0.0,
        .color_g = 0.0,
        .color_b = 0.0,
    };
    const size_t max_spans = db_snake_plan_span_capacity_needed(plan);
    if (max_spans == 0U) {
        return;
    }
    if (max_spans > g_state.snake_scratch.span_capacity) {
        failf("Vulkan snake grid scratch overflow (required=%zu capacity=%zu)",
              max_spans, g_state.snake_scratch.span_capacity);
    }
    db_snake_col_span_t *spans = g_state.snake_scratch.spans;
    const size_t span_count = db_snake_collect_damage_spans_for_plan(
        spans, max_spans, &grid_region, plan, NULL);
    db_vk_draw_snake_spans_coalesced(ctx, spans, span_count, color,
                                     DB_PATTERN_SNAKE_GRID, 0U,
                                     plan->active_cursor, plan->phase_flag,
                                     plan->batch_size, plan->phase_completed);
}

void db_vk_draw_snake_region_plan(const db_vk_owner_draw_ctx_t *ctx,
                                  const db_snake_plan_t *plan,
                                  uint32_t pattern_seed,
                                  uint32_t snake_prev_start,
                                  uint32_t snake_prev_count,
                                  const float color[3]) {
    const uint32_t render_mode = (uint32_t)g_state.runtime.pattern;
    const db_snake_region_t region =
        db_snake_region_from_index(pattern_seed, plan->active_shape_index);

    const size_t max_spans =
        db_snake_span_capacity_needed(snake_prev_count, plan->batch_size);
    if (max_spans == 0U) {
        return;
    }
    if (max_spans > g_state.snake_scratch.span_capacity) {
        failf("Vulkan snake scratch overflow (required=%zu capacity=%zu)",
              max_spans, g_state.snake_scratch.span_capacity);
    }
    db_snake_col_span_t *spans = g_state.snake_scratch.spans;
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (render_mode == DB_PATTERN_SNAKE_SHAPES) {
        if (g_state.snake_scratch.row_bounds != NULL) {
            const db_snake_shape_kind_t shape_kind =
                db_snake_shapes_kind_from_index(pattern_seed,
                                                plan->active_shape_index,
                                                DB_U32_SALT_PALETTE);
            if (db_snake_shape_cache_init_from_index(
                    &shape_cache, g_state.snake_scratch.row_bounds,
                    g_state.snake_scratch.row_bounds_capacity, pattern_seed,
                    plan->active_shape_index, DB_U32_SALT_PALETTE, &region,
                    shape_kind) != 0) {
                shape_cache_ptr = &shape_cache;
            }
        }
    }
    size_t span_count = db_snake_collect_damage_spans(
        spans, max_spans, &region, snake_prev_start, snake_prev_count,
        plan->active_cursor, plan->batch_size, shape_cache_ptr);
    if ((span_count == 0U) && (shape_cache_ptr != NULL) &&
        ((snake_prev_count > 0U) || (plan->batch_size > 0U))) {
        // Fallback: if shape-cache filtering degenerates to empty coverage,
        // fall back to region spans so frames do not stall/blank.
        span_count = db_snake_collect_damage_spans(
            spans, max_spans, &region, snake_prev_start, snake_prev_count,
            plan->active_cursor, plan->batch_size, NULL);
    }
    db_vk_draw_snake_spans_coalesced(
        ctx, spans, span_count, color, render_mode, plan->active_shape_index,
        plan->active_cursor, 0, plan->batch_size, plan->phase_completed);
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
    if (ctx->hash_readback_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(ctx->device, ctx->hash_readback_buffer, NULL);
    }
    if (ctx->hash_readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, ctx->hash_readback_memory, NULL);
    }
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
