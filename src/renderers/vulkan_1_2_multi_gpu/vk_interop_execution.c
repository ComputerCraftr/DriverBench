#include "vk_diagnostics.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_frame_plan.h"
#include "../../core/db_render_ir.h"

#ifdef __linux__
#include "core/db_log.h"
#include "core/db_poll_policy.h"
#include "core/db_render_types.h"
#include "vk_runtime_internal.h"
#include "vk_state_internal.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

#include "../../core/db_core.h"
#include "../../core/db_geometry.h"
#include "../../core/db_numeric.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
static uint32_t vk_buffer_row_words(uint32_t words_per_pixel) {
    return db_checked_mul_u32(BACKEND_NAME, "buffer_row_words",
                              g_state.backing.extent.width, words_per_pixel);
}

static uint32_t vk_buffer_word_offset(uint32_t source_x, uint32_t source_y,
                                      uint32_t words_per_pixel) {
    const uint32_t row_offset =
        db_checked_mul_u32(BACKEND_NAME, "buffer_row_offset", source_y,
                           g_state.backing.extent.width);
    const uint32_t pixel_offset = db_checked_add_u32(
        BACKEND_NAME, "buffer_pixel_offset", row_offset, source_x);
    return db_checked_mul_u32(BACKEND_NAME, "buffer_word_offset", pixel_offset,
                              words_per_pixel);
}

#define DB_VK_EXTERNAL_SEMAPHORE_HANDLE                                        \
    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
#define DB_VK_TRANSPORT_PACK_WORKGROUP_SIZE 16U
#include <unistd.h>

static uint32_t vk_external_queue_family(const db_vk_lane_slot_t *slot) {
    return ((slot != NULL) &&
            (slot->ownership_domain == DB_VK_EXTERNAL_OWNERSHIP_FOREIGN))
               ? VK_QUEUE_FAMILY_FOREIGN_EXT
               : VK_QUEUE_FAMILY_EXTERNAL;
}

static int vk_transfer_sync_fd(VkDevice exporting_device,
                               VkSemaphore exporting_semaphore,
                               VkDevice importing_device,
                               VkSemaphore importing_semaphore) {
    union {
        PFN_vkVoidFunction generic;
        PFN_vkGetSemaphoreFdKHR typed;
    } get_fd = {.generic = vkGetDeviceProcAddr(exporting_device,
                                               "vkGetSemaphoreFdKHR")};
    union {
        PFN_vkVoidFunction generic;
        PFN_vkImportSemaphoreFdKHR typed;
    } import_fd = {.generic = vkGetDeviceProcAddr(importing_device,
                                                  "vkImportSemaphoreFdKHR")};
    if ((get_fd.typed == NULL) || (import_fd.typed == NULL)) {
        return 0;
    }
    int fd = -1;
    const VkSemaphoreGetFdInfoKHR get_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = exporting_semaphore,
        .handleType = DB_VK_EXTERNAL_SEMAPHORE_HANDLE,
    };
    if ((get_fd.typed(exporting_device, &get_info, &fd) != VK_SUCCESS) ||
        (fd < 0)) {
        return 0;
    }
    const VkImportSemaphoreFdInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = importing_semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = DB_VK_EXTERNAL_SEMAPHORE_HANDLE,
        .fd = fd,
    };
    if (import_fd.typed(importing_device, &import_info) != VK_SUCCESS) {
        (void)close(fd);
        return 0;
    }
    return 1;
}
#endif

uint32_t db_vk_independent_lanes_submit(
    const db_frame_plan_t *plan, const db_vk_execution_plan_t *execution_plan,
    VkSemaphore *primary_wait_semaphores,
    // The Linux branch writes this array; non-Linux builds compile only the
    // no-op stub, which makes clang-tidy incorrectly infer pointer-to-const.
    VkPipelineStageFlags
        *primary_wait_stages, // NOLINT(readability-non-const-parameter)
    VkSemaphore *primary_signal_semaphores) {
#ifdef __linux__
    if ((plan == NULL) || (execution_plan == NULL) ||
        (primary_wait_semaphores == NULL) || (primary_wait_stages == NULL) ||
        (primary_signal_semaphores == NULL)) {
        return 0U;
    }
    uint32_t submitted = 0U;
    for (uint32_t lane_index = 1U;
         (lane_index < g_state.device.selection.lane_count) &&
         (lane_index < g_state.device.selection.active_lane_count);
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->initialized == 0) || (runtime->active == 0)) {
            continue;
        }
        runtime->active_slot = plan->frame_index % DB_VK_LANE_SLOT_COUNT;
        const VkResult reuse_wait = db_vk_wait_fence(
            runtime->device, runtime->fence, DB_PROGRESS_VK_WORKER_SLOT_REUSE,
            "worker_slot_reuse");
        if (reuse_wait == VK_TIMEOUT) {
            db_vk_independent_lane_quarantine(lane_index, "slot_reuse_timeout");
            continue;
        }
        if (reuse_wait != VK_SUCCESS) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "worker_fence_wait_failed");
            continue;
        }
        db_vk_lane_slot_t *const selected_slot =
            &runtime->slots[runtime->active_slot];
        if (selected_slot->ready_sync_state == DB_VK_SYNC_WAIT_SUBMITTED) {
            selected_slot->ready_sync_state = DB_VK_SYNC_PAYLOAD_CONSUMED;
        }
        if (selected_slot->reusable_sync_state == DB_VK_SYNC_FD_IMPORTED) {
            selected_slot->reusable_sync_state = DB_VK_SYNC_PAYLOAD_CONSUMED;
        }
        (void)vkResetFences(runtime->device, 1U, &runtime->fence);
        (void)vkResetCommandBuffer(runtime->command_buffer, 0U);
        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(runtime->command_buffer, &begin_info) !=
            VK_SUCCESS) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "worker_command_begin_failed");
            continue;
        }
        vkCmdResetQueryPool(runtime->command_buffer, runtime->timing_query_pool,
                            0U, 2U);
        vkCmdWriteTimestamp(runtime->command_buffer,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            runtime->timing_query_pool, 0U);
        VkSemaphore worker_waits[DB_VK_LANE_SLOT_COUNT] = {0};
        VkPipelineStageFlags worker_wait_stages[DB_VK_LANE_SLOT_COUNT] = {0};
        uint32_t worker_wait_count = 0U;
        for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
             slot_index++) {
            db_vk_lane_slot_t *const slot = &runtime->slots[slot_index];
            const int reacquire =
                DB_BOOL(slot->phase == DB_VK_EXTERNAL_SLOT_REUSABLE);
            if (reacquire != 0) {
                worker_waits[worker_wait_count] = slot->worker_reusable;
                worker_wait_stages[worker_wait_count++] =
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (slot->initialized != 0) {
                old_layout = (reacquire != 0)
                                 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            uint32_t source_queue_family = VK_QUEUE_FAMILY_IGNORED;
            if ((reacquire != 0) &&
                (runtime->transport != DB_VK_TRANSPORT_DMA_BUF_BUFFER)) {
                source_queue_family = vk_external_queue_family(slot);
            }
            const VkImageMemoryBarrier to_color = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask =
                    (reacquire != 0) ? VK_ACCESS_SHADER_READ_BIT : 0U,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = old_layout,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = source_queue_family,
                .dstQueueFamilyIndex = (reacquire != 0)
                                           ? runtime->queue_family_index
                                           : VK_QUEUE_FAMILY_IGNORED,
                .image = slot->worker_target.image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .levelCount = 1U,
                                     .layerCount = 1U},
            };
            vkCmdPipelineBarrier(runtime->command_buffer,
                                 (reacquire != 0)
                                     ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                     : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0U, 0U, NULL, 0U, NULL, 1U, &to_color);
            const VkRenderPassBeginInfo render_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = runtime->render_pass,
                .framebuffer = slot->worker_target.framebuffer,
                .renderArea = {.extent = g_state.backing.extent},
            };
            vkCmdBeginRenderPass(runtime->command_buffer, &render_info,
                                 VK_SUBPASS_CONTENTS_INLINE);
            if (slot->initialized == 0) {
                const VkClearValue background = db_vk_clear_value_from_rgba_f64(
                    g_state.runtime.seed_rgba_f64);
                const VkClearAttachment attachment = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .colorAttachment = 0U,
                    .clearValue = background,
                };
                const VkClearRect rect = {
                    .rect = {.extent = g_state.backing.extent},
                    .layerCount = 1U,
                };
                vkCmdClearAttachments(runtime->command_buffer, 1U, &attachment,
                                      1U, &rect);
            }
            const size_t draw_count = db_vk_frame_rect_count(plan);
            uint32_t first_valid_piece = UINT32_MAX;
            uint32_t valid_piece_count = 0U;
            for (size_t assignment_index = 0U;
                 assignment_index < execution_plan->assignment_count;
                 assignment_index++) {
                const db_vk_lane_assignment_t *const assignment =
                    &execution_plan->assignments[assignment_index];
                if ((assignment->lane != lane_index) ||
                    (assignment->piece_count == 0U) ||
                    (assignment->piece_first >= execution_plan->piece_count)) {
                    continue;
                }
                const db_vk_present_piece_t *const piece =
                    &execution_plan->pieces[assignment->piece_first];
                if (piece->instance_first >= draw_count) {
                    continue;
                }
                for (uint32_t instance = 0U; instance < piece->instance_count;
                     instance++) {
                    db_render_ir_fill_t fill = {0};
                    if (db_vk_frame_rect_at(
                            plan, (size_t)piece->instance_first + instance,
                            &fill) == 0) {
                        continue;
                    }
                    db_grid_block_t grid_block = {0};
                    if (db_render_ir_rect_to_grid_block(
                            fill.rect, g_state.runtime.grid_cols,
                            g_state.runtime.grid_rows, &grid_block) == 0) {
                        DB_RUNTIME_FAIL(
                            BACKEND_NAME,
                            "invalid independent-lane IR rectangle");
                    }
                    db_damage_block_t pixel_block = {0};
                    if (db_grid_block_to_pixel_block(
                            g_state.runtime.grid_cols,
                            g_state.runtime.grid_rows, &grid_block,
                            g_state.backing.extent.width,
                            g_state.backing.extent.height, &pixel_block) == 0) {
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
                        .rect = {.offset = {.x = db_checked_u32_to_i32(
                                                BACKEND_NAME, "clear_rect_x",
                                                pixel_block.col_start),
                                            .y = db_checked_u32_to_i32(
                                                BACKEND_NAME, "clear_rect_y",
                                                pixel_block.row_start)},
                                 .extent = {.width = pixel_block.col_count,
                                            .height = pixel_block.row_count}},
                        .layerCount = 1U,
                    };
                    vkCmdClearAttachments(runtime->command_buffer, 1U,
                                          &attachment, 1U, &rect);
                }
                if (first_valid_piece == UINT32_MAX) {
                    first_valid_piece = assignment->piece_first;
                }
                valid_piece_count += assignment->piece_count;
            }
            vkCmdEndRenderPass(runtime->command_buffer);
            slot->initialized = 1;
            slot->phase = DB_VK_EXTERNAL_SLOT_WORKER_OWNED;
            slot->content_generation = execution_plan->content_generation;
            slot->scheduling_epoch = execution_plan->scheduling_epoch;
            slot->last_applied_frame = plan->frame_index;
            slot->valid_piece_first =
                (first_valid_piece == UINT32_MAX) ? 0U : first_valid_piece;
            slot->valid_piece_count = valid_piece_count;
        }
        db_vk_lane_slot_t *const active_slot =
            &runtime->slots[runtime->active_slot];
        if (runtime->transport == DB_VK_TRANSPORT_DMA_BUF_BUFFER) {
            const VkImageMemoryBarrier image_to_read = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image = active_slot->worker_target.image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .levelCount = 1U,
                                     .layerCount = 1U},
            };
            vkCmdPipelineBarrier(runtime->command_buffer,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U,
                                 NULL, 0U, NULL, 1U, &image_to_read);
            vkCmdBindPipeline(runtime->command_buffer,
                              VK_PIPELINE_BIND_POINT_COMPUTE,
                              runtime->pack_pipeline);
            vkCmdBindDescriptorSets(
                runtime->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                runtime->pack_pipeline_layout, 0U, 1U,
                &active_slot->worker_pack_descriptor_set, 0U, NULL);
            const uint32_t words_per_pixel =
                db_pixel_format_u32_words_per_pixel(
                    g_state.backing.pixel_format);
            if (words_per_pixel == 0U) {
                DB_RUNTIME_FAIL(BACKEND_NAME,
                                "invalid working format for buffer packing");
            }
            for (size_t index = 0U; index < execution_plan->assignment_count;
                 index++) {
                const db_vk_lane_assignment_t *const assignment =
                    &execution_plan->assignments[index];
                if ((assignment->lane != lane_index) ||
                    (assignment->piece_first >= execution_plan->piece_count)) {
                    continue;
                }
                const db_vk_present_piece_t *const piece =
                    &execution_plan->pieces[assignment->piece_first];
                const uint32_t source_x = db_checked_i32_to_u32(
                    BACKEND_NAME, "pack_source_x", piece->source_rect.offset.x);
                const uint32_t source_y = db_checked_i32_to_u32(
                    BACKEND_NAME, "pack_source_y", piece->source_rect.offset.y);
                const db_vk_buffer_push_t push = {
                    .origin = {piece->source_rect.offset.x,
                               piece->source_rect.offset.y},
                    .extent = {piece->source_rect.extent.width,
                               piece->source_rect.extent.height},
                    .row_words = vk_buffer_row_words(words_per_pixel),
                    .word_offset = vk_buffer_word_offset(source_x, source_y,
                                                         words_per_pixel),
                    .rgba16f = DB_BOOL(words_per_pixel == 2U),
                };
                vkCmdPushConstants(
                    runtime->command_buffer, runtime->pack_pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(push), &push);
                vkCmdDispatch(runtime->command_buffer,
                              (piece->source_rect.extent.width +
                               DB_VK_TRANSPORT_PACK_WORKGROUP_SIZE - 1U) /
                                  DB_VK_TRANSPORT_PACK_WORKGROUP_SIZE,
                              (piece->source_rect.extent.height +
                               DB_VK_TRANSPORT_PACK_WORKGROUP_SIZE - 1U) /
                                  DB_VK_TRANSPORT_PACK_WORKGROUP_SIZE,
                              1U);
            }
            const VkBufferMemoryBarrier release_buffer = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = runtime->queue_family_index,
                .dstQueueFamilyIndex = vk_external_queue_family(active_slot),
                .buffer = active_slot->worker_shared_buffer,
                .size = active_slot->shared_buffer_size,
            };
            vkCmdPipelineBarrier(runtime->command_buffer,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U,
                                 NULL, 1U, &release_buffer, 0U, NULL);
        }
        const VkImageMemoryBarrier release = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0U,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = runtime->queue_family_index,
            .dstQueueFamilyIndex = vk_external_queue_family(active_slot),
            .image = active_slot->worker_target.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1U,
                                 .layerCount = 1U},
        };
        if (runtime->transport != DB_VK_TRANSPORT_DMA_BUF_BUFFER) {
            vkCmdPipelineBarrier(runtime->command_buffer,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U,
                                 NULL, 0U, NULL, 1U, &release);
        }
        vkCmdWriteTimestamp(runtime->command_buffer,
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            runtime->timing_query_pool, 1U);
        if (vkEndCommandBuffer(runtime->command_buffer) != VK_SUCCESS) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "worker_command_end_failed");
            continue;
        }
        const VkSubmitInfo submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = worker_wait_count,
            .pWaitSemaphores = worker_waits,
            .pWaitDstStageMask = worker_wait_stages,
            .commandBufferCount = 1U,
            .pCommandBuffers = &runtime->command_buffer,
            .signalSemaphoreCount = 1U,
            .pSignalSemaphores = &active_slot->worker_ready,
        };
        const VkResult submit_result =
            vkQueueSubmit(runtime->queue, 1U, &submit, runtime->fence);
        if (submit_result != VK_SUCCESS) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "worker_submit_failed");
            continue;
        }
        active_slot->ready_sync_state = DB_VK_SYNC_SIGNAL_SUBMITTED;
        active_slot->ready_sync_state = DB_VK_SYNC_FD_EXPORTED;
        if (vk_transfer_sync_fd(runtime->device, active_slot->worker_ready,
                                g_state.device.device,
                                active_slot->primary_ready) == 0) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "ready_sync_fd_failed");
            continue;
        }
        active_slot->ready_sync_state = DB_VK_SYNC_FD_IMPORTED;
        active_slot->phase = DB_VK_EXTERNAL_SLOT_READY;
        primary_wait_semaphores[submitted] = active_slot->primary_ready;
        primary_wait_stages[submitted] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        primary_signal_semaphores[submitted] = active_slot->primary_reusable;
        submitted++;
    }
    return submitted;
#else
    (void)plan;
    (void)execution_plan;
    (void)primary_wait_semaphores;
    (void)primary_wait_stages;
    (void)primary_signal_semaphores;
    return 0U;
#endif
}

uint64_t db_vk_independent_lane_timing_ns(uint32_t lane_index) {
#ifdef __linux__
    if ((lane_index == 0U) || (lane_index >= MAX_GPU_COUNT)) {
        return 0U;
    }
    db_vk_independent_lane_runtime_t *const runtime =
        &g_state.scheduler.independent_lanes[lane_index];
    uint64_t timestamps[2] = {0};
    if ((runtime->timing_query_pool == VK_NULL_HANDLE) ||
        (vkGetQueryPoolResults(runtime->device, runtime->timing_query_pool, 0U,
                               2U, sizeof(timestamps), timestamps,
                               sizeof(uint64_t),
                               VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)) {
        return 0U;
    }
    const uint64_t ticks = db_vk_timestamp_delta(timestamps[0], timestamps[1],
                                                 runtime->timestamp_valid_bits);
    return db_checked_double_to_u64(BACKEND_NAME, "independent_lane_timing_ns",
                                    DB_TO_F64(ticks) *
                                        runtime->timestamp_period_ns);
#else
    (void)lane_index;
    return 0U;
#endif
}

void db_vk_independent_lanes_export_reusable(void) {
#ifdef __linux__
    for (uint32_t lane_index = 1U;
         lane_index < g_state.device.selection.active_lane_count;
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->active == 0) || (runtime->initialized == 0)) {
            continue;
        }
        db_vk_lane_slot_t *const slot = &runtime->slots[runtime->active_slot];
        slot->reusable_sync_state = DB_VK_SYNC_SIGNAL_SUBMITTED;
        slot->reusable_sync_state = DB_VK_SYNC_FD_EXPORTED;
        if (vk_transfer_sync_fd(g_state.device.device, slot->primary_reusable,
                                runtime->device, slot->worker_reusable) == 0) {
            db_vk_independent_lane_quarantine(lane_index,
                                              "reusable_sync_fd_failed");
            continue;
        }
        slot->reusable_sync_state = DB_VK_SYNC_FD_IMPORTED;
    }
#endif
}

void db_vk_independent_lanes_record_composition(
    VkCommandBuffer command_buffer,
    const db_vk_execution_plan_t *execution_plan,
    VkBackingTargetState *destination_target) {
#ifdef __linux__
    if ((command_buffer == VK_NULL_HANDLE) || (execution_plan == NULL) ||
        (destination_target == NULL)) {
        return;
    }
    for (uint32_t lane_index = 1U;
         (lane_index < g_state.device.selection.lane_count) &&
         (lane_index < g_state.device.selection.active_lane_count);
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->initialized == 0) || (runtime->active == 0) ||
            (runtime->transport != DB_VK_TRANSPORT_DMA_BUF_BUFFER)) {
            continue;
        }
        db_vk_lane_slot_t *const slot = &runtime->slots[runtime->active_slot];
        const VkBufferMemoryBarrier acquire = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = vk_external_queue_family(slot),
            .dstQueueFamilyIndex =
                g_state.device.selection.lanes[0].queue_family_index,
            .buffer = slot->primary_shared_buffer,
            .size = slot->shared_buffer_size,
        };
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U,
                             NULL, 1U, &acquire, 0U, NULL);
        const VkRenderPassBeginInfo render_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = g_state.backing.render_pass,
            .framebuffer = destination_target->framebuffer,
            .renderArea = {.extent = g_state.backing.extent},
        };
        vkCmdBeginRenderPass(command_buffer, &render_info,
                             VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          runtime->unpack_pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                runtime->unpack_pipeline_layout, 0U, 1U,
                                &slot->primary_unpack_descriptor_set, 0U, NULL);
        const VkViewport viewport = {
            .width = db_u32_to_f32(g_state.backing.extent.width),
            .height = db_u32_to_f32(g_state.backing.extent.height),
            .maxDepth = 1.0F,
        };
        vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
        const uint32_t words_per_pixel =
            db_pixel_format_u32_words_per_pixel(g_state.backing.pixel_format);
        if (words_per_pixel == 0U) {
            DB_RUNTIME_FAIL(BACKEND_NAME,
                            "invalid working format for buffer composition");
        }
        for (size_t index = 0U; index < execution_plan->assignment_count;
             index++) {
            const db_vk_lane_assignment_t *const assignment =
                &execution_plan->assignments[index];
            if ((assignment->lane != lane_index) ||
                (assignment->piece_first >= execution_plan->piece_count)) {
                continue;
            }
            const db_vk_present_piece_t *const piece =
                &execution_plan->pieces[assignment->piece_first];
            const uint32_t source_x = db_checked_i32_to_u32(
                BACKEND_NAME, "unpack_source_x", piece->source_rect.offset.x);
            const uint32_t source_y = db_checked_i32_to_u32(
                BACKEND_NAME, "unpack_source_y", piece->source_rect.offset.y);
            const db_vk_buffer_push_t push = {
                .origin = {piece->destination_rect.offset.x,
                           piece->destination_rect.offset.y},
                .extent = {piece->destination_rect.extent.width,
                           piece->destination_rect.extent.height},
                .row_words = vk_buffer_row_words(words_per_pixel),
                .word_offset =
                    vk_buffer_word_offset(source_x, source_y, words_per_pixel),
                .rgba16f = DB_BOOL(words_per_pixel == 2U),
            };
            vkCmdPushConstants(command_buffer, runtime->unpack_pipeline_layout,
                               VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(push),
                               &push);
            vkCmdSetScissor(command_buffer, 0U, 1U, &piece->destination_rect);
            vkCmdDraw(command_buffer, 3U, 1U, 0U, 0U);
        }
        vkCmdEndRenderPass(command_buffer);
        VkBufferMemoryBarrier release = acquire;
        release.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        release.dstAccessMask = 0U;
        release.srcQueueFamilyIndex =
            g_state.device.selection.lanes[0].queue_family_index;
        release.dstQueueFamilyIndex = vk_external_queue_family(slot);
        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL,
                             1U, &release, 0U, NULL);
        slot->phase = DB_VK_EXTERNAL_SLOT_REUSABLE;
        return;
    }
    VkImageMemoryBarrier acquires[MAX_GPU_COUNT] = {0};
    uint32_t acquire_count = 0U;
    for (uint32_t lane_index = 1U;
         (lane_index < g_state.device.selection.lane_count) &&
         (lane_index < g_state.device.selection.active_lane_count);
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->initialized == 0) || (runtime->active == 0)) {
            continue;
        }
        db_vk_lane_slot_t *const slot = &runtime->slots[runtime->active_slot];
        acquires[acquire_count++] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0U,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = vk_external_queue_family(slot),
            .dstQueueFamilyIndex =
                g_state.device.selection.lanes[0].queue_family_index,
            .image = slot->primary_alias_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1U,
                                 .layerCount = 1U},
        };
    }
    if (acquire_count == 0U) {
        return;
    }
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, NULL,
                         0U, NULL, acquire_count, acquires);
    const VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_state.backing.render_pass,
        .framebuffer = destination_target->framebuffer,
        .renderArea = {.extent = g_state.backing.extent},
    };
    vkCmdBeginRenderPass(command_buffer, &render_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      g_state.pipelines.composition_pipeline);
    const VkViewport viewport = {
        .width = db_u32_to_f32(g_state.backing.extent.width),
        .height = db_u32_to_f32(g_state.backing.extent.height),
        .maxDepth = 1.0F,
    };
    vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
    for (uint32_t lane_index = 1U;
         (lane_index < g_state.device.selection.lane_count) &&
         (lane_index < g_state.device.selection.active_lane_count);
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->initialized == 0) || (runtime->active == 0)) {
            continue;
        }
        db_vk_lane_slot_t *const slot = &runtime->slots[runtime->active_slot];
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_state.pipelines.pipeline_layout, 0U, 1U,
                                &slot->primary_descriptor_set, 0U, NULL);
        for (size_t assignment_index = 0U;
             assignment_index < execution_plan->assignment_count;
             assignment_index++) {
            const db_vk_lane_assignment_t *const assignment =
                &execution_plan->assignments[assignment_index];
            if ((assignment->lane != lane_index) ||
                (assignment->piece_count == 0U) ||
                (assignment->piece_first >= execution_plan->piece_count)) {
                continue;
            }
            const db_vk_present_piece_t *const piece =
                &execution_plan->pieces[assignment->piece_first];
            vkCmdSetScissor(command_buffer, 0U, 1U, &piece->destination_rect);
            vkCmdDraw(command_buffer, 3U, 1U, 0U, 0U);
        }
        slot->phase = DB_VK_EXTERNAL_SLOT_PRIMARY_OWNED;
    }
    vkCmdEndRenderPass(command_buffer);
    VkImageMemoryBarrier releases[MAX_GPU_COUNT] = {0};
    uint32_t release_count = 0U;
    for (uint32_t lane_index = 1U;
         (lane_index < g_state.device.selection.lane_count) &&
         (lane_index < g_state.device.selection.active_lane_count);
         lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if ((runtime->initialized == 0) || (runtime->active == 0)) {
            continue;
        }
        db_vk_lane_slot_t *const slot = &runtime->slots[runtime->active_slot];
        releases[release_count++] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = 0U,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex =
                g_state.device.selection.lanes[0].queue_family_index,
            .dstQueueFamilyIndex = vk_external_queue_family(slot),
            .image = slot->primary_alias_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1U,
                                 .layerCount = 1U},
        };
        slot->phase = DB_VK_EXTERNAL_SLOT_REUSABLE;
    }
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL, 0U,
                         NULL, release_count, releases);
#else
    (void)command_buffer;
    (void)execution_plan;
    (void)destination_target;
#endif
}

void db_vk_independent_lanes_shutdown(void) {
#ifdef __linux__
    for (uint32_t lane_index = 1U; lane_index < MAX_GPU_COUNT; lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        if (runtime->device == VK_NULL_HANDLE) {
            continue;
        }
        if (runtime->fence != VK_NULL_HANDLE) {
            const VkResult wait_result = db_vk_wait_fence(
                runtime->device, runtime->fence,
                DB_PROGRESS_VK_CANDIDATE_COMPLETE, "worker_shutdown");
            if (wait_result != VK_SUCCESS) {
                DB_RUNTIME_FAIL("renderer_vulkan_1_2_multi_gpu",
                                "worker submission did not retire at shutdown");
            }
        }
        if (runtime->transport == DB_VK_TRANSPORT_DMA_BUF_BUFFER) {
            db_vk_buffer_transport_destroy(runtime);
        }
        for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
             slot_index++) {
            db_vk_lane_slot_t *const slot = &runtime->slots[slot_index];
            vkDestroySemaphore(runtime->device, slot->worker_ready, NULL);
            vkDestroySemaphore(runtime->device, slot->worker_reusable, NULL);
            vkDestroySemaphore(g_state.device.device, slot->primary_ready,
                               NULL);
            vkDestroySemaphore(g_state.device.device, slot->primary_reusable,
                               NULL);
            vkDestroyFramebuffer(runtime->device,
                                 slot->worker_target.framebuffer, NULL);
            vkDestroyImageView(runtime->device, slot->worker_target.view, NULL);
            vkDestroyImage(runtime->device, slot->worker_target.image, NULL);
            vkFreeMemory(runtime->device, slot->worker_target.memory, NULL);
            vkDestroyImageView(g_state.device.device, slot->primary_alias_view,
                               NULL);
            vkDestroyImage(g_state.device.device, slot->primary_alias_image,
                           NULL);
            vkFreeMemory(g_state.device.device, slot->primary_alias_memory,
                         NULL);
        }
        vkDestroyFence(runtime->device, runtime->fence, NULL);
        vkDestroyQueryPool(runtime->device, runtime->timing_query_pool, NULL);
        vkDestroyCommandPool(runtime->device, runtime->command_pool, NULL);
        vkDestroyRenderPass(runtime->device, runtime->render_pass, NULL);
        vkDestroyDevice(runtime->device, NULL);
        *runtime = (db_vk_independent_lane_runtime_t){0};
    }
#endif
}
