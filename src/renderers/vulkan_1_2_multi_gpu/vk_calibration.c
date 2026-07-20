#include "core/db_progress_policy.h"
#include "vk_diagnostics.h"
#include "vk_internal.h"
#include "vk_state_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_ir.h"
#include "vk_runtime_internal.h"
#include <vulkan/vulkan_core.h>

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

enum {
    DB_VK_CALIBRATION_REFERENCE = 0U,
    DB_VK_CALIBRATION_CANDIDATE = 1U,
};

static int vk_calibration_capture_deviation(VkDevice device,
                                            VkTimeDomainKHR host_domain,
                                            uint64_t *out_deviation_ns) {
    union {
        PFN_vkVoidFunction generic;
        PFN_vkGetCalibratedTimestampsKHR khr;
        PFN_vkGetCalibratedTimestampsEXT ext;
    } get_timestamps = {
        .generic = vkGetDeviceProcAddr(device, "vkGetCalibratedTimestampsKHR"),
    };
    int use_khr = 1;
    if (get_timestamps.generic == NULL) {
        get_timestamps.generic =
            vkGetDeviceProcAddr(device, "vkGetCalibratedTimestampsEXT");
        use_khr = 0;
    }
    if ((get_timestamps.generic == NULL) || (out_deviation_ns == NULL)) {
        return 0;
    }
    const VkCalibratedTimestampInfoKHR infos[2] = {
        {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
         .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR},
        {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
         .timeDomain = host_domain},
    };
    uint64_t timestamps[2] = {0};
    const VkResult result =
        (use_khr != 0) ? get_timestamps.khr(device, 2U, infos, timestamps,
                                            out_deviation_ns)
                       : get_timestamps.ext(device, 2U, infos, timestamps,
                                            out_deviation_ns);
    return DB_BOOL(result == VK_SUCCESS);
}

static void vk_calibration_initialize(void) {
    if (g_state.calibration.targets[0].image != VK_NULL_HANDLE) {
        return;
    }
    for (uint32_t index = 0U; index < 2U; index++) {
        db_vk_create_backing_target(
            g_state.device.present_phys, g_state.device.device,
            g_state.backing.format, g_state.backing.extent,
            g_state.backing.render_pass, 0U,
            &g_state.calibration.targets[index]);
    }
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_state.device.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateCommandBuffers(g_state.device.device, &command_info,
                                         &g_state.calibration.command_buffer));
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    DB_VK_CHECK(BACKEND_NAME, vkCreateFence(g_state.device.device, &fence_info,
                                            NULL, &g_state.calibration.fence));
    g_state.calibration.piece_storage = db_calloc_or_fail(
        BACKEND_NAME, "calibration_piece_storage", DB_VK_MAX_PIECES_PER_FRAME,
        sizeof(db_vk_present_piece_t), DB_CACHELINE_ALIGNMENT_BYTES);
    g_state.calibration.assignment_storage = db_calloc_or_fail(
        BACKEND_NAME, "calibration_assignment_storage",
        DB_VK_MAX_PIECES_PER_FRAME, sizeof(db_vk_lane_assignment_t),
        DB_CACHELINE_ALIGNMENT_BYTES);
    db_vk_calibration_state_open(&g_state.calibration.state);
}

static void vk_calibration_seed_targets(VkCommandBuffer command_buffer) {
    VkImageMemoryBarrier barriers[3] = {0};
    barriers[0] = (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = g_state.backing.targets[0].image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1U,
                             .layerCount = 1U},
    };
    for (uint32_t index = 0U; index < 2U; index++) {
        barriers[index + 1U] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = g_state.calibration.targets[index].layout_initialized
                             ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                             : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = g_state.calibration.targets[index].image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1U,
                                 .layerCount = 1U},
        };
    }
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL,
                         3U, barriers);
    const VkImageCopy copy = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1U},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1U},
        .extent = {.width = g_state.backing.extent.width,
                   .height = g_state.backing.extent.height,
                   .depth = 1U},
    };
    for (uint32_t index = 0U; index < 2U; index++) {
        vkCmdCopyImage(command_buffer, g_state.backing.targets[0].image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       g_state.calibration.targets[index].image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
    }
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for (uint32_t index = 0U; index < 2U; index++) {
        barriers[index + 1U].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[index + 1U].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[index + 1U].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[index + 1U].newLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        g_state.calibration.targets[index].layout_initialized = 1;
    }
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0U, NULL, 0U,
                         NULL, 3U, barriers);
}

static void vk_calibration_apply(const db_frame_plan_t *plan,
                                 const db_vk_execution_plan_t *execution_plan,
                                 VkCommandBuffer command_buffer,
                                 VkBackingTargetState *target, int candidate) {
    const VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_state.backing.render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = {.extent = g_state.backing.extent},
    };
    vkCmdBeginRenderPass(command_buffer, &render_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    const size_t draw_count = db_vk_frame_rect_count(plan);
    db_vk_frame_rect_iterator_t rect_iterator = {0};
    db_vk_frame_rect_iterator_begin(&rect_iterator, plan);
    size_t next_rect_index = 0U;
    for (size_t index = 0U; index < execution_plan->assignment_count; index++) {
        const db_vk_lane_assignment_t *const assignment =
            &execution_plan->assignments[index];
        if ((candidate != 0) && (assignment->lane != 0U)) {
            continue;
        }
        if ((assignment->piece_first >= execution_plan->piece_count) ||
            (execution_plan->pieces[assignment->piece_first].instance_first >=
             draw_count)) {
            continue;
        }
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[assignment->piece_first];
        for (uint32_t instance = 0U; instance < piece->instance_count;
             instance++) {
            db_render_ir_fill_t fill = {0};
            const size_t target_index =
                (size_t)piece->instance_first + instance;
            if (db_vk_frame_rect_iterator_advance_to(
                    &rect_iterator, &next_rect_index, target_index, &fill) ==
                0) {
                continue;
            }
            float color[3] = {0};
            db_rgb_f64_quantize_f16_to_f32_rgb3(fill.color.rgba, color);
            const VkClearAttachment attachment = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0U,
                .clearValue = {.color = {.float32 = {color[0], color[1],
                                                     color[2], 1.0F}}},
            };
            const VkClearRect rect = {
                .rect = {.offset = {.x = fill.rect.x, .y = fill.rect.y},
                         .extent = {.width = (uint32_t)fill.rect.width,
                                    .height = (uint32_t)fill.rect.height}},
                .layerCount = 1U,
            };
            vkCmdClearAttachments(command_buffer, 1U, &attachment, 1U, &rect);
        }
    }
    vkCmdEndRenderPass(command_buffer);
}

void db_vk_calibration_run_after_live(const db_frame_plan_t *plan) {
    if ((plan == NULL) || (g_state.device.selection.active_lane_count <= 1U) ||
        (g_state.device.selection.execution_mode !=
         DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES)) {
        return;
    }
    vk_calibration_initialize();
    const VkResult presentation_ready = db_vk_wait_fence(
        g_state.device.device, g_state.presentation.in_flight,
        DB_PROGRESS_VK_CALIBRATION_READY, "calibration_live_frame_ready");
    if (presentation_ready != VK_SUCCESS) {
        return;
    }
    db_vk_execution_plan_t execution_plan = {0};
    const uint32_t worker_share_bps =
        (g_state.calibration.split_search.complete != 0)
            ? g_state.calibration.split_search.selected_share_bps
            : db_vk_split_search_next_share(&g_state.calibration.split_search);
    if (db_vk_build_execution_plan_with_worker_share(
            plan, &g_state.scheduler.planner_workspace,
            g_state.device.selection.active_lane_count,
            DB_VK_SCHEDULING_STABLE_ROWS, worker_share_bps,
            g_state.scheduler.ema_ms_per_work_unit,
            g_state.scheduler.scheduling_epoch,
            g_state.scheduler.content_generation,
            g_state.calibration.piece_storage, DB_VK_MAX_PIECES_PER_FRAME,
            g_state.calibration.assignment_storage, DB_VK_MAX_PIECES_PER_FRAME,
            &execution_plan) == 0) {
        return;
    }
    VkSemaphore waits[MAX_GPU_COUNT] = {0};
    VkPipelineStageFlags wait_stages[MAX_GPU_COUNT] = {0};
    VkSemaphore signals[MAX_GPU_COUNT] = {0};
    const uint64_t candidate_start = db_now_ns_monotonic();
    const uint32_t worker_count = db_vk_independent_lanes_submit(
        plan, &execution_plan, waits, wait_stages, signals);
    if (worker_count == 0U) {
        db_vk_independent_lane_quarantine(1U, "calibration_worker_unavailable");
        return;
    }
    DB_VK_CHECK(BACKEND_NAME, vkResetFences(g_state.device.device, 1U,
                                            &g_state.calibration.fence));
    DB_VK_CHECK(BACKEND_NAME,
                vkResetCommandBuffer(g_state.calibration.command_buffer, 0U));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    DB_VK_CHECK(
        BACKEND_NAME,
        vkBeginCommandBuffer(g_state.calibration.command_buffer, &begin_info));
    vk_calibration_seed_targets(g_state.calibration.command_buffer);
    vk_calibration_apply(plan, &execution_plan,
                         g_state.calibration.command_buffer,
                         &g_state.calibration.targets[0], 0);
    vk_calibration_apply(plan, &execution_plan,
                         g_state.calibration.command_buffer,
                         &g_state.calibration.targets[1], 1);
    db_vk_independent_lanes_record_composition(
        g_state.calibration.command_buffer, &execution_plan,
        &g_state.calibration.targets[1]);
    DB_VK_CHECK(BACKEND_NAME,
                vkEndCommandBuffer(g_state.calibration.command_buffer));
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = worker_count,
        .pWaitSemaphores = waits,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1U,
        .pCommandBuffers = &g_state.calibration.command_buffer,
        .signalSemaphoreCount = worker_count,
        .pSignalSemaphores = signals,
    };
    DB_VK_CHECK(BACKEND_NAME, vkQueueSubmit(g_state.device.queue, 1U, &submit,
                                            g_state.calibration.fence));
    const VkResult candidate_ready = db_vk_wait_fence(
        g_state.device.device, g_state.calibration.fence,
        DB_PROGRESS_VK_CANDIDATE_COMPLETE, "calibration_candidate_complete");
    if (candidate_ready != VK_SUCCESS) {
        db_vk_independent_lane_quarantine(1U, "calibration_timeout");
        return;
    }
    db_vk_independent_lanes_export_reusable();
    const uint64_t candidate_ns = db_now_ns_monotonic() - candidate_start;
    const double candidate_ms = DB_TO_F64(candidate_ns) / DB_NS_PER_MS;
    uint64_t primary_deviation_ns = 0U;
    uint64_t worker_deviation_ns = 0U;
    const int calibrated = DB_BOOL(
        (g_state.calibration.calibrated_timestamps_enabled != 0) &&
        vk_calibration_capture_deviation(
            g_state.device.device, g_state.calibration.calibrated_host_domain,
            &primary_deviation_ns) &&
        vk_calibration_capture_deviation(
            g_state.scheduler.independent_lanes[1].device,
            g_state.calibration.calibrated_host_domain, &worker_deviation_ns));
    const uint64_t uncertainty_ns =
        db_checked_add_u64(BACKEND_NAME, "calibration_uncertainty_ns",
                           primary_deviation_ns, worker_deviation_ns);
    const uint64_t worker_gpu_ns = db_vk_independent_lane_timing_ns(1U);
    const uint64_t handoff_ns =
        db_u64_saturating_sub(candidate_ns, worker_gpu_ns);
    const uint64_t reference_hash = db_vk_compute_output_hash_from_image(
        g_state.calibration.targets[0].image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, g_state.backing.extent);
    const uint64_t candidate_hash = db_vk_compute_output_hash_from_image(
        g_state.calibration.targets[1].image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, g_state.backing.extent);
    const uint64_t live_hash = db_vk_compute_output_hash_from_image(
        g_state.backing.targets[0].image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_state.backing.extent);
    const int hashes_match = DB_BOOL((reference_hash == candidate_hash) &&
                                     (candidate_hash == live_hash));
    const db_log_field_t fields[] = {
        DB_LOG_U64("frame", plan->frame_index),
        DB_LOG_HEX64("reference_hash", reference_hash),
        DB_LOG_HEX64("candidate_hash", candidate_hash),
        DB_LOG_HEX64("live_hash", live_hash),
        DB_LOG_BOOL("match", hashes_match),
        DB_LOG_DOUBLE("candidate_ms", candidate_ms),
    };
    if (db_vk_trace_level() >= 2) {
        db_log_info(BACKEND_NAME, "vk_calibration_pair", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    if (hashes_match == 0) {
        db_vk_independent_lane_quarantine(1U, "determinism_mismatch");
        return;
    }
    if (g_state.calibration.correctness_passed == 0) {
        g_state.calibration.correctness_passed = 1;
        return;
    }
    if (g_state.calibration.split_search.complete == 0) {
        const db_vk_split_sample_t split_sample = {
            .host_critical_path_ns = candidate_ns,
            .worker_gpu_ns = worker_gpu_ns,
            .handoff_ns = handoff_ns,
            .uncertainty_ns = uncertainty_ns,
            .calibrated = calibrated,
            .valid = (calibrated == 0) ? 1
                                       : db_vk_timestamp_deviation_acceptable(
                                             uncertainty_ns, candidate_ns),
        };
        db_vk_split_search_record(&g_state.calibration.split_search,
                                  &split_sample);
        const db_log_field_t split_fields[] = {
            DB_LOG_U64("sample", g_state.calibration.split_search.sample_count),
            DB_LOG_U64("worker_share_bps", worker_share_bps),
            DB_LOG_DOUBLE("candidate_ms", candidate_ms),
            DB_LOG_BOOL("calibrated", calibrated),
            DB_LOG_U64("uncertainty_ns", uncertainty_ns),
            DB_LOG_U64("worker_gpu_ns", worker_gpu_ns),
            DB_LOG_U64("handoff_ns", handoff_ns),
            DB_LOG_BOOL("valid", split_sample.valid),
            DB_LOG_BOOL("complete", g_state.calibration.split_search.complete),
        };
        if (db_vk_trace_level() >= 2) {
            db_log_info(BACKEND_NAME, "vk_split_search_sample", split_fields,
                        DB_LOG_FIELD_COUNT(split_fields));
        }
        if (g_state.calibration.split_search.complete != 0) {
            g_state.scheduler.worker_share_bps =
                g_state.calibration.split_search.selected_share_bps;
            g_state.scheduler.scheduling_epoch =
                db_checked_add_u32(BACKEND_NAME, "scheduling_epoch",
                                   g_state.scheduler.scheduling_epoch, 1U);
            const db_log_field_t selected_fields[] = {
                DB_LOG_U64("worker_share_bps",
                           g_state.scheduler.worker_share_bps),
                DB_LOG_U64("scheduling_epoch",
                           g_state.scheduler.scheduling_epoch),
            };
            if (db_vk_trace_level() >= 1) {
                db_log_info(BACKEND_NAME, "vk_split_selected", selected_fields,
                            DB_LOG_FIELD_COUNT(selected_fields));
            }
        }
        return;
    }
    const db_vk_calibration_pair_t pair = {
        .primary_ms = (g_state.metrics.frame_time_ema_ms > 0.0)
                          ? g_state.metrics.frame_time_ema_ms
                          : candidate_ms,
        .candidate_ms = candidate_ms,
        .primary_state_hash = plan->expected_state_hash,
        .candidate_state_hash = plan->expected_state_hash,
        .primary_working_hash = reference_hash,
        .candidate_working_hash = candidate_hash,
        .primary_uncertainty_ns = primary_deviation_ns,
        .candidate_uncertainty_ns = uncertainty_ns,
        .calibrated = calibrated,
    };
    const db_vk_multi_gpu_phase_t previous_phase =
        g_state.calibration.state.phase;
    db_vk_calibration_state_record(&g_state.calibration.state, &pair);
    if (g_state.calibration.state.phase != previous_phase) {
        const db_log_field_t phase_fields[] = {
            DB_LOG_TOKEN("from", db_vk_multi_gpu_phase_name(previous_phase)),
            DB_LOG_TOKEN("to", db_vk_multi_gpu_phase_name(
                                   g_state.calibration.state.phase)),
            DB_LOG_U64("warmup_count", g_state.calibration.state.warmup_count),
            DB_LOG_U64("pair_count", g_state.calibration.state.pair_count),
            DB_LOG_BOOL("hashes_match",
                        g_state.calibration.state.result.hashes_match),
            DB_LOG_DOUBLE("median_improvement",
                          g_state.calibration.state.result.median_improvement),
            DB_LOG_DOUBLE("primary_p95_ms",
                          g_state.calibration.state.result.primary_p95_ms),
            DB_LOG_DOUBLE("candidate_p95_ms",
                          g_state.calibration.state.result.candidate_p95_ms),
            DB_LOG_TOKEN("reason", (g_state.calibration.state.phase ==
                                    DB_VK_MULTI_GPU_CALIBRATING)
                                       ? "warmup_complete"
                                       : ((g_state.calibration.state.phase ==
                                           DB_VK_MULTI_GPU_ACTIVE)
                                              ? "measured_benefit"
                                              : "measured_no_benefit")),
        };
        db_log_info(BACKEND_NAME, "vk_multi_gpu_phase", phase_fields,
                    DB_LOG_FIELD_COUNT(phase_fields));
    }
}

void db_vk_calibration_shutdown(void) {
    if (g_state.calibration.fence != VK_NULL_HANDLE) {
        vkDestroyFence(g_state.device.device, g_state.calibration.fence, NULL);
    }
    for (uint32_t index = 0U; index < 2U; index++) {
        db_vk_destroy_backing_target(g_state.device.device,
                                     &g_state.calibration.targets[index]);
    }
    free(g_state.calibration.piece_storage);
    free(g_state.calibration.assignment_storage);
}
