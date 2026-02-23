#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_CAP_MODE_VULKAN_DEVICE_GROUP_MULTI_GPU                              \
    "vulkan_device_group_multi_gpu"
#define DB_CAP_MODE_VULKAN_SINGLE_GPU "vulkan_single_gpu"
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define MASK_GPU0 1U
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
} db_vk_init_instance_surface_phase_t;

typedef struct {
    uint32_t device_group_mask;
    uint32_t gpu_count;
    int have_group;
    VkDevice device;
    VkPhysicalDevice present_phys;
    VkPresentModeKHR present_mode;
    VkQueue queue;
    uint32_t queue_family_index;
    uint32_t queue_timestamp_valid_bits;
    DeviceSelectionState selection;
    VkSurfaceFormatKHR surface_format;
    double timestamp_period_ns;
} db_vk_init_device_phase_t;

typedef struct {
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descriptor_set_layout;
    VkFence in_flight;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkQueryPool timing_query_pool;
    VkRenderPass render_pass;
    VkRenderPass history_render_pass;
    VkSampler history_sampler;
    HistoryTargetState history_targets[2];
    int gpu_timing_enabled;
    SwapchainState swapchain_state;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
} db_vk_init_pipeline_resources_phase_t;

typedef struct {
    const char *capability_mode;
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    db_benchmark_runtime_init_t runtime;
    uint32_t work_owner[MAX_BAND_OWNER];
} db_vk_init_scheduler_phase_t;

DeviceSelectionState db_vk_select_devices_and_group(VkInstance instance,
                                                    VkSurfaceKHR surface) {
    DeviceSelectionState selection = {0};

    DB_VK_CHECK(BACKEND_NAME, vkEnumeratePhysicalDevices(
                                  instance, &selection.phys_count, NULL));
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
        db_vk_fail(BACKEND_NAME, "vkEnumeratePhysicalDevices",
                   enumerate_phys_result, __FILE__, __LINE__);
    }

    DB_VK_CHECK(BACKEND_NAME, vkEnumeratePhysicalDeviceGroups(
                                  instance, &selection.group_count, NULL));
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
        db_vk_fail(BACKEND_NAME, "vkEnumeratePhysicalDeviceGroups",
                   enumerate_groups_result, __FILE__, __LINE__);
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

static void db_vk_init_phase_instance_surface(
    const db_vk_wsi_config_t *wsi_config,
    db_vk_init_instance_surface_phase_t *out_phase) {
    if ((wsi_config == NULL) || (out_phase == NULL) ||
        (wsi_config->window_handle == NULL) ||
        (wsi_config->get_required_instance_extensions == NULL) ||
        (wsi_config->create_window_surface == NULL) ||
        (wsi_config->get_framebuffer_size == NULL)) {
        failf("Invalid Vulkan WSI config provided to renderer init");
    }

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

    out_phase->instance = VK_NULL_HANDLE;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateInstance(&ici, NULL, &out_phase->instance));

    VkResult create_surface_result = wsi_config->create_window_surface(
        out_phase->instance, wsi_config->window_handle, &out_phase->surface,
        wsi_config->user_data);
    if (create_surface_result != VK_SUCCESS) {
        db_vk_fail(BACKEND_NAME, "create_window_surface", create_surface_result,
                   __FILE__, __LINE__);
    }
}

static void db_vk_init_phase_device(VkInstance instance, VkSurfaceKHR surface,
                                    db_vk_init_device_phase_t *out_phase) {
    if (out_phase == NULL) {
        return;
    }

    *out_phase = (db_vk_init_device_phase_t){0};
    out_phase->selection = db_vk_select_devices_and_group(instance, surface);
    out_phase->have_group = out_phase->selection.have_group;
    out_phase->gpu_count =
        out_phase->have_group ? out_phase->selection.chosen_count : 1U;
    out_phase->present_phys = out_phase->selection.present_phys;
    out_phase->device_group_mask =
        out_phase->have_group
            ? db_vk_build_device_group_mask(out_phase->selection.chosen_count)
            : 0U;

    uint32_t qfN = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(out_phase->present_phys, &qfN,
                                             NULL);
    VkQueueFamilyProperties *qf =
        (VkQueueFamilyProperties *)calloc(qfN, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(out_phase->present_phys, &qfN, qf);

    uint32_t gfxQF = UINT32_MAX;
    for (uint32_t i = 0; i < qfN; i++) {
        VkBool32 supp = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(out_phase->present_phys, i,
                                             surface, &supp);
        if (supp && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            gfxQF = i;
            break;
        }
    }
    if (gfxQF == UINT32_MAX) {
        failf("No graphics+present queue family found");
    }
    out_phase->queue_timestamp_valid_bits = qf[gfxQF].timestampValidBits;
    out_phase->queue_family_index = gfxQF;
    free(qf);

    VkPhysicalDeviceProperties phys_props;
    vkGetPhysicalDeviceProperties(out_phase->present_phys, &phys_props);
    out_phase->timestamp_period_ns = (double)phys_props.limits.timestampPeriod;

    float prio = 1.0F;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = out_phase->queue_family_index;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[MAX_GPU_COUNT];
    uint32_t devExtN = 0;
    devExts[devExtN++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceGroupDeviceCreateInfo dgci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
    dgci.physicalDeviceCount = out_phase->selection.chosen_count;
    dgci.pPhysicalDevices = out_phase->selection.chosen_phys;

    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.enabledExtensionCount = devExtN;
    dci.pEnabledFeatures = &feats;
    if (out_phase->have_group) {
        dci.pNext = &dgci;
    }

    DB_VK_CHECK(BACKEND_NAME, vkCreateDevice(out_phase->present_phys, &dci,
                                             NULL, &out_phase->device));
    vkGetDeviceQueue(out_phase->device, out_phase->queue_family_index, 0,
                     &out_phase->queue);

    uint32_t fmtN = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkGetPhysicalDeviceSurfaceFormatsKHR(out_phase->present_phys,
                                                     surface, &fmtN, NULL));
    VkSurfaceFormatKHR *fmts =
        (VkSurfaceFormatKHR *)calloc(fmtN, sizeof(VkSurfaceFormatKHR));
    DB_VK_CHECK(BACKEND_NAME,
                vkGetPhysicalDeviceSurfaceFormatsKHR(out_phase->present_phys,
                                                     surface, &fmtN, fmts));
    out_phase->surface_format = db_vk_choose_surface_format(fmts, fmtN);
    free(fmts);
    out_phase->present_mode =
        db_vk_choose_present_mode(out_phase->present_phys, surface);
}

static void db_vk_init_phase_pipeline_resources(
    const db_vk_wsi_config_t *wsi_config, VkSurfaceKHR surface,
    const db_vk_init_device_phase_t *device_phase,
    db_vk_init_pipeline_resources_phase_t *out_phase) {
    if ((device_phase == NULL) || (out_phase == NULL)) {
        return;
    }
    *out_phase = (db_vk_init_pipeline_resources_phase_t){0};

    VkAttachmentDescription colorAtt = {0};
    colorAtt.format = device_phase->surface_format.format;
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

    DB_VK_CHECK(BACKEND_NAME,
                vkCreateRenderPass(device_phase->device, &rpci, NULL,
                                   &out_phase->render_pass));

    VkAttachmentDescription historyAtt = colorAtt;
    historyAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    historyAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    historyAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo history_rpci = rpci;
    history_rpci.pAttachments = &historyAtt;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateRenderPass(device_phase->device, &history_rpci, NULL,
                                   &out_phase->history_render_pass));

    db_vk_create_swapchain_state(
        wsi_config, device_phase->present_phys, device_phase->device, surface,
        device_phase->surface_format, device_phase->present_mode,
        out_phase->render_pass, &out_phase->swapchain_state);

    db_vk_create_history_target(
        device_phase->present_phys, device_phase->device,
        device_phase->surface_format.format, out_phase->swapchain_state.extent,
        out_phase->history_render_pass, device_phase->device_group_mask,
        &out_phase->history_targets[0]);
    db_vk_create_history_target(
        device_phase->present_phys, device_phase->device,
        device_phase->surface_format.format, out_phase->swapchain_state.extent,
        out_phase->history_render_pass, device_phase->device_group_mask,
        &out_phase->history_targets[1]);

    size_t vsz = 0;
    size_t fsz = 0;
    uint8_t *vbin = db_read_file_or_fail(BACKEND_NAME, VERT_SPV_PATH, &vsz);
    uint8_t *fbin = db_read_file_or_fail(BACKEND_NAME, FRAG_SPV_PATH, &fsz);

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    smci.codeSize = vsz;
    smci.pCode = (const uint32_t *)vbin;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &vs));
    smci.codeSize = fsz;
    smci.pCode = (const uint32_t *)fbin;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &fs));
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

    float quadVerts[QUAD_VERT_FLOAT_COUNT] = {0, 0, 1, 0, 1, 1,
                                              0, 0, 1, 1, 0, 1};

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sizeof(quadVerts);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkCreateBuffer(device_phase->device, &bci, NULL,
                                             &out_phase->vertex_buffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_phase->device,
                                  out_phase->vertex_buffer, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(device_phase->present_phys, &mp);
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
    DB_VK_CHECK(BACKEND_NAME, vkAllocateMemory(device_phase->device, &mai, NULL,
                                               &out_phase->vertex_memory));
    DB_VK_CHECK(BACKEND_NAME, vkBindBufferMemory(device_phase->device,
                                                 out_phase->vertex_buffer,
                                                 out_phase->vertex_memory, 0));

    void *mapped = NULL;
    DB_VK_CHECK(BACKEND_NAME,
                vkMapMemory(device_phase->device, out_phase->vertex_memory, 0,
                            sizeof(quadVerts), 0, &mapped));
    {
        float *mapped_f32 = (float *)mapped;
        for (size_t i = 0; i < QUAD_VERT_FLOAT_COUNT; i++) {
            mapped_f32[i] = quadVerts[i];
        }
    }
    vkUnmapMemory(device_phase->device, out_phase->vertex_memory);

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
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateDescriptorSetLayout(device_phase->device, &dslci, NULL,
                                            &out_phase->descriptor_set_layout));

    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1U;
    plci.pSetLayouts = &out_phase->descriptor_set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreatePipelineLayout(device_phase->device, &plci, NULL,
                                       &out_phase->pipeline_layout));

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
    gp.layout = out_phase->pipeline_layout;
    gp.renderPass = out_phase->render_pass;
    gp.subpass = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateGraphicsPipelines(device_phase->device, VK_NULL_HANDLE,
                                          1, &gp, NULL, &out_phase->pipeline));

    VkSamplerCreateInfo sampler_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_ci.magFilter = VK_FILTER_NEAREST;
    sampler_ci.minFilter = VK_FILTER_NEAREST;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxLod = 0.0F;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateSampler(device_phase->device, &sampler_ci, NULL,
                                &out_phase->history_sampler));

    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1U;
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1U;
    dpci.poolSizeCount = 1U;
    dpci.pPoolSizes = &pool_size;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateDescriptorPool(device_phase->device, &dpci, NULL,
                                       &out_phase->descriptor_pool));

    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = out_phase->descriptor_pool;
    dsai.descriptorSetCount = 1U;
    dsai.pSetLayouts = &out_phase->descriptor_set_layout;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateDescriptorSets(device_phase->device, &dsai,
                                         &out_phase->descriptor_set));
    db_vk_update_history_descriptor(
        device_phase->device, out_phase->descriptor_set,
        out_phase->history_sampler, out_phase->history_targets[0].view);

    vkDestroyShaderModule(device_phase->device, vs, NULL);
    vkDestroyShaderModule(device_phase->device, fs, NULL);

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = device_phase->queue_family_index;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateCommandPool(device_phase->device, &cpci, NULL,
                                    &out_phase->command_pool));

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = out_phase->command_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    DB_VK_CHECK(BACKEND_NAME,
                vkAllocateCommandBuffers(device_phase->device, &cbai,
                                         &out_phase->command_buffer));

    VkSemaphoreCreateInfo sci2 = {.sType =
                                      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateSemaphore(device_phase->device, &sci2, NULL,
                                  &out_phase->image_available));
    DB_VK_CHECK(BACKEND_NAME, vkCreateSemaphore(device_phase->device, &sci2,
                                                NULL, &out_phase->render_done));

    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkCreateFence(device_phase->device, &fci, NULL,
                                            &out_phase->in_flight));

    out_phase->gpu_timing_enabled =
        (device_phase->queue_timestamp_valid_bits > 0U) &&
        (device_phase->timestamp_period_ns > 0.0);
    if (out_phase->gpu_timing_enabled) {
        VkQueryPoolCreateInfo qpci = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = TIMESTAMP_QUERY_COUNT;
        DB_VK_CHECK(BACKEND_NAME,
                    vkCreateQueryPool(device_phase->device, &qpci, NULL,
                                      &out_phase->timing_query_pool));
    }
}

static void
db_vk_init_phase_scheduler(const db_vk_init_device_phase_t *device_phase,
                           db_vk_init_scheduler_phase_t *out_phase) {
    if ((device_phase == NULL) || (out_phase == NULL)) {
        return;
    }
    *out_phase = (db_vk_init_scheduler_phase_t){0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &out_phase->runtime)) {
        failf("benchmark runtime init failed");
    }

    const db_pattern_t pattern = out_phase->runtime.pattern;
    const int multi_gpu =
        device_phase->have_group && (device_phase->gpu_count > 1U);
    out_phase->capability_mode = multi_gpu
                                     ? DB_CAP_MODE_VULKAN_DEVICE_GROUP_MULTI_GPU
                                     : DB_CAP_MODE_VULKAN_SINGLE_GPU;
    infof("using capability mode: %s", out_phase->capability_mode);

    if (pattern == DB_PATTERN_BANDS) {
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            out_phase->work_owner[b] = b % device_phase->gpu_count;
        }
    }
    for (uint32_t g = 0; g < device_phase->gpu_count; g++) {
        out_phase->ema_ms_per_work_unit[g] = DEFAULT_EMA_MS_PER_WORK_UNIT;
    }
}

void db_vk_init_impl(const db_vk_wsi_config_t *wsi_config) {
    db_vk_init_instance_surface_phase_t instance_surface_phase = {0};
    db_vk_init_device_phase_t device_phase = {0};
    db_vk_init_pipeline_resources_phase_t pipeline_phase = {0};
    db_vk_init_scheduler_phase_t scheduler_phase = {0};

    db_vk_init_phase_instance_surface(wsi_config, &instance_surface_phase);
    db_vk_init_phase_device(instance_surface_phase.instance,
                            instance_surface_phase.surface, &device_phase);
    db_vk_init_phase_pipeline_resources(wsi_config,
                                        instance_surface_phase.surface,
                                        &device_phase, &pipeline_phase);
    db_vk_init_phase_scheduler(&device_phase, &scheduler_phase);

    const db_vk_state_init_ctx_t init_ctx = {
        .wsi_config = wsi_config,
        .instance = instance_surface_phase.instance,
        .surface = instance_surface_phase.surface,
        .selection = device_phase.selection,
        .have_group = device_phase.have_group,
        .gpu_count = device_phase.gpu_count,
        .present_phys = device_phase.present_phys,
        .device = device_phase.device,
        .queue = device_phase.queue,
        .surface_format = device_phase.surface_format,
        .present_mode = device_phase.present_mode,
        .render_pass = pipeline_phase.render_pass,
        .history_render_pass = pipeline_phase.history_render_pass,
        .swapchain_state = pipeline_phase.swapchain_state,
        .history_targets = {pipeline_phase.history_targets[0],
                            pipeline_phase.history_targets[1]},
        .device_group_mask = device_phase.device_group_mask,
        .vertex_buffer = pipeline_phase.vertex_buffer,
        .vertex_memory = pipeline_phase.vertex_memory,
        .pipeline = pipeline_phase.pipeline,
        .pipeline_layout = pipeline_phase.pipeline_layout,
        .descriptor_set_layout = pipeline_phase.descriptor_set_layout,
        .descriptor_pool = pipeline_phase.descriptor_pool,
        .descriptor_set = pipeline_phase.descriptor_set,
        .history_sampler = pipeline_phase.history_sampler,
        .command_pool = pipeline_phase.command_pool,
        .command_buffer = pipeline_phase.command_buffer,
        .image_available = pipeline_phase.image_available,
        .render_done = pipeline_phase.render_done,
        .in_flight = pipeline_phase.in_flight,
        .timing_query_pool = pipeline_phase.timing_query_pool,
        .gpu_timing_enabled = pipeline_phase.gpu_timing_enabled,
        .runtime = scheduler_phase.runtime,
        .capability_mode = scheduler_phase.capability_mode,
        .work_owner = scheduler_phase.work_owner,
        .ema_ms_per_work_unit = scheduler_phase.ema_ms_per_work_unit,
        .timestamp_period_ns = device_phase.timestamp_period_ns,
    };
    db_vk_publish_initialized_state(&init_ctx);
}

// NOLINTEND(misc-include-cleaner)
