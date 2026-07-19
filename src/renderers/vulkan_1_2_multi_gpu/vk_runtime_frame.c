#include "../../core/db_conformance.h"
#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"
#include "../../core/db_progress_policy.h"
#include "../../core/db_render_result.h"
#include "../damage_trace.h"
#include "core/db_log.h"
#include "core/db_render_types.h"
#include "core/db_renderer_diagnostics.h"
#include "core/db_renderer_support.h"
#include "vk_diagnostics.h"
#include "vk_frame_finalize.h"
#include "vk_init_internal.h"
#include "vk_internal.h"

#include "core/db_render_ir.h"
#include "vk_renderer.h"
#include "vk_runtime_internal.h"
#include "vk_state_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

static const float db_vk_gradient_lookup_mode_threshold = 1.5F;

db_vk_frame_result_t db_vk_render_frame(const db_frame_plan_t *plan) {
    if (!g_state.initialized || plan == NULL) {
        return DB_VK_FRAME_STOP;
    }
    g_state.frame.frame_index = plan->frame_index;
    const db_render_ir_external_binding_t *const rebuild_binding =
        (plan->external_bindings.count > 0U)
            ? &plan->external_bindings.bindings[0]
            : NULL;
    const int use_raster_seed = DB_BOOL(rebuild_binding != NULL);

    const uint32_t gpu_count =
        db_vk_normalize_gpu_count(g_state.device.selection.active_lane_count);
    uint32_t active_gpu_count = 1U;
    if (g_state.calibration.state.phase == DB_VK_MULTI_GPU_ACTIVE) {
        active_gpu_count = gpu_count;
    }
    if (use_raster_seed != 0) {
        active_gpu_count = 1U;
    }
    if ((plan->rebuild_required != 0) && (plan->frame_index > 0U)) {
        g_state.scheduler.content_generation++;
    }
    if (active_gpu_count != g_state.scheduler.last_active_lane_count) {
        g_state.scheduler.scheduling_epoch++;
        g_state.scheduler.last_active_lane_count = active_gpu_count;
    }
    db_vk_scheduling_policy_t scheduling_policy = DB_VK_SCHEDULING_PRIMARY_ONLY;
    if (active_gpu_count > 1U) {
        scheduling_policy = DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS;
    }
    const uint32_t planned_gradient_commands =
        plan->update_metadata.gradient_count +
        ((plan->rebuild_required != 0) ? plan->rebuild_metadata.gradient_count
                                       : 0U);
    const char *const probe_implementation =
        getenv("DRIVERBENCH_PROBE_GRADIENT_IMPLEMENTATION");
    const int probe_child = getenv("DRIVERBENCH_PROBE_CHILD") != NULL;
    if ((planned_gradient_commands > 0U) &&
        (g_state.diagnostics.vk_gradient == DB_VK_GRADIENT_AUTO) &&
        (probe_child == 0)) {
        if (g_state.scheduler.gradient_applied.generation == 0U) {
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("code", "gradient_path_unqualified"),
                DB_LOG_TOKEN("reason", "run_snapshot_unavailable"),
                DB_LOG_U64("retained_lanes",
                           g_state.device.selection.active_lane_count),
            };
            db_log_error(BACKEND_NAME, "vk_execution_error", fields,
                         DB_LOG_FIELD_COUNT(fields));
            return DB_VK_FRAME_STOP;
        }
    }
    db_gradient_implementation_t gradient_implementation =
        DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES;
    if (probe_child && (probe_implementation != NULL) &&
        (strcmp(probe_implementation, "exact_lookup") == 0)) {
        gradient_implementation = DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP;
    } else if (g_state.diagnostics.vk_gradient == DB_VK_GRADIENT_SEMANTIC) {
        gradient_implementation = DB_GRADIENT_IMPLEMENTATION_SEMANTIC;
    } else if (g_state.diagnostics.vk_gradient == DB_VK_GRADIENT_AUTO) {
        gradient_implementation =
            g_state.scheduler.gradient_applied.implementation;
    }
    const int accelerated_gradient = DB_BOOL(
        gradient_implementation != DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES);
    db_vk_execution_plan_t execution_plan = {0};
    if (db_vk_build_execution_plan_for_gradient_path(
            plan, active_gpu_count, scheduling_policy,
            g_state.scheduler.ema_ms_per_work_unit,
            g_state.scheduler.scheduling_epoch,
            g_state.scheduler.content_generation,
            g_state.scheduler.piece_storage, DB_VK_MAX_PIECES_PER_FRAME,
            g_state.scheduler.assignment_storage, DB_VK_MAX_PIECES_PER_FRAME,
            &execution_plan, accelerated_gradient) == 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("code", "execution_plan_unavailable"),
            DB_LOG_U64("frame", plan->frame_index),
        };
        db_log_error(BACKEND_NAME, "vk_execution_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
        return DB_VK_FRAME_STOP;
    }
    if ((db_vk_trace_level() >= 1) &&
        ((plan->frame_index == 0U) || execution_plan.primary_only_fallback)) {
        const db_log_field_t fields[] = {
            DB_LOG_U64("frame", plan->frame_index),
            DB_LOG_U64("scheduling_epoch", execution_plan.scheduling_epoch),
            DB_LOG_U64("content_generation", execution_plan.content_generation),
            DB_LOG_U64("piece_count", execution_plan.piece_count),
            DB_LOG_U64("assignment_count", execution_plan.assignment_count),
            DB_LOG_U64("policy", execution_plan.policy),
            DB_LOG_BOOL("primary_only_fallback",
                        execution_plan.primary_only_fallback),
        };
        db_log_info(BACKEND_NAME, "vk_execution_plan", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    const uint64_t frame_budget_ns =
        db_vk_scheduler_frame_budget_ns(g_state.presentation.present_mode);
    const uint64_t frame_safety_ns =
        db_vk_scheduler_frame_safety_ns(g_state.presentation.present_mode);
    uint64_t scheduler_budget_ns = frame_budget_ns;
    if (g_state.metrics.frame_time_ema_ms > 0.0) {
        const uint64_t dynamic_budget_ns =
            (uint64_t)(g_state.metrics.frame_time_ema_ms * DB_NS_PER_MS);
        if (dynamic_budget_ns > scheduler_budget_ns) {
            scheduler_budget_ns = dynamic_budget_ns;
        }
    }
    const int have_group = DB_BOOL((active_gpu_count > 1U) &&
                                   (g_state.device.selection.execution_mode ==
                                    DB_VK_EXECUTION_MODE_DEVICE_GROUP));
    int frame_full_draw = 0;
    int frame_dirty_draw = 0;

    VkResult wait_result =
        db_vk_wait_fence(g_state.device.device, g_state.presentation.in_flight,
                         DB_PROGRESS_VK_PRIMARY_FENCE, "frame_in_flight");
    if (wait_result != VK_SUCCESS) {
        db_vk_fail(BACKEND_NAME, "vkWaitForFences", wait_result, __FILE__,
                   __LINE__);
    }
    size_t lookup_word_count = 0U;
    const size_t instance_count =
        db_vk_write_frame_instances_for_implementation(
            plan,
            (db_vk_ir_execute_instance_t *)g_state.pipelines.instance_mapped,
            DB_VK_MAX_PIECES_PER_FRAME,
            (uint32_t *)g_state.pipelines.lookup_mapped,
            DB_VK_LOOKUP_WORD_CAPACITY, &lookup_word_count,
            g_state.backing.pixel_format, gradient_implementation);
    if ((instance_count == 0U) && (db_vk_frame_rect_count(plan) != 0U)) {
        DB_RUNTIME_FAIL(BACKEND_NAME, "Vulkan instance stream overflow");
    }
    if ((db_vk_trace_level() >= 2) && (lookup_word_count > 0U)) {
        const uint32_t *const lookup =
            (const uint32_t *)g_state.pipelines.lookup_mapped;
        const db_vk_ir_execute_instance_t *const instances =
            (const db_vk_ir_execute_instance_t *)
                g_state.pipelines.instance_mapped;
        size_t gradient_index = 0U;
        while ((gradient_index < instance_count) &&
               (instances[gradient_index].gradient[0] <
                db_vk_gradient_lookup_mode_threshold)) {
            gradient_index++;
        }
        const db_log_field_t fields[] = {
            DB_LOG_U64("frame", plan->frame_index),
            DB_LOG_U64("word_count", lookup_word_count),
            DB_LOG_HEX64("first_word", lookup[0]),
            DB_LOG_HEX64("last_word", lookup[lookup_word_count - 1U]),
            DB_LOG_U64("instance", gradient_index),
            DB_LOG_DOUBLE("rect_y", (gradient_index < instance_count)
                                        ? db_f32_to_double(
                                              instances[gradient_index].rect[1])
                                        : 0.0),
            DB_LOG_DOUBLE(
                "row_start",
                (gradient_index < instance_count)
                    ? db_f32_to_double(instances[gradient_index].gradient[1])
                    : 0.0),
        };
        db_log_info(BACKEND_NAME, "vk_gradient_lookup", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    if (use_raster_seed != 0) {
        db_vk_prepare_raster_seed_upload(rebuild_binding);
    }

    if (g_state.device.gpu_timing_enabled &&
        g_state.scheduler.have_prev_timing_frame) {
        uint64_t query_results[TIMESTAMP_QUERY_COUNT] = {0};
        for (uint32_t g = 0; g < gpu_count; g++) {
            if ((g_state.scheduler.prev_frame_owner_used[g] == 0U) ||
                (g_state.scheduler.prev_frame_work_units[g] == 0U)) {
                continue;
            }
            const uint32_t base_query = g * TIMESTAMP_QUERIES_PER_GPU;
            VkResult query_result = vkGetQueryPoolResults(
                g_state.device.device, g_state.device.timing_query_pool,
                base_query, TIMESTAMP_QUERIES_PER_GPU,
                sizeof(uint64_t) * TIMESTAMP_QUERIES_PER_GPU,
                &query_results[base_query], sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (query_result == VK_SUCCESS) {
                const size_t base_query_idx = db_checked_u32_to_size(
                    BACKEND_NAME, "base_query_idx", base_query);
                const uint64_t start = query_results[base_query_idx];
                const uint64_t end = query_results[base_query_idx + 1U];
                if (end <= start) {
                    continue;
                }
                const double elapsed_ms = (DB_TO_F64(end - start) *
                                           g_state.device.timestamp_period_ns) /
                                          DB_NS_PER_MS;
                const double ms_per_unit =
                    elapsed_ms /
                    DB_TO_F64(g_state.scheduler.prev_frame_work_units[g]);
                g_state.scheduler.ema_ms_per_work_unit[g] =
                    (EMA_KEEP * g_state.scheduler.ema_ms_per_work_unit[g]) +
                    (EMA_NEW * ms_per_unit);
            }
        }
    }

    DB_VK_CHECK(BACKEND_NAME, vkResetFences(g_state.device.device, 1,
                                            &g_state.presentation.in_flight));

    VkSemaphore worker_wait_semaphores[MAX_GPU_COUNT] = {0};
    VkPipelineStageFlags worker_wait_stages[MAX_GPU_COUNT] = {0};
    VkSemaphore worker_reuse_semaphores[MAX_GPU_COUNT] = {0};
    uint32_t worker_submit_count = 0U;
    if ((active_gpu_count > 1U) && (g_state.device.selection.execution_mode ==
                                    DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES)) {
        worker_submit_count = db_vk_independent_lanes_submit(
            plan, &execution_plan, worker_wait_semaphores, worker_wait_stages,
            worker_reuse_semaphores);
        if (worker_submit_count == 0U) {
            active_gpu_count = 1U;
        }
    }

    const int use_present_path = (g_state.presentation.no_present_mode == 0);
    const int use_offscreen_target =
        (use_present_path == 0) &&
        (g_state.runtime.pipeline.uses_history_pipeline == 0);
    uint32_t img_index = 0;
    int acquire_suboptimal = 0;
    if (use_present_path) {
        VkResult ar = db_vk_acquire_next_image(
            g_state.device.device,
            g_state.presentation.swapchain_state.swapchain,
            g_state.presentation.image_available, &img_index);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
            db_vk_recreate_swapchain_and_backing_targets_with_reset();
            g_state.frame.frame_index++;
            return DB_VK_FRAME_RETRY;
        }
        if ((ar != VK_SUCCESS) && (ar != VK_SUBOPTIMAL_KHR)) {
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("code", "acquire_failed"),
                DB_LOG_TOKEN("result", db_vk_result_name(ar)),
                DB_LOG_I64("result_code", ar),
            };
            db_log_error(BACKEND_NAME, "vk_presentation_error", fields,
                         DB_LOG_FIELD_COUNT(fields));
            return DB_VK_FRAME_STOP;
        }
        acquire_suboptimal = (ar == VK_SUBOPTIMAL_KHR);
    }
    const int backing_index = 0;
    db_damage_trace_emit_frame_plan(DB_DAMAGE_TRACE_BACKEND_VULKAN,
                                    "vk_backing", g_state.backing.generation,
                                    plan);
    if ((use_raster_seed == 0) &&
        (g_state.runtime.pipeline.uses_history_pipeline != 0) &&
        (g_state.backing.descriptor_index != backing_index)) {
        db_vk_update_backing_descriptor(
            g_state.device.device, g_state.pipelines.descriptor_set,
            g_state.backing.sampler,
            g_state.backing.targets[backing_index].view);
        g_state.backing.descriptor_index = backing_index;
    }

    DB_VK_CHECK(BACKEND_NAME,
                vkResetCommandBuffer(g_state.device.command_buffer, 0));
    VkCommandBufferBeginInfo cbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    DB_VK_CHECK(BACKEND_NAME,
                vkBeginCommandBuffer(g_state.device.command_buffer, &cbi));
    const VkBufferMemoryBarrier host_buffers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = g_state.pipelines.instance_buffer,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = g_state.pipelines.lookup_buffer,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        },
    };
    vkCmdPipelineBarrier(
        g_state.device.command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0U, 0U, NULL,
        db_checked_size_to_u32(BACKEND_NAME, "host_buffer_barrier_count",
                               sizeof(host_buffers) / sizeof(host_buffers[0])),
        host_buffers, 0U, NULL);
    uint32_t frame_work_units[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_used[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_finished[MAX_GPU_COUNT] = {0};
    uint8_t owner_enabled[MAX_GPU_COUNT] = {0};
    owner_enabled[0] = 1U;
    if (have_group != 0) {
        for (uint32_t owner = 1U; owner < active_gpu_count; owner++) {
            owner_enabled[owner] = 1U;
        }
    }
    if (g_state.device.gpu_timing_enabled) {
        vkCmdResetQueryPool(g_state.device.command_buffer,
                            g_state.device.timing_query_pool, 0,
                            gpu_count * TIMESTAMP_QUERIES_PER_GPU);
    }

    const VkClearValue clear =
        db_vk_clear_value_from_rgba_f64(g_state.runtime.seed_rgba_f64);

    if ((g_state.runtime.pipeline.uses_history_pipeline != 0) &&
        (g_state.backing.targets[backing_index].layout_initialized == 0)) {
        VkImageMemoryBarrier history_to_clear[1] = {};
        for (size_t i = 0U; i < 1U; i++) {
            history_to_clear[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_clear[i].srcAccessMask = 0;
            history_to_clear[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_clear[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            history_to_clear[i].newLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_clear[i].image = g_state.backing.targets[i].image;
            history_to_clear[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_clear[i].subresourceRange.levelCount = 1U;
            history_to_clear[i].subresourceRange.layerCount = 1U;
        }
        vkCmdPipelineBarrier(g_state.device.command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 1U, history_to_clear);

        VkClearColorValue history_clear = {0};
        memcpy(history_clear.float32, clear.color.float32,
               sizeof(history_clear.float32));
        VkImageSubresourceRange history_range = {0};
        history_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        history_range.levelCount = 1U;
        history_range.layerCount = 1U;
        for (size_t i = 0U; i < 1U; i++) {
            vkCmdClearColorImage(g_state.device.command_buffer,
                                 g_state.backing.targets[i].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &history_clear, 1U, &history_range);
        }

        VkImageMemoryBarrier history_to_read[1] = {};
        for (size_t i = 0U; i < 1U; i++) {
            history_to_read[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_read[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_read[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            history_to_read[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_read[i].newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            history_to_read[i].image = g_state.backing.targets[i].image;
            history_to_read[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_read[i].subresourceRange.levelCount = 1U;
            history_to_read[i].subresourceRange.layerCount = 1U;
            g_state.backing.targets[i].layout_initialized = 1;
        }
        vkCmdPipelineBarrier(g_state.device.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 1U, history_to_read);
    }

    if ((use_raster_seed == 0) && (use_offscreen_target != 0) &&
        (g_state.backing.targets[0].layout_initialized == 0)) {
        VkImageMemoryBarrier offscreen_to_color = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        offscreen_to_color.srcAccessMask = 0;
        offscreen_to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        offscreen_to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        offscreen_to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        offscreen_to_color.image = g_state.backing.targets[0].image;
        offscreen_to_color.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        offscreen_to_color.subresourceRange.levelCount = 1U;
        offscreen_to_color.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(g_state.device.command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1U, &offscreen_to_color);
        g_state.backing.targets[0].layout_initialized = 1;
    }

    if (use_raster_seed != 0) {
        db_vk_record_raster_seed_upload(g_state.device.command_buffer,
                                        &g_state.backing.targets[backing_index],
                                        rebuild_binding);
    } else if (g_state.runtime.pipeline.uses_history_pipeline != 0) {
        VkImageMemoryBarrier backing_to_draw = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        backing_to_draw.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        backing_to_draw.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        backing_to_draw.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        backing_to_draw.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        backing_to_draw.image = g_state.backing.targets[backing_index].image;
        backing_to_draw.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        backing_to_draw.subresourceRange.levelCount = 1U;
        backing_to_draw.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(g_state.device.command_buffer,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1U, &backing_to_draw);
    }

    const uint64_t frame_start_ns = db_now_ns_monotonic();
    uint32_t grid_tiles_drawn = 0U;
    const size_t draw_count = db_vk_frame_rect_count(plan);
    (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
        .frame_index = plan->frame_index,
        .backend = DB_DAMAGE_TRACE_BACKEND_VULKAN,
        .stage = DB_DAMAGE_TRACE_STAGE_RENDERER_WRITE,
        .operation = (plan->rebuild_required != 0) ? DB_DAMAGE_TRACE_OP_REBUILD
                                                   : DB_DAMAGE_TRACE_OP_DRAW,
        .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
        .destination = DB_DAMAGE_TRACE_BUFFER_VK_IMAGE,
        .space = DB_DAMAGE_TRACE_SPACE_GRID,
        .width = plan->grid_cols,
        .height = plan->grid_rows,
        .pixel_format = g_state.backing.pixel_format,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
        .target = "vk_backing",
        .target_generation = g_state.backing.generation,
    });
    if (have_group != 0) {
        grid_tiles_drawn = db_vk_device_group_record(
            plan, &execution_plan, g_state.device.command_buffer,
            frame_work_units, frame_owner_used, frame_owner_finished);
    } else {
        VkRenderPassBeginInfo rbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        const int using_history_target =
            (g_state.runtime.pipeline.uses_history_pipeline != 0) ||
            (use_offscreen_target != 0);
        rbi.renderPass =
            ((g_state.runtime.pipeline.uses_history_pipeline != 0) ||
             (use_offscreen_target != 0))
                ? g_state.backing.render_pass
                : g_state.presentation.render_pass;
        if (using_history_target != 0) {
            const uint32_t history_target_index = 0U;
            rbi.framebuffer =
                g_state.backing.targets[history_target_index].framebuffer;
        } else {
            rbi.framebuffer =
                g_state.presentation.swapchain_state.framebuffers[img_index];
        }
        const VkExtent2D render_extent =
            (using_history_target != 0)
                ? g_state.backing.extent
                : g_state.presentation.swapchain_state.extent;
        rbi.renderArea.extent = render_extent;
        rbi.clearValueCount = (using_history_target != 0) ? 0U : 1U;
        rbi.pClearValues = (using_history_target != 0) ? NULL : &clear;
        vkCmdBeginRenderPass(g_state.device.command_buffer, &rbi,
                             VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(g_state.device.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          g_state.pipelines.pipeline);
        vkCmdBindDescriptorSets(g_state.device.command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_state.pipelines.pipeline_layout, 0U, 1U,
                                &g_state.pipelines.descriptor_set, 0U, NULL);

        const VkBuffer buffers[2] = {g_state.pipelines.vertex_buffer,
                                     g_state.pipelines.instance_buffer};
        const VkDeviceSize offsets[2] = {0U, 0U};
        vkCmdBindVertexBuffers(g_state.device.command_buffer, 0U, 2U, buffers,
                               offsets);

        uint32_t grid_tiles_per_gpu[MAX_GPU_COUNT] = {0};
        const uint32_t grid_rows = g_state.runtime.grid_rows;
        const uint32_t grid_cols = g_state.runtime.grid_cols;
        VkViewport vpo = {0};
        vpo.width = db_u32_to_f32(render_extent.width);
        vpo.height = db_u32_to_f32(render_extent.height);
        vpo.maxDepth = 1.0F;
        vkCmdSetViewport(g_state.device.command_buffer, 0, 1, &vpo);

        const db_vk_owner_draw_ctx_t draw_ctx = {
            .cmd = g_state.device.command_buffer,
            .extent = render_extent,
            .have_group = have_group,
            .active_gpu_count = active_gpu_count,
            .budget_ns = scheduler_budget_ns,
            .safety_ns = frame_safety_ns,
            .ema_ms_per_work_unit = g_state.scheduler.ema_ms_per_work_unit,
            .timing_enabled = g_state.device.gpu_timing_enabled,
            .timing_query_pool = g_state.device.timing_query_pool,
            .frame_owner_used = frame_owner_used,
            .frame_owner_finished = frame_owner_finished,
            .frame_work_units = frame_work_units,
            .grid_tiles_per_gpu = grid_tiles_per_gpu,
            .grid_tiles_drawn = &grid_tiles_drawn,
            .owner_enabled = owner_enabled,
            .grid_rows = grid_rows,
            .grid_cols = grid_cols,
        };
        for (size_t assignment_index = 0U;
             assignment_index < execution_plan.assignment_count;
             assignment_index++) {
            const db_vk_lane_assignment_t *const assignment =
                &execution_plan.assignments[assignment_index];
            if ((assignment->piece_count == 0U) ||
                (assignment->piece_first >= execution_plan.piece_count)) {
                continue;
            }
            const db_vk_present_piece_t *const piece =
                &execution_plan.pieces[assignment->piece_first];
            if (piece->instance_first >= draw_count) {
                continue;
            }
            db_grid_block_t geometry = {0};
            if (db_render_ir_rect_to_grid_block(
                    piece->logical_rect, plan->grid_cols, plan->grid_rows,
                    &geometry) == 0) {
                DB_RUNTIME_FAIL(BACKEND_NAME,
                                "invalid canonical Vulkan IR rectangle");
            }
            const db_vk_grid_row_block_draw_req_t req = {
                .span_units = db_grid_block_span_units_or_fail(
                    "canonical_block_units", &geometry),
                .owner = assignment->lane,
                .first_instance = piece->instance_first,
                .instance_count = piece->instance_count,
                .scissor = piece->destination_rect,
                .block = geometry,
            };
            db_vk_draw_owner_grid_row_block(&draw_ctx, &req);
        }
        if (draw_count > 0U) {
            const int rebuilt = DB_BOOL(plan->rebuild_required != 0);
            frame_full_draw = rebuilt;
            frame_dirty_draw = DB_BOOL(rebuilt == 0);
            g_state.backing.valid = 1;
        }

        vkCmdEndRenderPass(g_state.device.command_buffer);
    }

    if (worker_submit_count > 0U) {
        db_vk_independent_lanes_record_composition(
            g_state.device.command_buffer, &execution_plan,
            &g_state.backing.targets[0]);
    }

    if (g_state.runtime.pipeline.uses_history_pipeline != 0) {
        VkImageMemoryBarrier write_to_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_to_read.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        write_to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_to_read.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        write_to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_to_read.image = g_state.backing.targets[backing_index].image;
        write_to_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        write_to_read.subresourceRange.levelCount = 1U;
        write_to_read.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(g_state.device.command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 1U, &write_to_read);
        if (use_present_path != 0) {
            VkClearValue present_clear = {0};
            VkRenderPassBeginInfo present_rbi = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = g_state.presentation.render_pass,
                .framebuffer = g_state.presentation.swapchain_state
                                   .framebuffers[img_index],
                .renderArea = {.extent =
                                   g_state.presentation.swapchain_state.extent},
                .clearValueCount = 1U,
                .pClearValues = &present_clear,
            };
            vkCmdBeginRenderPass(g_state.device.command_buffer, &present_rbi,
                                 VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(g_state.device.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              g_state.presentation.present_pipeline);
            vkCmdBindDescriptorSets(
                g_state.device.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                g_state.pipelines.pipeline_layout, 0U, 1U,
                &g_state.pipelines.descriptor_set, 0U, NULL);
            db_vk_present_push_constants_t present_constants = {0};
            present_constants.hdr_output_enabled =
                (g_state.presentation.surface_format.colorSpace ==
                 VK_COLOR_SPACE_HDR10_ST2084_EXT)
                    ? 1.0F
                    : 0.0F;
            vkCmdPushConstants(g_state.device.command_buffer,
                               g_state.pipelines.pipeline_layout,
                               VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
                               sizeof(present_constants), &present_constants);
            VkViewport present_viewport = {
                .width = db_u32_to_f32(
                    g_state.presentation.swapchain_state.extent.width),
                .height = db_u32_to_f32(
                    g_state.presentation.swapchain_state.extent.height),
                .maxDepth = 1.0F,
            };
            VkRect2D present_scissor = {
                .extent = g_state.presentation.swapchain_state.extent,
            };
            vkCmdSetViewport(g_state.device.command_buffer, 0U, 1U,
                             &present_viewport);
            vkCmdSetScissor(g_state.device.command_buffer, 0U, 1U,
                            &present_scissor);
            vkCmdDraw(g_state.device.command_buffer, 3U, 1U, 0U, 0U);
            vkCmdEndRenderPass(g_state.device.command_buffer);
            const db_damage_block_t full_present_block = db_damage_block_full(
                g_state.presentation.swapchain_state.extent.height,
                g_state.presentation.swapchain_state.extent.width);
            (void)db_damage_trace_emit(&(const db_damage_trace_event_t){
                .frame_index = plan->frame_index,
                .backend = DB_DAMAGE_TRACE_BACKEND_VULKAN,
                .stage = DB_DAMAGE_TRACE_STAGE_PRESENT,
                .operation = DB_DAMAGE_TRACE_OP_DRAW,
                .source = DB_DAMAGE_TRACE_BUFFER_VK_IMAGE,
                .destination = DB_DAMAGE_TRACE_BUFFER_VK_SWAPCHAIN,
                .destination_index = img_index,
                .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
                .width = g_state.presentation.swapchain_state.extent.width,
                .height = g_state.presentation.swapchain_state.extent.height,
                .pixel_format = g_state.backing.pixel_format,
                .blocks = &full_present_block,
                .block_count = 1U,
                .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
                .target = "vk_backing",
                .target_generation = g_state.backing.generation,
                .present_method = "sample_fullscreen",
            });
            if ((img_index <
                 g_state.presentation.swapchain_state.image_count) &&
                (g_state.presentation.swapchain_state.image_layouts != NULL)) {
                g_state.presentation.swapchain_state.image_layouts[img_index] =
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
        }
        g_state.backing.targets[backing_index].layout_initialized = 1;
    }
    DB_VK_CHECK(BACKEND_NAME,
                vkEndCommandBuffer(g_state.device.command_buffer));

    VkPipelineStageFlags wait_stages[MAX_GPU_COUNT + 1U] = {0};
    VkSemaphore wait_semaphores[MAX_GPU_COUNT + 1U] = {0};
    VkSemaphore signal_semaphores[MAX_GPU_COUNT + 1U] = {0};
    uint32_t wait_count = 0U;
    uint32_t signal_count = 0U;
    if (use_present_path != 0) {
        wait_semaphores[wait_count] = g_state.presentation.image_available;
        wait_stages[wait_count++] =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        signal_semaphores[signal_count++] = g_state.presentation.render_done;
    }
    for (uint32_t worker = 0U; worker < worker_submit_count; worker++) {
        wait_semaphores[wait_count] = worker_wait_semaphores[worker];
        wait_stages[wait_count++] = worker_wait_stages[worker];
        signal_semaphores[signal_count++] = worker_reuse_semaphores[worker];
    }
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = wait_count;
    si.pWaitSemaphores = (wait_count > 0U) ? wait_semaphores : NULL;
    si.pWaitDstStageMask = (wait_count > 0U) ? wait_stages : NULL;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_state.device.command_buffer;
    si.signalSemaphoreCount = signal_count;
    si.pSignalSemaphores = (signal_count > 0U) ? signal_semaphores : NULL;
    uint32_t wait_device_indices[MAX_GPU_COUNT + 1U] = {0};
    uint32_t signal_device_indices[MAX_GPU_COUNT + 1U] = {0};
    const uint32_t command_buffer_device_mask =
        g_state.device.device_group_mask;
    const VkDeviceGroupSubmitInfo group_submit = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
        .waitSemaphoreCount = wait_count,
        .pWaitSemaphoreDeviceIndices = wait_device_indices,
        .commandBufferCount = 1U,
        .pCommandBufferDeviceMasks = &command_buffer_device_mask,
        .signalSemaphoreCount = signal_count,
        .pSignalSemaphoreDeviceIndices = signal_device_indices,
    };
    if (have_group != 0) {
        si.pNext = &group_submit;
    }
    DB_VK_CHECK(BACKEND_NAME, vkQueueSubmit(g_state.device.queue, 1, &si,
                                            g_state.presentation.in_flight));
    for (uint32_t lane = 1U; lane < g_state.device.selection.active_lane_count;
         lane++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane];
        if ((runtime->active != 0) && (runtime->initialized != 0)) {
            runtime->slots[runtime->active_slot].ready_sync_state =
                DB_VK_SYNC_WAIT_SUBMITTED;
        }
    }
    if (worker_submit_count > 0U) {
        db_vk_independent_lanes_export_reusable();
    }
    if (g_state.device.gpu_timing_enabled) {
        int any_owner_used = 0;
        for (uint32_t g = 0; g < gpu_count; g++) {
            g_state.scheduler.prev_frame_work_units[g] = frame_work_units[g];
            g_state.scheduler.prev_frame_owner_used[g] = frame_owner_used[g];
            if (frame_owner_used[g] != 0U) {
                any_owner_used = 1;
            }
        }
        g_state.scheduler.have_prev_timing_frame = any_owner_used;
    }

    if (use_present_path) {
        VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &g_state.presentation.render_done;
        pi.swapchainCount = 1;
        pi.pSwapchains = &g_state.presentation.swapchain_state.swapchain;
        pi.pImageIndices = &img_index;
        VkResult present_result = vkQueuePresentKHR(g_state.device.queue, &pi);
        if ((present_result != VK_SUCCESS) &&
            (present_result != VK_SUBOPTIMAL_KHR) &&
            (present_result != VK_ERROR_OUT_OF_DATE_KHR)) {
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("code", "present_failed"),
                DB_LOG_TOKEN("result", db_vk_result_name(present_result)),
                DB_LOG_I64("result_code", present_result),
            };
            db_log_error(BACKEND_NAME, "vk_presentation_error", fields,
                         DB_LOG_FIELD_COUNT(fields));
            return DB_VK_FRAME_STOP;
        }
        if (acquire_suboptimal || (present_result == VK_SUBOPTIMAL_KHR) ||
            (present_result == VK_ERROR_OUT_OF_DATE_KHR)) {
            db_vk_recreate_swapchain_and_backing_targets_with_reset();
            g_state.frame.frame_index++;
            return DB_VK_FRAME_RETRY;
        }
    }

    return db_vk_finalize_frame(&(const db_vk_frame_finalize_input_t){
        .plan = plan,
        .execution_plan = &execution_plan,
        .frame_work_units = frame_work_units,
        .frame_start_ns = frame_start_ns,
        .lookup_word_count = lookup_word_count,
        .gpu_count = gpu_count,
        .grid_tiles_drawn = grid_tiles_drawn,
        .backing_index = backing_index,
        .use_offscreen_target = use_offscreen_target,
        .frame_full_draw = frame_full_draw,
        .frame_dirty_draw = frame_dirty_draw,
        .gradient_implementation = gradient_implementation,
    });
}
