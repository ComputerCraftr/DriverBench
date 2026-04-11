#include "core/db_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <vulkan/vulkan_core.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_numeric.h"
#include "vk_internal.h"
#include "vk_renderer.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)

uint32_t db_vk_build_device_group_mask(uint32_t device_count) {
    uint32_t mask = 0U;
    for (uint32_t i = 0U; i < device_count; i++) {
        mask |= (1U << i);
    }
    return mask;
}

static VkExtent2D
db_vk_choose_surface_extent(const db_vk_wsi_config_t *wsi_config,
                            const VkSurfaceCapabilitiesKHR *caps) {
    VkExtent2D extent = caps->currentExtent;
    if (extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        wsi_config->get_framebuffer_size(wsi_config->window_handle, &width,
                                         &height);
        if ((width <= 0) || (height <= 0)) {
            width = BENCH_WINDOW_WIDTH_PX;
            height = BENCH_WINDOW_HEIGHT_PX;
        }
        extent.width =
            db_checked_int_to_u32(BACKEND_NAME, "surface_extent_width", width);
        extent.height = db_checked_int_to_u32(BACKEND_NAME,
                                              "surface_extent_height", height);
        extent.width = DB_CLAMP(extent.width, caps->minImageExtent.width,
                                caps->maxImageExtent.width);
        extent.height = DB_CLAMP(extent.height, caps->minImageExtent.height,
                                 caps->maxImageExtent.height);
    }
    return extent;
}

VkPresentModeKHR db_vk_choose_present_mode(VkPhysicalDevice present_phys,
                                           VkSurfaceKHR surface,
                                           int vsync_enabled) {
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t mode_count = 0;
    DB_VK_CHECK(BACKEND_NAME, vkGetPhysicalDeviceSurfacePresentModesKHR(
                                  present_phys, surface, &mode_count, NULL));
    VkPresentModeKHR *modes =
        (VkPresentModeKHR *)calloc(mode_count, sizeof(VkPresentModeKHR));
    DB_VK_CHECK(BACKEND_NAME, vkGetPhysicalDeviceSurfacePresentModesKHR(
                                  present_phys, surface, &mode_count, modes));
    if (vsync_enabled != 0) {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
    } else {
        int have_mailbox = 0;
        int have_immediate = 0;
        int have_fifo_relaxed = 0;
        for (uint32_t i = 0; i < mode_count; i++) {
            switch ((int)modes[i]) {
            case VK_PRESENT_MODE_MAILBOX_KHR:
                have_mailbox = 1;
                break;
            case VK_PRESENT_MODE_IMMEDIATE_KHR:
                have_immediate = 1;
                break;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                have_fifo_relaxed = 1;
                break;
            default:
                break;
            }
        }
        if (have_mailbox != 0) {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        } else if (have_fifo_relaxed != 0) {
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        } else if (have_immediate != 0) {
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
        }
    }
    free(modes);
    return present_mode;
}

VkSurfaceFormatKHR
db_vk_choose_surface_format(const VkSurfaceFormatKHR *formats,
                            uint32_t format_count) {
    if ((formats == NULL) || (format_count == 0U)) {
        runtime_failf("No Vulkan surface formats available");
    }

    const VkFormat preferred_formats[] = {VK_FORMAT_B8G8R8A8_UNORM,
                                          VK_FORMAT_R8G8B8A8_UNORM};
    for (size_t i = 0;
         i < (sizeof(preferred_formats) / sizeof(preferred_formats[0])); i++) {
        for (uint32_t j = 0; j < format_count; j++) {
            if (formats[j].format == preferred_formats[i]) {
                return formats[j];
            }
        }
    }

    for (uint32_t i = 0; i < format_count; i++) {
        if ((formats[i].format != VK_FORMAT_B8G8R8A8_SRGB) &&
            (formats[i].format != VK_FORMAT_R8G8B8A8_SRGB)) {
            return formats[i];
        }
    }
    return formats[0];
}

db_vk_surface_format_selection_t db_vk_resolve_surface_format_for_output(
    const VkSurfaceFormatKHR *formats, uint32_t format_count,
    db_output_format_request_t request, int metadata_supported) {
    if ((formats == NULL) || (format_count == 0U)) {
        runtime_failf("No Vulkan surface formats available");
    }
    db_vk_surface_format_selection_t selection = {
        .surface_format = db_vk_choose_surface_format(formats, format_count),
        .capability =
            {
                .metadata_supported = DB_BOOL(metadata_supported),
                .native_bit_depth = DB_HDR10_NATIVE_BIT_DEPTH,
                .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
                .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
                .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
                .unavailable_reason = "vulkan_hdr10_format_unavailable",
            },
    };
    const VkFormat hdr_formats[] = {
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    };
    int complete_pair_supported = 0;
    for (size_t preferred = 0U;
         preferred < (sizeof(hdr_formats) / sizeof(hdr_formats[0]));
         preferred++) {
        for (uint32_t index = 0U; index < format_count; index++) {
            if (formats[index].format == hdr_formats[preferred]) {
                selection.capability.native_format_supported = 1;
            }
            if (formats[index].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                selection.capability.colorspace_supported = 1;
                selection.capability.sink_hdr_supported = 1;
            }
            if ((formats[index].format == hdr_formats[preferred]) &&
                (formats[index].colorSpace ==
                 VK_COLOR_SPACE_HDR10_ST2084_EXT)) {
                complete_pair_supported = 1;
                if ((request != DB_OUTPUT_FORMAT_SDR) &&
                    (metadata_supported != 0)) {
                    selection.surface_format = formats[index];
                    selection.hdr_enabled = 1;
                }
            }
        }
    }
    if (metadata_supported == 0) {
        selection.capability.unavailable_reason =
            "vulkan_hdr_metadata_extension_unavailable";
    } else if (selection.capability.native_format_supported == 0) {
        selection.capability.unavailable_reason =
            "vulkan_hdr10_format_unavailable";
    } else if (selection.capability.colorspace_supported == 0) {
        selection.capability.unavailable_reason =
            "vulkan_hdr10_colorspace_unavailable";
    } else if (complete_pair_supported == 0) {
        selection.capability.unavailable_reason =
            "vulkan_hdr10_format_colorspace_pair_unavailable";
    } else {
        selection.capability.unavailable_reason = "none";
    }
    selection.capability.native_hdr_verified = selection.hdr_enabled;
    return selection;
}

static void db_vk_apply_hdr_metadata(VkDevice device, VkSwapchainKHR swapchain,
                                     VkSurfaceFormatKHR format) {
    if ((format.colorSpace != VK_COLOR_SPACE_HDR10_ST2084_EXT) ||
        (swapchain == VK_NULL_HANDLE)) {
        return;
    }
    const PFN_vkSetHdrMetadataEXT set_metadata =
        (PFN_vkSetHdrMetadataEXT)vkGetDeviceProcAddr(device,
                                                     "vkSetHdrMetadataEXT");
    if (set_metadata == NULL) {
        runtime_failf("HDR10 swapchain created without vkSetHdrMetadataEXT");
    }
    const VkHdrMetadataEXT metadata = {
        .sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
        .displayPrimaryRed = {.x = 0.708F, .y = 0.292F},
        .displayPrimaryGreen = {.x = 0.170F, .y = 0.797F},
        .displayPrimaryBlue = {.x = 0.131F, .y = 0.046F},
        .whitePoint = {.x = 0.3127F, .y = 0.3290F},
        .maxLuminance = db_double_to_f32(DB_HDR10_MASTERING_MAX_NITS),
        .minLuminance = db_double_to_f32(DB_HDR10_MASTERING_MIN_NITS),
        .maxContentLightLevel = db_double_to_f32(DB_HDR10_MAX_CLL_NITS),
        .maxFrameAverageLightLevel = db_double_to_f32(DB_HDR10_MAX_FALL_NITS),
    };
    set_metadata(device, 1U, &swapchain, &metadata);
}

static uint32_t vk_find_memory_type(VkPhysicalDevice phys, uint32_t type_bits,
                                    VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0U; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1U << i)) &&
            ((mp.memoryTypes[i].propertyFlags & required) == required)) {
            return i;
        }
    }
    runtime_failf("No matching Vulkan memory type for required flags 0x%x",
                  (unsigned)required);
}

void db_vk_create_backing_target(VkPhysicalDevice phys, VkDevice device,
                                 VkFormat format, VkExtent2D extent,
                                 VkRenderPass render_pass,
                                 uint32_t device_group_mask,
                                 VkBackingTargetState *target) {
    if ((target == NULL) || (extent.width == 0U) || (extent.height == 0U)) {
        runtime_failf("Invalid history target setup");
    }

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = extent.width, .height = extent.height, .depth = 1U},
        .mipLevels = 1U,
        .arrayLayers = 1U,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateImage(device, &ici, NULL, &target->image));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, target->image, &mr);
    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryAllocateFlagsInfo ma_flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = vk_find_memory_type(
        phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    target->memory_type_index = mai.memoryTypeIndex;
    VkPhysicalDeviceMemoryProperties memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(phys, &memory_properties);
    target->memory_heap_index =
        memory_properties.memoryTypes[mai.memoryTypeIndex].heapIndex;
    if (device_group_mask != 0U) {
        ma_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;
        ma_flags.deviceMask = device_group_mask;
        mai.pNext = &ma_flags;
    }
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateMemory(device, &mai, NULL, &target->memory));
    DB_VK_CHECK(BACKEND_NAME,
                vkBindImageMemory(device, target->image, target->memory, 0U));

    VkImageViewCreateInfo ivci = {.sType =
                                      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = target->image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1U;
    ivci.subresourceRange.layerCount = 1U;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateImageView(device, &ivci, NULL, &target->view));

    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass = render_pass;
    fbci.attachmentCount = 1U;
    fbci.pAttachments = &target->view;
    fbci.width = extent.width;
    fbci.height = extent.height;
    fbci.layers = 1U;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateFramebuffer(device, &fbci, NULL, &target->framebuffer));

    target->layout_initialized = 0;
}

void db_vk_create_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                  VkPhysicalDevice present_phys,
                                  VkDevice device, VkSurfaceKHR surface,
                                  VkSurfaceFormatKHR fmt,
                                  VkPresentModeKHR present_mode,
                                  VkRenderPass render_pass,
                                  SwapchainState *state) {
    VkSurfaceCapabilitiesKHR caps;
    DB_VK_CHECK(BACKEND_NAME, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                                  present_phys, surface, &caps));
    const VkExtent2D extent = db_vk_choose_surface_extent(wsi_config, &caps);
    if ((extent.width == 0U) || (extent.height == 0U)) {
        runtime_failf(
            "Window framebuffer size is zero; cannot create swapchain");
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR};
    create_info.minImageCount = caps.minImageCount + 1;
    if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        create_info.minImageCount = DB_MAX(create_info.minImageCount, 3U);
    }
    if (caps.maxImageCount &&
        (create_info.minImageCount > caps.maxImageCount)) {
        create_info.minImageCount = caps.maxImageCount;
    }
    create_info.imageFormat = fmt.format;
    create_info.imageColorSpace = fmt.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    DB_VK_CHECK(BACKEND_NAME, vkCreateSwapchainKHR(device, &create_info, NULL,
                                                   &state->swapchain));
    db_vk_apply_hdr_metadata(device, state->swapchain, fmt);
    state->extent = extent;

    VkResult get_images_result = vkGetSwapchainImagesKHR(
        device, state->swapchain, &state->image_count, NULL);
    if (get_images_result != VK_SUCCESS) {
        db_vk_fail(BACKEND_NAME, "vkGetSwapchainImagesKHR(count)",
                   get_images_result, __FILE__, __LINE__);
    }
    state->images = (VkImage *)calloc(state->image_count, sizeof(VkImage));
    get_images_result = vkGetSwapchainImagesKHR(
        device, state->swapchain, &state->image_count, state->images);
    if (get_images_result != VK_SUCCESS) {
        free((void *)state->images);
        state->images = NULL;
        state->image_count = 0;
        db_vk_fail(BACKEND_NAME, "vkGetSwapchainImagesKHR(images)",
                   get_images_result, __FILE__, __LINE__);
    }

    state->views =
        (VkImageView *)calloc(state->image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < state->image_count; i++) {
        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ivci.image = state->images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = fmt.format;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        DB_VK_CHECK(BACKEND_NAME,
                    vkCreateImageView(device, &ivci, NULL, &state->views[i]));
    }

    state->framebuffers =
        (VkFramebuffer *)calloc(state->image_count, sizeof(VkFramebuffer));
    state->image_layouts =
        (VkImageLayout *)calloc(state->image_count, sizeof(VkImageLayout));
    for (uint32_t i = 0; i < state->image_count; i++) {
        VkFramebufferCreateInfo fbci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = render_pass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &state->views[i];
        fbci.width = state->extent.width;
        fbci.height = state->extent.height;
        fbci.layers = 1;
        DB_VK_CHECK(BACKEND_NAME, vkCreateFramebuffer(device, &fbci, NULL,
                                                      &state->framebuffers[i]));
        state->image_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

void db_vk_update_backing_descriptor(VkDevice device,
                                     VkDescriptorSet descriptor_set,
                                     VkSampler sampler,
                                     VkImageView image_view) {
    if ((descriptor_set == VK_NULL_HANDLE) || (sampler == VK_NULL_HANDLE) ||
        (image_view == VK_NULL_HANDLE)) {
        return;
    }
    VkDescriptorImageInfo image_info = {0};
    image_info.sampler = sampler;
    image_info.imageView = image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {.sType =
                                      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor_set;
    write.dstBinding = 0U;
    write.descriptorCount = 1U;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(device, 1U, &write, 0U, NULL);
}
