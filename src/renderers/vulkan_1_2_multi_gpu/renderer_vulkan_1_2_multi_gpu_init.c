#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../renderer_benchmark_common.h"
#include "db_embedded_shaders.h"
#include "renderer_vulkan_1_2_multi_gpu.h"
#include "renderer_vulkan_1_2_multi_gpu_internal.h"

// NOLINTBEGIN(misc-include-cleaner)

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_VK_DEVICE_SCORE_SHIFT 24U
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define MASK_GPU0 1U
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

static char g_vk_capability_mode[DB_VK_CAPABILITY_MODE_MAX] = {0};

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
    uint32_t effective_gpu_count;
    int no_present_mode;
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    db_benchmark_runtime_init_t runtime;
} db_vk_init_scheduler_phase_t;

typedef enum {
    DB_VK_MULTI_DEVICE_POLICY_AUTO = 0,
    DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY = 1,
    DB_VK_MULTI_DEVICE_POLICY_INDEPENDENT_OK = 2,
} db_vk_multi_device_policy_t;

static const char *db_vk_compose_capability_mode(db_pattern_t pattern) {
    const char *draw_mode = db_vk_capability_draw_mode_name(pattern);
    (void)db_snprintf(g_vk_capability_mode, sizeof(g_vk_capability_mode),
                      "%s(upload=%s,backbuffer_replay=no)", draw_mode,
                      DB_CAP_MODE_VK_UPLOAD_NONE);
    return g_vk_capability_mode;
}

static const char *db_vk_present_mode_name(VkPresentModeKHR mode) {
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
}

static uint32_t db_vk_capped_enumeration_count_or_log(const char *label,
                                                      uint32_t reported_count,
                                                      uint32_t max_count) {
    if (reported_count > max_count) {
        infof("%s count %u exceeds local cap %u; truncating enumeration", label,
              reported_count, max_count);
        return max_count;
    }
    return reported_count;
}

static uint32_t db_vk_count_mask_bits(uint32_t mask) {
    uint32_t count = 0U;
    while (mask != 0U) {
        count += (mask & 1U);
        mask >>= 1U;
    }
    return count;
}

static uint64_t db_vk_group_score(uint32_t device_count,
                                  uint32_t present_mask) {
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

static int db_vk_allow_cpu_workers_from_runtime(void) {
    int allow_cpu_workers = 0;
    (void)db_parse_bool_text(
        db_runtime_option_get(DB_RUNTIME_OPT_VK_ALLOW_CPU_WORKERS),
        &allow_cpu_workers);
    return allow_cpu_workers;
}

static uint32_t db_vk_find_graphics_queue_family(VkPhysicalDevice phys) {
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

static int db_vk_queue_family_supports_present(VkPhysicalDevice phys,
                                               uint32_t family_index,
                                               VkSurfaceKHR surface) {
    if (family_index == UINT32_MAX) {
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

static void db_vk_probe_device_interop_extensions(
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

static void db_vk_set_lane_reason(db_vk_device_lane_t *lane,
                                  const char *reason) {
    if ((lane == NULL) || (reason == NULL)) {
        return;
    }
    (void)db_snprintf(lane->inactive_reason, sizeof(lane->inactive_reason),
                      "%s", reason);
}

static void db_vk_fill_lane_identity(db_vk_device_lane_t *lane,
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

static void db_vk_log_execution_plan(const DeviceSelectionState *selection) {
    if (selection == NULL) {
        return;
    }
    infof("execution plan: mode=%s primary_lane=%u active_lanes=%u "
          "discovered_lanes=%u",
          db_vk_scheduler_mode_name(selection->execution_mode),
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

    uint32_t group_count = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkEnumeratePhysicalDeviceGroups(instance, &group_count, NULL));
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

    DeviceGroupInfo best = {0};
    uint64_t best_score = 0U;
    for (uint32_t gi = 0; gi < selection.group_count; gi++) {
        VkPhysicalDeviceGroupProperties *group_props = &selection.groups[gi];
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

    const char *inst_exts[MAX_INSTANCE_EXTS];
    uint32_t inst_ext_count = 0;
    for (uint32_t i = 0; i < required_ext_count; i++) {
        inst_exts[inst_ext_count++] = required_exts[i];
    }
    inst_exts[inst_ext_count++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "multi_gpu_2d";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {.sType =
                                    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = inst_ext_count;
    ici.ppEnabledExtensionNames = inst_exts;

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
                                    int vsync_enabled,
                                    db_vk_init_device_phase_t *out_phase) {
    if (out_phase == NULL) {
        return;
    }

    *out_phase = (db_vk_init_device_phase_t){0};
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
        VkBool32 supp = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(out_phase->present_phys, i,
                                             surface, &supp);
        if (supp && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
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
    dev_exts[dev_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceGroupDeviceCreateInfo dgci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
    dgci.physicalDeviceCount = out_phase->selection.chosen_count;
    dgci.pPhysicalDevices = out_phase->selection.chosen_phys;

    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    dci.enabledExtensionCount = dev_ext_count;
    dci.pEnabledFeatures = &feats;
    if (out_phase->have_group) {
        dci.pNext = &dgci;
    }

    DB_VK_CHECK(BACKEND_NAME, vkCreateDevice(out_phase->present_phys, &dci,
                                             NULL, &out_phase->device));
    vkGetDeviceQueue(out_phase->device, out_phase->queue_family_index, 0,
                     &out_phase->queue);

    uint32_t fmt_count = 0;
    DB_VK_CHECK(BACKEND_NAME,
                vkGetPhysicalDeviceSurfaceFormatsKHR(
                    out_phase->present_phys, surface, &fmt_count, NULL));
    VkSurfaceFormatKHR *fmts =
        (VkSurfaceFormatKHR *)calloc(fmt_count, sizeof(VkSurfaceFormatKHR));
    DB_VK_CHECK(BACKEND_NAME,
                vkGetPhysicalDeviceSurfaceFormatsKHR(
                    out_phase->present_phys, surface, &fmt_count, fmts));
    out_phase->surface_format = db_vk_choose_surface_format(fmts, fmt_count);
    free(fmts);
    out_phase->present_mode = db_vk_choose_present_mode(out_phase->present_phys,
                                                        surface, vsync_enabled);
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
        infof("non-blocking present mode unavailable on this surface/device; "
              "runtime may be present-throttled");
    }
}

static void db_vk_init_phase_pipeline_resources(
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
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

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
    int no_present_requested = 0;
    (void)db_parse_bool_text(
        db_runtime_option_get(DB_RUNTIME_OPT_VK_NO_PRESENT),
        &no_present_requested);
    out_phase->no_present_mode = (no_present_requested != 0);
    out_phase->effective_gpu_count = device_phase->gpu_count;
    const int multi_gpu = out_phase->effective_gpu_count > 1U;
    out_phase->capability_mode = db_vk_compose_capability_mode(pattern);
    db_log_renderer_capability_mode(BACKEND_NAME, out_phase->capability_mode);
    (void)multi_gpu;
    db_log_renderer_scheduler_mode(
        BACKEND_NAME,
        db_vk_scheduler_mode_name(device_phase->selection.execution_mode));
    if (no_present_requested != 0) {
        infof("debug runtime: no-present mode enabled");
    }
    for (uint32_t g = 0; g < out_phase->effective_gpu_count; g++) {
        out_phase->ema_ms_per_work_unit[g] = DEFAULT_EMA_MS_PER_WORK_UNIT;
    }
}

void db_renderer_vulkan_1_2_multi_gpu_init(const db_vk_wsi_config_t *wsi_config,
                                           int vsync_enabled) {
    db_vk_init_instance_surface_phase_t instance_surface_phase = {0};
    db_vk_init_device_phase_t device_phase = {0};
    db_vk_init_pipeline_resources_phase_t pipeline_phase = {0};
    db_vk_init_scheduler_phase_t scheduler_phase = {0};

    db_vk_init_phase_instance_surface(wsi_config, &instance_surface_phase);
    db_vk_init_phase_device(instance_surface_phase.instance,
                            instance_surface_phase.surface, vsync_enabled,
                            &device_phase);
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
        .gpu_count = scheduler_phase.effective_gpu_count,
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
        .no_present_mode = scheduler_phase.no_present_mode,
        .ema_ms_per_work_unit = scheduler_phase.ema_ms_per_work_unit,
        .timestamp_period_ns = device_phase.timestamp_period_ns,
    };
    db_vk_publish_initialized_state(&init_ctx);
}

// NOLINTEND(misc-include-cleaner)
