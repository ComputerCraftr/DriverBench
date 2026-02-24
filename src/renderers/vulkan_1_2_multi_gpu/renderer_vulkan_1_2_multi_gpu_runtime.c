#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define COLOR_CHANNEL_ALPHA 3U
#define DB_CAP_MODE_VULKAN_SINGLE_GPU "vulkan_single_gpu"
#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define FRAME_BUDGET_NS 16666666ULL
#define FRAME_SAFETY_NS 2000000ULL
#define MASK_GPU0 1U
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define WAIT_TIMEOUT_NS 100000000ULL
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

db_vk_frame_result_t db_vk_render_frame_impl(void) {
    if (!g_state.initialized) {
        return DB_VK_FRAME_STOP;
    }

    const uint64_t budget_ns = FRAME_BUDGET_NS;
    const uint64_t safety_ns = FRAME_SAFETY_NS;
    const uint32_t gpuCount = g_state.gpu_count;
    const uint32_t active_gpu_count = (gpuCount > 0U) ? gpuCount : 1U;
    const int haveGroup = g_state.have_group;

    VkResult wait_result = vkWaitForFences(
        g_state.device, 1, &g_state.in_flight, VK_TRUE, WAIT_TIMEOUT_NS);
    if (wait_result == VK_TIMEOUT) {
        return DB_VK_FRAME_RETRY;
    }
    if (wait_result != VK_SUCCESS) {
        db_vk_fail(BACKEND_NAME, "vkWaitForFences", wait_result, __FILE__,
                   __LINE__);
    }

    if (g_state.gpu_timing_enabled && g_state.have_prev_timing_frame) {
        uint64_t query_results[TIMESTAMP_QUERY_COUNT] = {0};
        VkResult query_result = vkGetQueryPoolResults(
            g_state.device, g_state.timing_query_pool, 0,
            gpuCount * TIMESTAMP_QUERIES_PER_GPU,
            sizeof(uint64_t) * gpuCount * TIMESTAMP_QUERIES_PER_GPU,
            query_results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (query_result == VK_SUCCESS) {
            for (uint32_t g = 0; g < gpuCount; g++) {
                if ((g_state.prev_frame_owner_used[g] == 0U) ||
                    (g_state.prev_frame_work_units[g] == 0U)) {
                    continue;
                }
                const size_t base_query =
                    (size_t)g * (size_t)TIMESTAMP_QUERIES_PER_GPU;
                const uint64_t start = query_results[base_query];
                const uint64_t end = query_results[base_query + 1U];
                if (end <= start) {
                    continue;
                }
                const double elapsed_ms =
                    ((double)(end - start) * g_state.timestamp_period_ns) /
                    DB_NS_PER_MS_D;
                const double ms_per_unit =
                    elapsed_ms / (double)g_state.prev_frame_work_units[g];
                g_state.ema_ms_per_work_unit[g] =
                    (EMA_KEEP * g_state.ema_ms_per_work_unit[g]) +
                    (EMA_NEW * ms_per_unit);
            }
        }
    }

    DB_VK_CHECK(BACKEND_NAME,
                vkResetFences(g_state.device, 1, &g_state.in_flight));

    uint32_t imgIndex = 0;
    VkResult ar = vkAcquireNextImageKHR(
        g_state.device, g_state.swapchain_state.swapchain, WAIT_TIMEOUT_NS,
        g_state.image_available, VK_NULL_HANDLE, &imgIndex);
    if (ar == VK_TIMEOUT) {
        return DB_VK_FRAME_RETRY;
    }
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        const VkExtent2D old_extent = g_state.swapchain_state.extent;
        db_vk_recreate_swapchain_state(
            &g_state.wsi_config, g_state.present_phys, g_state.device,
            g_state.surface, g_state.surface_format, g_state.present_mode,
            g_state.render_pass, &g_state.swapchain_state);
        const int preserved = db_vk_recreate_history_targets_preserve(
            g_state.present_phys, g_state.device, g_state.surface_format.format,
            g_state.swapchain_state.extent, g_state.history_render_pass,
            g_state.device_group_mask, g_state.command_pool, g_state.queue,
            old_extent, g_state.history_targets, &g_state.history_read_index);
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[g_state.history_read_index].view);
        g_state.history_descriptor_index = g_state.history_read_index;
        if (((g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
             (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) &&
            (preserved == 0)) {
            g_state.snake_reset_pending = 1;
        }
        g_state.frame_index++;
        return DB_VK_FRAME_RETRY;
    }
    if ((ar != VK_SUCCESS) && (ar != VK_SUBOPTIMAL_KHR)) {
        infof("AcquireNextImage returned %s (%d), ending benchmark loop",
              db_vk_result_name(ar), (int)ar);
        return DB_VK_FRAME_STOP;
    }
    const int acquire_suboptimal = (ar == VK_SUBOPTIMAL_KHR);
    const int history_mode =
        db_pattern_uses_history_texture(g_state.runtime.pattern);
    const int read_index = g_state.history_read_index;
    const int write_index = (read_index == 0) ? 1 : 0;
    if (history_mode && (g_state.history_descriptor_index != read_index) &&
        ((read_index == 0) || (read_index == 1))) {
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[read_index].view);
        g_state.history_descriptor_index = read_index;
    }

    DB_VK_CHECK(BACKEND_NAME, vkResetCommandBuffer(g_state.command_buffer, 0));
    VkCommandBufferBeginInfo cbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    DB_VK_CHECK(BACKEND_NAME,
                vkBeginCommandBuffer(g_state.command_buffer, &cbi));
    uint32_t frame_work_units[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_used[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_finished[MAX_GPU_COUNT] = {0};
    if (g_state.gpu_timing_enabled) {
        vkCmdResetQueryPool(g_state.command_buffer, g_state.timing_query_pool,
                            0, gpuCount * TIMESTAMP_QUERIES_PER_GPU);
    }

    VkClearValue clear = {0};
    const int use_base_color =
        ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL) ||
         (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID)) &&
        (g_state.runtime.mode_phase_flag == 0);
    const float *clear_rgb = NULL;
    if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        static const float bands_rgb[3] = {
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
        clear_rgb = bands_rgb;
    } else if (use_base_color) {
        static const float base_rgb[3] = {
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
        clear_rgb = base_rgb;
    } else {
        static const float target_rgb[3] = {
            BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B};
        clear_rgb = target_rgb;
    }
    clear.color.float32[0] = clear_rgb[0];
    clear.color.float32[1] = clear_rgb[1];
    clear.color.float32[2] = clear_rgb[2];
    clear.color.float32[COLOR_CHANNEL_ALPHA] = 1.0F;

    if (history_mode &&
        ((g_state.history_targets[0].layout_initialized == 0) ||
         (g_state.history_targets[1].layout_initialized == 0))) {
        VkImageMemoryBarrier history_to_clear[2] = {{0}, {0}};
        for (size_t i = 0U; i < 2U; i++) {
            history_to_clear[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_clear[i].srcAccessMask = 0;
            history_to_clear[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_clear[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            history_to_clear[i].newLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_clear[i].image = g_state.history_targets[i].image;
            history_to_clear[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_clear[i].subresourceRange.levelCount = 1U;
            history_to_clear[i].subresourceRange.layerCount = 1U;
        }
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 2U, history_to_clear);

        VkClearColorValue history_clear = {
            .float32 = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                        BENCH_GRID_PHASE0_B, 1.0F}};
        VkImageSubresourceRange history_range = {0};
        history_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        history_range.levelCount = 1U;
        history_range.layerCount = 1U;
        for (size_t i = 0U; i < 2U; i++) {
            vkCmdClearColorImage(g_state.command_buffer,
                                 g_state.history_targets[i].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &history_clear, 1U, &history_range);
        }

        VkImageMemoryBarrier history_to_read[2] = {{0}, {0}};
        for (size_t i = 0U; i < 2U; i++) {
            history_to_read[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_read[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_read[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            history_to_read[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_read[i].newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            history_to_read[i].image = g_state.history_targets[i].image;
            history_to_read[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_read[i].subresourceRange.levelCount = 1U;
            history_to_read[i].subresourceRange.layerCount = 1U;
            g_state.history_targets[i].layout_initialized = 1;
        }
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 2U, history_to_read);
    }

    if (history_mode) {
        VkImageMemoryBarrier write_to_color = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_to_color.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        write_to_color.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        write_to_color.image = g_state.history_targets[write_index].image;
        write_to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        write_to_color.subresourceRange.levelCount = 1U;
        write_to_color.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1U, &write_to_color);
    }

    VkRenderPassBeginInfo rbi = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass =
        history_mode ? g_state.history_render_pass : g_state.render_pass;
    rbi.framebuffer = history_mode
                          ? g_state.history_targets[write_index].framebuffer
                          : g_state.swapchain_state.framebuffers[imgIndex];
    rbi.renderArea.extent = g_state.swapchain_state.extent;
    rbi.clearValueCount = history_mode ? 0U : 1U;
    rbi.pClearValues = history_mode ? NULL : &clear;
    vkCmdBeginRenderPass(g_state.command_buffer, &rbi,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(g_state.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      g_state.pipeline);
    vkCmdBindDescriptorSets(
        g_state.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        g_state.pipeline_layout, 0U, 1U, &g_state.descriptor_set, 0U, NULL);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(g_state.command_buffer, 0, 1, &g_state.vertex_buffer,
                           &off);

    double time_s = (double)g_state.frame_index / BENCH_TARGET_FPS_D;
    uint64_t frameStart = db_now_ns_monotonic();
    uint32_t grid_tiles_per_gpu[MAX_GPU_COUNT] = {0};
    uint32_t grid_tiles_drawn = 0U;
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint32_t grid_cols = db_grid_cols_effective();
    db_vk_push_constants_frame_static(
        g_state.command_buffer, g_state.pipeline_layout,
        g_state.swapchain_state.extent, grid_rows, grid_cols);
    VkViewport vpo = {0};
    vpo.width = (float)g_state.swapchain_state.extent.width;
    vpo.height = (float)g_state.swapchain_state.extent.height;
    vpo.maxDepth = 1.0F;
    vkCmdSetViewport(g_state.command_buffer, 0, 1, &vpo);

    if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        const float inv_extent_width =
            1.0F / (float)g_state.swapchain_state.extent.width;
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            uint32_t owner = g_state.work_owner[b];
            if (owner >= active_gpu_count) {
                owner = 0U;
            }
            owner = db_vk_select_owner_for_work(
                owner, active_gpu_count, 1U, frameStart, budget_ns, safety_ns,
                g_state.ema_ms_per_work_unit);
            if (owner == 0U) {
                g_state.work_owner[b] = 0U;
            }
            if (haveGroup) {
                vkCmdSetDeviceMask(g_state.command_buffer,
                                   (MASK_GPU0 << owner));
            }
            db_vk_owner_timing_begin(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_used);

            uint32_t x0 =
                (g_state.swapchain_state.extent.width * b) / BENCH_BANDS;
            uint32_t x1 =
                (g_state.swapchain_state.extent.width * (b + 1U)) / BENCH_BANDS;
            VkRect2D sc = {0};
            sc.offset.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
            sc.extent.width = x1 - x0;
            sc.extent.height = g_state.swapchain_state.extent.height;
            vkCmdSetScissor(g_state.command_buffer, 0, 1, &sc);

            float ndc_x0 = (2.0F * (float)x0 * inv_extent_width) - 1.0F;
            float ndc_x1 = (2.0F * (float)x1 * inv_extent_width) - 1.0F;
            float band_color[3] = {0.0F, 0.0F, 0.0F};
            db_band_color_rgb(b, BENCH_BANDS, time_s, &band_color[0],
                              &band_color[1], &band_color[2]);
            const db_vk_draw_dynamic_req_t draw_req = {
                .ndc_x0 = ndc_x0,
                .ndc_y0 = -1.0F,
                .ndc_x1 = ndc_x1,
                .ndc_y1 = 1.0F,
                .color = band_color,
                .render_mode = DB_PATTERN_BANDS,
                .gradient_head_row = 0U,
                .snake_shape_index = 0U,
                .mode_phase_flag = 0,
                .snake_cursor = 0U,
                .snake_batch_size = 0U,
                .snake_phase_completed = 0,
                .palette_cycle = 0U,
            };
            db_vk_push_constants_draw_dynamic(
                g_state.command_buffer, g_state.pipeline_layout, &draw_req);
            vkCmdDraw(g_state.command_buffer, DB_RECT_VERTEX_COUNT, 1, 0, 0);
            db_vk_owner_timing_end(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_finished);
            frame_work_units[owner] += 1U;
        }
    } else if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
               (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const int is_shapes =
            (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake_shape_index, g_state.runtime.snake_cursor,
            g_state.runtime.snake_prev_start, g_state.runtime.snake_prev_count,
            g_state.runtime.mode_phase_flag, g_state.runtime.bench_speed_step);
        const db_snake_plan_t plan = db_snake_plan_next_step(&request);
        const db_snake_step_target_t target = db_snake_step_target_from_plan(
            is_grid, g_state.runtime.pattern_seed, &plan);
        const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
        const db_vk_owner_draw_ctx_t draw_ctx = {
            .cmd = g_state.command_buffer,
            .layout = g_state.pipeline_layout,
            .extent = g_state.swapchain_state.extent,
            .have_group = haveGroup,
            .active_gpu_count = active_gpu_count,
            .frame_start_ns = frameStart,
            .budget_ns = budget_ns,
            .safety_ns = safety_ns,
            .ema_ms_per_work_unit = g_state.ema_ms_per_work_unit,
            .timing_enabled = g_state.gpu_timing_enabled,
            .timing_query_pool = g_state.timing_query_pool,
            .frame_owner_used = frame_owner_used,
            .frame_owner_finished = frame_owner_finished,
            .frame_work_units = frame_work_units,
            .grid_tiles_per_gpu = grid_tiles_per_gpu,
            .grid_tiles_drawn = &grid_tiles_drawn,
            .grid_rows = grid_rows,
            .grid_cols = grid_cols,
        };
        if (is_grid != 0) {
            db_vk_draw_snake_grid_plan(&draw_ctx, &plan,
                                       g_state.runtime.work_unit_count,
                                       shader_ignored_color);
            if (target.has_next_mode_phase_flag != 0) {
                g_state.runtime.mode_phase_flag = target.next_mode_phase_flag;
            }
        } else {
            const int had_reset_pending = g_state.snake_reset_pending;
            db_vk_draw_snake_region_plan(
                &draw_ctx, &plan, g_state.runtime.pattern_seed,
                g_state.runtime.snake_prev_start,
                g_state.runtime.snake_prev_count, g_state.snake_reset_pending,
                shader_ignored_color);
            if (had_reset_pending != 0) {
                g_state.snake_reset_pending = 0;
            }
            if (target.has_next_shape_index != 0) {
                g_state.runtime.snake_shape_index = target.next_shape_index;
            }
            if (plan.wrapped != 0) {
                g_state.snake_reset_pending = 1;
            }
        }
        g_state.runtime.snake_cursor = plan.next_cursor;
        g_state.runtime.snake_prev_start = plan.next_prev_start;
        g_state.runtime.snake_prev_count = plan.next_prev_count;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_step_t gradient_step = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient_head_row,
            g_state.runtime.mode_phase_flag, g_state.runtime.gradient_cycle,
            g_state.runtime.bench_speed_step);
        const db_gradient_damage_plan_t *plan = &gradient_step.plan;
        if ((grid_rows > 0U) && (grid_cols > 0U)) {
            const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
            const uint32_t span_units = grid_rows * grid_cols;
            const db_vk_owner_draw_ctx_t draw_ctx = {
                .cmd = g_state.command_buffer,
                .layout = g_state.pipeline_layout,
                .extent = g_state.swapchain_state.extent,
                .have_group = haveGroup,
                .active_gpu_count = active_gpu_count,
                .frame_start_ns = frameStart,
                .budget_ns = budget_ns,
                .safety_ns = safety_ns,
                .ema_ms_per_work_unit = g_state.ema_ms_per_work_unit,
                .timing_enabled = g_state.gpu_timing_enabled,
                .timing_query_pool = g_state.timing_query_pool,
                .frame_owner_used = frame_owner_used,
                .frame_owner_finished = frame_owner_finished,
                .frame_work_units = frame_work_units,
                .grid_tiles_per_gpu = grid_tiles_per_gpu,
                .grid_tiles_drawn = &grid_tiles_drawn,
                .grid_rows = grid_rows,
                .grid_cols = grid_cols,
            };
            const db_vk_grid_row_block_draw_req_t req = {
                .candidate_owner = 0U,
                .span_units = span_units,
                .row_start = 0U,
                .row_end = grid_rows,
                .color = shader_ignored_color,
                .render_mode = (uint32_t)g_state.runtime.pattern,
                .gradient_head_row = plan->render_head_row,
                .snake_shape_index = 0U,
                .mode_phase_flag = gradient_step.render_direction_down,
                .snake_cursor = 0U,
                .snake_batch_size = 0U,
                .snake_phase_completed = 0,
                .palette_cycle = plan->render_cycle_index,
            };
            db_vk_draw_owner_grid_row_block(&draw_ctx, &req);
        }
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_step);
    }

    if (haveGroup) {
        vkCmdSetDeviceMask(g_state.command_buffer, MASK_GPU0);
    }
    vkCmdEndRenderPass(g_state.command_buffer);

    if (history_mode) {
        VkImageMemoryBarrier write_to_src = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        write_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        write_to_src.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        write_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        write_to_src.image = g_state.history_targets[write_index].image;
        write_to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        write_to_src.subresourceRange.levelCount = 1U;
        write_to_src.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier swap_to_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        swap_to_dst.srcAccessMask = 0;
        swap_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swap_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swap_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swap_to_dst.image = g_state.swapchain_state.images[imgIndex];
        swap_to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_to_dst.subresourceRange.levelCount = 1U;
        swap_to_dst.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier pre_copy_barriers[2] = {write_to_src, swap_to_dst};
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 2U, pre_copy_barriers);

        VkImageCopy region = {0};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1U;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1U;
        region.extent.width = g_state.swapchain_state.extent.width;
        region.extent.height = g_state.swapchain_state.extent.height;
        region.extent.depth = 1U;
        vkCmdCopyImage(g_state.command_buffer,
                       g_state.history_targets[write_index].image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       g_state.swapchain_state.images[imgIndex],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region);

        VkImageMemoryBarrier write_back_to_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_back_to_read.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        write_back_to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_back_to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        write_back_to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_back_to_read.image = g_state.history_targets[write_index].image;
        write_back_to_read.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        write_back_to_read.subresourceRange.levelCount = 1U;
        write_back_to_read.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier swap_to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        swap_to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swap_to_present.dstAccessMask = 0;
        swap_to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swap_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swap_to_present.image = g_state.swapchain_state.images[imgIndex];
        swap_to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_to_present.subresourceRange.levelCount = 1U;
        swap_to_present.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier post_copy_barriers[2] = {write_back_to_read,
                                                      swap_to_present};
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 2U, post_copy_barriers);
        g_state.history_targets[write_index].layout_initialized = 1;
        g_state.history_read_index = write_index;
    }
    DB_VK_CHECK(BACKEND_NAME, vkEndCommandBuffer(g_state.command_buffer));

    VkPipelineStageFlags waitStage =
        history_mode ? VK_PIPELINE_STAGE_TRANSFER_BIT
                     : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_state.image_available;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_state.command_buffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_state.render_done;
    DB_VK_CHECK(BACKEND_NAME,
                vkQueueSubmit(g_state.queue, 1, &si, g_state.in_flight));
    if (g_state.gpu_timing_enabled) {
        int any_owner_used = 0;
        for (uint32_t g = 0; g < gpuCount; g++) {
            g_state.prev_frame_work_units[g] = frame_work_units[g];
            g_state.prev_frame_owner_used[g] = frame_owner_used[g];
            if (frame_owner_used[g] != 0U) {
                any_owner_used = 1;
            }
        }
        g_state.have_prev_timing_frame = any_owner_used;
    }

    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_state.render_done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_state.swapchain_state.swapchain;
    pi.pImageIndices = &imgIndex;
    VkResult present_result = vkQueuePresentKHR(g_state.queue, &pi);
    if ((present_result != VK_SUCCESS) &&
        (present_result != VK_SUBOPTIMAL_KHR) &&
        (present_result != VK_ERROR_OUT_OF_DATE_KHR)) {
        infof("QueuePresent returned %s (%d), ending benchmark loop",
              db_vk_result_name(present_result), (int)present_result);
        return DB_VK_FRAME_STOP;
    }
    if (acquire_suboptimal || (present_result == VK_SUBOPTIMAL_KHR) ||
        (present_result == VK_ERROR_OUT_OF_DATE_KHR)) {
        const VkExtent2D old_extent = g_state.swapchain_state.extent;
        db_vk_recreate_swapchain_state(
            &g_state.wsi_config, g_state.present_phys, g_state.device,
            g_state.surface, g_state.surface_format, g_state.present_mode,
            g_state.render_pass, &g_state.swapchain_state);
        const int preserved = db_vk_recreate_history_targets_preserve(
            g_state.present_phys, g_state.device, g_state.surface_format.format,
            g_state.swapchain_state.extent, g_state.history_render_pass,
            g_state.device_group_mask, g_state.command_pool, g_state.queue,
            old_extent, g_state.history_targets, &g_state.history_read_index);
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[g_state.history_read_index].view);
        g_state.history_descriptor_index = g_state.history_read_index;
        if (((g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
             (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) &&
            (preserved == 0)) {
            g_state.snake_reset_pending = 1;
        }
        g_state.frame_index++;
        return DB_VK_FRAME_RETRY;
    }

    if (!g_state.gpu_timing_enabled) {
        uint64_t frameEnd = db_now_ns_monotonic();
        double frame_ms = (double)(frameEnd - frameStart) / DB_NS_PER_MS_D;
        db_vk_update_ema_fallback(g_state.runtime.pattern, gpuCount,
                                  g_state.work_owner, grid_tiles_per_gpu,
                                  grid_tiles_drawn, frame_ms,
                                  g_state.ema_ms_per_work_unit);
    }

    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.bench_frames++;
    double bench_ms = (double)(db_now_ns_monotonic() - g_state.bench_start_ns) /
                      DB_NS_PER_MS_D;
    db_benchmark_log_periodic(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.bench_frames, g_state.runtime.work_unit_count, bench_ms,
        g_state.capability_mode, &g_state.next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS_D);
    g_state.frame_index++;
    return DB_VK_FRAME_OK;
}

void db_vk_shutdown_impl(void) {
    if (!g_state.initialized) {
        return;
    }
    uint64_t bench_end = db_now_ns_monotonic();
    double bench_ms =
        (double)(bench_end - g_state.bench_start_ns) / DB_NS_PER_MS_D;
    db_benchmark_log_final(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.bench_frames, g_state.runtime.work_unit_count, bench_ms,
        g_state.capability_mode);
    vkDeviceWaitIdle(g_state.device);
    const db_vk_cleanup_ctx_t cleanup = {
        .device = g_state.device,
        .in_flight = g_state.in_flight,
        .image_available = g_state.image_available,
        .render_done = g_state.render_done,
        .vertex_buffer = g_state.vertex_buffer,
        .vertex_memory = g_state.vertex_memory,
        .pipeline = g_state.pipeline,
        .pipeline_layout = g_state.pipeline_layout,
        .swapchain_state = &g_state.swapchain_state,
        .history_targets = g_state.history_targets,
        .render_pass = g_state.render_pass,
        .history_render_pass = g_state.history_render_pass,
        .command_pool = g_state.command_pool,
        .timing_query_pool = g_state.timing_query_pool,
        .descriptor_set_layout = g_state.descriptor_set_layout,
        .descriptor_pool = g_state.descriptor_pool,
        .history_sampler = g_state.history_sampler,
        .instance = g_state.instance,
        .surface = g_state.surface,
    };
    db_vk_cleanup_runtime(&cleanup);
    free(g_state.snake_spans);
    free(g_state.snake_row_bounds);
    g_state = (renderer_state_t){0};
}

const char *db_vk_capability_mode_impl(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : DB_CAP_MODE_VULKAN_SINGLE_GPU;
}

uint32_t db_vk_work_unit_count_impl(void) {
    return g_state.runtime.work_unit_count;
}

uint64_t db_vk_state_hash_impl(void) { return g_state.state_hash; }

// NOLINTEND(misc-include-cleaner)
