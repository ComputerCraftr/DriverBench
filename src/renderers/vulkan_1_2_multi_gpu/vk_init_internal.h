#ifndef DRIVERBENCH_VK_INIT_INTERNAL_H
#define DRIVERBENCH_VK_INIT_INTERNAL_H

#include <stdint.h>

#include "vk_internal.h"

#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"
#define DB_VK_DEVICE_SCORE_SHIFT 24U
#define MASK_GPU0 1U
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
} db_vk_init_instance_surface_phase_t;

typedef struct {
    uint32_t device_group_mask;
    uint32_t gpu_count;
    int have_group;
    int headless_offscreen;
    int native_hdr_enabled;
    int hdr_metadata_supported;
    db_native_output_capability_t native_output_capability;
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
    VkDescriptorSet lane_descriptor_sets[MAX_GPU_COUNT][DB_VK_LANE_SLOT_COUNT];
    VkDescriptorSetLayout descriptor_set_layout;
    VkFence in_flight;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkPipeline pipeline;
    VkPipeline present_pipeline;
    VkPipeline composition_pipeline;
    VkPipelineLayout pipeline_layout;
    VkQueryPool timing_query_pool;
    VkRenderPass render_pass;
    VkRenderPass backing_render_pass;
    VkFormat backing_format;
    db_pixel_format_t backing_pixel_format;
    VkSampler backing_sampler;
    VkBackingTargetState backing_targets[1];
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
    db_renderer_execution_config_t runtime;
} db_vk_init_scheduler_phase_t;

typedef enum {
    DB_VK_MULTI_DEVICE_POLICY_AUTO = 0,
    DB_VK_MULTI_DEVICE_POLICY_GROUP_ONLY = 1,
    DB_VK_MULTI_DEVICE_POLICY_INDEPENDENT_OK = 2,
} db_vk_multi_device_policy_t;

extern char g_vk_capability_mode[DB_VK_CAPABILITY_MODE_MAX];

int db_vk_wsi_is_headless(const db_vk_wsi_config_t *wsi_config);
int db_vk_instance_extension_supported(const char *extension_name);
uint32_t db_vk_capped_enumeration_count_or_log(const char *label,
                                               uint32_t reported_count,
                                               uint32_t max_count);
uint32_t db_vk_count_mask_bits(uint32_t mask);
const char *
db_vk_compose_capability_mode(const db_renderer_execution_config_t *config);
const char *db_vk_present_mode_name(VkPresentModeKHR mode);
uint64_t db_vk_group_score(uint32_t device_count, uint32_t present_mask);
int db_vk_allow_cpu_workers_from_runtime(void);
uint32_t db_vk_find_graphics_queue_family(VkPhysicalDevice phys);
int db_vk_queue_family_supports_present(VkPhysicalDevice phys,
                                        uint32_t queue_family_index,
                                        VkSurfaceKHR surface);
void db_vk_probe_device_interop_extensions(VkInstance instance,
                                           VkPhysicalDevice phys,
                                           db_vk_physical_device_info_t *info);
void db_vk_probe_device_hdr_surface(VkPhysicalDevice phys, VkSurfaceKHR surface,
                                    db_vk_physical_device_info_t *info);
int db_vk_probe_external_image_interop(VkPhysicalDevice phys, VkFormat format);
int db_vk_probe_external_buffer_interop(VkPhysicalDevice phys);
int db_vk_find_common_drm_modifier(VkPhysicalDevice worker,
                                   VkPhysicalDevice primary, VkFormat format,
                                   uint64_t *out_modifier);
void db_vk_set_lane_reason(db_vk_device_lane_t *lane, const char *reason);
void db_vk_fill_lane_identity(db_vk_device_lane_t *lane,
                              const db_vk_physical_device_info_t *info,
                              db_vk_lane_backend_t backend,
                              uint32_t physical_index);
void db_vk_log_execution_plan(const DeviceSelectionState *selection);
void db_vk_init_phase_instance_surface(
    const db_vk_wsi_config_t *wsi_config,
    db_vk_init_instance_surface_phase_t *out_phase);
void db_vk_init_phase_device(VkInstance instance, VkSurfaceKHR surface,
                             int vsync_enabled,
                             db_output_format_request_t output_request,
                             db_vk_init_device_phase_t *out_phase);
void db_vk_init_phase_pipeline_resources(
    const db_vk_wsi_config_t *wsi_config, VkSurfaceKHR surface,
    const db_vk_init_device_phase_t *device_phase,
    const db_renderer_runtime_contract_t *resolved_runtime,
    db_vk_init_pipeline_resources_phase_t *out_phase);
void db_vk_init_phase_scheduler(
    const db_vk_init_device_phase_t *device_phase,
    const db_renderer_runtime_contract_t *resolved_runtime,
    db_vk_init_scheduler_phase_t *out_phase);

#endif
