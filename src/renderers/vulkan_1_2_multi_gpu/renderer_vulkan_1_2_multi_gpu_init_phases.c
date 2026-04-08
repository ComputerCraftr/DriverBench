#include "../../config/benchmark_config.h"
#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "db_embedded_shaders.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

void db_vk_init_phase_instance_surface(
    const db_vk_wsi_config_t *wsi_config,
    db_vk_init_instance_surface_phase_t *out_phase) {
    if (out_phase == NULL) {
        failf("Invalid Vulkan init phase output");
    }

    const char *inst_exts[MAX_INSTANCE_EXTS];
    uint32_t inst_ext_count = 0;
    if (db_vk_wsi_is_headless(wsi_config) == 0) {
        uint32_t required_ext_count = 0;
        const char *const *required_exts =
            wsi_config->get_required_instance_extensions(&required_ext_count,
                                                         wsi_config->user_data);
        if ((required_ext_count == 0U) || (required_exts == NULL)) {
            failf(
                "Windowing backend did not provide Vulkan instance extensions");
        }
        for (uint32_t i = 0; i < required_ext_count; i++) {
            inst_exts[inst_ext_count++] = required_exts[i];
        }
    }
    inst_exts[inst_ext_count++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    VkInstanceCreateFlags instance_flags = 0U;
    if (db_vk_instance_extension_supported(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) != 0) {
        inst_exts[inst_ext_count++] =
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "multi_gpu_2d";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {.sType =
                                    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.flags = instance_flags;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = inst_ext_count;
    ici.ppEnabledExtensionNames = inst_exts;

    out_phase->instance = VK_NULL_HANDLE;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateInstance(&ici, NULL, &out_phase->instance));

    out_phase->surface = VK_NULL_HANDLE;
    if (db_vk_wsi_is_headless(wsi_config) == 0) {
        VkResult create_surface_result = wsi_config->create_window_surface(
            out_phase->instance, wsi_config->window_handle, &out_phase->surface,
            wsi_config->user_data);
        if (create_surface_result != VK_SUCCESS) {
            db_vk_fail(BACKEND_NAME, "create_window_surface",
                       create_surface_result, __FILE__, __LINE__);
        }
    }
}

void db_vk_init_phase_device(VkInstance instance, VkSurfaceKHR surface,
                             int vsync_enabled,
                             db_vk_init_device_phase_t *out_phase) {
    if (out_phase == NULL) {
        return;
    }

    *out_phase = (db_vk_init_device_phase_t){0};
    out_phase->headless_offscreen = (surface == VK_NULL_HANDLE) ? 1 : 0;
    out_phase->selection = db_vk_select_devices_and_group(instance, surface);
    out_phase->have_group = (out_phase->selection.execution_mode ==
                             DB_VK_EXECUTION_MODE_DEVICE_GROUP);
    out_phase->gpu_count =
        db_vk_normalize_gpu_count(out_phase->selection.active_lane_count);
    out_phase->present_phys = out_phase->selection.present_phys;
    out_phase->device_group_mask =
        out_phase->have_group
            ? db_vk_build_device_group_mask(out_phase->selection.chosen_count)
            : 0U;

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(out_phase->present_phys, &qf_count,
                                             NULL);
    VkQueueFamilyProperties *qf = (VkQueueFamilyProperties *)calloc(
        qf_count, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(out_phase->present_phys, &qf_count,
                                             qf);

    uint32_t gfx_qf = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) &&
            ((surface == VK_NULL_HANDLE) ||
             db_vk_queue_family_supports_present(out_phase->present_phys, i,
                                                 surface) != 0)) {
            gfx_qf = i;
            break;
        }
    }
    if (gfx_qf == UINT32_MAX) {
        failf("No graphics+present queue family found");
    }
    out_phase->queue_timestamp_valid_bits = qf[gfx_qf].timestampValidBits;
    out_phase->queue_family_index = gfx_qf;
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

    const char *dev_exts[MAX_GPU_COUNT];
    uint32_t dev_ext_count = 0;
    if (surface != VK_NULL_HANDLE) {
        dev_exts[dev_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }

    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceGroupDeviceCreateInfo dgci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
    dgci.physicalDeviceCount = out_phase->selection.chosen_count;
    dgci.pPhysicalDevices = out_phase->selection.chosen_phys;

    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = (dev_ext_count != 0U) ? dev_exts : NULL;
    dci.enabledExtensionCount = dev_ext_count;
    dci.pEnabledFeatures = &feats;
    if (out_phase->have_group) {
        dci.pNext = &dgci;
    }

    DB_VK_CHECK(BACKEND_NAME, vkCreateDevice(out_phase->present_phys, &dci,
                                             NULL, &out_phase->device));
    vkGetDeviceQueue(out_phase->device, out_phase->queue_family_index, 0,
                     &out_phase->queue);

    if (surface != VK_NULL_HANDLE) {
        uint32_t fmt_count = 0;
        DB_VK_CHECK(BACKEND_NAME,
                    vkGetPhysicalDeviceSurfaceFormatsKHR(
                        out_phase->present_phys, surface, &fmt_count, NULL));
        VkSurfaceFormatKHR *fmts =
            (VkSurfaceFormatKHR *)calloc(fmt_count, sizeof(VkSurfaceFormatKHR));
        DB_VK_CHECK(BACKEND_NAME,
                    vkGetPhysicalDeviceSurfaceFormatsKHR(
                        out_phase->present_phys, surface, &fmt_count, fmts));
        out_phase->surface_format =
            db_vk_choose_surface_format(fmts, fmt_count);
        free(fmts);
        out_phase->present_mode = db_vk_choose_present_mode(
            out_phase->present_phys, surface, vsync_enabled);
        infof("present mode selected: %s (vsync=%d)",
              db_vk_present_mode_name(out_phase->present_mode), vsync_enabled);
        infof("scheduler pacing policy: blocking=%d frame_budget_ns=%llu "
              "frame_safety_ns=%llu",
              db_vk_present_mode_is_blocking(out_phase->present_mode),
              (unsigned long long)db_vk_scheduler_frame_budget_ns(
                  out_phase->present_mode),
              (unsigned long long)db_vk_scheduler_frame_safety_ns(
                  out_phase->present_mode));
        if ((vsync_enabled == 0) &&
            (out_phase->present_mode == VK_PRESENT_MODE_FIFO_KHR)) {
            infof(
                "non-blocking present mode unavailable on this surface/device; "
                "runtime may be present-throttled");
        }
    } else {
        out_phase->surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
        out_phase->surface_format.colorSpace =
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        out_phase->present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        infof("present mode selected: headless_offscreen");
        infof("scheduler pacing policy: blocking=%d frame_budget_ns=%llu "
              "frame_safety_ns=%llu",
              0,
              (unsigned long long)db_vk_scheduler_frame_budget_ns(
                  out_phase->present_mode),
              (unsigned long long)db_vk_scheduler_frame_safety_ns(
                  out_phase->present_mode));
    }
}

void db_vk_init_phase_pipeline_resources(
    const db_vk_wsi_config_t *wsi_config, VkSurfaceKHR surface,
    const db_vk_init_device_phase_t *device_phase,
    db_vk_init_pipeline_resources_phase_t *out_phase) {
    if ((device_phase == NULL) || (out_phase == NULL)) {
        return;
    }
    *out_phase = (db_vk_init_pipeline_resources_phase_t){0};

    VkAttachmentDescription color_att = {
        .format = device_phase->surface_format.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = (surface != VK_NULL_HANDLE)
                           ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference color_ref = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color_att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    DB_VK_CHECK(BACKEND_NAME,
                vkCreateRenderPass(device_phase->device, &rpci, NULL,
                                   &out_phase->render_pass));

    VkAttachmentDescription history_att = color_att;
    history_att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    history_att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    history_att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo history_rpci = rpci;
    history_rpci.pAttachments = &history_att;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateRenderPass(device_phase->device, &history_rpci, NULL,
                                   &out_phase->history_render_pass));

    if ((surface != VK_NULL_HANDLE) && (wsi_config != NULL)) {
        db_vk_create_swapchain_state(
            wsi_config, device_phase->present_phys, device_phase->device,
            surface, device_phase->surface_format, device_phase->present_mode,
            out_phase->render_pass, &out_phase->swapchain_state);
    } else {
        out_phase->swapchain_state.extent.width = BENCH_WINDOW_WIDTH_PX;
        out_phase->swapchain_state.extent.height = BENCH_WINDOW_HEIGHT_PX;
        out_phase->swapchain_state.image_count = 0U;
    }

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

    if (DB_EMBEDDED_VULKAN_SPV_AVAILABLE == 0) {
        failf("Embedded Vulkan SPIR-V shaders are unavailable in this build");
    }

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    smci.codeSize =
        db_shader_vulkan_1_2_rect_vert_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_shader_vulkan_1_2_rect_vert_spv;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &vs));
    smci.codeSize =
        db_shader_vulkan_1_2_rect_frag_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_shader_vulkan_1_2_rect_frag_spv;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &fs));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    float quad_verts[QUAD_VERT_FLOAT_COUNT] = {0, 0, 1, 0, 1, 1,
                                               0, 0, 1, 1, 0, 1};

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sizeof(quad_verts);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkCreateBuffer(device_phase->device, &bci, NULL,
                                             &out_phase->vertex_buffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_phase->device,
                                  out_phase->vertex_buffer, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(device_phase->present_phys, &mp);
    uint32_t mem_index = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (MASK_GPU0 << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            mem_index = i;
            break;
        }
    }
    if (mem_index == UINT32_MAX) {
        failf("No host-visible + host-coherent memory type for vertex buffer");
    }

    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_index;
    DB_VK_CHECK(BACKEND_NAME, vkAllocateMemory(device_phase->device, &mai, NULL,
                                               &out_phase->vertex_memory));
    DB_VK_CHECK(BACKEND_NAME, vkBindBufferMemory(device_phase->device,
                                                 out_phase->vertex_buffer,
                                                 out_phase->vertex_memory, 0));

    void *mapped = NULL;
    DB_VK_CHECK(BACKEND_NAME,
                vkMapMemory(device_phase->device, out_phase->vertex_memory, 0,
                            sizeof(quad_verts), 0, &mapped));
    {
        float *mapped_f32 = (float *)mapped;
        for (size_t i = 0; i < QUAD_VERT_FLOAT_COUNT; i++) {
            mapped_f32[i] = quad_verts[i];
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
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn_states;

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

void db_vk_init_phase_scheduler(const db_vk_init_device_phase_t *device_phase,
                                db_vk_init_scheduler_phase_t *out_phase) {
    if ((device_phase == NULL) || (out_phase == NULL)) {
        return;
    }
    *out_phase = (db_vk_init_scheduler_phase_t){0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &out_phase->runtime)) {
        failf("benchmark runtime init failed");
    }

    const db_pattern_t pattern = out_phase->runtime.pattern;
    int no_present_requested = 0;
    (void)db_parse_bool_text(
        db_runtime_option_get(DB_RUNTIME_OPT_VK_NO_PRESENT),
        &no_present_requested);
    out_phase->no_present_mode =
        (no_present_requested != 0) || (device_phase->headless_offscreen != 0);
    out_phase->effective_gpu_count = device_phase->gpu_count;
    const int multi_gpu = out_phase->effective_gpu_count > 1U;
    out_phase->capability_mode = db_vk_compose_capability_mode(pattern);
    db_log_renderer_capability_mode(BACKEND_NAME, out_phase->capability_mode);
    (void)multi_gpu;
    db_log_renderer_scheduler_mode(
        BACKEND_NAME, db_vk_scheduler_mode_name_effective(
                          device_phase->selection.execution_mode,
                          device_phase->selection.active_lane_count));
    if (no_present_requested != 0) {
        infof("debug runtime: no-present mode enabled");
    } else if (device_phase->headless_offscreen != 0) {
        infof("headless offscreen mode enabled");
    }
    for (uint32_t g = 0; g < out_phase->effective_gpu_count; g++) {
        out_phase->ema_ms_per_work_unit[g] = DEFAULT_EMA_MS_PER_WORK_UNIT;
    }
}
