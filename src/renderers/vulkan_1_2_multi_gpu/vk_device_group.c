#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_state_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "core/db_render_types.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

int db_vk_device_group_peer_read_usable(VkPeerMemoryFeatureFlags features) {
    return DB_BOOL((features & VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT) != 0U);
}

void db_vk_device_group_lanes_init(void) {
    if (g_state.device.selection.execution_mode !=
        DB_VK_EXECUTION_MODE_DEVICE_GROUP) {
        return;
    }
    const uint32_t primary_device_index = db_checked_int_to_u32(
        BACKEND_NAME, "primary_group_lane_index",
        g_state.device.selection.lanes[0].group_lane_index);
    uint32_t active_count = 0U;
    for (uint32_t lane_index = 0U;
         lane_index < g_state.device.selection.active_lane_count;
         lane_index++) {
        db_vk_device_lane_t *const lane =
            &g_state.device.selection.lanes[lane_index];
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        const uint32_t remote_device_index = db_checked_int_to_u32(
            BACKEND_NAME, "remote_group_lane_index", lane->group_lane_index);
        runtime->device = g_state.device.device;
        runtime->phys = lane->phys;
        runtime->queue = g_state.device.queue;
        runtime->queue_family_index = lane->queue_family_index;
        runtime->active = 1;
        runtime->initialized = 1;
        runtime->scheduling_epoch = g_state.scheduler.scheduling_epoch;
        for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
             slot_index++) {
            db_vk_lane_slot_t *const slot = &runtime->slots[slot_index];
            db_vk_create_backing_target(
                g_state.device.present_phys, g_state.device.device,
                g_state.backing.format, g_state.backing.extent,
                g_state.backing.render_pass, lane->device_mask,
                &slot->worker_target);
            VkPeerMemoryFeatureFlags peer_features = 0U;
            vkGetDeviceGroupPeerMemoryFeatures(
                g_state.device.device, slot->worker_target.memory_heap_index,
                primary_device_index, remote_device_index, &peer_features);
            if ((lane_index != 0U) &&
                (db_vk_device_group_peer_read_usable(peer_features) == 0)) {
                runtime->active = 0;
                lane->active_for_scheduler = 0;
                db_vk_set_lane_reason(lane, "device_group_peer_read_missing");
                break;
            }
            slot->primary_descriptor_set =
                g_state.pipelines.lane_descriptor_sets[lane_index][slot_index];
            db_vk_update_backing_descriptor(
                g_state.device.device, slot->primary_descriptor_set,
                g_state.backing.sampler, slot->worker_target.view);
            slot->slot_generation = 1U;
            slot->phase = DB_VK_EXTERNAL_SLOT_REUSABLE;
        }
        if (runtime->active != 0) {
            active_count++;
        }
        const db_log_field_t fields[] = {
            DB_LOG_U64("lane", lane_index),
            DB_LOG_U64("device_index", remote_device_index),
            DB_LOG_BOOL("active", runtime->active),
            DB_LOG_STRING("reason", lane->inactive_reason),
        };
        db_log_info(BACKEND_NAME, "vk_device_group_lane_target", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    g_state.device.selection.active_lane_count = DB_MAX(active_count, 1U);
    g_state.device.gpu_count = g_state.device.selection.active_lane_count;
}

void db_vk_device_group_lanes_shutdown(void) {
    if (g_state.device.selection.execution_mode !=
        DB_VK_EXECUTION_MODE_DEVICE_GROUP) {
        return;
    }
    for (uint32_t lane_index = 0U;
         lane_index < g_state.device.selection.lane_count; lane_index++) {
        db_vk_independent_lane_runtime_t *const runtime =
            &g_state.scheduler.independent_lanes[lane_index];
        for (uint32_t slot_index = 0U; slot_index < DB_VK_LANE_SLOT_COUNT;
             slot_index++) {
            db_vk_destroy_backing_target(
                g_state.device.device,
                &runtime->slots[slot_index].worker_target);
        }
        *runtime = (db_vk_independent_lane_runtime_t){0};
    }
}

static void vk_group_transition_slot(VkCommandBuffer command_buffer,
                                     db_vk_lane_slot_t *slot,
                                     uint32_t device_mask) {
    vkCmdSetDeviceMask(command_buffer, device_mask);
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask =
            (slot->initialized != 0) ? VK_ACCESS_SHADER_READ_BIT : 0U,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = (slot->initialized != 0)
                         ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = slot->worker_target.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1U,
                             .layerCount = 1U},
    };
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0U, 0U,
                         NULL, 0U, NULL, 1U, &barrier);
}

static uint32_t
vk_group_render_lane(const db_frame_plan_t *plan,
                     const db_vk_execution_plan_t *execution_plan,
                     VkCommandBuffer command_buffer, uint32_t lane_index,
                     uint32_t slot_index, uint32_t *frame_work_units,
                     uint8_t *frame_owner_used, uint8_t *frame_owner_finished) {
    db_vk_independent_lane_runtime_t *const runtime =
        &g_state.scheduler.independent_lanes[lane_index];
    if ((runtime->active == 0) || (runtime->initialized == 0)) {
        return 0U;
    }
    db_vk_lane_slot_t *const slot = &runtime->slots[slot_index];
    const uint32_t device_mask =
        g_state.device.selection.lanes[lane_index].device_mask;
    const int seed_slot = DB_BOOL(slot->initialized == 0);
    vk_group_transition_slot(command_buffer, slot, device_mask);
    const VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_state.backing.render_pass,
        .framebuffer = slot->worker_target.framebuffer,
        .renderArea = {.extent = g_state.backing.extent},
    };
    vkCmdBeginRenderPass(command_buffer, &render_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    if (seed_slot != 0) {
        const VkClearValue background =
            db_vk_clear_value_from_rgba_f64(g_state.runtime.seed_rgba_f64);
        const VkClearAttachment attachment = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0U,
            .clearValue = background,
        };
        const VkClearRect clear_rect = {
            .rect = {.extent = g_state.backing.extent},
            .layerCount = 1U,
        };
        vkCmdClearAttachments(command_buffer, 1U, &attachment, 1U, &clear_rect);
    }
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      g_state.pipelines.pipeline);
    VkDeviceSize offset = 0U;
    vkCmdBindVertexBuffers(command_buffer, 0U, 1U,
                           &g_state.pipelines.vertex_buffer, &offset);
    const VkViewport viewport = {
        .width = db_u32_to_f32(g_state.backing.extent.width),
        .height = db_u32_to_f32(g_state.backing.extent.height),
        .maxDepth = 1.0F,
    };
    vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
    const db_colored_f64_block_view_t geometry =
        (plan->rebuild_seed.kind == DB_FRAME_REBUILD_SEED_GEOMETRY)
            ? plan->rebuild_seed.geometry
            : plan->geometry.current_blocks;
    uint32_t lane_work = 0U;
    uint32_t lane_tiles[DB_VK_MAX_LANES] = {0};
    uint32_t total_tiles = 0U;
    uint8_t owner_enabled[DB_VK_MAX_LANES] = {0};
    owner_enabled[lane_index] = 1U;
    db_vk_draw_payload_cache_t cache = {0};
    const db_vk_owner_draw_ctx_t draw_context = {
        .cmd = command_buffer,
        .layout = g_state.pipelines.pipeline_layout,
        .extent = g_state.backing.extent,
        .have_group = 1,
        .active_gpu_count =
            execution_plan->policy == DB_VK_SCHEDULING_PRIMARY_ONLY
                ? 1U
                : g_state.device.selection.active_lane_count,
        .timing_enabled = g_state.device.gpu_timing_enabled,
        .timing_query_pool = g_state.device.timing_query_pool,
        .frame_owner_used = frame_owner_used,
        .frame_owner_finished = frame_owner_finished,
        .frame_work_units = frame_work_units,
        .grid_tiles_per_gpu = lane_tiles,
        .grid_tiles_drawn = &total_tiles,
        .owner_enabled = owner_enabled,
        .grid_rows = plan->grid_rows,
        .grid_cols = plan->grid_cols,
        .payload_cache = &cache,
    };
    for (size_t assignment_index = 0U;
         assignment_index < execution_plan->assignment_count;
         assignment_index++) {
        const db_vk_lane_assignment_t *const assignment =
            &execution_plan->assignments[assignment_index];
        if ((assignment->lane != lane_index) ||
            (assignment->piece_first >= execution_plan->piece_count)) {
            continue;
        }
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[assignment->piece_first];
        if (piece->geometry_first >= geometry.count) {
            continue;
        }
        const db_colored_f64_block_t *const block =
            &geometry.blocks[piece->geometry_first];
        float color[3] = {0};
        db_rgb_f64_quantize_f16_to_f32_rgb3(block->rgb, color);
        const db_grid_block_t grid_block = {
            .row_start = block->row_start,
            .row_count = block->row_count,
            .col_start = block->col_start,
            .col_count = block->col_count,
        };
        const uint32_t work = db_grid_block_span_units_or_fail(
            "device_group_piece_work", &grid_block);
        db_vk_draw_owner_grid_row_block(
            &draw_context, &(const db_vk_grid_row_block_draw_req_t){
                               .span_units = work,
                               .owner = lane_index,
                               .block = grid_block,
                               .payload = {.color = color},
                           });
        lane_work += work;
    }
    vkCmdEndRenderPass(command_buffer);
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = slot->worker_target.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1U,
                             .layerCount = 1U},
    };
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, NULL,
                         0U, NULL, 1U, &barrier);
    slot->initialized = 1;
    slot->content_generation = execution_plan->content_generation;
    slot->scheduling_epoch = execution_plan->scheduling_epoch;
    slot->last_applied_frame = plan->frame_index;
    slot->valid_piece_count = (uint32_t)DB_BOOL(lane_work > 0U);
    return lane_work;
}

static void vk_group_compose(const db_frame_plan_t *plan,
                             const db_vk_execution_plan_t *execution_plan,
                             VkCommandBuffer command_buffer) {
    const uint32_t primary_mask = g_state.device.selection.lanes[0].device_mask;
    vkCmdSetDeviceMask(command_buffer, primary_mask);
    const VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_state.backing.render_pass,
        .framebuffer = g_state.backing.targets[0].framebuffer,
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
    for (size_t index = 0U; index < execution_plan->assignment_count; index++) {
        const db_vk_lane_assignment_t *const assignment =
            &execution_plan->assignments[index];
        if ((assignment->piece_first >= execution_plan->piece_count) ||
            (assignment->lane >= g_state.device.selection.active_lane_count)) {
            continue;
        }
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[assignment->piece_first];
        const db_vk_lane_slot_t *const slot =
            &g_state.scheduler.independent_lanes[assignment->lane]
                 .slots[plan->frame_index % DB_VK_LANE_SLOT_COUNT];
        if (db_vk_slot_result_is_current(slot, execution_plan,
                                         plan->frame_index) == 0) {
            continue;
        }
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_state.pipelines.pipeline_layout, 0U, 1U,
                                &slot->primary_descriptor_set, 0U, NULL);
        vkCmdSetScissor(command_buffer, 0U, 1U, &piece->destination_rect);
        vkCmdDraw(command_buffer, 3U, 1U, 0U, 0U);
    }
    vkCmdEndRenderPass(command_buffer);
}

uint32_t db_vk_device_group_record(const db_frame_plan_t *plan,
                                   const db_vk_execution_plan_t *execution_plan,
                                   VkCommandBuffer command_buffer,
                                   uint32_t *frame_work_units,
                                   uint8_t *frame_owner_used,
                                   uint8_t *frame_owner_finished) {
    if ((plan == NULL) || (execution_plan == NULL) ||
        (command_buffer == VK_NULL_HANDLE)) {
        return 0U;
    }
    uint32_t total_work = 0U;
    for (uint32_t lane = 0U; lane < g_state.device.selection.active_lane_count;
         lane++) {
        for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
            total_work += vk_group_render_lane(
                plan, execution_plan, command_buffer, lane, slot,
                frame_work_units, frame_owner_used, frame_owner_finished);
        }
    }
    vk_group_compose(plan, execution_plan, command_buffer);
    return total_work;
}
