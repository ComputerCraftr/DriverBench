#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"

#if !defined(VERT_SPV_PATH) || !defined(FRAG_SPV_PATH)
#error "Vulkan SPIR-V shader paths must be provided by the build system."
#endif

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define NS_PER_SECOND_U64 1000000000ULL
#define WAIT_TIMEOUT_NS 100000000ULL
#define NS_TO_MS_D 1e6
#define MAX_GPU_COUNT 8U
#define MAX_BAND_OWNER 64U
#define QUAD_VERT_FLOAT_COUNT 12U
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define FRAME_BUDGET_NS 16666666ULL
#define FRAME_SAFETY_NS 2000000ULL
#define BG_COLOR_R_F 0.05F
#define BG_COLOR_G_F 0.05F
#define BG_COLOR_B_F 0.07F
#define BG_COLOR_A_F 1.0F
#define MASK_GPU0 1U
#define HOST_COHERENT_MSCALE_NS 1e6
#define MAX_INSTANCE_EXTS 16U
#define SLOW_GPU_RATIO_THRESHOLD 1.5
#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define COLOR_CHANNEL_ALPHA 3U
#define TIMESTAMP_QUERIES_PER_GPU 2U
#define TIMESTAMP_QUERY_COUNT (MAX_GPU_COUNT * TIMESTAMP_QUERIES_PER_GPU)
#define DB_CAP_MODE_VULKAN_SINGLE_GPU "vulkan_single_gpu"
#define DB_CAP_MODE_VULKAN_DEVICE_GROUP_MULTI_GPU                              \
    "vulkan_device_group_multi_gpu"
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

static const char *vk_result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

static void __attribute__((noreturn)) vk_fail(const char *expr, VkResult result,
                                              const char *file, int line) {
    failf("%s failed: %s (%d) at %s:%d", expr, vk_result_name(result),
          (int)result, file, line);
    __builtin_unreachable();
}

#define VK_CHECK(x)                                                            \
    do {                                                                       \
        VkResult _r = (x);                                                     \
        if (_r != VK_SUCCESS)                                                  \
            vk_fail(#x, _r, __FILE__, __LINE__);                               \
    } while (0)

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * NS_PER_SECOND_U64) + (uint64_t)ts.tv_nsec;
}

static uint32_t db_vk_build_device_group_mask(uint32_t device_count) {
    uint32_t mask = 0U;
    for (uint32_t i = 0U; i < device_count; i++) {
        mask |= (1U << i);
    }
    return mask;
}

typedef struct {
    float offset_ndc[2];
    float scale_ndc[2];
    float color[4];
    float base_color[4];
    float target_color[4];
    uint32_t render_mode;
    uint32_t grid_rows;
    uint32_t gradient_head_row;
    int32_t grid_clearing_phase;
    uint32_t gradient_window_rows;
    uint32_t viewport_height;
    uint32_t grid_cols;
    uint32_t snake_active_cursor;
    uint32_t snake_batch_size;
    int32_t snake_phase_completed;
    uint32_t viewport_width;
    uint32_t palette_cycle;
    uint32_t pattern_seed;
} PushConstants;

typedef struct {
    VkPhysicalDeviceGroupProperties grp;
    uint32_t presentableMask; // which physical devices can present to the
                              // surface (bitmask)
} DeviceGroupInfo;

typedef struct {
    uint32_t phys_count;
    VkPhysicalDevice phys[MAX_GPU_COUNT];
    uint32_t group_count;
    VkPhysicalDeviceGroupProperties groups[MAX_GPU_COUNT];
    int have_group;
    uint32_t chosen_count;
    VkPhysicalDevice chosen_phys[MAX_GPU_COUNT];
    uint32_t present_mask;
    VkPhysicalDevice present_phys;
} DeviceSelectionState;

typedef struct {
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
} SwapchainState;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    int layout_initialized;
} HistoryTargetState;

typedef struct {
    int initialized;
    db_vk_wsi_config_t wsi_config;
    VkInstance instance;
    VkSurfaceKHR surface;
    DeviceSelectionState selection;
    int have_group;
    uint32_t gpu_count;
    VkPhysicalDevice present_phys;
    VkDevice device;
    VkQueue queue;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkRenderPass render_pass;
    VkRenderPass history_render_pass;
    SwapchainState swapchain_state;
    HistoryTargetState history_targets[2];
    int history_read_index;
    int history_descriptor_index;
    uint32_t device_group_mask;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkSampler history_sampler;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkFence in_flight;
    VkQueryPool timing_query_pool;
    int gpu_timing_enabled;
    db_pattern_t pattern;
    uint32_t work_unit_count;
    const char *capability_mode;
    uint32_t work_owner[MAX_BAND_OWNER];
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    uint64_t bench_start_ns;
    uint64_t bench_frames;
    double next_progress_log_due_ms;
    uint64_t frame_index;
    uint32_t grid_cursor;
    uint32_t rect_snake_rect_index;
    uint32_t rect_snake_prev_start;
    uint32_t rect_snake_prev_count;
    int rect_snake_reset_pending;
    uint32_t pattern_seed;
    int grid_clearing_phase;
    uint32_t gradient_head_row;
    uint32_t gradient_cycle;
    int gradient_direction_down;
    uint32_t gradient_window_rows;
    uint32_t prev_frame_work_units[MAX_GPU_COUNT];
    uint8_t prev_frame_owner_used[MAX_GPU_COUNT];
    int have_prev_timing_frame;
    double timestamp_period_ns;
} renderer_state_t;

static renderer_state_t g_state = {0};
static const VkShaderStageFlags DB_PC_STAGES =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

static void db_vk_push_constants_frame_static(VkCommandBuffer cmd,
                                              VkPipelineLayout layout,
                                              VkExtent2D extent,
                                              uint32_t grid_rows,
                                              uint32_t grid_cols) {
    const float base_color[4] = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                                 BENCH_GRID_PHASE0_B, 1.0F};
    const float target_color[4] = {BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G,
                                   BENCH_GRID_PHASE1_B, 1.0F};

    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, gradient_window_rows),
                       sizeof(g_state.gradient_window_rows),
                       &g_state.gradient_window_rows);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, viewport_height),
                       sizeof(extent.height), &extent.height);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, grid_cols),
                       sizeof(grid_cols), &grid_cols);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, base_color),
                       sizeof(base_color), base_color);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, target_color),
                       sizeof(target_color), target_color);

    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, grid_rows),
                       sizeof(grid_rows), &grid_rows);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, viewport_width),
                       sizeof(extent.width), &extent.width);
}

static void db_vk_push_constants_draw_dynamic(
    VkCommandBuffer cmd, VkPipelineLayout layout, float ndc_x0, float ndc_y0,
    float ndc_x1, float ndc_y1, const float color[3], uint32_t render_mode,
    uint32_t gradient_head_row, int grid_clearing_phase,
    uint32_t snake_active_cursor, uint32_t snake_batch_size,
    int snake_phase_completed, uint32_t palette_cycle) {
    PushConstants pc = {0};
    pc.offset_ndc[0] = ndc_x0;
    pc.offset_ndc[1] = ndc_y0;
    pc.scale_ndc[0] = (ndc_x1 - ndc_x0);
    pc.scale_ndc[1] = (ndc_y1 - ndc_y0);
    pc.color[0] = color[0];
    pc.color[1] = color[1];
    pc.color[2] = color[2];
    pc.color[COLOR_CHANNEL_ALPHA] = 1.0F;
    pc.render_mode = render_mode;
    pc.gradient_head_row = gradient_head_row;
    pc.grid_clearing_phase = (int32_t)grid_clearing_phase;
    pc.snake_active_cursor = snake_active_cursor;
    pc.snake_batch_size = snake_batch_size;
    pc.snake_phase_completed = (int32_t)snake_phase_completed;
    pc.palette_cycle = palette_cycle;
    pc.pattern_seed =
        (render_mode == DB_PATTERN_RECT_SNAKE) ? g_state.pattern_seed : 0U;
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES, 0U,
                       (uint32_t)offsetof(PushConstants, render_mode), &pc);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, render_mode),
                       sizeof(pc.render_mode), &pc.render_mode);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, gradient_head_row),
                       sizeof(pc.gradient_head_row), &pc.gradient_head_row);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, grid_clearing_phase),
                       sizeof(pc.grid_clearing_phase), &pc.grid_clearing_phase);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_active_cursor),
                       sizeof(pc.snake_active_cursor), &pc.snake_active_cursor);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_batch_size),
                       sizeof(pc.snake_batch_size), &pc.snake_batch_size);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, snake_phase_completed),
                       sizeof(pc.snake_phase_completed),
                       &pc.snake_phase_completed);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, palette_cycle),
                       sizeof(pc.palette_cycle), &pc.palette_cycle);
    vkCmdPushConstants(cmd, layout, DB_PC_STAGES,
                       (uint32_t)offsetof(PushConstants, pattern_seed),
                       sizeof(pc.pattern_seed), &pc.pattern_seed);
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

static VkPresentModeKHR db_vk_choose_present_mode(VkPhysicalDevice present_phys,
                                                  VkSurfaceKHR surface) {
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t mode_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(present_phys, surface,
                                                       &mode_count, NULL));
    VkPresentModeKHR *modes =
        (VkPresentModeKHR *)calloc(mode_count, sizeof(VkPresentModeKHR));
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(present_phys, surface,
                                                       &mode_count, modes));
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

static VkSurfaceFormatKHR
db_vk_choose_surface_format(const VkSurfaceFormatKHR *formats,
                            uint32_t format_count) {
    if ((formats == NULL) || (format_count == 0U)) {
        failf("No Vulkan surface formats available");
    }

    // Prefer linear UNORM formats so color response matches OpenGL paths.
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

    // Fall back to first non-sRGB format when possible.
    for (uint32_t i = 0; i < format_count; i++) {
        if ((formats[i].format != VK_FORMAT_B8G8R8A8_SRGB) &&
            (formats[i].format != VK_FORMAT_R8G8B8A8_SRGB)) {
            return formats[i];
        }
    }
    return formats[0];
}

static void db_vk_destroy_swapchain_state(VkDevice device,
                                          SwapchainState *state) {
    if ((state == NULL) || (state->swapchain == VK_NULL_HANDLE)) {
        return;
    }
    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyFramebuffer(device, state->framebuffers[i], NULL);
    }
    free((void *)state->framebuffers);
    state->framebuffers = NULL;

    for (uint32_t i = 0; i < state->image_count; i++) {
        vkDestroyImageView(device, state->views[i], NULL);
    }
    free((void *)state->views);
    state->views = NULL;

    free((void *)state->images);
    state->images = NULL;
    state->image_count = 0;

    vkDestroySwapchainKHR(device, state->swapchain, NULL);
    state->swapchain = VK_NULL_HANDLE;
}

static void db_vk_destroy_history_target(VkDevice device,
                                         HistoryTargetState *target) {
    if (target == NULL) {
        return;
    }
    if (target->framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, target->framebuffer, NULL);
    }
    if (target->view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, target->view, NULL);
    }
    if (target->image != VK_NULL_HANDLE) {
        vkDestroyImage(device, target->image, NULL);
    }
    if (target->memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, target->memory, NULL);
    }
    *target = (HistoryTargetState){0};
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

static void db_vk_create_history_target(VkPhysicalDevice phys, VkDevice device,
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
    VK_CHECK(vkCreateImage(device, &ici, NULL, &target->image));

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
    VK_CHECK(vkAllocateMemory(device, &mai, NULL, &target->memory));
    VK_CHECK(vkBindImageMemory(device, target->image, target->memory, 0U));

    VkImageViewCreateInfo ivci = {.sType =
                                      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = target->image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1U;
    ivci.subresourceRange.layerCount = 1U;
    VK_CHECK(vkCreateImageView(device, &ivci, NULL, &target->view));

    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass = render_pass;
    fbci.attachmentCount = 1U;
    fbci.pAttachments = &target->view;
    fbci.width = extent.width;
    fbci.height = extent.height;
    fbci.layers = 1U;
    VK_CHECK(vkCreateFramebuffer(device, &fbci, NULL, &target->framebuffer));

    target->layout_initialized = 0;
}

static void db_vk_create_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                         VkPhysicalDevice present_phys,
                                         VkDevice device, VkSurfaceKHR surface,
                                         VkSurfaceFormatKHR fmt,
                                         VkPresentModeKHR present_mode,
                                         VkRenderPass render_pass,
                                         SwapchainState *state) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(present_phys, surface,
                                                       &caps));
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

    VK_CHECK(
        vkCreateSwapchainKHR(device, &create_info, NULL, &state->swapchain));
    state->extent = extent;

    VkResult get_images_result = vkGetSwapchainImagesKHR(
        device, state->swapchain, &state->image_count, NULL);
    if (get_images_result != VK_SUCCESS) {
        vk_fail("vkGetSwapchainImagesKHR(count)", get_images_result, __FILE__,
                __LINE__);
    }
    state->images = (VkImage *)calloc(state->image_count, sizeof(VkImage));
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    get_images_result = vkGetSwapchainImagesKHR(
        device, state->swapchain, &state->image_count, state->images);
    if (get_images_result != VK_SUCCESS) {
        free((void *)state->images);
        state->images = NULL;
        state->image_count = 0;
        vk_fail("vkGetSwapchainImagesKHR(images)", get_images_result, __FILE__,
                __LINE__);
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
        VK_CHECK(vkCreateImageView(device, &ivci, NULL, &state->views[i]));
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
        VK_CHECK(
            vkCreateFramebuffer(device, &fbci, NULL, &state->framebuffers[i]));
    }
}

static void db_vk_recreate_swapchain_state(
    const db_vk_wsi_config_t *wsi_config, VkPhysicalDevice present_phys,
    VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR fmt,
    VkPresentModeKHR present_mode, VkRenderPass render_pass,
    SwapchainState *state) {
    VK_CHECK(vkDeviceWaitIdle(device));
    db_vk_destroy_swapchain_state(device, state);
    db_vk_create_swapchain_state(wsi_config, present_phys, device, surface, fmt,
                                 present_mode, render_pass, state);
}

static int db_vk_copy_history_image_preserve(VkDevice device,
                                             VkCommandPool command_pool,
                                             VkQueue queue, VkImage src_image,
                                             VkExtent2D src_extent,
                                             VkImage dst_image,
                                             VkExtent2D dst_extent) {
    if ((src_image == VK_NULL_HANDLE) || (dst_image == VK_NULL_HANDLE) ||
        (src_extent.width == 0U) || (src_extent.height == 0U) ||
        (dst_extent.width == 0U) || (dst_extent.height == 0U)) {
        return 0;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1U;
    VkCommandBuffer copy_cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &copy_cmd));

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(copy_cmd, &begin_info));

    VkImageMemoryBarrier pre_copy[2] = {{0}, {0}};
    pre_copy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_copy[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pre_copy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pre_copy[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pre_copy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre_copy[0].image = src_image;
    pre_copy[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_copy[0].subresourceRange.levelCount = 1U;
    pre_copy[0].subresourceRange.layerCount = 1U;
    pre_copy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_copy[1].srcAccessMask = 0;
    pre_copy[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre_copy[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pre_copy[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre_copy[1].image = dst_image;
    pre_copy[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_copy[1].subresourceRange.levelCount = 1U;
    pre_copy[1].subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         2U, pre_copy);

    VkImageCopy region = {0};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1U;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1U;
    region.extent.width = (src_extent.width < dst_extent.width)
                              ? src_extent.width
                              : dst_extent.width;
    region.extent.height = (src_extent.height < dst_extent.height)
                               ? src_extent.height
                               : dst_extent.height;
    region.extent.depth = 1U;
    vkCmdCopyImage(copy_cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U,
                   &region);

    VkImageMemoryBarrier post_copy[2] = {{0}, {0}};
    post_copy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_copy[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post_copy[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post_copy[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post_copy[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_copy[0].image = src_image;
    post_copy[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_copy[0].subresourceRange.levelCount = 1U;
    post_copy[0].subresourceRange.layerCount = 1U;
    post_copy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_copy[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post_copy[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post_copy[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post_copy[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post_copy[1].image = dst_image;
    post_copy[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_copy[1].subresourceRange.levelCount = 1U;
    post_copy[1].subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(copy_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 2U, post_copy);

    VK_CHECK(vkEndCommandBuffer(copy_cmd));
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1U;
    submit_info.pCommandBuffers = &copy_cmd;
    VK_CHECK(vkQueueSubmit(queue, 1U, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, command_pool, 1U, &copy_cmd);
    return 1;
}

static int db_vk_recreate_history_targets_preserve(
    VkPhysicalDevice phys, VkDevice device, VkFormat format, VkExtent2D extent,
    VkRenderPass render_pass, uint32_t device_group_mask,
    VkCommandPool command_pool, VkQueue queue, VkExtent2D old_extent,
    HistoryTargetState *targets, int *read_index) {
    if ((targets == NULL) || (read_index == NULL)) {
        return 0;
    }

    VK_CHECK(vkDeviceWaitIdle(device));
    HistoryTargetState old_targets[2] = {targets[0], targets[1]};
    const int old_read = *read_index;
    targets[0] = (HistoryTargetState){0};
    targets[1] = (HistoryTargetState){0};
    db_vk_create_history_target(phys, device, format, extent, render_pass,
                                device_group_mask, &targets[0]);
    db_vk_create_history_target(phys, device, format, extent, render_pass,
                                device_group_mask, &targets[1]);

    int copied = 0;
    if ((old_read == 0 || old_read == 1) &&
        (old_targets[old_read].layout_initialized != 0)) {
        copied = db_vk_copy_history_image_preserve(
            device, command_pool, queue, old_targets[old_read].image,
            old_extent, targets[0].image, extent);
        copied = copied &&
                 db_vk_copy_history_image_preserve(
                     device, command_pool, queue, old_targets[old_read].image,
                     old_extent, targets[1].image, extent);
    }
    targets[0].layout_initialized = copied;
    targets[1].layout_initialized = copied;
    *read_index = 0;

    db_vk_destroy_history_target(device, &old_targets[0]);
    db_vk_destroy_history_target(device, &old_targets[1]);
    return copied;
}

static void db_vk_update_history_descriptor(VkDevice device,
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

static void db_vk_grid_span_bounds_ndc(uint32_t row, uint32_t col_start,
                                       uint32_t col_end, uint32_t rows,
                                       uint32_t cols, float *x0, float *y0,
                                       float *x1, float *y1) {
    const float inv_cols = 1.0F / (float)cols;
    const float inv_rows = 1.0F / (float)rows;
    *x0 = (2.0F * (float)col_start * inv_cols) - 1.0F;
    *x1 = (2.0F * (float)col_end * inv_cols) - 1.0F;
    *y0 = (2.0F * (float)row * inv_rows) - 1.0F;
    *y1 = (2.0F * (float)(row + 1U) * inv_rows) - 1.0F;
}

static void
db_vk_draw_grid_span(VkCommandBuffer cmd, VkPipelineLayout layout,
                     VkExtent2D extent, uint32_t grid_rows, uint32_t grid_cols,
                     uint32_t row, uint32_t col_start, uint32_t col_end,
                     const float color[3], uint32_t render_mode,
                     uint32_t gradient_head_row, int grid_clearing_phase,
                     uint32_t snake_active_cursor, uint32_t snake_batch_size,
                     int snake_phase_completed, uint32_t palette_cycle) {
    if ((grid_rows == 0U) || (grid_cols == 0U) || (col_end <= col_start) ||
        (row >= grid_rows)) {
        return;
    }

    uint32_t x0 = (extent.width * col_start) / grid_cols;
    uint32_t x1 = (extent.width * col_end) / grid_cols;
    uint32_t y0 = (extent.height * row) / grid_rows;
    uint32_t y1 = (extent.height * (row + 1U)) / grid_rows;
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }

    VkRect2D sc;
    sc.offset.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
    sc.offset.y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", y0);
    sc.extent.width = x1 - x0;
    sc.extent.height = y1 - y0;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    float ndc_x0 = 0.0F;
    float ndc_y0 = 0.0F;
    float ndc_x1 = 0.0F;
    float ndc_y1 = 0.0F;
    db_vk_grid_span_bounds_ndc(row, col_start, col_end, grid_rows, grid_cols,
                               &ndc_x0, &ndc_y0, &ndc_x1, &ndc_y1);

    db_vk_push_constants_draw_dynamic(
        cmd, layout, ndc_x0, ndc_y0, ndc_x1, ndc_y1, color, render_mode,
        gradient_head_row, grid_clearing_phase, snake_active_cursor,
        snake_batch_size, snake_phase_completed, palette_cycle);
    vkCmdDraw(cmd, DB_RECT_VERTEX_COUNT, 1, 0, 0);
}

static void db_vk_draw_grid_row_block(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row_start,
    uint32_t row_end, const float color[3], uint32_t render_mode,
    uint32_t gradient_head_row, int grid_clearing_phase,
    uint32_t snake_active_cursor, uint32_t snake_batch_size,
    int snake_phase_completed, uint32_t palette_cycle) {
    if ((row_end <= row_start) || (row_start >= grid_rows) ||
        (grid_rows == 0U) || (grid_cols == 0U)) {
        return;
    }
    if (row_end > grid_rows) {
        row_end = grid_rows;
    }

    const uint32_t y0 = (extent.height * row_start) / grid_rows;
    const uint32_t y1 = (extent.height * row_end) / grid_rows;
    if (y1 <= y0) {
        return;
    }
    VkRect2D sc = {0};
    sc.offset.x = 0;
    sc.offset.y = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", y0);
    sc.extent.width = extent.width;
    sc.extent.height = y1 - y0;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    const float inv_rows = 1.0F / (float)grid_rows;
    const float ndc_y0 = (2.0F * (float)row_start * inv_rows) - 1.0F;
    const float ndc_y1 = (2.0F * (float)row_end * inv_rows) - 1.0F;
    db_vk_push_constants_draw_dynamic(
        cmd, layout, -1.0F, ndc_y0, 1.0F, ndc_y1, color, render_mode,
        gradient_head_row, grid_clearing_phase, snake_active_cursor,
        snake_batch_size, snake_phase_completed, palette_cycle);
    vkCmdDraw(cmd, DB_RECT_VERTEX_COUNT, 1, 0, 0);
}

static uint32_t
db_vk_select_owner_for_work(uint32_t candidate_owner, uint32_t gpu_count,
                            uint32_t work_units, uint64_t frame_start_ns,
                            uint64_t budget_ns, uint64_t safety_ns,
                            const double *ema_ms_per_unit);
static void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                                     VkQueryPool query_pool, uint32_t owner,
                                     uint8_t *owner_started);
static void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                                   VkQueryPool query_pool, uint32_t owner,
                                   uint8_t *owner_finished);

static void db_vk_draw_owner_grid_span(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3], uint32_t render_mode,
    uint32_t gradient_head_row, int grid_clearing_phase,
    uint32_t snake_active_cursor, uint32_t snake_batch_size,
    int snake_phase_completed, uint32_t palette_cycle) {
    if ((span_units == 0U) || (grid_tiles_drawn == NULL) ||
        (grid_tiles_per_gpu == NULL) || (frame_work_units == NULL)) {
        return;
    }

    uint32_t owner = db_vk_select_owner_for_work(
        candidate_owner, active_gpu_count, span_units, frame_start_ns,
        budget_ns, safety_ns, ema_ms_per_work_unit);
    grid_tiles_per_gpu[owner] += span_units;
    *grid_tiles_drawn += span_units;
    if (have_group) {
        vkCmdSetDeviceMask(cmd, (MASK_GPU0 << owner));
    }
    db_vk_owner_timing_begin(cmd, timing_enabled, timing_query_pool, owner,
                             frame_owner_used);
    db_vk_draw_grid_span(cmd, layout, extent, grid_rows, grid_cols, row,
                         col_start, col_end, color, render_mode,
                         gradient_head_row, grid_clearing_phase,
                         snake_active_cursor, snake_batch_size,
                         snake_phase_completed, palette_cycle);
    db_vk_owner_timing_end(cmd, timing_enabled, timing_query_pool, owner,
                           frame_owner_finished);
    frame_work_units[owner] += span_units;
}

static void db_vk_draw_owner_grid_row_block(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row_start,
    uint32_t row_end, const float color[3], uint32_t render_mode,
    uint32_t gradient_head_row, int grid_clearing_phase,
    uint32_t snake_active_cursor, uint32_t snake_batch_size,
    int snake_phase_completed, uint32_t palette_cycle) {
    if ((span_units == 0U) || (grid_tiles_drawn == NULL) ||
        (grid_tiles_per_gpu == NULL) || (frame_work_units == NULL)) {
        return;
    }

    uint32_t owner = db_vk_select_owner_for_work(
        candidate_owner, active_gpu_count, span_units, frame_start_ns,
        budget_ns, safety_ns, ema_ms_per_work_unit);
    grid_tiles_per_gpu[owner] += span_units;
    *grid_tiles_drawn += span_units;
    if (have_group) {
        vkCmdSetDeviceMask(cmd, (MASK_GPU0 << owner));
    }
    db_vk_owner_timing_begin(cmd, timing_enabled, timing_query_pool, owner,
                             frame_owner_used);
    db_vk_draw_grid_row_block(cmd, layout, extent, grid_rows, grid_cols,
                              row_start, row_end, color, render_mode,
                              gradient_head_row, grid_clearing_phase,
                              snake_active_cursor, snake_batch_size,
                              snake_phase_completed, palette_cycle);
    db_vk_owner_timing_end(cmd, timing_enabled, timing_query_pool, owner,
                           frame_owner_finished);
    frame_work_units[owner] += span_units;
}

static void db_vk_draw_owner_grid_span_legacy(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3]) {
    db_vk_draw_owner_grid_span(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row, col_start, col_end, color,
        DB_PATTERN_BANDS, 0U, 0, 0U, 0U, 0, 0U);
}

static void db_vk_draw_owner_grid_span_snake(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3], uint32_t active_cursor,
    int clearing_phase, uint32_t batch_size, int phase_completed) {
    db_vk_draw_owner_grid_span(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row, col_start, col_end, color,
        DB_PATTERN_SNAKE_GRID, active_cursor, clearing_phase, active_cursor,
        batch_size, phase_completed, 0U);
}

static void db_vk_draw_owner_grid_span_gradient_window(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3], uint32_t head_row,
    uint32_t cycle_index, int direction_down) {
    db_vk_draw_owner_grid_span(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row, col_start, col_end, color,
        DB_PATTERN_GRADIENT_SWEEP, head_row, 0, row, 0U, direction_down,
        cycle_index);
}

static void db_vk_draw_owner_grid_span_gradient(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3], uint32_t head_row,
    uint32_t cycle_index) {
    db_vk_draw_owner_grid_span(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row, col_start, col_end, color,
        DB_PATTERN_GRADIENT_FILL, head_row, 0, 0U, 0U, 0, cycle_index);
}

static void db_vk_draw_owner_grid_row_block_gradient(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row_start,
    uint32_t row_end, const float color[3], uint32_t head_row,
    uint32_t cycle_index) {
    db_vk_draw_owner_grid_row_block(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row_start, row_end, color,
        DB_PATTERN_GRADIENT_FILL, head_row, 0, 0U, 0U, 0, cycle_index);
}

static void db_vk_draw_owner_grid_span_rect(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row, uint32_t col_start,
    uint32_t col_end, const float color[3], uint32_t rect_index,
    uint32_t active_cursor, uint32_t batch_size, int rect_completed) {
    db_vk_draw_owner_grid_span(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row, col_start, col_end, color,
        DB_PATTERN_RECT_SNAKE, rect_index, 0, active_cursor, batch_size,
        rect_completed, 0U);
}

static void db_vk_draw_owner_grid_row_block_rect(
    VkCommandBuffer cmd, VkPipelineLayout layout, VkExtent2D extent,
    int have_group, uint32_t active_gpu_count, uint32_t candidate_owner,
    uint32_t span_units, uint64_t frame_start_ns, uint64_t budget_ns,
    uint64_t safety_ns, const double *ema_ms_per_work_unit, int timing_enabled,
    VkQueryPool timing_query_pool, uint8_t *frame_owner_used,
    uint8_t *frame_owner_finished, uint32_t *frame_work_units,
    uint32_t *grid_tiles_per_gpu, uint32_t *grid_tiles_drawn,
    uint32_t grid_rows, uint32_t grid_cols, uint32_t row_start,
    uint32_t row_end, const float color[3], uint32_t rect_index,
    uint32_t active_cursor, uint32_t batch_size, int rect_completed) {
    db_vk_draw_owner_grid_row_block(
        cmd, layout, extent, have_group, active_gpu_count, candidate_owner,
        span_units, frame_start_ns, budget_ns, safety_ns, ema_ms_per_work_unit,
        timing_enabled, timing_query_pool, frame_owner_used,
        frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
        grid_tiles_drawn, grid_rows, grid_cols, row_start, row_end, color,
        DB_PATTERN_RECT_SNAKE, rect_index, 0, active_cursor, batch_size,
        rect_completed, 0U);
}

static uint32_t
db_vk_select_owner_for_work(uint32_t candidate_owner, uint32_t gpu_count,
                            uint32_t work_units, uint64_t frame_start_ns,
                            uint64_t budget_ns, uint64_t safety_ns,
                            const double *ema_ms_per_unit) {
    uint32_t owner = candidate_owner;
    if (owner >= gpu_count) {
        owner = 0U;
    }
    if ((owner == 0U) || (gpu_count <= 1U) || (ema_ms_per_unit == NULL)) {
        return 0U;
    }

    const double base = ema_ms_per_unit[0];
    if (base > 0.0) {
        const double ratio = ema_ms_per_unit[owner] / base;
        if (ratio > SLOW_GPU_RATIO_THRESHOLD) {
            return 0U;
        }
    }

    const uint64_t predicted_ns =
        (uint64_t)(ema_ms_per_unit[owner] * HOST_COHERENT_MSCALE_NS *
                   (double)((work_units > 0U) ? work_units : 1U));
    const uint64_t now = now_ns();
    if ((now + predicted_ns) > (frame_start_ns + budget_ns - safety_ns)) {
        return 0U;
    }
    return owner;
}

static void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                                     VkQueryPool query_pool, uint32_t owner,
                                     uint8_t *owner_started) {
    if ((!timing_enabled) || (owner_started == NULL)) {
        return;
    }
    if (owner_started[owner] == 0U) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                            owner * TIMESTAMP_QUERIES_PER_GPU);
        owner_started[owner] = 1U;
    }
}

static void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                                   VkQueryPool query_pool, uint32_t owner,
                                   uint8_t *owner_finished) {
    if ((!timing_enabled) || (owner_finished == NULL)) {
        return;
    }
    if (owner_finished[owner] != 0U) {
        return;
    }
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool,
                        (owner * TIMESTAMP_QUERIES_PER_GPU) + 1U);
    owner_finished[owner] = 1U;
}

static void db_vk_update_ema_fallback(db_pattern_t pattern, uint32_t gpu_count,
                                      const uint32_t *work_owner,
                                      const uint32_t *grid_tiles_per_gpu,
                                      uint32_t grid_tiles_drawn,
                                      double frame_ms,
                                      double *ema_ms_per_work_unit) {
    if (pattern == DB_PATTERN_BANDS) {
        const double ms_per_work_unit = frame_ms / (double)BENCH_BANDS;
        uint32_t bands_per_gpu[MAX_GPU_COUNT] = {0};
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            bands_per_gpu[work_owner[b]]++;
        }
        for (uint32_t g = 0; g < gpu_count; g++) {
            if (bands_per_gpu[g] == 0U) {
                continue;
            }
            ema_ms_per_work_unit[g] = (EMA_KEEP * ema_ms_per_work_unit[g]) +
                                      (EMA_NEW * ms_per_work_unit);
        }
        return;
    }

    const double ms_per_tile =
        frame_ms / (double)((grid_tiles_drawn > 0U) ? grid_tiles_drawn : 1U);
    for (uint32_t g = 0; g < gpu_count; g++) {
        if (grid_tiles_per_gpu[g] == 0U) {
            continue;
        }
        ema_ms_per_work_unit[g] =
            (EMA_KEEP * ema_ms_per_work_unit[g]) + (EMA_NEW * ms_per_tile);
    }
}

static void db_vk_cleanup_runtime(
    VkDevice device, VkFence in_flight, VkSemaphore image_available,
    VkSemaphore render_done, VkBuffer vertex_buffer,
    VkDeviceMemory vertex_memory, VkPipeline pipeline,
    VkPipelineLayout pipeline_layout, SwapchainState *swapchain_state,
    HistoryTargetState *history_targets, VkRenderPass render_pass,
    VkRenderPass history_render_pass, VkCommandPool command_pool,
    VkQueryPool timing_query_pool, VkDescriptorSetLayout descriptor_set_layout,
    VkDescriptorPool descriptor_pool, VkSampler history_sampler,
    VkInstance instance, VkSurfaceKHR surface,
    DeviceSelectionState *selection) {
    vkDestroyFence(device, in_flight, NULL);
    vkDestroySemaphore(device, image_available, NULL);
    vkDestroySemaphore(device, render_done, NULL);
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    if (history_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, history_sampler, NULL);
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    }
    if (descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
    }
    db_vk_destroy_swapchain_state(device, swapchain_state);
    db_vk_destroy_history_target(device, &history_targets[0]);
    db_vk_destroy_history_target(device, &history_targets[1]);
    vkDestroyRenderPass(device, history_render_pass, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    if (timing_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, timing_query_pool, NULL);
    }
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    (void)selection;
}

static DeviceSelectionState
db_vk_select_devices_and_group(VkInstance instance, VkSurfaceKHR surface) {
    DeviceSelectionState selection = {0};

    VK_CHECK(vkEnumeratePhysicalDevices(instance, &selection.phys_count, NULL));
    if (selection.phys_count == 0) {
        failf("No Vulkan physical devices found");
    }
    if (selection.phys_count > MAX_GPU_COUNT) {
        failf("Too many Vulkan physical devices (%u > %u)",
              selection.phys_count, MAX_GPU_COUNT);
    }
    VkResult enumerate_phys_result = vkEnumeratePhysicalDevices(
        instance, &selection.phys_count, selection.phys);
    if (enumerate_phys_result != VK_SUCCESS) {
        vk_fail("vkEnumeratePhysicalDevices", enumerate_phys_result, __FILE__,
                __LINE__);
    }

    VK_CHECK(vkEnumeratePhysicalDeviceGroups(instance, &selection.group_count,
                                             NULL));
    if (selection.group_count > MAX_GPU_COUNT) {
        failf("Too many Vulkan device groups (%u > %u)", selection.group_count,
              MAX_GPU_COUNT);
    }
    for (uint32_t i = 0; i < selection.group_count; i++) {
        selection.groups[i].sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    }
    VkResult enumerate_groups_result = vkEnumeratePhysicalDeviceGroups(
        instance, &selection.group_count, selection.groups);
    if (enumerate_groups_result != VK_SUCCESS) {
        vk_fail("vkEnumeratePhysicalDeviceGroups", enumerate_groups_result,
                __FILE__, __LINE__);
    }

    DeviceGroupInfo best = {0};
    for (uint32_t gi = 0; gi < selection.group_count; gi++) {
        VkPhysicalDeviceGroupProperties *group_props = &selection.groups[gi];
        if (group_props->physicalDeviceCount < 2) {
            continue;
        }

        uint32_t mask = 0;
        for (uint32_t di = 0; di < group_props->physicalDeviceCount; di++) {
            VkPhysicalDevice pd = group_props->physicalDevices[di];
            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_count, NULL);
            VkQueueFamilyProperties *queue_props =
                (VkQueueFamilyProperties *)calloc(
                    queue_count, sizeof(VkQueueFamilyProperties));
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_count,
                                                     queue_props);

            for (uint32_t qi = 0; qi < queue_count; qi++) {
                VkBool32 supports_present = 0;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, qi, surface,
                                                     &supports_present);
                if (supports_present &&
                    (queue_props[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    mask |= (MASK_GPU0 << di);
                    break;
                }
            }
            free((void *)queue_props);
        }

        if (mask == 0U) {
            continue;
        }

        best.grp = *group_props;
        best.presentableMask = mask;
        selection.have_group = 1;
        break;
    }

    selection.chosen_count = 1U;
    selection.present_mask = MASK_GPU0;
    if (selection.have_group) {
        selection.chosen_count = best.grp.physicalDeviceCount;
        if (selection.chosen_count > MAX_GPU_COUNT) {
            infof("Device group has %u devices; capping active GPUs to %u",
                  selection.chosen_count, MAX_GPU_COUNT);
            selection.chosen_count = MAX_GPU_COUNT;
        }
        for (uint32_t i = 0; i < selection.chosen_count; i++) {
            selection.chosen_phys[i] = best.grp.physicalDevices[i];
        }
        const uint32_t usable_mask =
            (selection.chosen_count >= 32U)
                ? 0xFFFFFFFFU
                : ((1U << selection.chosen_count) - 1U);
        selection.present_mask = best.presentableMask & usable_mask;
        infof("Using device group with %u devices (presentMask=0x%x)",
              selection.chosen_count, selection.present_mask);
    } else {
        selection.chosen_phys[0] = selection.phys[0];
        infof("No usable device group found; running single-GPU");
    }

    uint32_t present_device_index = 0;
    if (selection.have_group && !(selection.present_mask & MASK_GPU0)) {
        for (uint32_t i = 0; i < selection.chosen_count; i++) {
            if (selection.present_mask & (MASK_GPU0 << i)) {
                present_device_index = i;
                break;
            }
        }
    }
    selection.present_phys = selection.chosen_phys[present_device_index];
    return selection;
}

void db_renderer_vulkan_1_2_multi_gpu_init(
    const db_vk_wsi_config_t *wsi_config) {
    if ((wsi_config == NULL) || (wsi_config->window_handle == NULL) ||
        (wsi_config->get_required_instance_extensions == NULL) ||
        (wsi_config->create_window_surface == NULL) ||
        (wsi_config->get_framebuffer_size == NULL)) {
        failf("Invalid Vulkan WSI config provided to renderer init");
    }

    // ---------------- Instance ----------------
    uint32_t required_ext_count = 0;
    const char *const *required_exts =
        wsi_config->get_required_instance_extensions(&required_ext_count,
                                                     wsi_config->user_data);
    if ((required_ext_count == 0U) || (required_exts == NULL)) {
        failf("Windowing backend did not provide Vulkan instance extensions");
    }

    const char *instExts[MAX_INSTANCE_EXTS];
    uint32_t instExtN = 0;
    for (uint32_t i = 0; i < required_ext_count; i++) {
        instExts[instExtN++] = required_exts[i];
    }
    instExts[instExtN++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "multi_gpu_2d";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {.sType =
                                    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = instExtN;
    ici.ppEnabledExtensionNames = instExts;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&ici, NULL, &instance));

    VkSurfaceKHR surface;
    VkResult create_surface_result = wsi_config->create_window_surface(
        instance, wsi_config->window_handle, &surface, wsi_config->user_data);
    if (create_surface_result != VK_SUCCESS) {
        vk_fail("create_window_surface", create_surface_result, __FILE__,
                __LINE__);
    }

    // ---------------- Enumerate physical devices + groups ----------------
    DeviceSelectionState selection =
        db_vk_select_devices_and_group(instance, surface);
    const int haveGroup = selection.have_group;
    const uint32_t chosenCount = selection.chosen_count;
    VkPhysicalDevice *const chosenPhys = selection.chosen_phys;
    const VkPhysicalDevice presentPhys = selection.present_phys;
    const uint32_t device_group_mask =
        haveGroup ? db_vk_build_device_group_mask(chosenCount) : 0U;

    // ---------------- Queue family selection (graphics+present)
    // ----------------
    uint32_t qfN = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(presentPhys, &qfN, NULL);
    VkQueueFamilyProperties *qf =
        (VkQueueFamilyProperties *)calloc(qfN, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(presentPhys, &qfN, qf);

    uint32_t gfxQF = UINT32_MAX;
    for (uint32_t i = 0; i < qfN; i++) {
        VkBool32 supp = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(presentPhys, i, surface, &supp);
        if (supp && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            gfxQF = i;
            break;
        }
    }
    if (gfxQF == UINT32_MAX) {
        failf("No graphics+present queue family found");
    }
    const uint32_t queue_timestamp_valid_bits = qf[gfxQF].timestampValidBits;
    free(qf);

    VkPhysicalDeviceProperties phys_props;
    vkGetPhysicalDeviceProperties(presentPhys, &phys_props);
    const double timestamp_period_ns =
        (double)phys_props.limits.timestampPeriod;

    // ---------------- Device creation (with device group pNext if available)
    // ----------------
    float prio = 1.0F;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = gfxQF;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[MAX_GPU_COUNT];
    uint32_t devExtN = 0;
    devExts[devExtN++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    // Device-group functionality is requested via
    // VkDeviceGroupDeviceCreateInfo.

    VkPhysicalDeviceFeatures feats = {0};

    VkDeviceGroupDeviceCreateInfo dgci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
    dgci.physicalDeviceCount = chosenCount;
    dgci.pPhysicalDevices = chosenPhys;

    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.enabledExtensionCount = devExtN;
    dci.pEnabledFeatures = &feats;

    if (haveGroup) {
        dci.pNext = &dgci;
    }

    VkDevice device;
    VK_CHECK(vkCreateDevice(presentPhys, &dci, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, gfxQF, 0, &queue);

    // ---------------- Surface format / present mode ----------------
    uint32_t fmtN = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(presentPhys, surface, &fmtN,
                                                  NULL));
    VkSurfaceFormatKHR *fmts =
        (VkSurfaceFormatKHR *)calloc(fmtN, sizeof(VkSurfaceFormatKHR));
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(presentPhys, surface, &fmtN,
                                                  fmts));
    VkSurfaceFormatKHR fmt = db_vk_choose_surface_format(fmts, fmtN);
    free(fmts);

    VkPresentModeKHR presentMode =
        db_vk_choose_present_mode(presentPhys, surface);

    // ---------------- Render pass ----------------
    VkAttachmentDescription colorAtt = {0};
    colorAtt.format = fmt.format;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &colorAtt;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &rpci, NULL, &renderPass));

    VkAttachmentDescription historyAtt = colorAtt;
    historyAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    historyAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    historyAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo history_rpci = rpci;
    history_rpci.pAttachments = &historyAtt;
    VkRenderPass historyRenderPass;
    VK_CHECK(
        vkCreateRenderPass(device, &history_rpci, NULL, &historyRenderPass));

    // ---------------- Swapchain state ----------------
    SwapchainState swapchain_state = {0};
    db_vk_create_swapchain_state(wsi_config, presentPhys, device, surface, fmt,
                                 presentMode, renderPass, &swapchain_state);
    HistoryTargetState history_targets[2] = {{0}, {0}};
    db_vk_create_history_target(presentPhys, device, fmt.format,
                                swapchain_state.extent, historyRenderPass,
                                device_group_mask, &history_targets[0]);
    db_vk_create_history_target(presentPhys, device, fmt.format,
                                swapchain_state.extent, historyRenderPass,
                                device_group_mask, &history_targets[1]);

    // ---------------- Pipeline (rectangles) ----------------
    size_t vsz = 0;
    size_t fsz = 0;
    uint8_t *vbin = db_read_file_or_fail(BACKEND_NAME, VERT_SPV_PATH, &vsz);
    uint8_t *fbin = db_read_file_or_fail(BACKEND_NAME, FRAG_SPV_PATH, &fsz);

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs, fs;
    smci.codeSize = vsz;
    smci.pCode = (const uint32_t *)vbin;
    VK_CHECK(vkCreateShaderModule(device, &smci, NULL, &vs));
    smci.codeSize = fsz;
    smci.pCode = (const uint32_t *)fbin;
    VK_CHECK(vkCreateShaderModule(device, &smci, NULL, &fs));
    free(vbin);
    free(fbin);

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // Vertex buffer: a unit quad as two triangles (6 verts), in_pos in [0..1]
    float quadVerts[QUAD_VERT_FLOAT_COUNT] = {0, 0, 1, 0, 1, 1,
                                              0, 0, 1, 1, 0, 1};

    // Create a tiny host-visible vertex buffer (skipping allocator
    // sophistication)
    VkBuffer vbuf;
    VkDeviceMemory vmem;

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sizeof(quadVerts);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(device, &bci, NULL, &vbuf));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, vbuf, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(presentPhys, &mp);

    uint32_t memIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (MASK_GPU0 << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT32_MAX) {
        failf("No host-visible + host-coherent memory type for vertex buffer");
    }

    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memIndex;
    VK_CHECK(vkAllocateMemory(device, &mai, NULL, &vmem));
    VK_CHECK(vkBindBufferMemory(device, vbuf, vmem, 0));

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, vmem, 0, sizeof(quadVerts), 0, &mapped));
    {
        float *mapped_f32 = (float *)mapped;
        for (size_t i = 0; i < QUAD_VERT_FLOAT_COUNT; i++) {
            mapped_f32[i] = quadVerts[i];
        }
    }
    vkUnmapMemory(device, vmem);

    VkVertexInputBindingDescription bind = {0};
    bind.binding = 0;
    bind.stride = sizeof(float) * 2;
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr = {0};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vis = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = 1;
    vis.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                  VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushConstants);

    VkDescriptorSetLayoutBinding history_binding = {0};
    history_binding.binding = 0U;
    history_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    history_binding.descriptorCount = 1U;
    history_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1U;
    dslci.pBindings = &history_binding;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &dslci, NULL,
                                         &descriptor_set_layout));

    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1U;
    plci.pSetLayouts = &descriptor_set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, NULL, &layout));

    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vis;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = layout;
    gp.renderPass = renderPass;
    gp.subpass = 0;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL,
                                       &pipeline));

    VkSamplerCreateInfo sampler_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_ci.magFilter = VK_FILTER_NEAREST;
    sampler_ci.minFilter = VK_FILTER_NEAREST;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxLod = 0.0F;
    VkSampler history_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_ci, NULL, &history_sampler));

    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1U;
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1U;
    dpci.poolSizeCount = 1U;
    dpci.pPoolSizes = &pool_size;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &dpci, NULL, &descriptor_pool));

    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptor_pool;
    dsai.descriptorSetCount = 1U;
    dsai.pSetLayouts = &descriptor_set_layout;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &dsai, &descriptor_set));
    db_vk_update_history_descriptor(device, descriptor_set, history_sampler,
                                    history_targets[0].view);

    vkDestroyShaderModule(device, vs, NULL);
    vkDestroyShaderModule(device, fs, NULL);

    // ---------------- Command pool/buffers ----------------
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = gfxQF;

    VkCommandPool cmdPool;
    VK_CHECK(vkCreateCommandPool(device, &cpci, NULL, &cmdPool));

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));

    // ---------------- Sync ----------------
    VkSemaphoreCreateInfo sci2 = {.sType =
                                      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore imageAvail, renderDone;
    VK_CHECK(vkCreateSemaphore(device, &sci2, NULL, &imageAvail));
    VK_CHECK(vkCreateSemaphore(device, &sci2, NULL, &renderDone));

    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence inFlight;
    VK_CHECK(vkCreateFence(device, &fci, NULL, &inFlight));

    const int gpu_timing_enabled =
        (queue_timestamp_valid_bits > 0U) && (timestamp_period_ns > 0.0);
    VkQueryPool timing_query_pool = VK_NULL_HANDLE;
    if (gpu_timing_enabled) {
        VkQueryPoolCreateInfo qpci = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = TIMESTAMP_QUERY_COUNT;
        VK_CHECK(vkCreateQueryPool(device, &qpci, NULL, &timing_query_pool));
    }

    // ---------------- Opportunistic scheduler state ----------------
    uint32_t work_owner[MAX_BAND_OWNER];
    uint32_t gpuCount = haveGroup ? chosenCount : 1;
    db_pattern_t pattern = DB_PATTERN_GRADIENT_SWEEP;
    if (!db_parse_benchmark_pattern_from_env(&pattern)) {
        const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
        failf("Invalid %s='%s' (expected: %s|%s|%s|%s|%s)",
              DB_BENCHMARK_MODE_ENV, (mode != NULL) ? mode : "",
              DB_BENCHMARK_MODE_GRADIENT_SWEEP, DB_BENCHMARK_MODE_BANDS,
              DB_BENCHMARK_MODE_SNAKE_GRID, DB_BENCHMARK_MODE_GRADIENT_FILL,
              DB_BENCHMARK_MODE_RECT_SNAKE);
    }
    uint32_t work_unit_count = db_pattern_work_unit_count(pattern);
    if (work_unit_count == 0U) {
        failf("Invalid work-unit geometry for mode '%s'",
              db_pattern_mode_name(pattern));
    }
    const int multi_gpu = haveGroup && (gpuCount > 1U);
    const char *capability_mode = NULL;
    const uint32_t run_seed =
        db_benchmark_random_seed_from_env_or_time(BACKEND_NAME);
    capability_mode = multi_gpu ? DB_CAP_MODE_VULKAN_DEVICE_GROUP_MULTI_GPU
                                : DB_CAP_MODE_VULKAN_SINGLE_GPU;

    // Start: round robin across GPUs
    if (pattern == DB_PATTERN_BANDS) {
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            work_owner[b] = b % gpuCount;
        }
    }

    // EMA ms per band per GPU
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    for (uint32_t g = 0; g < gpuCount; g++) {
        ema_ms_per_work_unit[g] = DEFAULT_EMA_MS_PER_WORK_UNIT; // seed guess
    }

    // Frame budget (ns): approximate 60Hz if FIFO, else still useful heuristic
    const uint64_t budget_ns = FRAME_BUDGET_NS;
    const uint64_t safety_ns = FRAME_SAFETY_NS;
    uint32_t prev_frame_work_units[MAX_GPU_COUNT] = {0};
    uint8_t prev_frame_owner_used[MAX_GPU_COUNT] = {0};
    int have_prev_timing_frame = 0;

    g_state.initialized = 1;
    g_state.wsi_config = *wsi_config;
    g_state.instance = instance;
    g_state.surface = surface;
    g_state.selection = selection;
    g_state.have_group = haveGroup;
    g_state.gpu_count = gpuCount;
    g_state.present_phys = presentPhys;
    g_state.device = device;
    g_state.queue = queue;
    g_state.surface_format = fmt;
    g_state.present_mode = presentMode;
    g_state.render_pass = renderPass;
    g_state.history_render_pass = historyRenderPass;
    g_state.swapchain_state = swapchain_state;
    g_state.history_targets[0] = history_targets[0];
    g_state.history_targets[1] = history_targets[1];
    g_state.history_read_index = 0;
    g_state.history_descriptor_index = 0;
    g_state.device_group_mask = device_group_mask;
    g_state.vertex_buffer = vbuf;
    g_state.vertex_memory = vmem;
    g_state.pipeline = pipeline;
    g_state.pipeline_layout = layout;
    g_state.descriptor_set_layout = descriptor_set_layout;
    g_state.descriptor_pool = descriptor_pool;
    g_state.descriptor_set = descriptor_set;
    g_state.history_sampler = history_sampler;
    g_state.command_pool = cmdPool;
    g_state.command_buffer = cmd;
    g_state.image_available = imageAvail;
    g_state.render_done = renderDone;
    g_state.in_flight = inFlight;
    g_state.timing_query_pool = timing_query_pool;
    g_state.gpu_timing_enabled = gpu_timing_enabled;
    g_state.pattern = pattern;
    g_state.work_unit_count = work_unit_count;
    g_state.capability_mode = capability_mode;
    for (uint32_t i = 0; i < MAX_BAND_OWNER; i++) {
        g_state.work_owner[i] = work_owner[i];
    }
    for (uint32_t i = 0; i < MAX_GPU_COUNT; i++) {
        g_state.ema_ms_per_work_unit[i] = ema_ms_per_work_unit[i];
        g_state.prev_frame_work_units[i] = 0U;
        g_state.prev_frame_owner_used[i] = 0U;
    }
    g_state.have_prev_timing_frame = have_prev_timing_frame;
    g_state.timestamp_period_ns = timestamp_period_ns;
    g_state.bench_start_ns = now_ns();
    g_state.bench_frames = 0U;
    g_state.next_progress_log_due_ms = 0.0;
    g_state.frame_index = 0U;
    g_state.grid_cursor = 0U;
    g_state.rect_snake_rect_index = 0U;
    g_state.rect_snake_prev_start = 0U;
    g_state.rect_snake_prev_count = 0U;
    g_state.rect_snake_reset_pending = 1;
    g_state.pattern_seed = run_seed;
    g_state.grid_clearing_phase = 0;
    g_state.gradient_head_row = 0U;
    g_state.gradient_cycle =
        db_benchmark_cycle_from_seed(run_seed, DB_PALETTE_SALT);
    g_state.gradient_direction_down = 1;
    g_state.gradient_window_rows = db_gradient_window_rows_effective();
}

db_vk_frame_result_t db_renderer_vulkan_1_2_multi_gpu_render_frame(void) {
    if (!g_state.initialized) {
        return DB_VK_FRAME_STOP;
    }

    const uint64_t budget_ns = FRAME_BUDGET_NS;
    const uint64_t safety_ns = FRAME_SAFETY_NS;
    const uint32_t gpuCount = g_state.gpu_count;
    const uint32_t active_gpu_count = (gpuCount > 0U) ? gpuCount : 1U;
    const int haveGroup = g_state.have_group;

    VkResult wait_result = vkWaitForFences(
        g_state.device, 1, &g_state.in_flight, VK_TRUE, WAIT_TIMEOUT_NS);
    if (wait_result == VK_TIMEOUT) {
        return DB_VK_FRAME_RETRY;
    }
    if (wait_result != VK_SUCCESS) {
        vk_fail("vkWaitForFences", wait_result, __FILE__, __LINE__);
    }

    if (g_state.gpu_timing_enabled && g_state.have_prev_timing_frame) {
        uint64_t query_results[TIMESTAMP_QUERY_COUNT] = {0};
        VkResult query_result = vkGetQueryPoolResults(
            g_state.device, g_state.timing_query_pool, 0,
            gpuCount * TIMESTAMP_QUERIES_PER_GPU,
            sizeof(uint64_t) * gpuCount * TIMESTAMP_QUERIES_PER_GPU,
            query_results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (query_result == VK_SUCCESS) {
            for (uint32_t g = 0; g < gpuCount; g++) {
                if ((g_state.prev_frame_owner_used[g] == 0U) ||
                    (g_state.prev_frame_work_units[g] == 0U)) {
                    continue;
                }
                const size_t base_query =
                    (size_t)g * (size_t)TIMESTAMP_QUERIES_PER_GPU;
                const uint64_t start = query_results[base_query];
                const uint64_t end = query_results[base_query + 1U];
                if (end <= start) {
                    continue;
                }
                const double elapsed_ms =
                    ((double)(end - start) * g_state.timestamp_period_ns) /
                    NS_TO_MS_D;
                const double ms_per_unit =
                    elapsed_ms / (double)g_state.prev_frame_work_units[g];
                g_state.ema_ms_per_work_unit[g] =
                    (EMA_KEEP * g_state.ema_ms_per_work_unit[g]) +
                    (EMA_NEW * ms_per_unit);
            }
        }
    }

    VK_CHECK(vkResetFences(g_state.device, 1, &g_state.in_flight));

    uint32_t imgIndex = 0;
    VkResult ar = vkAcquireNextImageKHR(
        g_state.device, g_state.swapchain_state.swapchain, WAIT_TIMEOUT_NS,
        g_state.image_available, VK_NULL_HANDLE, &imgIndex);
    if (ar == VK_TIMEOUT) {
        return DB_VK_FRAME_RETRY;
    }
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        const VkExtent2D old_extent = g_state.swapchain_state.extent;
        db_vk_recreate_swapchain_state(
            &g_state.wsi_config, g_state.present_phys, g_state.device,
            g_state.surface, g_state.surface_format, g_state.present_mode,
            g_state.render_pass, &g_state.swapchain_state);
        const int preserved = db_vk_recreate_history_targets_preserve(
            g_state.present_phys, g_state.device, g_state.surface_format.format,
            g_state.swapchain_state.extent, g_state.history_render_pass,
            g_state.device_group_mask, g_state.command_pool, g_state.queue,
            old_extent, g_state.history_targets, &g_state.history_read_index);
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[g_state.history_read_index].view);
        g_state.history_descriptor_index = g_state.history_read_index;
        if ((g_state.pattern == DB_PATTERN_RECT_SNAKE) && (preserved == 0)) {
            g_state.rect_snake_reset_pending = 1;
        }
        g_state.frame_index++;
        return DB_VK_FRAME_RETRY;
    }
    if ((ar != VK_SUCCESS) && (ar != VK_SUBOPTIMAL_KHR)) {
        infof("AcquireNextImage returned %s (%d), ending benchmark loop",
              vk_result_name(ar), (int)ar);
        return DB_VK_FRAME_STOP;
    }
    const int acquire_suboptimal = (ar == VK_SUBOPTIMAL_KHR);
    const int history_mode = db_pattern_uses_history_texture(g_state.pattern);
    const int read_index = g_state.history_read_index;
    const int write_index = (read_index == 0) ? 1 : 0;
    if (history_mode && (g_state.history_descriptor_index != read_index) &&
        ((read_index == 0) || (read_index == 1))) {
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[read_index].view);
        g_state.history_descriptor_index = read_index;
    }

    VK_CHECK(vkResetCommandBuffer(g_state.command_buffer, 0));
    VkCommandBufferBeginInfo cbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(g_state.command_buffer, &cbi));
    uint32_t frame_work_units[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_used[MAX_GPU_COUNT] = {0};
    uint8_t frame_owner_finished[MAX_GPU_COUNT] = {0};
    if (g_state.gpu_timing_enabled) {
        vkCmdResetQueryPool(g_state.command_buffer, g_state.timing_query_pool,
                            0, gpuCount * TIMESTAMP_QUERIES_PER_GPU);
    }

    VkClearValue clear = {0};
    const int use_base_color = ((g_state.pattern == DB_PATTERN_GRADIENT_FILL) ||
                                (g_state.pattern == DB_PATTERN_SNAKE_GRID)) &&
                               (g_state.grid_clearing_phase == 0);
    const float *clear_rgb = NULL;
    if (g_state.pattern == DB_PATTERN_BANDS) {
        static const float bands_rgb[3] = {BG_COLOR_R_F, BG_COLOR_G_F,
                                           BG_COLOR_B_F};
        clear_rgb = bands_rgb;
    } else if (use_base_color) {
        static const float base_rgb[3] = {
            BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
        clear_rgb = base_rgb;
    } else {
        static const float target_rgb[3] = {
            BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B};
        clear_rgb = target_rgb;
    }
    clear.color.float32[0] = clear_rgb[0];
    clear.color.float32[1] = clear_rgb[1];
    clear.color.float32[2] = clear_rgb[2];
    clear.color.float32[COLOR_CHANNEL_ALPHA] = BG_COLOR_A_F;

    if (history_mode &&
        ((g_state.history_targets[0].layout_initialized == 0) ||
         (g_state.history_targets[1].layout_initialized == 0))) {
        VkImageMemoryBarrier history_to_clear[2] = {{0}, {0}};
        for (size_t i = 0U; i < 2U; i++) {
            history_to_clear[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_clear[i].srcAccessMask = 0;
            history_to_clear[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_clear[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            history_to_clear[i].newLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_clear[i].image = g_state.history_targets[i].image;
            history_to_clear[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_clear[i].subresourceRange.levelCount = 1U;
            history_to_clear[i].subresourceRange.layerCount = 1U;
        }
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 2U, history_to_clear);

        VkClearColorValue history_clear = {
            .float32 = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                        BENCH_GRID_PHASE0_B, 1.0F}};
        VkImageSubresourceRange history_range = {0};
        history_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        history_range.levelCount = 1U;
        history_range.layerCount = 1U;
        for (size_t i = 0U; i < 2U; i++) {
            vkCmdClearColorImage(g_state.command_buffer,
                                 g_state.history_targets[i].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &history_clear, 1U, &history_range);
        }

        VkImageMemoryBarrier history_to_read[2] = {{0}, {0}};
        for (size_t i = 0U; i < 2U; i++) {
            history_to_read[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            history_to_read[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            history_to_read[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            history_to_read[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            history_to_read[i].newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            history_to_read[i].image = g_state.history_targets[i].image;
            history_to_read[i].subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            history_to_read[i].subresourceRange.levelCount = 1U;
            history_to_read[i].subresourceRange.layerCount = 1U;
            g_state.history_targets[i].layout_initialized = 1;
        }
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 2U, history_to_read);
    }

    if (history_mode) {
        VkImageMemoryBarrier write_to_color = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_to_color.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        write_to_color.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        write_to_color.image = g_state.history_targets[write_index].image;
        write_to_color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        write_to_color.subresourceRange.levelCount = 1U;
        write_to_color.subresourceRange.layerCount = 1U;
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1U, &write_to_color);
    }

    VkRenderPassBeginInfo rbi = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass =
        history_mode ? g_state.history_render_pass : g_state.render_pass;
    rbi.framebuffer = history_mode
                          ? g_state.history_targets[write_index].framebuffer
                          : g_state.swapchain_state.framebuffers[imgIndex];
    rbi.renderArea.extent = g_state.swapchain_state.extent;
    rbi.clearValueCount = history_mode ? 0U : 1U;
    rbi.pClearValues = history_mode ? NULL : &clear;
    vkCmdBeginRenderPass(g_state.command_buffer, &rbi,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(g_state.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      g_state.pipeline);
    vkCmdBindDescriptorSets(
        g_state.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        g_state.pipeline_layout, 0U, 1U, &g_state.descriptor_set, 0U, NULL);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(g_state.command_buffer, 0, 1, &g_state.vertex_buffer,
                           &off);

    double time_s = (double)g_state.frame_index / BENCH_TARGET_FPS_D;
    uint64_t frameStart = now_ns();
    uint32_t grid_tiles_per_gpu[MAX_GPU_COUNT] = {0};
    uint32_t grid_tiles_drawn = 0U;
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint32_t grid_cols = db_grid_cols_effective();
    db_vk_push_constants_frame_static(
        g_state.command_buffer, g_state.pipeline_layout,
        g_state.swapchain_state.extent, grid_rows, grid_cols);
    VkViewport vpo = {0};
    vpo.width = (float)g_state.swapchain_state.extent.width;
    vpo.height = (float)g_state.swapchain_state.extent.height;
    vpo.maxDepth = 1.0F;
    vkCmdSetViewport(g_state.command_buffer, 0, 1, &vpo);

    if (g_state.pattern == DB_PATTERN_BANDS) {
        const float inv_extent_width =
            1.0F / (float)g_state.swapchain_state.extent.width;
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            uint32_t owner = g_state.work_owner[b];
            if (owner >= active_gpu_count) {
                owner = 0U;
            }
            owner = db_vk_select_owner_for_work(
                owner, active_gpu_count, 1U, frameStart, budget_ns, safety_ns,
                g_state.ema_ms_per_work_unit);
            if (owner == 0U) {
                g_state.work_owner[b] = 0U;
            }
            if (haveGroup) {
                vkCmdSetDeviceMask(g_state.command_buffer,
                                   (MASK_GPU0 << owner));
            }
            db_vk_owner_timing_begin(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_used);

            uint32_t x0 =
                (g_state.swapchain_state.extent.width * b) / BENCH_BANDS;
            uint32_t x1 =
                (g_state.swapchain_state.extent.width * (b + 1U)) / BENCH_BANDS;
            VkRect2D sc = {0};
            sc.offset.x = db_checked_u32_to_i32(BACKEND_NAME, "vk_i32", x0);
            sc.extent.width = x1 - x0;
            sc.extent.height = g_state.swapchain_state.extent.height;
            vkCmdSetScissor(g_state.command_buffer, 0, 1, &sc);

            float ndc_x0 = (2.0F * (float)x0 * inv_extent_width) - 1.0F;
            float ndc_x1 = (2.0F * (float)x1 * inv_extent_width) - 1.0F;
            float band_color[3] = {0.0F, 0.0F, 0.0F};
            db_band_color_rgb(b, BENCH_BANDS, time_s, &band_color[0],
                              &band_color[1], &band_color[2]);
            db_vk_push_constants_draw_dynamic(
                g_state.command_buffer, g_state.pipeline_layout, ndc_x0, -1.0F,
                ndc_x1, 1.0F, band_color, DB_PATTERN_BANDS, 0U, 0, 0U, 0U, 0,
                0U);
            vkCmdDraw(g_state.command_buffer, DB_RECT_VERTEX_COUNT, 1, 0, 0);
            db_vk_owner_timing_end(
                g_state.command_buffer, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, owner, frame_owner_finished);
            frame_work_units[owner] += 1U;
        }
    } else if (g_state.pattern == DB_PATTERN_SNAKE_GRID) {
        const db_snake_damage_plan_t grid_plan = db_snake_grid_plan_next_step(
            g_state.grid_cursor, 0U, 0U, g_state.grid_clearing_phase,
            g_state.work_unit_count);
        const uint32_t batch_size = grid_plan.batch_size;
        const uint32_t active_cursor = grid_plan.active_cursor;
        const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
        const uint32_t full_rows = active_cursor / grid_cols;
        const uint32_t row_remainder = active_cursor % grid_cols;
        const uint32_t row_span_units = grid_cols;

        for (uint32_t row = 0; row < full_rows; row++) {
            db_vk_draw_owner_grid_span_snake(
                g_state.command_buffer, g_state.pipeline_layout,
                g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                row % active_gpu_count, row_span_units, frameStart, budget_ns,
                safety_ns, g_state.ema_ms_per_work_unit,
                g_state.gpu_timing_enabled, g_state.timing_query_pool,
                frame_owner_used, frame_owner_finished, frame_work_units,
                grid_tiles_per_gpu, &grid_tiles_drawn, grid_rows, grid_cols,
                row, 0U, grid_cols, shader_ignored_color, active_cursor,
                grid_plan.clearing_phase, batch_size,
                grid_plan.phase_completed);
        }

        if ((row_remainder > 0U) && (full_rows < grid_rows)) {
            const uint32_t span_units = row_remainder;
            uint32_t col_start = 0U;
            uint32_t col_end = row_remainder;
            if ((full_rows & 1U) != 0U) {
                col_start = grid_cols - row_remainder;
                col_end = grid_cols;
            }
            db_vk_draw_owner_grid_span_snake(
                g_state.command_buffer, g_state.pipeline_layout,
                g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                full_rows % active_gpu_count, span_units, frameStart, budget_ns,
                safety_ns, g_state.ema_ms_per_work_unit,
                g_state.gpu_timing_enabled, g_state.timing_query_pool,
                frame_owner_used, frame_owner_finished, frame_work_units,
                grid_tiles_per_gpu, &grid_tiles_drawn, grid_rows, grid_cols,
                full_rows, col_start, col_end, shader_ignored_color,
                active_cursor, grid_plan.clearing_phase, batch_size,
                grid_plan.phase_completed);
        }

        for (uint32_t update_index = 0; update_index < batch_size;
             update_index++) {
            const uint32_t tile_step = active_cursor + update_index;
            if (tile_step >= g_state.work_unit_count) {
                break;
            }
            const uint32_t tile_index = db_grid_tile_index_from_step(tile_step);
            const uint32_t row = tile_index / grid_cols;
            const uint32_t col = tile_index % grid_cols;
            db_vk_draw_owner_grid_span_snake(
                g_state.command_buffer, g_state.pipeline_layout,
                g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                tile_step % active_gpu_count, 1U, frameStart, budget_ns,
                safety_ns, g_state.ema_ms_per_work_unit,
                g_state.gpu_timing_enabled, g_state.timing_query_pool,
                frame_owner_used, frame_owner_finished, frame_work_units,
                grid_tiles_per_gpu, &grid_tiles_drawn, grid_rows, grid_cols,
                row, col, col + 1U, shader_ignored_color, active_cursor,
                grid_plan.clearing_phase, batch_size,
                grid_plan.phase_completed);
        }

        g_state.grid_cursor = grid_plan.next_cursor;
        g_state.grid_clearing_phase = grid_plan.next_clearing_phase;
    } else if (g_state.pattern == DB_PATTERN_RECT_SNAKE) {
        const db_rect_snake_plan_t plan = db_rect_snake_plan_next_step(
            g_state.pattern_seed, g_state.rect_snake_rect_index,
            g_state.grid_cursor);
        const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
        const db_rect_snake_rect_t rect = db_rect_snake_rect_from_index(
            g_state.pattern_seed, plan.active_rect_index);
        if (g_state.rect_snake_reset_pending != 0) {
            const uint32_t span_units = grid_rows * grid_cols;
            db_vk_draw_owner_grid_row_block_rect(
                g_state.command_buffer, g_state.pipeline_layout,
                g_state.swapchain_state.extent, haveGroup, active_gpu_count, 0U,
                span_units, frameStart, budget_ns, safety_ns,
                g_state.ema_ms_per_work_unit, g_state.gpu_timing_enabled,
                g_state.timing_query_pool, frame_owner_used,
                frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
                &grid_tiles_drawn, grid_rows, grid_cols, 0U, grid_rows,
                shader_ignored_color, plan.active_rect_index,
                plan.active_cursor, plan.batch_size, plan.rect_completed);
            g_state.rect_snake_reset_pending = 0;
        } else {
            db_snake_col_span_t
                spans[(size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U];
            size_t span_count = 0U;
            db_snake_append_step_spans_for_rect(
                spans, (size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U,
                &span_count, rect.x, rect.y, rect.width, rect.height,
                g_state.rect_snake_prev_start, g_state.rect_snake_prev_count);
            db_snake_append_step_spans_for_rect(
                spans, (size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U,
                &span_count, rect.x, rect.y, rect.width, rect.height,
                plan.active_cursor, plan.batch_size);
            for (size_t i = 0U; i < span_count; i++) {
                const uint32_t span_units =
                    spans[i].col_end - spans[i].col_start;
                db_vk_draw_owner_grid_span_rect(
                    g_state.command_buffer, g_state.pipeline_layout,
                    g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                    spans[i].row % active_gpu_count, span_units, frameStart,
                    budget_ns, safety_ns, g_state.ema_ms_per_work_unit,
                    g_state.gpu_timing_enabled, g_state.timing_query_pool,
                    frame_owner_used, frame_owner_finished, frame_work_units,
                    grid_tiles_per_gpu, &grid_tiles_drawn, grid_rows, grid_cols,
                    spans[i].row, spans[i].col_start, spans[i].col_end,
                    shader_ignored_color, plan.active_rect_index,
                    plan.active_cursor, plan.batch_size, plan.rect_completed);
            }
        }
        g_state.rect_snake_prev_start = plan.active_cursor;
        g_state.rect_snake_prev_count =
            (plan.rect_completed != 0) ? 0U : plan.batch_size;
        g_state.grid_cursor = plan.next_cursor;
        g_state.rect_snake_rect_index = plan.next_rect_index;
        if (plan.wrapped != 0) {
            g_state.rect_snake_reset_pending = 1;
            g_state.rect_snake_prev_count = 0U;
        }
    } else if ((g_state.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const int is_sweep = (g_state.pattern == DB_PATTERN_GRADIENT_SWEEP);
        const db_gradient_damage_plan_t plan = db_gradient_plan_next_frame(
            g_state.gradient_head_row,
            is_sweep ? g_state.gradient_direction_down : 1,
            g_state.gradient_cycle, is_sweep ? 0 : 1);

        if (is_sweep != 0) {
            if ((grid_rows > 0U) && (plan.dirty_row_count > 0U)) {
                const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
                for (uint32_t i = 0U; i < plan.dirty_row_count; i++) {
                    const uint32_t row = plan.dirty_row_start + i;
                    if (row >= grid_rows) {
                        break;
                    }
                    const uint32_t span_units = grid_cols;
                    db_vk_draw_owner_grid_span_gradient_window(
                        g_state.command_buffer, g_state.pipeline_layout,
                        g_state.swapchain_state.extent, haveGroup,
                        active_gpu_count, row % active_gpu_count, span_units,
                        frameStart, budget_ns, safety_ns,
                        g_state.ema_ms_per_work_unit,
                        g_state.gpu_timing_enabled, g_state.timing_query_pool,
                        frame_owner_used, frame_owner_finished,
                        frame_work_units, grid_tiles_per_gpu, &grid_tiles_drawn,
                        grid_rows, grid_cols, row, 0U, grid_cols,
                        shader_ignored_color, plan.render_head_row,
                        plan.render_cycle_index, plan.render_direction_down);
                }
            }
            g_state.gradient_direction_down = plan.next_direction_down;
        } else {
            const uint32_t head_row = plan.render_head_row;
            const uint32_t head_row_clamped =
                (head_row < grid_rows) ? head_row : grid_rows;
            const uint32_t cycle_index = plan.render_cycle_index;

            const float shader_ignored_color[3] = {0.0F, 0.0F, 0.0F};
            const uint32_t solid_rows =
                db_gradient_solid_rows_before_head(head_row);
            if ((grid_rows > 0U) && (plan.dirty_row_count > 0U) &&
                (head_row == 0U)) {
                const uint32_t span_units = grid_rows * grid_cols;
                db_vk_draw_owner_grid_row_block_gradient(
                    g_state.command_buffer, g_state.pipeline_layout,
                    g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                    0U, span_units, frameStart, budget_ns, safety_ns,
                    g_state.ema_ms_per_work_unit, g_state.gpu_timing_enabled,
                    g_state.timing_query_pool, frame_owner_used,
                    frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
                    &grid_tiles_drawn, grid_rows, grid_cols, 0U, grid_rows,
                    shader_ignored_color, head_row, cycle_index);
            } else if ((solid_rows > 0U) && (grid_rows > 0U) &&
                       (plan.dirty_row_count > 0U)) {
                const uint32_t span_units = solid_rows * grid_cols;
                const uint32_t row_start = 0U;
                const uint32_t row_end = solid_rows;
                db_vk_draw_owner_grid_row_block_gradient(
                    g_state.command_buffer, g_state.pipeline_layout,
                    g_state.swapchain_state.extent, haveGroup, active_gpu_count,
                    0U, span_units, frameStart, budget_ns, safety_ns,
                    g_state.ema_ms_per_work_unit, g_state.gpu_timing_enabled,
                    g_state.timing_query_pool, frame_owner_used,
                    frame_owner_finished, frame_work_units, grid_tiles_per_gpu,
                    &grid_tiles_drawn, grid_rows, grid_cols, row_start, row_end,
                    shader_ignored_color, head_row, cycle_index);
                for (uint32_t row = solid_rows; row < head_row_clamped; row++) {
                    db_vk_draw_owner_grid_span_gradient(
                        g_state.command_buffer, g_state.pipeline_layout,
                        g_state.swapchain_state.extent, haveGroup,
                        active_gpu_count, row % active_gpu_count, grid_cols,
                        frameStart, budget_ns, safety_ns,
                        g_state.ema_ms_per_work_unit,
                        g_state.gpu_timing_enabled, g_state.timing_query_pool,
                        frame_owner_used, frame_owner_finished,
                        frame_work_units, grid_tiles_per_gpu, &grid_tiles_drawn,
                        grid_rows, grid_cols, row, 0U, grid_cols,
                        shader_ignored_color, head_row, cycle_index);
                }
            } else if ((grid_rows > 0U) && (plan.dirty_row_count > 0U)) {
                for (uint32_t row = solid_rows; row < head_row_clamped; row++) {
                    db_vk_draw_owner_grid_span_gradient(
                        g_state.command_buffer, g_state.pipeline_layout,
                        g_state.swapchain_state.extent, haveGroup,
                        active_gpu_count, row % active_gpu_count, grid_cols,
                        frameStart, budget_ns, safety_ns,
                        g_state.ema_ms_per_work_unit,
                        g_state.gpu_timing_enabled, g_state.timing_query_pool,
                        frame_owner_used, frame_owner_finished,
                        frame_work_units, grid_tiles_per_gpu, &grid_tiles_drawn,
                        grid_rows, grid_cols, row, 0U, grid_cols,
                        shader_ignored_color, head_row, cycle_index);
                }
            }
        }
        g_state.gradient_head_row = plan.next_head_row;
        g_state.gradient_cycle = plan.next_cycle_index;
    }

    if (haveGroup) {
        vkCmdSetDeviceMask(g_state.command_buffer, MASK_GPU0);
    }
    vkCmdEndRenderPass(g_state.command_buffer);

    if (history_mode) {
        VkImageMemoryBarrier write_to_src = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        write_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        write_to_src.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        write_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        write_to_src.image = g_state.history_targets[write_index].image;
        write_to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        write_to_src.subresourceRange.levelCount = 1U;
        write_to_src.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier swap_to_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        swap_to_dst.srcAccessMask = 0;
        swap_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swap_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swap_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swap_to_dst.image = g_state.swapchain_state.images[imgIndex];
        swap_to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_to_dst.subresourceRange.levelCount = 1U;
        swap_to_dst.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier pre_copy_barriers[2] = {write_to_src, swap_to_dst};
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 2U, pre_copy_barriers);

        VkImageCopy region = {0};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1U;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1U;
        region.extent.width = g_state.swapchain_state.extent.width;
        region.extent.height = g_state.swapchain_state.extent.height;
        region.extent.depth = 1U;
        vkCmdCopyImage(g_state.command_buffer,
                       g_state.history_targets[write_index].image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       g_state.swapchain_state.images[imgIndex],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region);

        VkImageMemoryBarrier write_back_to_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        write_back_to_read.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        write_back_to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_back_to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        write_back_to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write_back_to_read.image = g_state.history_targets[write_index].image;
        write_back_to_read.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        write_back_to_read.subresourceRange.levelCount = 1U;
        write_back_to_read.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier swap_to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        swap_to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swap_to_present.dstAccessMask = 0;
        swap_to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swap_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swap_to_present.image = g_state.swapchain_state.images[imgIndex];
        swap_to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_to_present.subresourceRange.levelCount = 1U;
        swap_to_present.subresourceRange.layerCount = 1U;

        VkImageMemoryBarrier post_copy_barriers[2] = {write_back_to_read,
                                                      swap_to_present};
        vkCmdPipelineBarrier(g_state.command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 2U, post_copy_barriers);
        g_state.history_targets[write_index].layout_initialized = 1;
        g_state.history_read_index = write_index;
    }
    VK_CHECK(vkEndCommandBuffer(g_state.command_buffer));

    VkPipelineStageFlags waitStage =
        history_mode ? VK_PIPELINE_STAGE_TRANSFER_BIT
                     : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_state.image_available;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_state.command_buffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_state.render_done;
    VK_CHECK(vkQueueSubmit(g_state.queue, 1, &si, g_state.in_flight));
    if (g_state.gpu_timing_enabled) {
        int any_owner_used = 0;
        for (uint32_t g = 0; g < gpuCount; g++) {
            g_state.prev_frame_work_units[g] = frame_work_units[g];
            g_state.prev_frame_owner_used[g] = frame_owner_used[g];
            if (frame_owner_used[g] != 0U) {
                any_owner_used = 1;
            }
        }
        g_state.have_prev_timing_frame = any_owner_used;
    }

    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_state.render_done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_state.swapchain_state.swapchain;
    pi.pImageIndices = &imgIndex;
    VkResult present_result = vkQueuePresentKHR(g_state.queue, &pi);
    if ((present_result != VK_SUCCESS) &&
        (present_result != VK_SUBOPTIMAL_KHR) &&
        (present_result != VK_ERROR_OUT_OF_DATE_KHR)) {
        infof("QueuePresent returned %s (%d), ending benchmark loop",
              vk_result_name(present_result), (int)present_result);
        return DB_VK_FRAME_STOP;
    }
    if (acquire_suboptimal || (present_result == VK_SUBOPTIMAL_KHR) ||
        (present_result == VK_ERROR_OUT_OF_DATE_KHR)) {
        const VkExtent2D old_extent = g_state.swapchain_state.extent;
        db_vk_recreate_swapchain_state(
            &g_state.wsi_config, g_state.present_phys, g_state.device,
            g_state.surface, g_state.surface_format, g_state.present_mode,
            g_state.render_pass, &g_state.swapchain_state);
        const int preserved = db_vk_recreate_history_targets_preserve(
            g_state.present_phys, g_state.device, g_state.surface_format.format,
            g_state.swapchain_state.extent, g_state.history_render_pass,
            g_state.device_group_mask, g_state.command_pool, g_state.queue,
            old_extent, g_state.history_targets, &g_state.history_read_index);
        db_vk_update_history_descriptor(
            g_state.device, g_state.descriptor_set, g_state.history_sampler,
            g_state.history_targets[g_state.history_read_index].view);
        g_state.history_descriptor_index = g_state.history_read_index;
        if ((g_state.pattern == DB_PATTERN_RECT_SNAKE) && (preserved == 0)) {
            g_state.rect_snake_reset_pending = 1;
        }
        g_state.frame_index++;
        return DB_VK_FRAME_RETRY;
    }

    if (!g_state.gpu_timing_enabled) {
        uint64_t frameEnd = now_ns();
        double frame_ms = (double)(frameEnd - frameStart) / NS_TO_MS_D;
        db_vk_update_ema_fallback(g_state.pattern, gpuCount, g_state.work_owner,
                                  grid_tiles_per_gpu, grid_tiles_drawn,
                                  frame_ms, g_state.ema_ms_per_work_unit);
    }

    g_state.bench_frames++;
    double bench_ms = (double)(now_ns() - g_state.bench_start_ns) / NS_TO_MS_D;
    db_benchmark_log_periodic(
        "Vulkan", RENDERER_NAME, BACKEND_NAME, g_state.bench_frames,
        g_state.work_unit_count, bench_ms, g_state.capability_mode,
        &g_state.next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS_D);
    g_state.frame_index++;
    return DB_VK_FRAME_OK;
}

void db_renderer_vulkan_1_2_multi_gpu_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }
    uint64_t bench_end = now_ns();
    double bench_ms = (double)(bench_end - g_state.bench_start_ns) / NS_TO_MS_D;
    db_benchmark_log_final("Vulkan", RENDERER_NAME, BACKEND_NAME,
                           g_state.bench_frames, g_state.work_unit_count,
                           bench_ms, g_state.capability_mode);
    vkDeviceWaitIdle(g_state.device);
    db_vk_cleanup_runtime(
        g_state.device, g_state.in_flight, g_state.image_available,
        g_state.render_done, g_state.vertex_buffer, g_state.vertex_memory,
        g_state.pipeline, g_state.pipeline_layout, &g_state.swapchain_state,
        g_state.history_targets, g_state.render_pass,
        g_state.history_render_pass, g_state.command_pool,
        g_state.timing_query_pool, g_state.descriptor_set_layout,
        g_state.descriptor_pool, g_state.history_sampler, g_state.instance,
        g_state.surface, &g_state.selection);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_vulkan_1_2_multi_gpu_capability_mode(void) {
    return (g_state.capability_mode != NULL) ? g_state.capability_mode
                                             : DB_CAP_MODE_VULKAN_SINGLE_GPU;
}

uint32_t db_renderer_vulkan_1_2_multi_gpu_work_unit_count(void) {
    return g_state.work_unit_count;
}
// NOLINTEND(misc-include-cleaner)
