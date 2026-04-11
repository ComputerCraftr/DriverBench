#include "../../core/db_numeric.h"
#include "vk_init_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

int db_vk_probe_external_buffer_interop(VkPhysicalDevice phys) {
#ifdef __linux__
    const VkPhysicalDeviceExternalBufferInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .flags = 0U,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    VkExternalBufferProperties properties = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
    };
    vkGetPhysicalDeviceExternalBufferProperties(phys, &buffer_info,
                                                &properties);
    const VkExternalMemoryFeatureFlags required =
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    return DB_BOOL((properties.externalMemoryProperties.externalMemoryFeatures &
                    required) == required);
#else
    (void)phys;
    return 0;
#endif
}

int db_vk_probe_external_image_interop(VkPhysicalDevice phys, VkFormat format) {
#ifdef __linux__
    const VkPhysicalDeviceExternalImageFormatInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    const VkPhysicalDeviceImageFormatInfo2 image_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &external_info,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkExternalImageFormatProperties external_properties = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
    };
    VkImageFormatProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &external_properties,
    };
    if (vkGetPhysicalDeviceImageFormatProperties2(phys, &image_info,
                                                  &properties) != VK_SUCCESS) {
        return 0;
    }
    const VkExternalMemoryFeatureFlags features =
        external_properties.externalMemoryProperties.externalMemoryFeatures;
    return DB_BOOL((features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) &&
                   (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT));
#else
    (void)phys;
    (void)format;
    return 0;
#endif
}

static VkDrmFormatModifierPropertiesEXT *
vk_query_drm_modifiers(VkPhysicalDevice phys, VkFormat format,
                       uint32_t *out_count) {
    *out_count = 0U;
    VkDrmFormatModifierPropertiesListEXT list = {
        .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
    };
    VkFormatProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = &list,
    };
    vkGetPhysicalDeviceFormatProperties2(phys, format, &properties);
    if (list.drmFormatModifierCount == 0U) {
        return NULL;
    }
    VkDrmFormatModifierPropertiesEXT *modifiers =
        (VkDrmFormatModifierPropertiesEXT *)calloc(
            list.drmFormatModifierCount,
            sizeof(VkDrmFormatModifierPropertiesEXT));
    if (modifiers == NULL) {
        runtime_failf("failed to allocate DRM modifier properties");
    }
    list.pDrmFormatModifierProperties = modifiers;
    vkGetPhysicalDeviceFormatProperties2(phys, format, &properties);
    *out_count = list.drmFormatModifierCount;
    return modifiers;
}

int db_vk_find_common_drm_modifier(VkPhysicalDevice worker,
                                   VkPhysicalDevice primary, VkFormat format,
                                   uint64_t *out_modifier) {
    uint32_t worker_count = 0U;
    uint32_t primary_count = 0U;
    VkDrmFormatModifierPropertiesEXT *worker_modifiers =
        vk_query_drm_modifiers(worker, format, &worker_count);
    VkDrmFormatModifierPropertiesEXT *primary_modifiers =
        vk_query_drm_modifiers(primary, format, &primary_count);
    int found = 0;
    for (uint32_t worker_index = 0U;
         (worker_index < worker_count) && (found == 0); worker_index++) {
        const VkDrmFormatModifierPropertiesEXT *const worker_modifier =
            &worker_modifiers[worker_index];
        if ((worker_modifier->drmFormatModifierTilingFeatures &
             VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0U) {
            continue;
        }
        for (uint32_t primary_index = 0U; primary_index < primary_count;
             primary_index++) {
            const VkDrmFormatModifierPropertiesEXT *const primary_modifier =
                &primary_modifiers[primary_index];
            if ((worker_modifier->drmFormatModifier ==
                 primary_modifier->drmFormatModifier) &&
                (worker_modifier->drmFormatModifierPlaneCount ==
                 primary_modifier->drmFormatModifierPlaneCount) &&
                ((primary_modifier->drmFormatModifierTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0U)) {
                *out_modifier = worker_modifier->drmFormatModifier;
                found = 1;
                break;
            }
        }
    }
    free(primary_modifiers);
    free(worker_modifiers);
    return found;
}
