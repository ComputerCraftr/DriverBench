#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_history_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define FRAME_BUDGET_NS 16666666ULL
#define FRAME_SAFETY_NS 2000000ULL
#define MASK_GPU0 1U
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define WAIT_TIMEOUT_NS 100000000ULL
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

static const float db_vk_shader_ignored_color_rgb[3] = {0.0F, 0.0F, 0.0F};

static inline db_vk_grid_row_block_draw_req_t db_vk_gradient_row_block_req(
    const db_dirty_row_range_t *range, uint32_t active_gpu_count,
    uint32_t grid_cols, db_pattern_t pattern, const db_gradient_state_t *state,
    uint32_t frame_index) {
    if ((range == NULL) || (state == NULL)) {
        return (db_vk_grid_row_block_draw_req_t){0};
    }
    return (db_vk_grid_row_block_draw_req_t){
        .candidate_owner = range->row_start % active_gpu_count,
        .span_units = range->row_count * grid_cols,
        .row_start = range->row_start,
        .row_end = range->row_start + range->row_count,
        .payload =
            {
                .color = db_vk_shader_ignored_color_rgb,
                .render_mode = (uint32_t)pattern,
                .gradient_head_row = state->head_row,
                .snake_shape_index = 0U,
                .direction_flag = state->direction_down,
                .snake_cursor = 0U,
                .snake_batch_size = 0U,
                .snake_phase_completed = 0,
                .palette_cycle = state->cycle_index,
                .frame_index = frame_index,
                .band_count = 0U,
            },
    };
}

static void db_vk_recreate_swapchain_and_history_targets_with_reset(void) {
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

db_vk_frame_result_t db_renderer_vulkan_1_2_multi_gpu_render_frame(void) {
    if (!g_state.initialized) {
        return DB_VK_FRAME_STOP;
    }

    const uint32_t gpu_count = db_vk_normalize_gpu_count(g_state.gpu_count);
    const uint32_t active_gpu_count = gpu_count;
    const int have_group = g_state.have_group;
    int frame_full_draw = 0;
    int frame_dirty_draw = 0;

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
            gpu_count * TIMESTAMP_QUERIES_PER_GPU,
            sizeof(uint64_t) * gpu_count * TIMESTAMP_QUERIES_PER_GPU,
            query_results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (query_result == VK_SUCCESS) {
            for (uint32_t g = 0; g < gpu_count; g++) {
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
                    DB_NS_PER_MS;
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

    uint32_t img_index = 0;
    VkResult ar = vkAcquireNextImageKHR(
        g_state.device, g_state.swapchain_state.swapchain, WAIT_TIMEOUT_NS,
        g_state.image_available, VK_NULL_HANDLE, &img_index);
    if (ar == VK_TIMEOUT) {
        return DB_VK_FRAME_RETRY;
    }
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        db_vk_recreate_swapchain_and_history_targets_with_reset();
        g_state.frame.frame_index++;
        return DB_VK_FRAME_RETRY;
    }
    if ((ar != VK_SUCCESS) && (ar != VK_SUBOPTIMAL_KHR)) {
        infof("AcquireNextImage returned %s (%d), ending benchmark loop",
              db_vk_result_name(ar), (int)ar);
        return DB_VK_FRAME_STOP;
    }
    const int acquire_suboptimal = (ar == VK_SUBOPTIMAL_KHR);
    const db_history_runtime_mode_flags_t mode_flags =
        db_history_runtime_mode_flags(&g_state.runtime);
    const int read_index = db_history_pair_read_index(&g_state.history_pair);
    const int write_index = db_history_pair_write_index(&g_state.history_pair);
    if (db_history_pair_sync_descriptor_index_if_needed(
            mode_flags.uses_history_pipeline, &g_state.history_descriptor_index,
            &g_state.history_pair) != 0) {
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[read_index].view);
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
                            0, gpu_count * TIMESTAMP_QUERIES_PER_GPU);
    }

    VkClearValue clear = {0};
    db_history_seed_background_rgba_f32(&g_state.runtime, clear.color.float32);

    if ((mode_flags.uses_history_pipeline != 0) &&
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

        VkClearColorValue history_clear = {0};
        db_copy_bytes(history_clear.float32, clear.color.float32,
                      sizeof(history_clear.float32));
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

    if (mode_flags.uses_history_pipeline != 0) {
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
    rbi.renderPass = (mode_flags.uses_history_pipeline != 0)
                         ? g_state.history_render_pass
                         : g_state.render_pass;
    rbi.framebuffer = (mode_flags.uses_history_pipeline != 0)
                          ? g_state.history_targets[write_index].framebuffer
                          : g_state.swapchain_state.framebuffers[img_index];
    rbi.renderArea.extent = g_state.swapchain_state.extent;
    rbi.clearValueCount = (mode_flags.uses_history_pipeline != 0) ? 0U : 1U;
    rbi.pClearValues = (mode_flags.uses_history_pipeline != 0) ? NULL : &clear;
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

    uint64_t frame_start_ns = db_now_ns_monotonic();
    uint32_t grid_tiles_per_gpu[MAX_GPU_COUNT] = {0};
    uint32_t grid_tiles_drawn = 0U;
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint32_t grid_cols = db_grid_cols_effective();
    db_vk_push_constants_frame_static(
        g_state.command_buffer, g_state.pipeline_layout,
        g_state.swapchain_state.extent, grid_rows, grid_cols);
    VkViewport vpo = {0};
    vpo.width = db_double_to_f32((double)g_state.swapchain_state.extent.width);
    vpo.height =
        db_double_to_f32((double)g_state.swapchain_state.extent.height);
    vpo.maxDepth = 1.0F;
    vkCmdSetViewport(g_state.command_buffer, 0, 1, &vpo);

    if (mode_flags.is_bands != 0) {
        frame_full_draw = 1;
        for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
            const uint32_t x0 =
                (g_state.swapchain_state.extent.width * band) / BENCH_BANDS;
            const uint32_t x1 =
                (g_state.swapchain_state.extent.width * (band + 1U)) /
                BENCH_BANDS;
            if (x1 <= x0) {
                continue;
            }

            const uint32_t col_start = (grid_cols * band) / BENCH_BANDS;
            const uint32_t col_end = (grid_cols * (band + 1U)) / BENCH_BANDS;
            const uint32_t span_units =
                (col_end > col_start) ? ((col_end - col_start) * grid_rows)
                                      : 0U;
            if (span_units == 0U) {
                continue;
            }

            const uint32_t candidate_owner = g_state.work_owner[band];
            const uint32_t owner = db_vk_select_owner_for_work(
                candidate_owner, active_gpu_count, span_units, frame_start_ns,
                FRAME_BUDGET_NS, FRAME_SAFETY_NS, g_state.ema_ms_per_work_unit);
            if (have_group) {
                vkCmdSetDeviceMask(g_state.command_buffer,
                                   (MASK_GPU0 << owner));
            }
            db_vk_owner_timing_begin(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_used);

            VkRect2D scissor = {0};
            scissor.offset.x =
                db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
            scissor.offset.y = 0;
            scissor.extent.width = x1 - x0;
            scissor.extent.height = g_state.swapchain_state.extent.height;
            vkCmdSetScissor(g_state.command_buffer, 0, 1, &scissor);

            const db_vk_draw_dynamic_req_t draw_req = {
                .ndc_x0 = -1.0F,
                .ndc_y0 = -1.0F,
                .ndc_x1 = 1.0F,
                .ndc_y1 = 1.0F,
                .payload =
                    {
                        .color = db_vk_shader_ignored_color_rgb,
                        .render_mode = DB_PATTERN_BANDS,
                        .gradient_head_row = 0U,
                        .direction_flag = 0,
                        .snake_cursor = 0U,
                        .snake_batch_size = 0U,
                        .snake_shape_index = 0U,
                        .snake_phase_completed = 0,
                        .palette_cycle = 0U,
                        .frame_index = g_state.frame.frame_index,
                        .band_count = BENCH_BANDS,
                    },
            };
            db_vk_push_constants_draw_dynamic(
                g_state.command_buffer, g_state.pipeline_layout, &draw_req);
            vkCmdDraw(g_state.command_buffer, DB_RECT_VERTEX_COUNT, 1, 0, 0);

            db_vk_owner_timing_end(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_finished);
            frame_work_units[owner] += span_units;
            grid_tiles_per_gpu[owner] += span_units;
            grid_tiles_drawn += span_units;
        }
    } else if (mode_flags.is_snake_history_texture != 0) {
        const db_history_snake_step_eval_t eval =
            db_history_eval_snake_step_from_runtime(&g_state.runtime);
        const db_snake_plan_t plan = eval.plan;
        const db_vk_owner_draw_ctx_t draw_ctx = {
            .cmd = g_state.command_buffer,
            .layout = g_state.pipeline_layout,
            .extent = g_state.swapchain_state.extent,
            .have_group = have_group,
            .active_gpu_count = active_gpu_count,
            .frame_start_ns = frame_start_ns,
            .budget_ns = FRAME_BUDGET_NS,
            .safety_ns = FRAME_SAFETY_NS,
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
        if (eval.is_grid_mode != 0) {
            db_vk_draw_snake_grid_plan(&draw_ctx, &plan,
                                       db_vk_shader_ignored_color_rgb);
            frame_dirty_draw = 1;
        } else {
            db_vk_draw_snake_region_plan(&draw_ctx, &plan,
                                         g_state.runtime.pattern_seed,
                                         g_state.runtime.snake.prev_start,
                                         g_state.runtime.snake.prev_count,
                                         db_vk_shader_ignored_color_rgb);
            frame_dirty_draw = 1;
        }
        db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
    } else if (mode_flags.is_gradient != 0) {
        const db_gradient_damage_plan_t plan = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient.head_row,
            g_state.runtime.gradient.direction_down,
            g_state.runtime.gradient.cycle_index,
            g_state.runtime.bench_speed_step);
        if ((grid_rows > 0U) && (grid_cols > 0U)) {
            const db_vk_owner_draw_ctx_t draw_ctx = {
                .cmd = g_state.command_buffer,
                .layout = g_state.pipeline_layout,
                .extent = g_state.swapchain_state.extent,
                .have_group = have_group,
                .active_gpu_count = active_gpu_count,
                .frame_start_ns = frame_start_ns,
                .budget_ns = FRAME_BUDGET_NS,
                .safety_ns = FRAME_SAFETY_NS,
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
            db_dirty_row_range_t curr_ranges[2] = {{0U, 0U}, {0U, 0U}};
            size_t curr_count = db_gradient_collect_dirty_ranges_clamped(
                &plan, grid_rows, curr_ranges, 2U);
            const int seeded_full = db_history_apply_full_seed_rows_if_needed(
                &g_state.history_pair.is_valid, grid_rows, curr_ranges, 2U,
                &curr_count);
            if (seeded_full != 0) {
                frame_full_draw = 1;
            } else if (g_state.gradient_prev_frame.draw_count > 0U) {
                db_dirty_row_range_t replay_ranges[2] = {{0U, 0U}, {0U, 0U}};
                const size_t replay_base_count =
                    (g_state.gradient_prev_frame.draw_count < 2U)
                        ? g_state.gradient_prev_frame.draw_count
                        : 2U;
                size_t replay_count = db_gradient_subtract_replay_ranges(
                    g_state.gradient_prev_frame.draw_rows, replay_base_count,
                    curr_ranges, curr_count, replay_ranges, 2U);
                for (size_t i = 0U; i < replay_count; i++) {
                    const db_dirty_row_range_t replay = replay_ranges[i];
                    if (replay.row_count == 0U) {
                        continue;
                    }
                    const db_vk_grid_row_block_draw_req_t replay_req =
                        db_vk_gradient_row_block_req(
                            &replay, active_gpu_count, grid_cols,
                            g_state.runtime.pattern,
                            &g_state.gradient_prev_frame.state,
                            g_state.frame.frame_index);
                    db_vk_draw_owner_grid_row_block(&draw_ctx, &replay_req);
                }
            }

            for (size_t i = 0U; i < curr_count; i++) {
                const db_dirty_row_range_t range = curr_ranges[i];
                if (range.row_count == 0U) {
                    continue;
                }
                const db_vk_grid_row_block_draw_req_t req =
                    db_vk_gradient_row_block_req(
                        &range, active_gpu_count, grid_cols,
                        g_state.runtime.pattern, &plan.render_state,
                        g_state.frame.frame_index);
                db_vk_draw_owner_grid_row_block(&draw_ctx, &req);
            }
            if (seeded_full == 0) {
                frame_dirty_draw = 1;
            }

            db_history_gradient_replay_state_store(&g_state.gradient_prev_frame,
                                                   curr_ranges, curr_count,
                                                   &plan.render_state);
        }
        db_gradient_apply_step_to_runtime(&g_state.runtime, &plan);
    }

    if (have_group) {
        vkCmdSetDeviceMask(g_state.command_buffer, MASK_GPU0);
    }
    vkCmdEndRenderPass(g_state.command_buffer);

    if (mode_flags.uses_history_pipeline != 0) {
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
        swap_to_dst.image = g_state.swapchain_state.images[img_index];
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
                       g_state.swapchain_state.images[img_index],
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
        swap_to_present.image = g_state.swapchain_state.images[img_index];
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
        db_history_pair_flip_to_write(&g_state.history_pair);
    }
    DB_VK_CHECK(BACKEND_NAME, vkEndCommandBuffer(g_state.command_buffer));

    VkPipelineStageFlags wait_stage =
        (mode_flags.uses_history_pipeline != 0)
            ? VK_PIPELINE_STAGE_TRANSFER_BIT
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_state.image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_state.command_buffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_state.render_done;
    DB_VK_CHECK(BACKEND_NAME,
                vkQueueSubmit(g_state.queue, 1, &si, g_state.in_flight));
    if (g_state.gpu_timing_enabled) {
        int any_owner_used = 0;
        for (uint32_t g = 0; g < gpu_count; g++) {
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
    pi.pImageIndices = &img_index;
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
        db_vk_recreate_swapchain_and_history_targets_with_reset();
        g_state.frame.frame_index++;
        return DB_VK_FRAME_RETRY;
    }

    if (!g_state.gpu_timing_enabled) {
        uint64_t frame_end_ns = db_now_ns_monotonic();
        double frame_ms =
            (double)(frame_end_ns - frame_start_ns) / DB_NS_PER_MS;
        db_vk_update_ema_fallback(gpu_count, frame_work_units, frame_ms,
                                  g_state.ema_ms_per_work_unit);
    }

    g_state.frame.state_hash = db_benchmark_runtime_state_hash_cross_renderer(
        &g_state.runtime, g_state.frame.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    const db_history_draw_stats_counted_t counted_draw =
        db_history_classify_counted_draw(frame_full_draw, frame_dirty_draw,
                                         grid_tiles_drawn);
    db_history_record_draw_stats(
        &g_state.frame.full_draw_frames, &g_state.frame.dirty_draw_frames,
        counted_draw.counted_full_draw, counted_draw.counted_dirty_draw);
    g_state.bench_frames++;
    double bench_ms =
        (double)(db_now_ns_monotonic() - g_state.bench_start_ns) / DB_NS_PER_MS;
    db_benchmark_log_periodic(
        "Vulkan", RENDERER_NAME,
        (g_state.log_backend_name != NULL) ? g_state.log_backend_name
                                           : BACKEND_NAME,
        g_state.bench_frames, g_state.runtime.work_unit_count, bench_ms,
        g_state.capability_mode, &g_state.next_progress_log_due_ms,
        BENCH_LOG_INTERVAL_MS);
    g_state.frame.frame_index++;
    return DB_VK_FRAME_OK;
}

void db_renderer_vulkan_1_2_multi_gpu_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }
    uint64_t bench_end = db_now_ns_monotonic();
    double bench_ms =
        (double)(bench_end - g_state.bench_start_ns) / DB_NS_PER_MS;
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
    free(g_state.snake_scratch.spans);
    free(g_state.snake_scratch.row_bounds);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_vulkan_1_2_multi_gpu_capability_mode(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : DB_CAP_MODE_VK_DRAW_TILES_FULL
               "(upload=" DB_CAP_MODE_VK_UPLOAD_NONE ",backbuffer_replay=no)";
}

uint32_t db_renderer_vulkan_1_2_multi_gpu_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, g_state.initialized);
}

uint64_t db_renderer_vulkan_1_2_multi_gpu_state_hash(void) {
    return g_state.frame.state_hash;
}

void db_renderer_vulkan_1_2_multi_gpu_draw_stats(uint64_t *full_draw_frames,
                                                 uint64_t *dirty_draw_frames) {
    if (full_draw_frames != NULL) {
        *full_draw_frames = g_state.frame.full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = g_state.frame.dirty_draw_frames;
    }
}

// NOLINTEND(misc-include-cleaner)
