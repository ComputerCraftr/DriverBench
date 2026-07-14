#include "vk_diagnostics.h"
#include "vk_init_internal.h"

#include "core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_render_types.h"
#include "vk_internal.h"
#include "vk_renderer.h"

#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

void db_vk_init_phase_instance_surface(
    const db_vk_wsi_config_t *wsi_config,
    db_vk_init_instance_surface_phase_t *out_phase) {
    if (out_phase == NULL) {
        runtime_failf("Invalid Vulkan init phase output");
    }

    const char *inst_exts[MAX_INSTANCE_EXTS];
    uint32_t inst_ext_count = 0;
    if (db_vk_wsi_is_headless(wsi_config) == 0) {
        uint32_t required_ext_count = 0;
        const char *const *required_exts =
            wsi_config->get_required_instance_extensions(&required_ext_count);
        if ((required_ext_count == 0U) || (required_exts == NULL)) {
            runtime_failf(
                "Windowing backend did not provide Vulkan instance extensions");
        }
        for (uint32_t i = 0; i < required_ext_count; i++) {
            inst_exts[inst_ext_count++] = required_exts[i];
        }
    }
    inst_exts[inst_ext_count++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    if (db_vk_instance_extension_supported(
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) != 0) {
        inst_exts[inst_ext_count++] =
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
    }
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
            out_phase->instance, wsi_config->window_handle,
            &out_phase->surface);
        if (create_surface_result != VK_SUCCESS) {
            db_vk_fail(BACKEND_NAME, "create_window_surface",
                       create_surface_result, __FILE__, __LINE__);
        }
    }
}

void db_vk_init_phase_device(VkInstance instance, VkSurfaceKHR surface,
                             int vsync_enabled,
                             db_output_format_request_t output_request,
                             db_pixel_format_t working_format,
                             db_vk_init_device_phase_t *out_phase) {
    if (out_phase == NULL) {
        return;
    }

    *out_phase = (db_vk_init_device_phase_t){0};
    out_phase->headless_offscreen = DB_BOOL(surface == VK_NULL_HANDLE);
    const VkFormat working_image_format =
        db_vk_image_format_from_pixel_format(working_format);
    if (working_image_format == VK_FORMAT_UNDEFINED) {
        runtime_failf("unsupported Vulkan working pixel format");
    }
    out_phase->selection = db_vk_select_devices_and_group(
        instance, surface, output_request, working_image_format);
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
    if (qf_count == 0U) {
        runtime_failf("Vulkan physical device has no queue families");
    }
    VkQueueFamilyProperties *qf =
        (VkQueueFamilyProperties *)db_calloc_array_or_fail(
            BACKEND_NAME, "queue family properties", qf_count, sizeof(*qf));
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
        runtime_failf("No graphics+present queue family found");
    }
    out_phase->queue_timestamp_valid_bits = qf[gfx_qf].timestampValidBits;
    out_phase->queue_family_index = gfx_qf;
    free(qf);

    VkPhysicalDeviceProperties phys_props;
    vkGetPhysicalDeviceProperties(out_phase->present_phys, &phys_props);
    out_phase->timestamp_period_ns =
        db_f32_to_double(phys_props.limits.timestampPeriod);

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
        out_phase->hdr_metadata_supported =
            out_phase->selection
                .phys_info[out_phase->selection.primary_phys_index]
                .supports_hdr_metadata;
        if (out_phase->hdr_metadata_supported != 0) {
            dev_exts[dev_ext_count++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
        }
    }
#ifdef __linux__
    if (out_phase->selection.phys_info[out_phase->selection.primary_phys_index]
            .supports_external_memory_interop != 0) {
        dev_exts[dev_ext_count++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
    }
    if (out_phase->selection.phys_info[out_phase->selection.primary_phys_index]
            .supports_external_semaphore_interop != 0) {
        dev_exts[dev_ext_count++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
    }
    if (out_phase->selection.phys_info[out_phase->selection.primary_phys_index]
            .supports_dma_buf != 0) {
        dev_exts[dev_ext_count++] =
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
    }
    if (out_phase->selection.phys_info[out_phase->selection.primary_phys_index]
            .supports_drm_modifier != 0) {
        dev_exts[dev_ext_count++] =
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
    }
    if (out_phase->selection.phys_info[out_phase->selection.primary_phys_index]
            .supports_calibrated_timestamps_khr != 0) {
        dev_exts[dev_ext_count++] = VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    } else if (out_phase->selection
                   .phys_info[out_phase->selection.primary_phys_index]
                   .supports_calibrated_timestamps_ext != 0) {
        dev_exts[dev_ext_count++] = VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    }
#endif

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
        if (fmt_count == 0U) {
            runtime_failf("Vulkan surface exposes no formats");
        }
        VkSurfaceFormatKHR *fmts =
            (VkSurfaceFormatKHR *)db_calloc_array_or_fail(
                BACKEND_NAME, "surface formats", fmt_count, sizeof(*fmts));
        DB_VK_CHECK(BACKEND_NAME,
                    vkGetPhysicalDeviceSurfaceFormatsKHR(
                        out_phase->present_phys, surface, &fmt_count, fmts));
        const db_vk_surface_format_selection_t format_selection =
            db_vk_resolve_surface_format_for_output(
                fmts, fmt_count, output_request,
                out_phase->hdr_metadata_supported);
        out_phase->surface_format = format_selection.surface_format;
        out_phase->native_hdr_enabled = format_selection.hdr_enabled;
        out_phase->native_output_capability = format_selection.capability;
        free(fmts);
        const db_log_field_t output_fields[] = {
            DB_LOG_TOKEN("output_request",
                         db_output_format_request_name(output_request)),
            DB_LOG_BOOL("native_format_supported",
                        format_selection.capability.native_format_supported),
            DB_LOG_BOOL("colorspace_supported",
                        format_selection.capability.colorspace_supported),
            DB_LOG_BOOL("metadata_supported",
                        format_selection.capability.metadata_supported),
            DB_LOG_BOOL("native_hdr_selected", format_selection.hdr_enabled),
            DB_LOG_TOKEN(
                "surface_format",
                db_vk_format_name(format_selection.surface_format.format)),
            DB_LOG_STRING("reason",
                          format_selection.capability.unavailable_reason),
        };
        db_log_info(BACKEND_NAME, "vk_wsi_output_capability", output_fields,
                    DB_LOG_FIELD_COUNT(output_fields));
        if ((output_request == DB_OUTPUT_FORMAT_HDR) &&
            (format_selection.hdr_enabled == 0)) {
            runtime_failf("output-format=hdr requested but Vulkan WSI HDR10 is "
                          "unavailable: %s",
                          format_selection.capability.unavailable_reason);
        }
        out_phase->present_mode = db_vk_choose_present_mode(
            out_phase->present_phys, surface, vsync_enabled);
        const db_log_field_t present_fields[] = {
            DB_LOG_TOKEN("present_mode",
                         db_vk_present_mode_name(out_phase->present_mode)),
            DB_LOG_BOOL("vsync", vsync_enabled),
            DB_LOG_BOOL("blocking", db_vk_present_mode_is_blocking(
                                        out_phase->present_mode)),
            DB_LOG_U64("frame_budget_ns", db_vk_scheduler_frame_budget_ns(
                                              out_phase->present_mode)),
            DB_LOG_U64("frame_safety_ns", db_vk_scheduler_frame_safety_ns(
                                              out_phase->present_mode)),
        };
        db_log_info(BACKEND_NAME, "vk_present_policy", present_fields,
                    DB_LOG_FIELD_COUNT(present_fields));
        if ((vsync_enabled == 0) &&
            (out_phase->present_mode == VK_PRESENT_MODE_FIFO_KHR)) {
            const db_log_field_t fallback_fields[] = {
                DB_LOG_TOKEN("requested_mode", "nonblocking"),
                DB_LOG_TOKEN("effective_mode", "fifo"),
                DB_LOG_TOKEN("reason", "surface_mode_unavailable"),
            };
            db_log_info(BACKEND_NAME, "vk_present_fallback", fallback_fields,
                        DB_LOG_FIELD_COUNT(fallback_fields));
        }
    } else {
        out_phase->surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
        out_phase->surface_format.colorSpace =
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        out_phase->present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        out_phase->native_output_capability = (db_native_output_capability_t){
            .native_bit_depth = DB_SDR_NATIVE_BIT_DEPTH,
            .hdr_format = DB_NATIVE_OUTPUT_XRGB2101010,
            .hdr_colorspace = DB_OUTPUT_COLORSPACE_BT2020,
            .hdr_transfer = DB_OUTPUT_TRANSFER_PQ,
            .unavailable_reason = "headless_native_output_unavailable",
        };
        const db_log_field_t present_fields[] = {
            DB_LOG_TOKEN("present_mode", "headless_offscreen"),
            DB_LOG_BOOL("vsync", 0),
            DB_LOG_BOOL("blocking", 0),
            DB_LOG_U64("frame_budget_ns", db_vk_scheduler_frame_budget_ns(
                                              out_phase->present_mode)),
            DB_LOG_U64("frame_safety_ns", db_vk_scheduler_frame_safety_ns(
                                              out_phase->present_mode)),
        };
        db_log_info(BACKEND_NAME, "vk_present_policy", present_fields,
                    DB_LOG_FIELD_COUNT(present_fields));
    }
}
