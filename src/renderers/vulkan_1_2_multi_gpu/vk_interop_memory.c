#include "vk_diagnostics.h"
#ifdef __linux__
#include "vk_internal.h"
#include "vk_state_internal.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "../../core/db_log.h"
#include "../../core/db_numeric.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#include <unistd.h>

static void vk_log_interop_failure(const char *stage, VkResult result) {
    if (db_vk_trace_level() < 2) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("stage", stage),
        DB_LOG_TOKEN("result", db_vk_result_name(result)),
        DB_LOG_I64("result_code", result),
    };
    db_log_info(BACKEND_NAME, "vk_interop_failure", fields,
                DB_LOG_FIELD_COUNT(fields));
}

static PFN_vkVoidFunction vk_interop_proc(VkDevice device, const char *name) {
    return vkGetDeviceProcAddr(device, name);
}

static VkExternalMemoryHandleTypeFlagBits
vk_memory_handle_type(db_vk_transport_kind_t transport) {
    return (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
               ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
               : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
}

int db_vk_create_external_image(VkPhysicalDevice phys, VkDevice device,
                                VkFormat format, VkExtent2D extent,
                                VkRenderPass render_pass,
                                db_vk_transport_kind_t transport,
                                uint64_t drm_modifier, uint32_t lane_index,
                                uint32_t worker_memory_type,
                                db_vk_lane_slot_t *slot) {
    const VkImageDrmFormatModifierListCreateInfoEXT modifier_info = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
        .drmFormatModifierCount = 1U,
        .pDrmFormatModifiers = &drm_modifier,
    };
    const VkExternalMemoryImageCreateInfo external_image = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE) ? &modifier_info
                                                              : NULL,
        .handleTypes = vk_memory_handle_type(transport),
    };
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_image,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = extent.width, .height = extent.height, .depth = 1U},
        .mipLevels = 1U,
        .arrayLayers = 1U,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
                      ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT
                      : VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result =
        vkCreateImage(device, &image_info, NULL, &slot->worker_target.image);
    if (result != VK_SUCCESS) {
        vk_log_interop_failure("worker_create_image", result);
        return 0;
    }
    VkMemoryDedicatedRequirements dedicated_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 requirements2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicated_requirements,
    };
    const VkImageMemoryRequirementsInfo2 requirements_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = slot->worker_target.image,
    };
    vkGetImageMemoryRequirements2(device, &requirements_info, &requirements2);
    const VkMemoryRequirements requirements = requirements2.memoryRequirements;
    slot->worker_requirement_size = requirements.size;
    slot->worker_requirement_alignment = requirements.alignment;
    slot->worker_requirement_type_bits = requirements.memoryTypeBits;
    slot->worker_dedicated_required =
        DB_BOOL(dedicated_requirements.requiresDedicatedAllocation);
    slot->worker_dedicated_preferred =
        DB_BOOL(dedicated_requirements.prefersDedicatedAllocation);
    if ((worker_memory_type >= 32U) ||
        ((requirements.memoryTypeBits & (1U << worker_memory_type)) == 0U)) {
        return 0;
    }
    VkPhysicalDeviceMemoryProperties memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(phys, &memory_properties);
    if (worker_memory_type >= memory_properties.memoryTypeCount) {
        return 0;
    }
    const uint32_t heap_index =
        memory_properties.memoryTypes[worker_memory_type].heapIndex;
    slot->worker_target.memory_type_index = worker_memory_type;
    slot->worker_target.memory_heap_index = heap_index;
    const db_log_field_t candidate_fields[] = {
        DB_LOG_U64("lane", lane_index),
        DB_LOG_TOKEN("device_role", "worker"),
        DB_LOG_U64("type_index", worker_memory_type),
        DB_LOG_HEX64("requirement_type_bits", requirements.memoryTypeBits),
        DB_LOG_HEX64(
            "property_flags",
            memory_properties.memoryTypes[worker_memory_type].propertyFlags),
        DB_LOG_U64("heap_index", heap_index),
        DB_LOG_HEX64("heap_flags",
                     memory_properties.memoryHeaps[heap_index].flags),
        DB_LOG_U64("requirement_size", requirements.size),
        DB_LOG_U64("requirement_alignment", requirements.alignment),
        DB_LOG_BOOL("dedicated_required", slot->worker_dedicated_required),
        DB_LOG_BOOL("dedicated_preferred", slot->worker_dedicated_preferred),
    };
    if (db_vk_trace_level() >= 2) {
        db_log_info(BACKEND_NAME, "vk_memory_type_candidate", candidate_fields,
                    DB_LOG_FIELD_COUNT(candidate_fields));
    }
    const VkMemoryDedicatedAllocateInfo dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = slot->worker_target.image,
    };
    const VkExportMemoryAllocateInfo export_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated_info,
        .handleTypes = vk_memory_handle_type(transport),
    };
    const VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &export_info,
        .allocationSize = requirements.size,
        .memoryTypeIndex = worker_memory_type,
    };
    result = vkAllocateMemory(device, &allocate_info, NULL,
                              &slot->worker_target.memory);
    if (result != VK_SUCCESS) {
        vk_log_interop_failure("worker_allocate_memory", result);
        return 0;
    }
    slot->allocation_size = allocate_info.allocationSize;
    result = vkBindImageMemory(device, slot->worker_target.image,
                               slot->worker_target.memory, 0U);
    if (result != VK_SUCCESS) {
        vk_log_interop_failure("worker_bind_memory", result);
        return 0;
    }
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = slot->worker_target.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1U,
                             .layerCount = 1U},
    };
    if (vkCreateImageView(device, &view_info, NULL,
                          &slot->worker_target.view) != VK_SUCCESS) {
        return 0;
    }
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1U,
        .pAttachments = &slot->worker_target.view,
        .width = extent.width,
        .height = extent.height,
        .layers = 1U,
    };
    return DB_BOOL(vkCreateFramebuffer(device, &framebuffer_info, NULL,
                                       &slot->worker_target.framebuffer) ==
                   VK_SUCCESS);
}

int db_vk_import_external_image(VkPhysicalDevice primary_phys,
                                VkDevice primary_device, VkDevice worker_device,
                                VkFormat format, VkExtent2D extent,
                                uint32_t lane_index,
                                db_vk_transport_kind_t transport,
                                db_vk_lane_slot_t *slot) {
    union {
        PFN_vkVoidFunction generic;
        PFN_vkGetMemoryFdKHR typed;
    } get_memory_fd = {.generic =
                           vk_interop_proc(worker_device, "vkGetMemoryFdKHR")};
    if (get_memory_fd.typed == NULL) {
        return 0;
    }
    const VkMemoryGetFdInfoKHR get_fd_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = slot->worker_target.memory,
        .handleType = vk_memory_handle_type(transport),
    };
    int memory_fd = -1;
    if (get_memory_fd.typed(worker_device, &get_fd_info, &memory_fd) !=
        VK_SUCCESS) {
        return 0;
    }
    VkSubresourceLayout plane_layouts[4] = {0};
    uint32_t plane_count = 0U;
    uint64_t actual_modifier = 0U;
    if (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE) {
        union {
            PFN_vkVoidFunction generic;
            PFN_vkGetImageDrmFormatModifierPropertiesEXT typed;
        } get_modifier = {
            .generic = vkGetDeviceProcAddr(
                worker_device, "vkGetImageDrmFormatModifierPropertiesEXT")};
        VkImageDrmFormatModifierPropertiesEXT modifier_properties = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
        };
        if ((get_modifier.typed == NULL) ||
            (get_modifier.typed(worker_device, slot->worker_target.image,
                                &modifier_properties) != VK_SUCCESS)) {
            vk_log_interop_failure("modifier_plane_layout_rejected",
                                   VK_ERROR_FORMAT_NOT_SUPPORTED);
            (void)close(memory_fd);
            return 0;
        }
        actual_modifier = modifier_properties.drmFormatModifier;
        VkDrmFormatModifierPropertiesListEXT modifier_list = {
            .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
        };
        VkFormatProperties2 format_properties = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
            .pNext = &modifier_list,
        };
        vkGetPhysicalDeviceFormatProperties2(
            g_state.scheduler.independent_lanes[lane_index].phys, format,
            &format_properties);
        VkDrmFormatModifierPropertiesEXT modifier_entries[64] = {0};
        modifier_list.drmFormatModifierCount =
            DB_MIN(modifier_list.drmFormatModifierCount, 64U);
        modifier_list.pDrmFormatModifierProperties = modifier_entries;
        vkGetPhysicalDeviceFormatProperties2(
            g_state.scheduler.independent_lanes[lane_index].phys, format,
            &format_properties);
        for (uint32_t index = 0U; index < modifier_list.drmFormatModifierCount;
             index++) {
            if (modifier_entries[index].drmFormatModifier == actual_modifier) {
                plane_count =
                    modifier_entries[index].drmFormatModifierPlaneCount;
                break;
            }
        }
        if ((plane_count == 0U) || (plane_count > 4U)) {
            vk_log_interop_failure("modifier_plane_layout_rejected",
                                   VK_ERROR_FORMAT_NOT_SUPPORTED);
            (void)close(memory_fd);
            return 0;
        }
        static const VkImageAspectFlags plane_aspects[4] = {
            VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
            VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
            VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
            VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT,
        };
        for (uint32_t plane = 0U; plane < plane_count; plane++) {
            const VkImageSubresource subresource = {
                .aspectMask = plane_aspects[plane],
            };
            vkGetImageSubresourceLayout(worker_device,
                                        slot->worker_target.image, &subresource,
                                        &plane_layouts[plane]);
            const db_log_field_t fields[] = {
                DB_LOG_U64("lane", lane_index),
                DB_LOG_U64("plane", plane),
                DB_LOG_HEX64("modifier", actual_modifier),
                DB_LOG_U64("offset", plane_layouts[plane].offset),
                DB_LOG_U64("row_pitch", plane_layouts[plane].rowPitch),
                DB_LOG_U64("array_pitch", plane_layouts[plane].arrayPitch),
                DB_LOG_U64("depth_pitch", plane_layouts[plane].depthPitch),
            };
            if (db_vk_trace_level() >= 2) {
                db_log_info(BACKEND_NAME, "vk_memory_plane_layout", fields,
                            DB_LOG_FIELD_COUNT(fields));
            }
            plane_layouts[plane].size = 0U;
        }
    }
    const VkImageDrmFormatModifierExplicitCreateInfoEXT explicit_modifier = {
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = actual_modifier,
        .drmFormatModifierPlaneCount = plane_count,
        .pPlaneLayouts = plane_layouts,
    };
    const VkExternalMemoryImageCreateInfo external_image = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
                     ? &explicit_modifier
                     : NULL,
        .handleTypes = vk_memory_handle_type(transport),
    };
    const VkImageCreateInfo alias_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_image,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = extent.width, .height = extent.height, .depth = 1U},
        .mipLevels = 1U,
        .arrayLayers = 1U,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE)
                      ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT
                      : VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(primary_device, &alias_info, NULL,
                                    &slot->primary_alias_image);
    if (result != VK_SUCCESS) {
        vk_log_interop_failure("primary_create_alias_image", result);
        (void)close(memory_fd);
        return 0;
    }
    VkMemoryDedicatedRequirements dedicated_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 requirements2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicated_requirements,
    };
    const VkImageMemoryRequirementsInfo2 requirements_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = slot->primary_alias_image,
    };
    vkGetImageMemoryRequirements2(primary_device, &requirements_info,
                                  &requirements2);
    const VkMemoryRequirements requirements = requirements2.memoryRequirements;
    slot->primary_dedicated_required =
        DB_BOOL(dedicated_requirements.requiresDedicatedAllocation);
    slot->primary_dedicated_preferred =
        DB_BOOL(dedicated_requirements.prefersDedicatedAllocation);
    uint32_t fd_type_bits = requirements.memoryTypeBits;
    if (transport == DB_VK_TRANSPORT_DMA_BUF_IMAGE) {
        union {
            PFN_vkVoidFunction generic;
            PFN_vkGetMemoryFdPropertiesKHR typed;
        } get_fd_properties = {
            .generic = vkGetDeviceProcAddr(primary_device,
                                           "vkGetMemoryFdPropertiesKHR")};
        VkMemoryFdPropertiesKHR fd_properties = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        if ((get_fd_properties.typed == NULL) ||
            (get_fd_properties.typed(
                 primary_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                 memory_fd, &fd_properties) != VK_SUCCESS)) {
            (void)close(memory_fd);
            return 0;
        }
        fd_type_bits = fd_properties.memoryTypeBits;
    }
    const uint32_t compatible_type_bits = db_vk_import_memory_type_bits(
        fd_type_bits, requirements.memoryTypeBits);
    const db_log_field_t attempt_fields[] = {
        DB_LOG_U64("lane", lane_index),
        DB_LOG_TOKEN("format", db_vk_format_name(format)),
        DB_LOG_U64("width", extent.width),
        DB_LOG_U64("height", extent.height),
        DB_LOG_U64("transport", transport),
        DB_LOG_HEX64("modifier", actual_modifier),
        DB_LOG_U64("plane_count", plane_count),
        DB_LOG_U64("worker_usage", VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
        DB_LOG_U64("primary_usage", VK_IMAGE_USAGE_SAMPLED_BIT),
        DB_LOG_U64("worker_type_index", slot->worker_target.memory_type_index),
        DB_LOG_U64("worker_allocation_size", slot->allocation_size),
        DB_LOG_U64("worker_requirement_size", slot->worker_requirement_size),
        DB_LOG_U64("worker_requirement_alignment",
                   slot->worker_requirement_alignment),
        DB_LOG_HEX64("worker_requirement_type_bits",
                     slot->worker_requirement_type_bits),
        DB_LOG_BOOL("worker_dedicated_required",
                    slot->worker_dedicated_required),
        DB_LOG_BOOL("worker_dedicated_preferred",
                    slot->worker_dedicated_preferred),
        DB_LOG_HEX64("fd_type_bits", fd_type_bits),
        DB_LOG_HEX64("primary_requirement_type_bits",
                     requirements.memoryTypeBits),
        DB_LOG_U64("primary_requirement_size", requirements.size),
        DB_LOG_U64("primary_requirement_alignment", requirements.alignment),
        DB_LOG_BOOL("primary_dedicated_required",
                    slot->primary_dedicated_required),
        DB_LOG_BOOL("primary_dedicated_preferred",
                    slot->primary_dedicated_preferred),
        DB_LOG_HEX64("final_intersection", compatible_type_bits),
    };
    if (db_vk_trace_level() >= 2) {
        db_log_info(BACKEND_NAME, "vk_memory_transport_attempt", attempt_fields,
                    DB_LOG_FIELD_COUNT(attempt_fields));
    }
    if (compatible_type_bits == 0U) {
        vk_log_interop_failure("dma_buf_memory_type_intersection_empty",
                               VK_ERROR_FORMAT_NOT_SUPPORTED);
        (void)close(memory_fd);
        return 0;
    }
    if (requirements.size > slot->allocation_size) {
        vk_log_interop_failure("alias_allocation_size_mismatch",
                               VK_ERROR_FORMAT_NOT_SUPPORTED);
        (void)close(memory_fd);
        return 0;
    }
    const VkMemoryDedicatedAllocateInfo dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = slot->primary_alias_image,
    };
    const VkImportMemoryFdInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &dedicated_info,
        .handleType = vk_memory_handle_type(transport),
        .fd = memory_fd,
    };
    VkPhysicalDeviceMemoryProperties primary_memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(primary_phys,
                                        &primary_memory_properties);
    uint32_t selected_primary_type = UINT32_MAX;
    for (uint32_t memory_type = 0U;
         memory_type < primary_memory_properties.memoryTypeCount;
         memory_type++) {
        if ((compatible_type_bits & (1U << memory_type)) == 0U) {
            continue;
        }
        const uint32_t heap_index =
            primary_memory_properties.memoryTypes[memory_type].heapIndex;
        const db_log_field_t fields[] = {
            DB_LOG_U64("lane", lane_index),
            DB_LOG_TOKEN("device_role", "primary"),
            DB_LOG_U64("type_index", memory_type),
            DB_LOG_HEX64("property_flags",
                         primary_memory_properties.memoryTypes[memory_type]
                             .propertyFlags),
            DB_LOG_U64("heap_index", heap_index),
            DB_LOG_HEX64(
                "heap_flags",
                primary_memory_properties.memoryHeaps[heap_index].flags),
        };
        if (db_vk_trace_level() >= 2) {
            db_log_info(BACKEND_NAME, "vk_memory_type_candidate", fields,
                        DB_LOG_FIELD_COUNT(fields));
        }
        const VkMemoryAllocateInfo allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &import_info,
            .allocationSize = slot->allocation_size,
            .memoryTypeIndex = memory_type,
        };
        result = vkAllocateMemory(primary_device, &allocate_info, NULL,
                                  &slot->primary_alias_memory);
        if (result == VK_SUCCESS) {
            selected_primary_type = memory_type;
            break;
        }
        const db_log_field_t failure_fields[] = {
            DB_LOG_U64("lane", lane_index),
            DB_LOG_U64("type_index", memory_type),
            DB_LOG_TOKEN("result", db_vk_result_name(result)),
            DB_LOG_I64("result_code", result),
        };
        if (db_vk_trace_level() >= 2) {
            db_log_info(BACKEND_NAME, "vk_memory_type_rejected", failure_fields,
                        DB_LOG_FIELD_COUNT(failure_fields));
        }
    }
    if (selected_primary_type == UINT32_MAX) {
        vk_log_interop_failure("primary_import_memory", result);
        (void)close(memory_fd);
        return 0;
    }
    const db_log_field_t success_fields[] = {
        DB_LOG_U64("lane", lane_index),
        DB_LOG_U64("worker_type_index", slot->worker_target.memory_type_index),
        DB_LOG_U64("primary_type_index", selected_primary_type),
        DB_LOG_HEX64("modifier", actual_modifier),
        DB_LOG_TOKEN("result", "accepted"),
    };
    if (db_vk_trace_level() >= 2) {
        db_log_info(BACKEND_NAME, "vk_memory_transport_result", success_fields,
                    DB_LOG_FIELD_COUNT(success_fields));
    }
    result = vkBindImageMemory(primary_device, slot->primary_alias_image,
                               slot->primary_alias_memory, 0U);
    if (result != VK_SUCCESS) {
        vk_log_interop_failure("primary_bind_alias_memory", result);
        return 0;
    }
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = slot->primary_alias_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1U,
                             .layerCount = 1U},
    };
    return DB_BOOL(vkCreateImageView(primary_device, &view_info, NULL,
                                     &slot->primary_alias_view) == VK_SUCCESS);
}

#endif

#ifndef __linux__
typedef int db_vk_interop_memory_translation_unit_t;
#endif
