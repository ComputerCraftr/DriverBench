#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define MASK_GPU0 1U
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)

uint32_t db_vk_build_device_group_mask(uint32_t device_count) {
    uint32_t mask = 0U;
    for (uint32_t i = 0U; i < device_count; i++) {
        mask |= (1U << i);
    }
    return mask;
}

static uint32_t db_vk_clamp_u32(uint32_t value, uint32_t min_v,
                                uint32_t max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static VkExtent2D
db_vk_choose_surface_extent(const db_vk_wsi_config_t *wsi_config,
                            const VkSurfaceCapabilitiesKHR *caps) {
    VkExtent2D extent = caps->currentExtent;
    if (extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        wsi_config->get_framebuffer_size(wsi_config->window_handle, &width,
                                         &height, wsi_config->user_data);
        if ((width <= 0) || (height <= 0)) {
            width = BENCH_WINDOW_WIDTH_PX;
            height = BENCH_WINDOW_HEIGHT_PX;
        }
        extent.width =
            db_checked_int_to_u32(BACKEND_NAME, "surface_extent_width", width);
        extent.height = db_checked_int_to_u32(BACKEND_NAME,
                                              "surface_extent_height", height);
        extent.width = db_vk_clamp_u32(extent.width, caps->minImageExtent.width,
                                       caps->maxImageExtent.width);
        extent.height =
            db_vk_clamp_u32(extent.height, caps->minImageExtent.height,
                            caps->maxImageExtent.height);
    }
    return extent;
}

VkPresentModeKHR db_vk_choose_present_mode(VkPhysicalDevice present_phys,
                                           VkSurfaceKHR surface) {
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t mode_count = 0;
    DB_VK_CHECK(BACKEND_NAME, vkGetPhysicalDeviceSurfacePresentModesKHR(
                                  present_phys, surface, &mode_count, NULL));
    VkPresentModeKHR *modes =
        (VkPresentModeKHR *)calloc(mode_count, sizeof(VkPresentModeKHR));
    DB_VK_CHECK(BACKEND_NAME, vkGetPhysicalDeviceSurfacePresentModesKHR(
                                  present_phys, surface, &mode_count, modes));
    if (BENCH_VSYNC_ENABLED != 0) {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
    } else {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (uint32_t i = 0; i < mode_count; i++) {
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                break;
            }
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }
    }
    free(modes);
    return present_mode;
}

VkSurfaceFormatKHR
db_vk_choose_surface_format(const VkSurfaceFormatKHR *formats,
                            uint32_t format_count) {
    if ((formats == NULL) || (format_count == 0U)) {
        failf("No Vulkan surface formats available");
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

static uint32_t db_vk_find_memory_type(VkPhysicalDevice phys,
                                       uint32_t type_bits,
                                       VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0U; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1U << i)) &&
            ((mp.memoryTypes[i].propertyFlags & required) == required)) {
            return i;
        }
    }
    failf("No matching Vulkan memory type for required flags 0x%x",
          (unsigned)required);
}

void db_vk_create_history_target(VkPhysicalDevice phys, VkDevice device,
                                 VkFormat format, VkExtent2D extent,
                                 VkRenderPass render_pass,
                                 uint32_t device_group_mask,
                                 HistoryTargetState *target) {
    if ((target == NULL) || (extent.width == 0U) || (extent.height == 0U)) {
        failf("Invalid history target setup");
    }

    VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent.width = extent.width;
    ici.extent.height = extent.height;
    ici.extent.depth = 1U;
    ici.mipLevels = 1U;
    ici.arrayLayers = 1U;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateImage(device, &ici, NULL, &target->image));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, target->image, &mr);
    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryAllocateFlagsInfo ma_flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = db_vk_find_memory_type(
        phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
        failf("Window framebuffer size is zero; cannot create swapchain");
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create_info.surface = surface;
    create_info.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount &&
        (create_info.minImageCount > caps.maxImageCount)) {
        create_info.minImageCount = caps.maxImageCount;
    }
    create_info.imageFormat = fmt.format;
    create_info.imageColorSpace = fmt.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    DB_VK_CHECK(BACKEND_NAME, vkCreateSwapchainKHR(device, &create_info, NULL,
                                                   &state->swapchain));
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
    }
}

void db_vk_update_history_descriptor(VkDevice device,
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

// NOLINTEND(misc-include-cleaner)
