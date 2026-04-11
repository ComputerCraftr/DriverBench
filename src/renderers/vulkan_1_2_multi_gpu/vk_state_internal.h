#ifndef DRIVERBENCH_VK_STATE_INTERNAL_H
#define DRIVERBENCH_VK_STATE_INTERNAL_H

#include "vk_internal.h"

#define g_state g_vk_state

typedef struct db_vk_state_init_ctx {
    const db_vk_wsi_config_t *wsi_config;
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
    VkRenderPass backing_render_pass;
    VkFormat backing_format;
    db_pixel_format_t backing_pixel_format;
    SwapchainState swapchain_state;
    VkBackingTargetState backing_targets[1];
    uint32_t device_group_mask;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkPipeline pipeline;
    VkPipeline present_pipeline;
    VkPipeline composition_pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSet lane_descriptor_sets[MAX_GPU_COUNT][DB_VK_LANE_SLOT_COUNT];
    VkSampler backing_sampler;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkFence in_flight;
    VkQueryPool timing_query_pool;
    int gpu_timing_enabled;
    db_renderer_execution_config_t runtime;
    const char *capability_mode;
    int no_present_mode;
    const double *ema_ms_per_work_unit;
    double timestamp_period_ns;
} db_vk_state_init_ctx_t;

typedef struct {
    VkInstance instance;
    VkPhysicalDevice present_phys;
    VkDevice device;
    VkQueue queue;
    DeviceSelectionState selection;
    uint32_t gpu_count;
    uint32_t device_group_mask;
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
    VkQueryPool timing_query_pool;
    double timestamp_period_ns;
    int gpu_timing_enabled;
} db_vk_device_runtime_t;

typedef struct {
    VkRenderPass render_pass;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    SwapchainState swapchain_state;
    db_vk_wsi_config_t wsi_config;
    VkPipeline present_pipeline;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkFence in_flight;
    int no_present_mode;
} db_vk_presentation_runtime_t;

typedef struct {
    VkRenderPass render_pass;
    VkFormat format;
    db_pixel_format_t pixel_format;
    VkSampler sampler;
    VkBackingTargetState targets[1];
    VkExtent2D extent;
    uint32_t generation;
    int descriptor_index;
    int valid;
    VkBuffer rebuild_upload_buffer;
    VkDeviceMemory rebuild_upload_memory;
    size_t rebuild_upload_size_bytes;
} db_vk_backing_runtime_t;

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkPipeline pipeline;
    VkPipeline composition_pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSet lane_descriptor_sets[MAX_GPU_COUNT][DB_VK_LANE_SLOT_COUNT];
    VkDescriptorSetLayout descriptor_set_layout;
} db_vk_pipeline_runtime_t;

typedef struct {
    db_vk_independent_lane_runtime_t independent_lanes[MAX_GPU_COUNT];
    db_vk_present_piece_t *piece_storage;
    db_vk_lane_assignment_t *assignment_storage;
    uint32_t scheduling_epoch;
    uint32_t content_generation;
    uint32_t last_active_lane_count;
    uint32_t worker_share_bps;
    db_vk_multi_gpu_phase_t multi_gpu_phase;
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    int have_prev_timing_frame;
    uint8_t prev_frame_owner_used[MAX_GPU_COUNT];
    uint32_t prev_frame_work_units[MAX_GPU_COUNT];
    uint64_t cumulative_work_units[MAX_GPU_COUNT];
    uint64_t cumulative_frames_with_work[MAX_GPU_COUNT];
} db_vk_scheduler_runtime_t;

typedef struct {
    VkBackingTargetState targets[2];
    VkCommandBuffer command_buffer;
    VkFence fence;
    db_vk_present_piece_t *piece_storage;
    db_vk_lane_assignment_t *assignment_storage;
    db_vk_calibration_state_t state;
    db_vk_split_search_t split_search;
    VkTimeDomainKHR calibrated_host_domain;
    int calibrated_timestamps_enabled;
    int correctness_passed;
} db_vk_calibration_runtime_t;

typedef struct {
    uint64_t bench_frames;
    uint64_t bench_start_ns;
    double next_progress_log_due_ms;
    double frame_time_ema_ms;
    double frame_jitter_ema_ms;
    double present_frame_ema_ms;
    double present_jitter_ema_ms;
    double present_frame_p50_ms;
    double present_frame_p95_ms;
    double present_frame_p99_ms;
    uint64_t present_retries;
    double *render_frame_samples_ms;
    uint32_t render_frame_samples_count;
    uint32_t render_frame_samples_capacity;
} db_vk_metrics_runtime_t;

typedef struct {
    VkBuffer hash_readback_buffer;
    VkDeviceMemory hash_readback_memory;
    size_t hash_readback_size_bytes;
    uint64_t output_hash;
    int output_hash_enabled;
} db_vk_hash_runtime_t;

typedef struct {
    db_vk_device_runtime_t device;
    db_vk_presentation_runtime_t presentation;
    db_vk_backing_runtime_t backing;
    db_vk_pipeline_runtime_t pipelines;
    db_vk_scheduler_runtime_t scheduler;
    db_vk_calibration_runtime_t calibration;
    db_vk_metrics_runtime_t metrics;
    db_vk_hash_runtime_t hash;
    db_renderer_execution_config_t runtime;
    db_renderer_frame_stats_t frame;
    const char *capability_mode;
    const char *log_backend_name;
    int initialized;
} renderer_state_t;

extern renderer_state_t g_vk_state;

#endif
