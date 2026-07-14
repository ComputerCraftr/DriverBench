#include "../../config/benchmark_config.h"
#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_log.h"
#include "core/db_renderer_runtime_contract.h"
#include "vk_diagnostics.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "db_embedded_shaders.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

void db_vk_init_phase_pipeline_resources(
    const db_vk_wsi_config_t *wsi_config, VkSurfaceKHR surface,
    const db_vk_init_device_phase_t *device_phase,
    const db_renderer_runtime_contract_t *resolved_runtime,
    db_vk_init_pipeline_resources_phase_t *out_phase) {
    if ((device_phase == NULL) || (resolved_runtime == NULL) ||
        (out_phase == NULL)) {
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
        .initialLayout = (surface != VK_NULL_HANDLE)
                             ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                             : VK_IMAGE_LAYOUT_UNDEFINED,
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

    out_phase->backing_pixel_format =
        resolved_runtime->format.surface_pixel_format;
    out_phase->backing_format =
        db_vk_image_format_from_pixel_format(out_phase->backing_pixel_format);
    VkFormatProperties backing_properties = {0};
    vkGetPhysicalDeviceFormatProperties(device_phase->present_phys,
                                        out_phase->backing_format,
                                        &backing_properties);
    const VkFormatFeatureFlags required_backing_features =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((backing_properties.optimalTilingFeatures &
         required_backing_features) != required_backing_features) {
        runtime_failf(
            "requested Vulkan working format lacks render/sample support");
    }
    VkAttachmentDescription history_att = color_att;
    history_att.format = out_phase->backing_format;
    history_att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    history_att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    history_att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo history_rpci = rpci;
    history_rpci.pAttachments = &history_att;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateRenderPass(device_phase->device, &history_rpci, NULL,
                                   &out_phase->backing_render_pass));

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

    const VkExtent2D logical_extent = {
        .width = resolved_runtime->execution.grid_cols,
        .height = resolved_runtime->execution.grid_rows,
    };
    db_vk_create_backing_target(device_phase->present_phys,
                                device_phase->device, out_phase->backing_format,
                                logical_extent, out_phase->backing_render_pass,
                                device_phase->device_group_mask,
                                &out_phase->backing_targets[0]);
    if (DB_EMBEDDED_VULKAN_SPV_AVAILABLE == 0) {
        runtime_failf(
            "Embedded Vulkan SPIR-V shaders are unavailable in this build");
    }

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkShaderModule present_vs = VK_NULL_HANDLE;
    VkShaderModule present_fs = VK_NULL_HANDLE;
    smci.codeSize = db_vk_ir_execute_vert_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_vk_ir_execute_vert_spv;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &vs));
    smci.codeSize = db_vk_ir_execute_frag_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_vk_ir_execute_frag_spv;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateShaderModule(device_phase->device, &smci, NULL, &fs));
    smci.codeSize = db_vk_presentation_vert_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_vk_presentation_vert_spv;
    DB_VK_CHECK(BACKEND_NAME, vkCreateShaderModule(device_phase->device, &smci,
                                                   NULL, &present_vs));
    smci.codeSize = db_vk_presentation_frag_spv_word_count * sizeof(uint32_t);
    smci.pCode = db_vk_presentation_frag_spv;
    DB_VK_CHECK(BACKEND_NAME, vkCreateShaderModule(device_phase->device, &smci,
                                                   NULL, &present_fs));

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
        runtime_failf(
            "No host-visible + host-coherent memory type for vertex buffer");
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

    const VkDeviceSize instance_bytes =
        (VkDeviceSize)(DB_VK_MAX_PIECES_PER_FRAME *
                       sizeof(db_vk_ir_execute_instance_t));
    bci.size = instance_bytes;
    DB_VK_CHECK(BACKEND_NAME, vkCreateBuffer(device_phase->device, &bci, NULL,
                                             &out_phase->instance_buffer));
    vkGetBufferMemoryRequirements(device_phase->device,
                                  out_phase->instance_buffer, &mr);
    mem_index = UINT32_MAX;
    for (uint32_t i = 0U; i < mp.memoryTypeCount; i++) {
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (((mr.memoryTypeBits & (MASK_GPU0 << i)) != 0U) &&
            ((mp.memoryTypes[i].propertyFlags & required) == required)) {
            mem_index = i;
            break;
        }
    }
    if (mem_index == UINT32_MAX) {
        runtime_failf("No host-visible memory type for instance buffer");
    }
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_index;
    DB_VK_CHECK(BACKEND_NAME, vkAllocateMemory(device_phase->device, &mai, NULL,
                                               &out_phase->instance_memory));
    DB_VK_CHECK(BACKEND_NAME,
                vkBindBufferMemory(device_phase->device,
                                   out_phase->instance_buffer,
                                   out_phase->instance_memory, 0U));
    DB_VK_CHECK(BACKEND_NAME,
                vkMapMemory(device_phase->device, out_phase->instance_memory,
                            0U, instance_bytes, 0U,
                            &out_phase->instance_mapped));

    const VkDeviceSize lookup_bytes =
        (VkDeviceSize)DB_VK_LOOKUP_WORD_CAPACITY * sizeof(uint32_t);
    bci.size = lookup_bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    DB_VK_CHECK(BACKEND_NAME, vkCreateBuffer(device_phase->device, &bci, NULL,
                                             &out_phase->lookup_buffer));
    vkGetBufferMemoryRequirements(device_phase->device,
                                  out_phase->lookup_buffer, &mr);
    mem_index = UINT32_MAX;
    for (uint32_t i = 0U; i < mp.memoryTypeCount; i++) {
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (((mr.memoryTypeBits & (MASK_GPU0 << i)) != 0U) &&
            ((mp.memoryTypes[i].propertyFlags & required) == required)) {
            mem_index = i;
            break;
        }
    }
    if (mem_index == UINT32_MAX) {
        runtime_failf("No host-visible memory type for gradient lookup");
    }
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mem_index;
    DB_VK_CHECK(BACKEND_NAME, vkAllocateMemory(device_phase->device, &mai, NULL,
                                               &out_phase->lookup_memory));
    DB_VK_CHECK(BACKEND_NAME, vkBindBufferMemory(device_phase->device,
                                                 out_phase->lookup_buffer,
                                                 out_phase->lookup_memory, 0U));
    DB_VK_CHECK(BACKEND_NAME,
                vkMapMemory(device_phase->device, out_phase->lookup_memory, 0U,
                            lookup_bytes, 0U, &out_phase->lookup_mapped));

    const VkVertexInputBindingDescription binds[2] = {
        {.binding = 0U,
         .stride = sizeof(float) * 2U,
         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1U,
         .stride = sizeof(db_vk_ir_execute_instance_t),
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    const VkVertexInputAttributeDescription attrs[5] = {
        {.location = 0U,
         .binding = 0U,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = 0U},
        {.location = 1U,
         .binding = 1U,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = offsetof(db_vk_ir_execute_instance_t, rect)},
        {.location = 2U,
         .binding = 1U,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = offsetof(db_vk_ir_execute_instance_t, start_color)},
        {.location = 3U,
         .binding = 1U,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = offsetof(db_vk_ir_execute_instance_t, end_color)},
        {.location = 4U,
         .binding = 1U,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = offsetof(db_vk_ir_execute_instance_t, gradient)},
    };

    VkPipelineVertexInputStateCreateInfo vis = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vis.vertexBindingDescriptionCount = 2U;
    vis.pVertexBindingDescriptions = binds;
    vis.vertexAttributeDescriptionCount = 5U;
    vis.pVertexAttributeDescriptions = attrs;

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
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(db_vk_present_push_constants_t);

    const VkDescriptorSetLayoutBinding descriptor_bindings[2] = {
        {.binding = 0U,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1U,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1U,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1U,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 2U;
    dslci.pBindings = descriptor_bindings;
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
    gp.renderPass = out_phase->backing_render_pass;
    gp.subpass = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateGraphicsPipelines(device_phase->device, VK_NULL_HANDLE,
                                          1, &gp, NULL, &out_phase->pipeline));

    VkPipelineShaderStageCreateInfo present_stages[2] = {stages[0], stages[1]};
    present_stages[0].module = present_vs;
    present_stages[1].module = present_fs;
    VkPipelineVertexInputStateCreateInfo present_vis = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkGraphicsPipelineCreateInfo present_gp = gp;
    present_gp.pStages = present_stages;
    present_gp.pVertexInputState = &present_vis;
    present_gp.renderPass = out_phase->render_pass;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateGraphicsPipelines(device_phase->device, VK_NULL_HANDLE,
                                          1, &present_gp, NULL,
                                          &out_phase->present_pipeline));
    present_gp.renderPass = out_phase->backing_render_pass;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateGraphicsPipelines(device_phase->device, VK_NULL_HANDLE,
                                          1, &present_gp, NULL,
                                          &out_phase->composition_pipeline));
    const db_log_field_t presentation_fields[] = {
        DB_LOG_TOKEN("method", "sample_fullscreen"),
        DB_LOG_TOKEN("filter", "nearest"),
        DB_LOG_TOKEN("working_format",
                     db_vk_format_name(out_phase->backing_format)),
        DB_LOG_TOKEN("native_format",
                     db_vk_format_name(device_phase->surface_format.format)),
        DB_LOG_BOOL("transfer_destination", 0),
    };
    db_log_info(BACKEND_NAME, "vk_presentation_pipeline", presentation_fields,
                DB_LOG_FIELD_COUNT(presentation_fields));

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
                                &out_phase->backing_sampler));

    const uint32_t descriptor_set_count =
        1U + (MAX_GPU_COUNT * DB_VK_LANE_SLOT_COUNT);
    const VkDescriptorPoolSize pool_sizes[2] = {
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = descriptor_set_count},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = descriptor_set_count},
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = descriptor_set_count;
    dpci.poolSizeCount = 2U;
    dpci.pPoolSizes = pool_sizes;
    DB_VK_CHECK(BACKEND_NAME,
                vkCreateDescriptorPool(device_phase->device, &dpci, NULL,
                                       &out_phase->descriptor_pool));

    VkDescriptorSetLayout
        descriptor_layouts[1U + (MAX_GPU_COUNT * DB_VK_LANE_SLOT_COUNT)] = {0};
    for (uint32_t i = 0U; i < descriptor_set_count; i++) {
        descriptor_layouts[i] = out_phase->descriptor_set_layout;
    }
    VkDescriptorSet
        allocated_sets[1U + (MAX_GPU_COUNT * DB_VK_LANE_SLOT_COUNT)] = {0};
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = out_phase->descriptor_pool;
    dsai.descriptorSetCount = descriptor_set_count;
    dsai.pSetLayouts = descriptor_layouts;
    DB_VK_CHECK(BACKEND_NAME, vkAllocateDescriptorSets(device_phase->device,
                                                       &dsai, allocated_sets));
    out_phase->descriptor_set = allocated_sets[0];
    uint32_t descriptor_index = 1U;
    for (uint32_t lane = 0U; lane < MAX_GPU_COUNT; lane++) {
        for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
            out_phase->lane_descriptor_sets[lane][slot] =
                allocated_sets[descriptor_index++];
        }
    }
    db_vk_update_backing_descriptor(
        device_phase->device, out_phase->descriptor_set,
        out_phase->backing_sampler, out_phase->backing_targets[0].view);
    const VkDescriptorBufferInfo lookup_info = {
        .buffer = out_phase->lookup_buffer,
        .offset = 0U,
        .range = lookup_bytes,
    };
    for (uint32_t index = 0U; index < descriptor_set_count; index++) {
        const VkWriteDescriptorSet lookup_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = allocated_sets[index],
            .dstBinding = 1U,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &lookup_info,
        };
        vkUpdateDescriptorSets(device_phase->device, 1U, &lookup_write, 0U,
                               NULL);
    }

    vkDestroyShaderModule(device_phase->device, vs, NULL);
    vkDestroyShaderModule(device_phase->device, fs, NULL);
    vkDestroyShaderModule(device_phase->device, present_vs, NULL);
    vkDestroyShaderModule(device_phase->device, present_fs, NULL);

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

void db_vk_init_phase_scheduler(
    const db_vk_init_device_phase_t *device_phase,
    const db_renderer_runtime_contract_t *resolved_runtime,
    db_vk_init_scheduler_phase_t *out_phase) {
    if ((device_phase == NULL) || (out_phase == NULL)) {
        return;
    }
    *out_phase = (db_vk_init_scheduler_phase_t){0};
    if (resolved_runtime == NULL) {
        runtime_failf("missing resolved runtime");
    }
    out_phase->runtime = resolved_runtime->execution;

    int no_present_requested = 0;
    (void)db_parse_bool_text(
        db_runtime_option_get(DB_RUNTIME_OPT_VK_NO_PRESENT),
        &no_present_requested);
    out_phase->no_present_mode =
        (no_present_requested != 0) || (device_phase->headless_offscreen != 0);
    out_phase->effective_gpu_count = device_phase->gpu_count;
    out_phase->capability_mode =
        db_vk_compose_capability_mode(&out_phase->runtime);
    db_log_renderer_capability(
        BACKEND_NAME, db_vk_capability_draw_mode_name(&out_phase->runtime),
        "persistent_backing", 0, DB_CAP_MODE_VK_UPLOAD_NONE);
    db_log_renderer_scheduler_mode(
        BACKEND_NAME, db_vk_scheduler_mode_name_effective(
                          device_phase->selection.execution_mode,
                          device_phase->selection.active_lane_count));
    if (no_present_requested != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("present_method", "disabled"),
            DB_LOG_TOKEN("reason", "explicit_no_present"),
        };
        db_log_info(BACKEND_NAME, "vk_runtime_mode", fields,
                    DB_LOG_FIELD_COUNT(fields));
    } else if (device_phase->headless_offscreen != 0) {
        const db_log_field_t fields[] = {
            DB_LOG_TOKEN("present_method", "offscreen"),
            DB_LOG_TOKEN("reason", "headless_surface"),
        };
        db_log_info(BACKEND_NAME, "vk_runtime_mode", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
    for (uint32_t g = 0; g < out_phase->effective_gpu_count; g++) {
        out_phase->ema_ms_per_work_unit[g] = DEFAULT_EMA_MS_PER_WORK_UNIT;
    }
}
