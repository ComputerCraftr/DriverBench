#include "vk_internal.h"

#ifdef __linux__
#include "vk_state_internal.h"

#include <stdint.h>

#include "../../core/db_numeric.h"
#include "../../core/db_render_types.h"
#include "db_embedded_shaders.h"
#include <unistd.h>
#include <vulkan/vulkan_core.h>

static uint32_t vk_buffer_memory_type(VkPhysicalDevice physical_device,
                                      uint32_t bits, int prefer_device_local) {
    VkPhysicalDeviceMemoryProperties properties = {0};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (uint32_t pass = 0U; pass < 2U; pass++) {
        for (uint32_t index = 0U; index < properties.memoryTypeCount; index++) {
            if ((bits & (1U << index)) == 0U) {
                continue;
            }
            const int local =
                DB_BOOL((properties.memoryTypes[index].propertyFlags &
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U);
            if ((prefer_device_local > 0) && (pass == 0U) && (local == 0)) {
                continue;
            }
            if ((prefer_device_local < 0) && (pass == 0U) && (local != 0)) {
                continue;
            }
            return index;
        }
    }
    return UINT32_MAX;
}

static int vk_create_shared_buffer(db_vk_independent_lane_runtime_t *runtime,
                                   db_vk_lane_slot_t *slot, VkDeviceSize size) {
    const VkExternalMemoryBufferCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = &external,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(runtime->device, &info, NULL,
                       &slot->worker_shared_buffer) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryDedicatedRequirements dedicated = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicated,
    };
    const VkBufferMemoryRequirementsInfo2 requirements_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        .buffer = slot->worker_shared_buffer,
    };
    vkGetBufferMemoryRequirements2(runtime->device, &requirements_info,
                                   &requirements);
    const uint32_t worker_type = vk_buffer_memory_type(
        runtime->phys, requirements.memoryRequirements.memoryTypeBits, -1);
    if (worker_type == UINT32_MAX) {
        return 0;
    }
    const VkMemoryDedicatedAllocateInfo dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .buffer = slot->worker_shared_buffer,
    };
    const VkExportMemoryAllocateInfo export_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated_info,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &export_info,
        .allocationSize = requirements.memoryRequirements.size,
        .memoryTypeIndex = worker_type,
    };
    if ((vkAllocateMemory(runtime->device, &allocation, NULL,
                          &slot->worker_shared_memory) != VK_SUCCESS) ||
        (vkBindBufferMemory(runtime->device, slot->worker_shared_buffer,
                            slot->worker_shared_memory, 0U) != VK_SUCCESS)) {
        return 0;
    }
    union {
        PFN_vkVoidFunction generic;
        PFN_vkGetMemoryFdKHR typed;
    } get_fd = {.generic =
                    vkGetDeviceProcAddr(runtime->device, "vkGetMemoryFdKHR")};
    int fd = -1;
    const VkMemoryGetFdInfoKHR fd_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = slot->worker_shared_memory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    if ((get_fd.typed == NULL) ||
        (get_fd.typed(runtime->device, &fd_info, &fd) != VK_SUCCESS)) {
        return 0;
    }
    if (vkCreateBuffer(g_state.device.device, &info, NULL,
                       &slot->primary_shared_buffer) != VK_SUCCESS) {
        (void)close(fd);
        return 0;
    }
    VkMemoryRequirements primary_requirements = {0};
    vkGetBufferMemoryRequirements(g_state.device.device,
                                  slot->primary_shared_buffer,
                                  &primary_requirements);
    union {
        PFN_vkVoidFunction generic;
        PFN_vkGetMemoryFdPropertiesKHR typed;
    } get_fd_properties = {
        .generic = vkGetDeviceProcAddr(g_state.device.device,
                                       "vkGetMemoryFdPropertiesKHR")};
    VkMemoryFdPropertiesKHR fd_properties = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
    };
    if ((get_fd_properties.typed == NULL) ||
        (get_fd_properties.typed(g_state.device.device,
                                 VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                 fd, &fd_properties) != VK_SUCCESS)) {
        (void)close(fd);
        return 0;
    }
    const uint32_t primary_bits =
        fd_properties.memoryTypeBits & primary_requirements.memoryTypeBits;
    const uint32_t primary_type =
        vk_buffer_memory_type(g_state.device.present_phys, primary_bits, 0);
    if ((primary_type == UINT32_MAX) ||
        (primary_requirements.size > requirements.memoryRequirements.size)) {
        (void)close(fd);
        return 0;
    }
    const VkMemoryDedicatedAllocateInfo primary_dedicated = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .buffer = slot->primary_shared_buffer,
    };
    const VkImportMemoryFdInfoKHR import = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &primary_dedicated,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = fd,
    };
    const VkMemoryAllocateInfo primary_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import,
        .allocationSize = requirements.memoryRequirements.size,
        .memoryTypeIndex = primary_type,
    };
    if (vkAllocateMemory(g_state.device.device, &primary_allocation, NULL,
                         &slot->primary_shared_memory) != VK_SUCCESS) {
        (void)close(fd);
        return 0;
    }
    if (vkBindBufferMemory(g_state.device.device, slot->primary_shared_buffer,
                           slot->primary_shared_memory, 0U) != VK_SUCCESS) {
        return 0;
    }
    slot->shared_buffer_size = size;
    return 1;
}

static int vk_create_pack_pipeline(db_vk_independent_lane_runtime_t *runtime) {
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding = 0U,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1U,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1U,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1U,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
    };
    const VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2U,
        .pBindings = bindings,
    };
    if (vkCreateDescriptorSetLayout(runtime->device, &layout_info, NULL,
                                    &runtime->pack_descriptor_set_layout) !=
        VK_SUCCESS) {
        return 0;
    }
    const VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                      .size = sizeof(db_vk_buffer_push_t)};
    const VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1U,
        .pSetLayouts = &runtime->pack_descriptor_set_layout,
        .pushConstantRangeCount = 1U,
        .pPushConstantRanges = &push,
    };
    if (vkCreatePipelineLayout(runtime->device, &pipeline_layout, NULL,
                               &runtime->pack_pipeline_layout) != VK_SUCCESS) {
        return 0;
    }
    const VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = db_vk_pack_comp_spv_word_count * sizeof(uint32_t),
        .pCode = db_vk_pack_comp_spv,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(runtime->device, &module_info, NULL, &module) !=
        VK_SUCCESS) {
        return 0;
    }
    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = module,
                  .pName = "main"},
        .layout = runtime->pack_pipeline_layout,
    };
    const VkResult result =
        vkCreateComputePipelines(runtime->device, VK_NULL_HANDLE, 1U,
                                 &pipeline_info, NULL, &runtime->pack_pipeline);
    vkDestroyShaderModule(runtime->device, module, NULL);
    return DB_BOOL(result == VK_SUCCESS);
}

static int
vk_create_buffer_descriptors(db_vk_independent_lane_runtime_t *runtime) {
    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(runtime->device, &sampler_info, NULL,
                        &runtime->pack_sampler) != VK_SUCCESS) {
        return 0;
    }
    const VkDescriptorPoolSize sizes[2] = {
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = DB_VK_LANE_SLOT_COUNT},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = DB_VK_LANE_SLOT_COUNT},
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = DB_VK_LANE_SLOT_COUNT,
        .poolSizeCount = 2U,
        .pPoolSizes = sizes,
    };
    if (vkCreateDescriptorPool(runtime->device, &pool_info, NULL,
                               &runtime->pack_descriptor_pool) != VK_SUCCESS) {
        return 0;
    }
    VkDescriptorSetLayout layouts[DB_VK_LANE_SLOT_COUNT] = {
        runtime->pack_descriptor_set_layout,
        runtime->pack_descriptor_set_layout};
    VkDescriptorSet sets[DB_VK_LANE_SLOT_COUNT] = {0};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = runtime->pack_descriptor_pool,
        .descriptorSetCount = DB_VK_LANE_SLOT_COUNT,
        .pSetLayouts = layouts,
    };
    if (vkAllocateDescriptorSets(runtime->device, &allocate, sets) !=
        VK_SUCCESS) {
        return 0;
    }
    for (uint32_t index = 0U; index < DB_VK_LANE_SLOT_COUNT; index++) {
        db_vk_lane_slot_t *const slot = &runtime->slots[index];
        slot->worker_pack_descriptor_set = sets[index];
        const VkDescriptorImageInfo image = {
            .sampler = runtime->pack_sampler,
            .imageView = slot->worker_target.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkDescriptorBufferInfo buffer = {
            .buffer = slot->worker_shared_buffer,
            .range = slot->shared_buffer_size,
        };
        const VkWriteDescriptorSet writes[2] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = sets[index],
             .dstBinding = 0U,
             .descriptorCount = 1U,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &image},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = sets[index],
             .dstBinding = 1U,
             .descriptorCount = 1U,
             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .pBufferInfo = &buffer},
        };
        vkUpdateDescriptorSets(runtime->device, 2U, writes, 0U, NULL);
    }
    return 1;
}

static int
vk_create_unpack_pipeline(db_vk_independent_lane_runtime_t *runtime) {
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0U,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo descriptor_layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1U,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(
            g_state.device.device, &descriptor_layout, NULL,
            &runtime->unpack_descriptor_set_layout) != VK_SUCCESS) {
        return 0;
    }
    const VkPushConstantRange push = {.stageFlags =
                                          VK_SHADER_STAGE_FRAGMENT_BIT,
                                      .size = sizeof(db_vk_buffer_push_t)};
    const VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1U,
        .pSetLayouts = &runtime->unpack_descriptor_set_layout,
        .pushConstantRangeCount = 1U,
        .pPushConstantRanges = &push,
    };
    if (vkCreatePipelineLayout(g_state.device.device, &pipeline_layout, NULL,
                               &runtime->unpack_pipeline_layout) !=
        VK_SUCCESS) {
        return 0;
    }
    VkShaderModule modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = db_vk_present_vert_spv_word_count * sizeof(uint32_t),
        .pCode = db_vk_present_vert_spv,
    };
    if (vkCreateShaderModule(g_state.device.device, &module_info, NULL,
                             &modules[0]) != VK_SUCCESS) {
        return 0;
    }
    module_info.codeSize = db_vk_unpack_frag_spv_word_count * sizeof(uint32_t);
    module_info.pCode = db_vk_unpack_frag_spv;
    if (vkCreateShaderModule(g_state.device.device, &module_info, NULL,
                             &modules[1]) != VK_SUCCESS) {
        vkDestroyShaderModule(g_state.device.device, modules[0], NULL);
        return 0;
    }
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = modules[0],
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = modules[1],
         .pName = "main"},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1U,
        .scissorCount = 1U};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0F};
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1U,
        .pAttachments = &attachment};
    const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                              VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2U,
        .pDynamicStates = dynamic_states};
    const VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2U,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = runtime->unpack_pipeline_layout,
        .renderPass = g_state.backing.render_pass,
    };
    const VkResult result =
        vkCreateGraphicsPipelines(g_state.device.device, VK_NULL_HANDLE, 1U,
                                  &pipeline, NULL, &runtime->unpack_pipeline);
    vkDestroyShaderModule(g_state.device.device, modules[1], NULL);
    vkDestroyShaderModule(g_state.device.device, modules[0], NULL);
    if (result != VK_SUCCESS) {
        return 0;
    }
    const VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = DB_VK_LANE_SLOT_COUNT};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = DB_VK_LANE_SLOT_COUNT,
        .poolSizeCount = 1U,
        .pPoolSizes = &pool_size};
    if (vkCreateDescriptorPool(g_state.device.device, &pool, NULL,
                               &runtime->unpack_descriptor_pool) !=
        VK_SUCCESS) {
        return 0;
    }
    const VkDescriptorSetLayout layouts[DB_VK_LANE_SLOT_COUNT] = {
        runtime->unpack_descriptor_set_layout,
        runtime->unpack_descriptor_set_layout};
    VkDescriptorSet sets[DB_VK_LANE_SLOT_COUNT] = {0};
    const VkDescriptorSetAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = runtime->unpack_descriptor_pool,
        .descriptorSetCount = DB_VK_LANE_SLOT_COUNT,
        .pSetLayouts = layouts};
    if (vkAllocateDescriptorSets(g_state.device.device, &allocation, sets) !=
        VK_SUCCESS) {
        return 0;
    }
    for (uint32_t index = 0U; index < DB_VK_LANE_SLOT_COUNT; index++) {
        db_vk_lane_slot_t *const slot = &runtime->slots[index];
        slot->primary_unpack_descriptor_set = sets[index];
        const VkDescriptorBufferInfo buffer = {
            .buffer = slot->primary_shared_buffer,
            .range = slot->shared_buffer_size};
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[index],
            .dstBinding = 0U,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer};
        vkUpdateDescriptorSets(g_state.device.device, 1U, &write, 0U, NULL);
    }
    return 1;
}

int db_vk_buffer_transport_create(db_vk_independent_lane_runtime_t *runtime,
                                  uint32_t lane_index) {
    (void)lane_index;
    if ((runtime == NULL) || (runtime->device == VK_NULL_HANDLE)) {
        return 0;
    }
    VkPhysicalDeviceProperties worker_properties = {0};
    VkPhysicalDeviceProperties primary_properties = {0};
    vkGetPhysicalDeviceProperties(runtime->phys, &worker_properties);
    vkGetPhysicalDeviceProperties(g_state.device.present_phys,
                                  &primary_properties);
    const VkDeviceSize pixel_size =
        (g_state.backing.pixel_format == DB_PIXEL_FORMAT_RGBA16F) ? 8U : 4U;
    const VkDeviceSize size = (VkDeviceSize)g_state.backing.extent.width *
                              g_state.backing.extent.height * pixel_size;
    if ((size > worker_properties.limits.maxStorageBufferRange) ||
        (size > primary_properties.limits.maxStorageBufferRange) ||
        (vk_create_pack_pipeline(runtime) == 0)) {
        goto fail;
    }
    for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
        if (vk_create_shared_buffer(runtime, &runtime->slots[slot], size) ==
            0) {
            goto fail;
        }
    }
    if ((vk_create_buffer_descriptors(runtime) != 0) &&
        (vk_create_unpack_pipeline(runtime) != 0)) {
        return 1;
    }
fail:
    db_vk_buffer_transport_destroy(runtime);
    return 0;
}

void db_vk_buffer_transport_destroy(db_vk_independent_lane_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    vkDestroyPipeline(runtime->device, runtime->pack_pipeline, NULL);
    vkDestroySampler(runtime->device, runtime->pack_sampler, NULL);
    vkDestroyDescriptorPool(runtime->device, runtime->pack_descriptor_pool,
                            NULL);
    vkDestroyPipelineLayout(runtime->device, runtime->pack_pipeline_layout,
                            NULL);
    vkDestroyDescriptorSetLayout(runtime->device,
                                 runtime->pack_descriptor_set_layout, NULL);
    vkDestroyPipeline(g_state.device.device, runtime->unpack_pipeline, NULL);
    vkDestroyDescriptorPool(g_state.device.device,
                            runtime->unpack_descriptor_pool, NULL);
    vkDestroyPipelineLayout(g_state.device.device,
                            runtime->unpack_pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(g_state.device.device,
                                 runtime->unpack_descriptor_set_layout, NULL);
    runtime->pack_pipeline = VK_NULL_HANDLE;
    runtime->pack_sampler = VK_NULL_HANDLE;
    runtime->pack_descriptor_pool = VK_NULL_HANDLE;
    runtime->pack_pipeline_layout = VK_NULL_HANDLE;
    runtime->pack_descriptor_set_layout = VK_NULL_HANDLE;
    runtime->unpack_pipeline = VK_NULL_HANDLE;
    runtime->unpack_descriptor_pool = VK_NULL_HANDLE;
    runtime->unpack_pipeline_layout = VK_NULL_HANDLE;
    runtime->unpack_descriptor_set_layout = VK_NULL_HANDLE;
    for (uint32_t slot = 0U; slot < DB_VK_LANE_SLOT_COUNT; slot++) {
        vkDestroyBuffer(runtime->device,
                        runtime->slots[slot].worker_shared_buffer, NULL);
        vkFreeMemory(runtime->device, runtime->slots[slot].worker_shared_memory,
                     NULL);
        vkDestroyBuffer(g_state.device.device,
                        runtime->slots[slot].primary_shared_buffer, NULL);
        vkFreeMemory(g_state.device.device,
                     runtime->slots[slot].primary_shared_memory, NULL);
        runtime->slots[slot].worker_shared_buffer = VK_NULL_HANDLE;
        runtime->slots[slot].worker_shared_memory = VK_NULL_HANDLE;
        runtime->slots[slot].primary_shared_buffer = VK_NULL_HANDLE;
        runtime->slots[slot].primary_shared_memory = VK_NULL_HANDLE;
    }
}
#else
int db_vk_buffer_transport_create(db_vk_independent_lane_runtime_t *runtime,
                                  uint32_t lane_index) {
    (void)runtime;
    (void)lane_index;
    return 0;
}
void db_vk_buffer_transport_destroy(db_vk_independent_lane_runtime_t *runtime) {
    (void)runtime;
}
#endif
