#include "../../core/db_render_types.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan_core.h>

static VkDeviceSize vk_align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1U) {
        return value;
    }
    const VkDeviceSize remainder = value % alignment;
    return (remainder == 0U) ? value : value + (alignment - remainder);
}

int db_vk_build_shared_buffer_plan(const db_vk_execution_plan_t *execution_plan,
                                   db_pixel_format_t format,
                                   VkDeviceSize segment_base_alignment,
                                   VkDeviceSize max_segment_range,
                                   db_vk_shared_piece_layout_t *layouts,
                                   size_t layout_capacity,
                                   db_vk_shared_buffer_plan_t *out_plan) {
    if ((execution_plan == NULL) || (layouts == NULL) || (out_plan == NULL) ||
        (max_segment_range == 0U)) {
        return 0;
    }
    *out_plan = (db_vk_shared_buffer_plan_t){0};
    const VkDeviceSize pixel_size =
        (format == DB_PIXEL_FORMAT_RGBA16F) ? 8U : 4U;
    const VkDeviceSize base_alignment =
        (segment_base_alignment > 4U) ? segment_base_alignment : 4U;
    uint32_t segment = 0U;
    VkDeviceSize segment_used = 0U;
    VkDeviceSize segment_base = 0U;
    for (size_t index = 0U; index < execution_plan->piece_count; index++) {
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[index];
        const VkDeviceSize row_pitch =
            (VkDeviceSize)piece->destination_rect.extent.width * pixel_size;
        const VkDeviceSize byte_size =
            row_pitch * (VkDeviceSize)piece->destination_rect.extent.height;
        if (row_pitch > UINT32_MAX) {
            out_plan->rerouted_piece_count++;
            continue;
        }
        VkDeviceSize offset = vk_align_up(segment_used, 4U);
        if ((byte_size > max_segment_range) ||
            (out_plan->layout_count >= layout_capacity)) {
            out_plan->rerouted_piece_count++;
            continue;
        }
        if ((offset > max_segment_range) ||
            (byte_size > max_segment_range - offset)) {
            segment++;
            if (segment >= DB_VK_MAX_BATCHES_PER_LANE) {
                out_plan->rerouted_piece_count++;
                continue;
            }
            segment_base =
                vk_align_up(segment_base + max_segment_range, base_alignment);
            offset = 0U;
        }
        layouts[out_plan->layout_count++] = (db_vk_shared_piece_layout_t){
            .piece_index = piece->piece_index,
            .destination_rect = piece->destination_rect,
            .row_pitch = (uint32_t)row_pitch,
            .segment_index = segment,
            .byte_offset = offset,
            .byte_size = byte_size,
            .format = format,
            .content_generation = execution_plan->content_generation,
            .scheduling_epoch = execution_plan->scheduling_epoch,
        };
        segment_used = offset + byte_size;
        const VkDeviceSize end = segment_base + segment_used;
        if (end > out_plan->total_size) {
            out_plan->total_size = end;
        }
    }
    out_plan->segment_base_alignment = base_alignment;
    out_plan->segment_range = max_segment_range;
    out_plan->segment_count =
        (out_plan->layout_count == 0U) ? 0U : segment + 1U;
    return 1;
}
