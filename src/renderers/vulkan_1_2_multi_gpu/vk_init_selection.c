#include "../../config/runtime_options.h"
#include "../../core/db_byte_codec.h"
#include "../../core/db_core.h"
#include "../../core/db_format_contract.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_renderer_support.h"
#include "vk_diagnostics.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include "vk_renderer.h"
#include "vk_selection_diagnostics.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

int db_vk_wsi_is_headless(const db_vk_wsi_config_t *wsi_config) {
    return DB_BOOL((wsi_config == NULL) ||
                   (wsi_config->window_handle == NULL) ||
                   (wsi_config->get_required_instance_extensions == NULL) ||
                   (wsi_config->create_window_surface == NULL) ||
                   (wsi_config->get_framebuffer_size == NULL));
}

int db_vk_instance_extension_supported(const char *extension_name) {
    uint32_t extension_count = 0U;
    DB_VK_CHECK(BACKEND_NAME, vkEnumerateInstanceExtensionProperties(
                                  NULL, &extension_count, NULL));
    if ((extension_count == 0U) || (extension_name == NULL)) {
        return 0;
    }
    VkExtensionProperties *extensions = (VkExtensionProperties *)calloc(
        extension_count, sizeof(VkExtensionProperties));
    if (extensions == NULL) {
        runtime_failf(
            "failed to allocate Vulkan instance extension property array");
    }
    DB_VK_CHECK(BACKEND_NAME, vkEnumerateInstanceExtensionProperties(
                                  NULL, &extension_count, extensions));
    int supported = 0;
    for (uint32_t i = 0U; i < extension_count; i++) {
        if (strcmp(extensions[i].extensionName, extension_name) == 0) {
            supported = 1;
            break;
        }
    }
    free(extensions);
    return supported;
}

const char *
db_vk_compose_capability_mode(const db_renderer_execution_config_t *config) {
    const char *draw_mode = db_vk_capability_draw_mode_name(config);
    (void)db_snprintf(g_vk_capability_mode, sizeof(g_vk_capability_mode),
                      "%s(upload=%s,backbuffer_replay=no)", draw_mode,
                      DB_CAP_MODE_VK_UPLOAD_NONE);
    return g_vk_capability_mode;
}

const char *db_vk_present_mode_name(VkPresentModeKHR mode) {
    switch ((int)mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo_relaxed";
    default:
        return "unknown";
    }
}

uint32_t db_vk_capped_enumeration_count_or_log(const char *label,
                                               uint32_t reported_count,
                                               uint32_t max_count) {
    if (reported_count > max_count) {
        const db_log_field_t fields[] = {
            DB_LOG_STRING("resource", label),
            DB_LOG_U64("reported_count", reported_count),
            DB_LOG_U64("retained_count", max_count),
        };
        db_log_info(BACKEND_NAME, "vk_enumeration_truncated", fields,
                    DB_LOG_FIELD_COUNT(fields));
        return max_count;
    }
    return reported_count;
}

uint32_t db_vk_count_mask_bits(uint32_t mask) {
    uint32_t count = 0U;
    while (mask != 0U) {
        count += (mask & 1U);
        mask >>= 1U;
    }
    return count;
}

uint64_t db_vk_group_score(uint32_t device_count, uint32_t present_mask) {
    const uint32_t presentable_count = db_vk_count_mask_bits(present_mask);
    return (((uint64_t)presentable_count) << 32U) | (uint64_t)device_count;
}

static const char *
db_vk_multi_device_policy_name(db_vk_multi_device_policy_t policy) {
    switch (policy) {
    case DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY:
        return "group_only";
    case DB_VK_MULTI_DEVICE_POLICY_INDEPENDENT_OK:
        return "independent_ok";
    case DB_VK_MULTI_DEVICE_POLICY_AUTO:
    default:
        return "auto";
    }
}

static db_vk_multi_device_policy_t
db_vk_multi_device_policy_from_runtime(void) {
    const char *const text =
        db_runtime_option_get(DB_RUNTIME_OPT_VK_MULTI_DEVICE_POLICY);
    if ((text != NULL) && (strcmp(text, "group_only") == 0)) {
        return DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY;
    }
    if ((text != NULL) && (strcmp(text, "independent_ok") == 0)) {
        return DB_VK_MULTI_DEVICE_POLICY_INDEPENDENT_OK;
    }
    return DB_VK_MULTI_DEVICE_POLICY_AUTO;
}

int db_vk_allow_cpu_workers_from_runtime(void) {
    int allow_cpu_workers = 0;
    (void)db_parse_bool_text(
        db_runtime_option_get(DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS),
        &allow_cpu_workers);
    return allow_cpu_workers;
}

uint32_t db_vk_find_graphics_queue_family(VkPhysicalDevice phys) {
    uint32_t queue_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &queue_count, NULL);
    if (queue_count == 0U) {
        return UINT32_MAX;
    }
    VkQueueFamilyProperties *queue_props =
        (VkQueueFamilyProperties *)db_calloc_array_or_fail(
            BACKEND_NAME, "queue family properties", queue_count,
            sizeof(*queue_props));
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &queue_count, queue_props);
    uint32_t family_index = UINT32_MAX;
    for (uint32_t qi = 0U; qi < queue_count; qi++) {
        if ((queue_props[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            family_index = qi;
            break;
        }
    }
    free(queue_props);
    return family_index;
}

int db_vk_queue_family_supports_present(VkPhysicalDevice phys,
                                        uint32_t family_index,
                                        VkSurfaceKHR surface) {
    if ((family_index == UINT32_MAX) || (surface == VK_NULL_HANDLE)) {
        return 0;
    }
    VkBool32 supports_present = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(phys, family_index, surface,
                                         &supports_present);
    return DB_BOOL(supports_present);
}

static int
db_vk_device_extension_list_has(const VkExtensionProperties *extensions,
                                uint32_t extension_count,
                                const char *extension_name) {
    if ((extensions == NULL) || (extension_name == NULL)) {
        return 0;
    }
    for (uint32_t i = 0U; i < extension_count; i++) {
        if (strcmp(extensions[i].extensionName, extension_name) == 0) {
            return 1;
        }
    }
    return 0;
}

void db_vk_probe_device_interop_extensions(VkInstance instance,
                                           VkPhysicalDevice phys,
                                           db_vk_physical_device_info_t *info) {
    if (info == NULL) {
        return;
    }

    uint32_t extension_count = 0U;
    DB_VK_CHECK(BACKEND_NAME, vkEnumerateDeviceExtensionProperties(
                                  phys, NULL, &extension_count, NULL));
    if (extension_count == 0U) {
        return;
    }
    VkExtensionProperties *extensions = (VkExtensionProperties *)calloc(
        extension_count, sizeof(VkExtensionProperties));
    if (extensions == NULL) {
        runtime_failf("failed to allocate Vulkan extension property array");
    }
    DB_VK_CHECK(BACKEND_NAME, vkEnumerateDeviceExtensionProperties(
                                  phys, NULL, &extension_count, extensions));
    const int have_external_memory =
        db_vk_device_extension_list_has(
            extensions, extension_count,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
        db_vk_device_extension_list_has(
            extensions, extension_count,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    const int have_external_semaphore =
        db_vk_device_extension_list_has(
            extensions, extension_count,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
        db_vk_device_extension_list_has(
            extensions, extension_count,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    info->supports_external_memory_interop = have_external_memory;
    info->supports_external_semaphore_interop = have_external_semaphore;
    info->supports_dma_buf = db_vk_device_extension_list_has(
        extensions, extension_count,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    info->supports_drm_modifier = db_vk_device_extension_list_has(
        extensions, extension_count,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    info->supports_dma_buf_buffer = DB_BOOL(
        info->supports_dma_buf && db_vk_probe_external_buffer_interop(phys));
    info->supports_foreign_queue = db_vk_device_extension_list_has(
        extensions, extension_count,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    info->supports_calibrated_timestamps_khr = db_vk_device_extension_list_has(
        extensions, extension_count,
        VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    info->supports_calibrated_timestamps_ext = db_vk_device_extension_list_has(
        extensions, extension_count,
        VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    info->supports_hdr_metadata = db_vk_device_extension_list_has(
        extensions, extension_count, VK_EXT_HDR_METADATA_EXTENSION_NAME);
    free(extensions);
    if ((info->supports_calibrated_timestamps_khr != 0) ||
        (info->supports_calibrated_timestamps_ext != 0)) {
        union {
            PFN_vkVoidFunction generic;
            PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR khr;
            PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT ext;
        } get_domains = {
            .generic = vkGetInstanceProcAddr(
                instance,
                (info->supports_calibrated_timestamps_khr != 0)
                    ? "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR"
                    : "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"),
        };
        uint32_t domain_count = 0U;
        VkResult count_result = VK_ERROR_EXTENSION_NOT_PRESENT;
        if (get_domains.generic != NULL) {
            count_result = (info->supports_calibrated_timestamps_khr != 0)
                               ? get_domains.khr(phys, &domain_count, NULL)
                               : get_domains.ext(phys, &domain_count, NULL);
        }
        if ((get_domains.generic != NULL) && (count_result == VK_SUCCESS)) {
            VkTimeDomainKHR domains[8] = {0};
            domain_count = DB_MIN(domain_count, 8U);
            const VkResult domains_result =
                (info->supports_calibrated_timestamps_khr != 0)
                    ? get_domains.khr(phys, &domain_count, domains)
                    : get_domains.ext(phys, &domain_count, domains);
            if (domains_result == VK_SUCCESS) {
                for (uint32_t index = 0U; index < domain_count; index++) {
                    info->supports_time_domain_monotonic_raw |=
                        DB_BOOL(domains[index] ==
                                VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR);
                    info->supports_time_domain_monotonic |= DB_BOOL(
                        domains[index] == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR);
                }
            }
        }
    }
    const VkPhysicalDeviceExternalSemaphoreInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkExternalSemaphoreProperties semaphore_properties = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
    };
    vkGetPhysicalDeviceExternalSemaphoreProperties(phys, &semaphore_info,
                                                   &semaphore_properties);
    const VkExternalSemaphoreFeatureFlags required =
        VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
        VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    info->supports_sync_fd =
        DB_BOOL((semaphore_properties.externalSemaphoreFeatures & required) ==
                required);
}

void db_vk_probe_device_hdr_surface(VkPhysicalDevice phys, VkSurfaceKHR surface,
                                    db_vk_physical_device_info_t *info) {
    if ((phys == VK_NULL_HANDLE) || (surface == VK_NULL_HANDLE) ||
        (info == NULL) || (info->supports_present == 0)) {
        return;
    }
    uint32_t format_count = 0U;
    if ((vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &format_count,
                                              NULL) != VK_SUCCESS) ||
        (format_count == 0U)) {
        return;
    }
    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)calloc(format_count, sizeof(*formats));
    if (formats == NULL) {
        runtime_failf("failed to allocate Vulkan surface format array");
    }
    const VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        phys, surface, &format_count, formats);
    if ((result == VK_SUCCESS) || (result == VK_INCOMPLETE)) {
        const db_vk_surface_format_selection_t selection =
            db_vk_resolve_surface_format_for_output(formats, format_count,
                                                    DB_OUTPUT_FORMAT_AUTO, 1);
        info->supports_hdr10_format =
            selection.capability.native_format_supported;
        info->supports_hdr10_colorspace =
            selection.capability.colorspace_supported;
        info->supports_hdr10_surface_pair = selection.hdr_enabled;
    }
    free(formats);
}

static int db_vk_physical_device_supports_verified_hdr(
    const db_vk_physical_device_info_t *info) {
    return DB_BOOL((info != NULL) && (info->supports_present != 0) &&
                   (info->supports_hdr10_surface_pair != 0) &&
                   (info->supports_hdr_metadata != 0));
}

static uint64_t
db_vk_physical_device_score(const db_vk_physical_device_info_t *info,
                            db_output_format_request_t output_request) {
    if ((info == NULL) || (info->supports_graphics == 0)) {
        return 0U;
    }
    const int supports_verified_hdr =
        db_vk_physical_device_supports_verified_hdr(info);
    if ((output_request == DB_OUTPUT_FORMAT_HDR) &&
        (supports_verified_hdr == 0)) {
        return 0U;
    }
    uint64_t score = 0U;
    if ((output_request == DB_OUTPUT_FORMAT_AUTO) &&
        (supports_verified_hdr != 0)) {
        score += (1ULL << DB_VK_HDR_DEVICE_SCORE_BONUS_SHIFT);
    }
    if (info->supports_present != 0) {
        score += (1ULL << 32U);
    }
    switch ((int)info->properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score += (5ULL << DB_VK_DEVICE_SCORE_SHIFT);
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score += (4ULL << DB_VK_DEVICE_SCORE_SHIFT);
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score += (3ULL << DB_VK_DEVICE_SCORE_SHIFT);
        break;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        score += (2ULL << DB_VK_DEVICE_SCORE_SHIFT);
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score += (1ULL << DB_VK_DEVICE_SCORE_SHIFT);
        break;
    default:
        break;
    }
    return score + (uint64_t)(MAX_GPU_COUNT - info->queue_family_index);
}

uint32_t db_vk_choose_primary_physical_index_for_output(
    const DeviceSelectionState *selection,
    db_output_format_request_t output_request) {
    if ((selection == NULL) || (selection->phys_count == 0U)) {
        return 0U;
    }
    uint32_t best_index = 0U;
    uint64_t best_score = 0U;
    for (uint32_t i = 0U; i < selection->phys_count; i++) {
        const uint64_t score = db_vk_physical_device_score(
            &selection->phys_info[i], output_request);
        if ((i == 0U) || (score > best_score)) {
            best_score = score;
            best_index = i;
        }
    }
    return best_index;
}

void db_vk_set_lane_reason(db_vk_device_lane_t *lane, const char *reason) {
    if ((lane == NULL) || (reason == NULL)) {
        return;
    }
    (void)db_snprintf(lane->inactive_reason, sizeof(lane->inactive_reason),
                      "%s", reason);
}

void db_vk_fill_lane_identity(db_vk_device_lane_t *lane,
                              const db_vk_physical_device_info_t *info,
                              db_vk_lane_backend_t backend,
                              uint32_t physical_index) {
    if ((lane == NULL) || (info == NULL)) {
        return;
    }
    *lane = (db_vk_device_lane_t){0};
    lane->phys = info->phys;
    lane->backend = backend;
    lane->physical_index = physical_index;
    lane->queue_family_index = info->queue_family_index;
    lane->can_present = info->supports_present;
    lane->supports_required_format_usage = 1;
    lane->can_compose_to_primary =
        (info->supports_external_memory_interop != 0) &&
        (info->supports_external_semaphore_interop != 0) &&
        (info->supports_external_image_export != 0);
    lane->group_index = -1;
    lane->group_lane_index = -1;
    (void)db_snprintf(lane->name, sizeof(lane->name), "%s",
                      info->properties.deviceName);
}

DeviceSelectionState
db_vk_select_devices_and_group(VkInstance instance, VkSurfaceKHR surface,
                               db_output_format_request_t output_request,
                               VkFormat working_format) {
    DeviceSelectionState selection = {0};
    const db_vk_multi_device_policy_t multi_device_policy =
        db_vk_multi_device_policy_from_runtime();
    const int allow_cpu_workers = db_vk_allow_cpu_workers_from_runtime();

    uint32_t phys_count = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkEnumeratePhysicalDevices(instance, &phys_count, NULL));
    if (phys_count == 0U) {
        runtime_failf("No Vulkan physical devices found");
    }
    selection.phys_count = db_vk_capped_enumeration_count_or_log(
        "physical device", phys_count, MAX_GPU_COUNT);
    VkResult enumerate_phys_result = vkEnumeratePhysicalDevices(
        instance, &selection.phys_count, selection.phys);
    if ((enumerate_phys_result != VK_SUCCESS) &&
        (enumerate_phys_result != VK_INCOMPLETE)) {
        db_vk_fail(BACKEND_NAME, "vkEnumeratePhysicalDevices",
                   enumerate_phys_result, __FILE__, __LINE__);
    }
    for (uint32_t i = 0U; i < selection.phys_count; i++) {
        db_vk_physical_device_info_t *info = &selection.phys_info[i];
        info->phys = selection.phys[i];
        vkGetPhysicalDeviceProperties(info->phys, &info->properties);
        VkPhysicalDeviceDriverProperties driver_properties;
        driver_properties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        driver_properties.pNext = NULL;
        VkPhysicalDeviceIDProperties id_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        id_properties.pNext = &driver_properties;
        VkPhysicalDeviceProperties2 properties2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &id_properties,
        };
        vkGetPhysicalDeviceProperties2(info->phys, &properties2);
        info->driver_properties = driver_properties;
        memcpy(info->device_uuid, id_properties.deviceUUID, VK_UUID_SIZE);
        info->queue_family_index = db_vk_find_graphics_queue_family(info->phys);
        info->supports_graphics = (info->queue_family_index != UINT32_MAX);
        info->supports_present = db_vk_queue_family_supports_present(
            info->phys, info->queue_family_index, surface);
        db_vk_probe_device_interop_extensions(instance, info->phys, info);
        db_vk_probe_device_hdr_surface(info->phys, surface, info);
        info->supports_external_image_export =
            db_vk_probe_external_image_interop(
                info->phys, working_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
        info->supports_external_image_import =
            db_vk_probe_external_image_interop(
                info->phys, working_format, VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    }
    selection.primary_phys_index =
        db_vk_choose_primary_physical_index_for_output(&selection,
                                                       output_request);
    const char *const probe_uuid = getenv("DRIVERBENCH_PROBE_DEVICE_UUID");
    if ((probe_uuid != NULL) && (strlen(probe_uuid) == (VK_UUID_SIZE * 2U))) {
        for (uint32_t index = 0U; index < selection.phys_count; index++) {
            char uuid_text[(VK_UUID_SIZE * 2U) + 1U] = {0};
            if (db_hex_encode_lower(selection.phys_info[index].device_uuid,
                                    VK_UUID_SIZE, uuid_text,
                                    sizeof(uuid_text)) == 0) {
                runtime_failf("failed to encode Vulkan device UUID");
            }
            if (strcmp(uuid_text, probe_uuid) == 0) {
                selection.primary_phys_index = index;
                break;
            }
        }
    }
    int prefer_hdr_presenter = 0;
    if (output_request != DB_OUTPUT_FORMAT_SDR) {
        for (uint32_t index = 0U; index < selection.phys_count; index++) {
            prefer_hdr_presenter |= db_vk_physical_device_supports_verified_hdr(
                &selection.phys_info[index]);
        }
    }

    DeviceGroupInfo best = {0};
    if (surface != VK_NULL_HANDLE) {
        uint32_t group_count = 0;
        DB_VK_CHECK(BACKEND_NAME, vkEnumeratePhysicalDeviceGroups(
                                      instance, &group_count, NULL));
        selection.group_count = db_vk_capped_enumeration_count_or_log(
            "physical device group", group_count, MAX_GPU_COUNT);
        for (uint32_t i = 0; i < selection.group_count; i++) {
            selection.groups[i].sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
        }
        VkResult enumerate_groups_result = vkEnumeratePhysicalDeviceGroups(
            instance, &selection.group_count, selection.groups);
        if ((enumerate_groups_result != VK_SUCCESS) &&
            (enumerate_groups_result != VK_INCOMPLETE)) {
            db_vk_fail(BACKEND_NAME, "vkEnumeratePhysicalDeviceGroups",
                       enumerate_groups_result, __FILE__, __LINE__);
        }

        uint64_t best_score = 0U;
        for (uint32_t gi = 0; gi < selection.group_count; gi++) {
            VkPhysicalDeviceGroupProperties *group_props =
                &selection.groups[gi];
            if (group_props->physicalDeviceCount < 2) {
                continue;
            }

            uint32_t mask = 0U;
            uint32_t hdr_mask = 0U;
            const uint32_t group_device_scan_count =
                DB_MIN(group_props->physicalDeviceCount, MAX_GPU_COUNT);
            for (uint32_t di = 0; di < group_device_scan_count; di++) {
                VkPhysicalDevice pd = group_props->physicalDevices[di];
                uint32_t queue_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_count,
                                                         NULL);
                if (queue_count == 0U) {
                    continue;
                }
                VkQueueFamilyProperties *queue_props =
                    (VkQueueFamilyProperties *)db_calloc_array_or_fail(
                        BACKEND_NAME, "group queue family properties",
                        queue_count, sizeof(*queue_props));
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_count,
                                                         queue_props);

                for (uint32_t qi = 0; qi < queue_count; qi++) {
                    VkBool32 supports_present = 0;
                    vkGetPhysicalDeviceSurfaceSupportKHR(pd, qi, surface,
                                                         &supports_present);
                    if (supports_present &&
                        (queue_props[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        mask |= (MASK_GPU0 << di);
                        for (uint32_t physical_index = 0U;
                             physical_index < selection.phys_count;
                             physical_index++) {
                            if ((selection.phys_info[physical_index].phys ==
                                 pd) &&
                                (db_vk_physical_device_supports_verified_hdr(
                                     &selection.phys_info[physical_index]) !=
                                 0)) {
                                hdr_mask |= (MASK_GPU0 << di);
                                break;
                            }
                        }
                        break;
                    }
                }
                free(queue_props);
            }

            if ((mask == 0U) ||
                ((prefer_hdr_presenter != 0) && (hdr_mask == 0U))) {
                continue;
            }
            selection.group_info[gi].grp = *group_props;
            selection.group_info[gi].presentable_mask = mask;
            selection.group_info[gi].hdr_presentable_mask = hdr_mask;

            const uint64_t group_score =
                db_vk_group_score(group_device_scan_count, mask);
            if ((selection.have_group == 0) || (group_score > best_score)) {
                best.grp = *group_props;
                best.presentable_mask = mask;
                best.hdr_presentable_mask = hdr_mask;
                best_score = group_score;
                selection.have_group = 1;
            }
        }
    }

    selection.chosen_count = 1U;
    selection.present_mask = MASK_GPU0;
    selection.present_phys = selection.phys[selection.primary_phys_index];
    selection.execution_mode = DB_VK_EXECUTION_MODE_SINGLE_GPU;
    if (selection.have_group) {
        selection.chosen_count = best.grp.physicalDeviceCount;
        if (selection.chosen_count > MAX_GPU_COUNT) {
            const db_log_field_t fields[] = {
                DB_LOG_U64("reported_count", selection.chosen_count),
                DB_LOG_U64("retained_count", MAX_GPU_COUNT),
            };
            db_log_info(BACKEND_NAME, "vk_device_group_truncated", fields,
                        DB_LOG_FIELD_COUNT(fields));
            selection.chosen_count = MAX_GPU_COUNT;
        }
        for (uint32_t i = 0; i < selection.chosen_count; i++) {
            selection.chosen_phys[i] = best.grp.physicalDevices[i];
        }
        const uint32_t usable_mask =
            (selection.chosen_count >= 32U)
                ? 0xFFFFFFFFU
                : ((1U << selection.chosen_count) - 1U);
        selection.present_mask = best.presentable_mask & usable_mask;
        selection.execution_mode = (selection.chosen_count > 1U)
                                       ? DB_VK_EXECUTION_MODE_DEVICE_GROUP
                                       : DB_VK_EXECUTION_MODE_SINGLE_GPU;
    } else {
        selection.chosen_phys[0] = selection.phys[selection.primary_phys_index];
        selection.execution_mode =
            ((selection.phys_count > 1U) &&
             (multi_device_policy != DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY))
                ? DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES
                : DB_VK_EXECUTION_MODE_SINGLE_GPU;
    }

    uint32_t present_device_index = 0U;
    const uint32_t preferred_present_mask =
        (selection.have_group && (prefer_hdr_presenter != 0))
            ? best.hdr_presentable_mask
            : selection.present_mask;
    if (selection.have_group && ((preferred_present_mask & MASK_GPU0) == 0U)) {
        for (uint32_t i = 0; i < selection.chosen_count; i++) {
            if ((preferred_present_mask & (MASK_GPU0 << i)) != 0U) {
                present_device_index = i;
                break;
            }
        }
    }
    if (selection.execution_mode == DB_VK_EXECUTION_MODE_DEVICE_GROUP) {
        selection.primary_lane_index = present_device_index;
        selection.present_phys = selection.chosen_phys[present_device_index];
        selection.lane_count = selection.chosen_count;
        selection.active_lane_count = selection.chosen_count;
        for (uint32_t i = 0U; i < selection.chosen_count; i++) {
            uint32_t physical_index = 0U;
            for (; physical_index < selection.phys_count; physical_index++) {
                if (selection.phys_info[physical_index].phys ==
                    selection.chosen_phys[i]) {
                    break;
                }
            }
            if (i == present_device_index) {
                selection.primary_phys_index = physical_index;
            }
            db_vk_device_lane_t *lane = &selection.lanes[i];
            db_vk_fill_lane_identity(lane, &selection.phys_info[physical_index],
                                     DB_VK_LANE_BACKEND_GROUP, physical_index);
            lane->device_mask = MASK_GPU0 << i;
            lane->group_lane_index =
                db_checked_u32_to_int(BACKEND_NAME, "group_lane_index", i);
            lane->active_for_scheduler = 1;
            lane->can_compose_to_primary = 1;
        }
    } else {
        selection.primary_lane_index = 0U;
        selection.lane_count = 1U;
        selection.active_lane_count = 1U;
        db_vk_device_lane_t *primary_lane = &selection.lanes[0];
        db_vk_fill_lane_identity(
            primary_lane, &selection.phys_info[selection.primary_phys_index],
            DB_VK_LANE_BACKEND_PRIMARY, selection.primary_phys_index);
        primary_lane->device_mask = MASK_GPU0;
        primary_lane->active_for_scheduler = 1;
        primary_lane->can_compose_to_primary = 1;
        for (uint32_t physical_index = 0U;
             physical_index < selection.phys_count; physical_index++) {
            if (physical_index == selection.primary_phys_index) {
                continue;
            }
            if (selection.lane_count >= MAX_GPU_COUNT) {
                break;
            }
            db_vk_device_lane_t *lane =
                &selection.lanes[selection.lane_count++];
            db_vk_fill_lane_identity(lane, &selection.phys_info[physical_index],
                                     DB_VK_LANE_BACKEND_INDEPENDENT,
                                     physical_index);
            lane->device_mask = MASK_GPU0;
            lane->active_for_scheduler = 0;
            const db_vk_physical_device_info_t *const worker_info =
                &selection.phys_info[physical_index];
            const db_vk_physical_device_info_t *const primary_info =
                &selection.phys_info[selection.primary_phys_index];
            const int opaque_identity_compatible =
                DB_BOOL(memcmp(worker_info->device_uuid,
                               primary_info->device_uuid, VK_UUID_SIZE) == 0);
            uint64_t common_modifier = 0U;
            const int have_dma_extensions =
                DB_BOOL(worker_info->supports_dma_buf &&
                        worker_info->supports_drm_modifier &&
                        primary_info->supports_dma_buf &&
                        primary_info->supports_drm_modifier);
            int have_common_modifier = 0;
            if (have_dma_extensions != 0) {
                have_common_modifier = db_vk_find_common_drm_modifier(
                    worker_info->phys, primary_info->phys, working_format,
                    &common_modifier);
            }
            const db_vk_transport_capabilities_t capabilities = {
                .opaque_identity_compatible = opaque_identity_compatible,
                .opaque_external_image =
                    worker_info->supports_external_image_export &&
                    primary_info->supports_external_image_import,
                .dma_buf_external_image = have_dma_extensions,
                .dma_buf_modifier_compatible = have_common_modifier,
                .dma_buf_external_buffer =
                    worker_info->supports_dma_buf_buffer &&
                    primary_info->supports_dma_buf_buffer,
                .sync_fd_semaphore = worker_info->supports_sync_fd &&
                                     primary_info->supports_sync_fd,
                .external_domain_supported = 1,
            };
            const db_vk_transport_profile_t profile =
                db_vk_negotiate_transport(&capabilities);
            lane->transport = profile.transport;
#ifdef DB_VK_TEST_FORCE_BUFFER_TRANSPORT
            if (capabilities.dma_buf_external_buffer != 0) {
                lane->transport = DB_VK_TRANSPORT_DMA_BUF_BUFFER;
            }
#endif
            lane->ownership_domain = profile.ownership_domain;
            lane->drm_modifier = common_modifier;
            lane->can_compose_to_primary = profile.supported;
            if ((selection.phys_info[physical_index].properties.deviceType ==
                 VK_PHYSICAL_DEVICE_TYPE_CPU) &&
                (allow_cpu_workers == 0)) {
                lane->can_compose_to_primary = 0;
                db_vk_set_lane_reason(lane,
                                      "cpu worker disabled by runtime policy");
            } else if (profile.supported == 0) {
                db_vk_set_lane_reason(
                    lane, (have_dma_extensions != 0)
                              ? "no_common_drm_modifier"
                              : "no_compatible_external_memory_transport");
            } else {
                db_vk_set_lane_reason(lane, "transport_profile_accepted");
            }
        }
    }
    const db_log_field_t policy_fields[] = {
        DB_LOG_TOKEN("policy",
                     db_vk_multi_device_policy_name(multi_device_policy)),
        DB_LOG_BOOL("allow_cpu_workers", allow_cpu_workers),
    };
    db_log_info(BACKEND_NAME, "vk_multi_device_policy", policy_fields,
                DB_LOG_FIELD_COUNT(policy_fields));
    db_vk_log_physical_devices(&selection);
    db_vk_log_execution_plan(&selection);
    return selection;
}
