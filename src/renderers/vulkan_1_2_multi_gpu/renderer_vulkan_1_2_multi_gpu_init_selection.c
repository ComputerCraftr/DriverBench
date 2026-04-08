#include "../../config/runtime_options.h"
#include "../../core/db_core.h"
#include "../renderer_benchmark_types.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

int db_vk_wsi_is_headless(const db_vk_wsi_config_t *wsi_config) {
    return ((wsi_config == NULL) || (wsi_config->window_handle == NULL) ||
            (wsi_config->get_required_instance_extensions == NULL) ||
            (wsi_config->create_window_surface == NULL) ||
            (wsi_config->get_framebuffer_size == NULL))
               ? 1
               : 0;
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
        failf("failed to allocate Vulkan instance extension property array");
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

const char *db_vk_compose_capability_mode(db_pattern_t pattern) {
    const char *draw_mode = db_vk_capability_draw_mode_name(pattern);
    (void)db_snprintf(g_vk_capability_mode, sizeof(g_vk_capability_mode),
                      "%s(upload=%s,backbuffer_replay=no)", draw_mode,
                      DB_CAP_MODE_VK_UPLOAD_NONE);
    return g_vk_capability_mode;
}

const char *db_vk_present_mode_name(VkPresentModeKHR mode) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (mode) {
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
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

uint32_t db_vk_capped_enumeration_count_or_log(const char *label,
                                               uint32_t reported_count,
                                               uint32_t max_count) {
    if (reported_count > max_count) {
        infof("%s count %u exceeds local cap %u; truncating enumeration", label,
              reported_count, max_count);
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
    VkQueueFamilyProperties *queue_props = (VkQueueFamilyProperties *)calloc(
        queue_count, sizeof(VkQueueFamilyProperties));
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
    return (supports_present != 0) ? 1 : 0;
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

void db_vk_probe_device_interop_extensions(
    VkPhysicalDevice phys, int *supports_external_memory_interop,
    int *supports_external_semaphore_interop) {
    if (supports_external_memory_interop != NULL) {
        *supports_external_memory_interop = 0;
    }
    if (supports_external_semaphore_interop != NULL) {
        *supports_external_semaphore_interop = 0;
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
        failf("failed to allocate Vulkan extension property array");
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
    free(extensions);

    if (supports_external_memory_interop != NULL) {
        *supports_external_memory_interop = have_external_memory;
    }
    if (supports_external_semaphore_interop != NULL) {
        *supports_external_semaphore_interop = have_external_semaphore;
    }
}

static uint64_t
db_vk_physical_device_score(const db_vk_physical_device_info_t *info) {
    if ((info == NULL) || (info->supports_graphics == 0)) {
        return 0U;
    }
    uint64_t score = 0U;
    if (info->supports_present != 0) {
        score += (1ULL << 32U);
    }
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
    switch (info->properties.deviceType) {
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
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return score + (uint64_t)(MAX_GPU_COUNT - info->queue_family_index);
}

static uint32_t
db_vk_choose_primary_physical_index(const DeviceSelectionState *selection) {
    uint32_t best_index = 0U;
    uint64_t best_score = 0U;
    for (uint32_t i = 0U; i < selection->phys_count; i++) {
        const uint64_t score =
            db_vk_physical_device_score(&selection->phys_info[i]);
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
        (info->supports_external_semaphore_interop != 0);
    lane->group_index = -1;
    lane->group_lane_index = -1;
    (void)db_snprintf(lane->name, sizeof(lane->name), "%s",
                      info->properties.deviceName);
}

void db_vk_log_execution_plan(const DeviceSelectionState *selection) {
    if (selection == NULL) {
        return;
    }
    infof("execution plan: mode=%s primary_lane=%u active_lanes=%u "
          "discovered_lanes=%u",
          db_vk_scheduler_mode_name_effective(selection->execution_mode,
                                              selection->active_lane_count),
          selection->primary_lane_index, selection->active_lane_count,
          selection->lane_count);
    for (uint32_t i = 0U; i < selection->lane_count; i++) {
        const db_vk_device_lane_t *lane = &selection->lanes[i];
        const char *backend_name = "primary";
        if (lane->backend == DB_VK_LANE_BACKEND_GROUP) {
            backend_name = "group_lane";
        } else if (lane->backend == DB_VK_LANE_BACKEND_INDEPENDENT) {
            backend_name = "independent_lane";
        }
        infof("lane[%u]: backend=%s phys_index=%u name=%s present=%d "
              "compose=%d active=%d reason=%s",
              i, backend_name, lane->physical_index, lane->name,
              lane->can_present, lane->can_compose_to_primary,
              lane->active_for_scheduler,
              (lane->inactive_reason[0] != '\0') ? lane->inactive_reason
                                                 : "active");
    }
}

DeviceSelectionState db_vk_select_devices_and_group(VkInstance instance,
                                                    VkSurfaceKHR surface) {
    DeviceSelectionState selection = {0};
    const db_vk_multi_device_policy_t multi_device_policy =
        db_vk_multi_device_policy_from_runtime();
    const int allow_cpu_workers = db_vk_allow_cpu_workers_from_runtime();

    uint32_t phys_count = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkEnumeratePhysicalDevices(instance, &phys_count, NULL));
    if (phys_count == 0U) {
        failf("No Vulkan physical devices found");
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
        info->queue_family_index = db_vk_find_graphics_queue_family(info->phys);
        info->supports_graphics = (info->queue_family_index != UINT32_MAX);
        info->supports_present = db_vk_queue_family_supports_present(
            info->phys, info->queue_family_index, surface);
        db_vk_probe_device_interop_extensions(
            info->phys, &info->supports_external_memory_interop,
            &info->supports_external_semaphore_interop);
    }
    selection.primary_phys_index =
        db_vk_choose_primary_physical_index(&selection);

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

            uint32_t mask = 0;
            const uint32_t group_device_scan_count =
                (group_props->physicalDeviceCount < MAX_GPU_COUNT)
                    ? group_props->physicalDeviceCount
                    : MAX_GPU_COUNT;
            for (uint32_t di = 0; di < group_device_scan_count; di++) {
                VkPhysicalDevice pd = group_props->physicalDevices[di];
                uint32_t queue_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &queue_count,
                                                         NULL);
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
            selection.group_info[gi].grp = *group_props;
            selection.group_info[gi].presentable_mask = mask;

            const uint64_t group_score =
                db_vk_group_score(group_device_scan_count, mask);
            if ((selection.have_group == 0) || (group_score > best_score)) {
                best.grp = *group_props;
                best.presentable_mask = mask;
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
        selection.present_mask = best.presentable_mask & usable_mask;
        selection.execution_mode = (selection.chosen_count > 1U)
                                       ? DB_VK_EXECUTION_MODE_DEVICE_GROUP
                                       : DB_VK_EXECUTION_MODE_SINGLE_GPU;
        infof("Using device group with %u devices (presentMask=0x%x)",
              selection.chosen_count, selection.present_mask);
    } else {
        selection.chosen_phys[0] = selection.phys[selection.primary_phys_index];
        selection.execution_mode =
            ((selection.phys_count > 1U) &&
             (multi_device_policy != DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY))
                ? DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES
                : DB_VK_EXECUTION_MODE_SINGLE_GPU;
        if (selection.execution_mode ==
            DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES) {
            infof("No usable device group found; planning independent-device "
                  "execution");
        } else {
            infof("No usable device group found; running single-GPU");
        }
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
            db_vk_device_lane_t *lane = &selection.lanes[i];
            db_vk_fill_lane_identity(lane, &selection.phys_info[physical_index],
                                     DB_VK_LANE_BACKEND_GROUP, physical_index);
            lane->device_mask = MASK_GPU0 << i;
            lane->group_lane_index = (int)i;
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
            if ((selection.phys_info[physical_index]
                     .supports_external_memory_interop == 0) ||
                (selection.phys_info[physical_index]
                     .supports_external_semaphore_interop == 0)) {
                db_vk_set_lane_reason(
                    lane, "independent worker lacks external memory/semaphore "
                          "interop required for GPU composition");
            } else if ((selection.phys_info[physical_index]
                            .properties.deviceType ==
                        VK_PHYSICAL_DEVICE_TYPE_CPU) &&
                       (allow_cpu_workers == 0)) {
                db_vk_set_lane_reason(lane,
                                      "cpu worker disabled by runtime policy");
            } else {
                db_vk_set_lane_reason(
                    lane, "independent worker discovered; cross-device GPU "
                          "composition path not initialized");
            }
        }
    }
    infof("multi-device policy: policy=%s allow_cpu_workers=%d",
          db_vk_multi_device_policy_name(multi_device_policy),
          allow_cpu_workers);
    db_vk_log_execution_plan(&selection);
    return selection;
}
