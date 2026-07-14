#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../../core/db_render_types.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan_core.h>

static int vk_try_align_up(VkDeviceSize value, VkDeviceSize alignment,
                           VkDeviceSize *out_value) {
    if (out_value == NULL) {
        return 0;
    }
    if (alignment <= 1U) {
        *out_value = value;
        return 1;
    }
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0U) {
        *out_value = value;
        return 1;
    }
    return db_try_add_u64(value, alignment - remainder, out_value);
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
    const VkDeviceSize pixel_size = db_pixel_format_bytes_per_pixel(format);
    if (pixel_size == 0U) {
        return 0;
    }
    const VkDeviceSize base_alignment =
        DB_MAX(segment_base_alignment, (VkDeviceSize)4U);
    uint32_t segment = 0U;
    VkDeviceSize segment_used = 0U;
    VkDeviceSize segment_base = 0U;
    for (size_t index = 0U; index < execution_plan->piece_count; index++) {
        if (segment >= DB_VK_MAX_BATCHES_PER_LANE) {
            out_plan->rerouted_piece_count++;
            continue;
        }
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[index];
        VkDeviceSize row_pitch = 0U;
        VkDeviceSize byte_size = 0U;
        if ((db_try_mul_u64(piece->destination_rect.extent.width, pixel_size,
                            &row_pitch) == 0) ||
            (db_try_mul_u64(row_pitch, piece->destination_rect.extent.height,
                            &byte_size) == 0) ||
            (row_pitch > UINT32_MAX)) {
            out_plan->rerouted_piece_count++;
            continue;
        }
        VkDeviceSize offset = 0U;
        if (vk_try_align_up(segment_used, 4U, &offset) == 0) {
            out_plan->rerouted_piece_count++;
            continue;
        }
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
            VkDeviceSize unaligned_segment_base = 0U;
            if ((db_try_add_u64(segment_base, max_segment_range,
                                &unaligned_segment_base) == 0) ||
                (vk_try_align_up(unaligned_segment_base, base_alignment,
                                 &segment_base) == 0)) {
                out_plan->rerouted_piece_count++;
                segment = DB_VK_MAX_BATCHES_PER_LANE;
                continue;
            }
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
        if ((db_try_add_u64(offset, byte_size, &segment_used) == 0)) {
            out_plan->layout_count--;
            out_plan->rerouted_piece_count++;
            continue;
        }
        VkDeviceSize end = 0U;
        if (db_try_add_u64(segment_base, segment_used, &end) == 0) {
            out_plan->layout_count--;
            out_plan->rerouted_piece_count++;
            segment = DB_VK_MAX_BATCHES_PER_LANE;
            continue;
        }
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
