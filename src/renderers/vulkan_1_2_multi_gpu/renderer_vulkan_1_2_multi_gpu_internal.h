#ifndef DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_INTERNAL_H
#define DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_INTERNAL_H

// NOLINTBEGIN(misc-include-cleaner)

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#include "../../config/benchmark_config.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"

#define MAX_BAND_OWNER BENCH_BANDS
#define MAX_GPU_COUNT 8U
#define MAX_INSTANCE_EXTS 16U
#define QUAD_VERT_FLOAT_COUNT 12U
#define TIMESTAMP_QUERIES_PER_GPU 2U
#define TIMESTAMP_QUERY_COUNT (MAX_GPU_COUNT * TIMESTAMP_QUERIES_PER_GPU)
#define DB_CAP_MODE_VK_DRAW_HISTORY_DIRTY "history_dirty_draw"
#define DB_CAP_MODE_VK_DRAW_TILES_FULL "tiles_full_draw"
#define DB_CAP_MODE_VK_SCHED_DEVICE_GROUP "device_group_multi_gpu"
#define DB_CAP_MODE_VK_SCHED_INDEPENDENT_MULTI_DEVICE "independent_multi_device"
#define DB_CAP_MODE_VK_SCHED_SINGLE_GPU "single_gpu"
#define DB_CAP_MODE_VK_UPLOAD_NONE "none"
#define DB_VK_CAPABILITY_MODE_MAX 128U
#define DB_VK_LANE_REASON_MAX 96U
#define DB_VK_BLOCKING_FRAME_BUDGET_NS 16666666ULL
#define DB_VK_BLOCKING_FRAME_SAFETY_NS 2000000ULL
#define DB_VK_NONBLOCKING_FRAME_BUDGET_NS 4000000ULL
#define DB_VK_NONBLOCKING_FRAME_SAFETY_NS 500000ULL

typedef struct {
    float offset_ndc[2];
    float scale_ndc[2];
    float color[4];
    float base_color[4];
    float target_color[4];
    uint32_t gradient_head_row;
    uint32_t gradient_window_rows;
    uint32_t grid_cols;
    uint32_t grid_rows;
    int32_t gradient_direction_flag;
    uint32_t palette_cycle;
    uint32_t render_mode;
    uint32_t snake_batch_size;
    uint32_t snake_cursor;
    int32_t snake_phase_flag;
    int32_t snake_phase_completed;
    uint32_t snake_shape_kind;
    uint32_t snake_region_height;
    uint32_t snake_region_width;
    uint32_t snake_region_x;
    uint32_t snake_region_y;
    float snake_region_color[4];
    float snake_profile0[4];
    float snake_profile1[4];
    float snake_profile2[4];
    uint32_t snake_triangle_variant;
    uint32_t viewport_height;
    uint32_t viewport_width;
    uint32_t frame_index;
    uint32_t band_count;
} PushConstants;

typedef struct {
    uint32_t render_mode;
    uint32_t shape_index;
    uint32_t pattern_seed;
    int valid;
    uint32_t snake_shape_kind;
    uint32_t snake_region_height;
    uint32_t snake_region_width;
    uint32_t snake_region_x;
    uint32_t snake_region_y;
    float snake_region_color[4];
    float snake_profile0[4];
    float snake_profile1[4];
    float snake_profile2[4];
    uint32_t snake_triangle_variant;
} db_vk_shape_uniform_cache_t;

typedef struct {
    VkPhysicalDeviceGroupProperties grp;
    uint32_t presentable_mask;
} DeviceGroupInfo;

typedef enum {
    DB_VK_EXECUTION_MODE_SINGLE_GPU = 0,
    DB_VK_EXECUTION_MODE_DEVICE_GROUP = 1,
    DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES = 2,
} db_vk_execution_mode_t;

typedef enum {
    DB_VK_LANE_BACKEND_PRIMARY = 0,
    DB_VK_LANE_BACKEND_GROUP = 1,
    DB_VK_LANE_BACKEND_INDEPENDENT = 2,
} db_vk_lane_backend_t;

typedef struct {
    VkPhysicalDevice phys;
    VkPhysicalDeviceProperties properties;
    uint32_t queue_family_index;
    int supports_graphics;
    int supports_present;
    int supports_external_memory_interop;
    int supports_external_semaphore_interop;
} db_vk_physical_device_info_t;

typedef struct {
    VkPhysicalDevice phys;
    db_vk_lane_backend_t backend;
    uint32_t physical_index;
    uint32_t queue_family_index;
    uint32_t device_mask;
    int group_index;
    int group_lane_index;
    int can_present;
    int can_compose_to_primary;
    int supports_required_format_usage;
    int active_for_scheduler;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    char inactive_reason[DB_VK_LANE_REASON_MAX];
} db_vk_device_lane_t;

typedef struct {
    uint32_t phys_count;
    VkPhysicalDevice phys[MAX_GPU_COUNT];
    db_vk_physical_device_info_t phys_info[MAX_GPU_COUNT];
    uint32_t group_count;
    VkPhysicalDeviceGroupProperties groups[MAX_GPU_COUNT];
    DeviceGroupInfo group_info[MAX_GPU_COUNT];
    int have_group;
    uint32_t chosen_count;
    VkPhysicalDevice chosen_phys[MAX_GPU_COUNT];
    uint32_t present_mask;
    VkPhysicalDevice present_phys;
    db_vk_execution_mode_t execution_mode;
    uint32_t primary_phys_index;
    uint32_t primary_lane_index;
    uint32_t lane_count;
    uint32_t active_lane_count;
    db_vk_device_lane_t lanes[MAX_GPU_COUNT];
} DeviceSelectionState;

typedef struct {
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
} SwapchainState;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    int layout_initialized;
} HistoryTargetState;

typedef struct {
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
    VkRenderPass history_render_pass;
    SwapchainState swapchain_state;
    HistoryTargetState history_targets[2];
    uint32_t device_group_mask;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkSampler history_sampler;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkFence in_flight;
    VkQueryPool timing_query_pool;
    int gpu_timing_enabled;
    db_benchmark_runtime_init_t runtime;
    const char *capability_mode;
    int no_present_mode;
    const double *ema_ms_per_work_unit;
    double timestamp_period_ns;
} db_vk_state_init_ctx_t;

typedef struct {
    uint64_t bench_frames;
    db_renderer_frame_stats_t frame;
    uint64_t bench_start_ns;
    const char *capability_mode;
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDevice device;
    uint32_t device_group_mask;
    double ema_ms_per_work_unit[MAX_GPU_COUNT];
    uint32_t gpu_count;
    int gpu_timing_enabled;
    uint32_t gradient_window_rows;
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_history_pair_state_t history_pair;
    db_history_pattern_mode_flags_t runtime_flags;
    int no_present_mode;
    int have_prev_timing_frame;
    int history_descriptor_index;
    VkRenderPass history_render_pass;
    VkSampler history_sampler;
    HistoryTargetState history_targets[2];
    VkSemaphore image_available;
    VkFence in_flight;
    int initialized;
    VkInstance instance;
    const char *log_backend_name;
    double next_progress_log_due_ms;
    double frame_time_ema_ms;
    double frame_jitter_ema_ms;
    double present_frame_ema_ms;
    double present_jitter_ema_ms;
    double present_frame_p50_ms;
    double present_frame_p95_ms;
    double present_frame_p99_ms;
    uint64_t present_retries;
    db_benchmark_runtime_init_t runtime;
    VkPhysicalDevice present_phys;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkPresentModeKHR present_mode;
    uint8_t prev_frame_owner_used[MAX_GPU_COUNT];
    uint32_t prev_frame_work_units[MAX_GPU_COUNT];
    uint64_t cumulative_work_units[MAX_GPU_COUNT];
    uint64_t cumulative_frames_with_work[MAX_GPU_COUNT];
    double *render_frame_samples_ms;
    uint32_t render_frame_samples_count;
    uint32_t render_frame_samples_capacity;
    VkQueue queue;
    VkSemaphore render_done;
    VkRenderPass render_pass;
    DeviceSelectionState selection;
    db_history_snake_scratch_t snake_scratch;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surface_format;
    SwapchainState swapchain_state;
    double timestamp_period_ns;
    VkQueryPool timing_query_pool;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer hash_readback_buffer;
    VkDeviceMemory hash_readback_memory;
    size_t hash_readback_size_bytes;
    db_vk_wsi_config_t wsi_config;
    uint64_t output_hash;
    int output_hash_enabled;
    db_vk_shape_uniform_cache_t shape_uniform_cache;
} renderer_state_t;

typedef struct {
    const float *color;
    uint32_t render_mode;
    uint32_t gradient_head_row;
    int gradient_direction_flag;
    int snake_phase_flag;
    uint32_t snake_cursor;
    uint32_t snake_batch_size;
    uint32_t snake_shape_index;
    int snake_phase_completed;
    uint32_t palette_cycle;
    uint32_t frame_index;
    uint32_t band_count;
} db_vk_draw_payload_t;

typedef struct {
    float color[3];
    db_vk_draw_payload_t payload;
    int valid;
} db_vk_draw_payload_cache_t;

typedef struct {
    uint32_t span_units;
    uint32_t row_start;
    uint32_t row_end;
    uint32_t col_start;
    uint32_t col_end;
    db_vk_draw_payload_t payload;
} db_vk_grid_row_block_draw_req_t;

typedef struct {
    float ndc_x0;
    float ndc_y0;
    float ndc_x1;
    float ndc_y1;
    db_vk_draw_payload_t payload;
} db_vk_draw_dynamic_req_t;

typedef struct {
    VkCommandBuffer cmd;
    VkPipelineLayout layout;
    VkExtent2D extent;
    int have_group;
    uint32_t active_gpu_count;
    uint64_t budget_ns;
    uint64_t safety_ns;
    const double *ema_ms_per_work_unit;
    int timing_enabled;
    VkQueryPool timing_query_pool;
    uint8_t *frame_owner_used;
    uint8_t *frame_owner_finished;
    uint32_t *frame_work_units;
    uint32_t *grid_tiles_per_gpu;
    uint32_t *grid_tiles_drawn;
    uint32_t grid_rows;
    uint32_t grid_cols;
    db_vk_draw_payload_cache_t *payload_cache;
} db_vk_owner_draw_ctx_t;

typedef struct {
    VkDevice device;
    VkFence in_flight;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer hash_readback_buffer;
    VkDeviceMemory hash_readback_memory;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    SwapchainState *swapchain_state;
    HistoryTargetState *history_targets;
    VkRenderPass render_pass;
    VkRenderPass history_render_pass;
    VkCommandPool command_pool;
    VkQueryPool timing_query_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkSampler history_sampler;
    VkInstance instance;
    VkSurfaceKHR surface;
} db_vk_cleanup_ctx_t;

static inline const char *
db_vk_capability_draw_mode_name(db_pattern_t pattern) {
    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(pattern);
    const int uses_history = pattern_flags.uses_history_pipeline;
    return (uses_history != 0) ? DB_CAP_MODE_VK_DRAW_HISTORY_DIRTY
                               : DB_CAP_MODE_VK_DRAW_TILES_FULL;
}

static inline const char *
db_vk_scheduler_mode_name(db_vk_execution_mode_t execution_mode) {
    if (execution_mode == DB_VK_EXECUTION_MODE_DEVICE_GROUP) {
        return DB_CAP_MODE_VK_SCHED_DEVICE_GROUP;
    }
    if (execution_mode == DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES) {
        return DB_CAP_MODE_VK_SCHED_INDEPENDENT_MULTI_DEVICE;
    }
    return DB_CAP_MODE_VK_SCHED_SINGLE_GPU;
}

static inline uint32_t db_vk_normalize_gpu_count(uint32_t gpu_count) {
    if (gpu_count == 0U) {
        return 1U;
    }
    return (gpu_count > MAX_GPU_COUNT) ? MAX_GPU_COUNT : gpu_count;
}

static inline int db_vk_present_mode_is_blocking(VkPresentModeKHR mode) {
    return (mode == VK_PRESENT_MODE_FIFO_KHR) ||
           (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
}

static inline uint64_t
db_vk_scheduler_frame_budget_ns(VkPresentModeKHR present_mode) {
    if (db_vk_present_mode_is_blocking(present_mode) != 0) {
        return DB_VK_BLOCKING_FRAME_BUDGET_NS;
    }
    return DB_VK_NONBLOCKING_FRAME_BUDGET_NS;
}

static inline uint64_t
db_vk_scheduler_frame_safety_ns(VkPresentModeKHR present_mode) {
    if (db_vk_present_mode_is_blocking(present_mode) != 0) {
        return DB_VK_BLOCKING_FRAME_SAFETY_NS;
    }
    return DB_VK_NONBLOCKING_FRAME_SAFETY_NS;
}

extern renderer_state_t g_state;

static inline const char *db_vk_result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

static inline void __attribute__((noreturn))
db_vk_fail(const char *backend_name, const char *expr, VkResult result,
           const char *file, int line) {
    db_failf(backend_name, "%s failed: %s (%d) at %s:%d", expr,
             db_vk_result_name(result), (int)result, file, line);
    __builtin_unreachable();
}

#define DB_VK_CHECK(backend_name, x)                                           \
    do {                                                                       \
        VkResult _r = (x);                                                     \
        if (_r != VK_SUCCESS)                                                  \
            db_vk_fail((backend_name), #x, _r, __FILE__, __LINE__);            \
    } while (0)

uint32_t db_vk_build_device_group_mask(uint32_t device_count);
VkSurfaceFormatKHR
db_vk_choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR db_vk_choose_present_mode(VkPhysicalDevice present_phys,
                                           VkSurfaceKHR surface,
                                           int vsync_enabled);
void db_vk_create_history_target(VkPhysicalDevice phys, VkDevice device,
                                 VkFormat format, VkExtent2D extent,
                                 VkRenderPass render_pass,
                                 uint32_t device_group_mask,
                                 HistoryTargetState *out_target);
void db_vk_create_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                  VkPhysicalDevice present_phys,
                                  VkDevice device, VkSurfaceKHR surface,
                                  VkSurfaceFormatKHR fmt,
                                  VkPresentModeKHR present_mode,
                                  VkRenderPass render_pass,
                                  SwapchainState *out_state);
void db_vk_update_history_descriptor(VkDevice device,
                                     VkDescriptorSet descriptor_set,
                                     VkSampler sampler, VkImageView image_view);
DeviceSelectionState db_vk_select_devices_and_group(VkInstance instance,
                                                    VkSurfaceKHR surface);
void db_vk_push_constants_frame_static(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       VkExtent2D extent, uint32_t grid_rows,
                                       uint32_t grid_cols);
void db_vk_push_constants_draw_dynamic(VkCommandBuffer cmd,
                                       VkPipelineLayout layout,
                                       const db_vk_draw_dynamic_req_t *req);
void db_vk_recreate_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                    VkPhysicalDevice present_phys,
                                    VkDevice device, VkSurfaceKHR surface,
                                    VkSurfaceFormatKHR surface_format,
                                    VkPresentModeKHR present_mode,
                                    VkRenderPass render_pass,
                                    SwapchainState *state);
int db_vk_recreate_history_targets_preserve(
    VkPhysicalDevice phys, VkDevice device, VkFormat format, VkExtent2D extent,
    VkRenderPass render_pass, uint32_t device_group_mask,
    VkCommandPool command_pool, VkQueue queue, VkExtent2D old_extent,
    HistoryTargetState history_targets[2], int *history_pair_read_index);
void db_vk_draw_owner_grid_row_block(
    const db_vk_owner_draw_ctx_t *ctx,
    const db_vk_grid_row_block_draw_req_t *req);
uint32_t db_vk_select_owner_for_work(uint32_t gpu_count, uint32_t work_units,
                                     uint64_t budget_ns, uint64_t safety_ns,
                                     const double *ema_ms_per_unit,
                                     const uint32_t *frame_work_units);
void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                              VkQueryPool query_pool, uint32_t owner,
                              uint8_t *owner_started);
void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                            VkQueryPool query_pool, uint32_t owner,
                            uint8_t *owner_finished);
void db_vk_draw_snake_grid_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                const float color[3]);
void db_vk_draw_snake_region_plan(const db_vk_owner_draw_ctx_t *ctx,
                                  const db_snake_plan_t *plan,
                                  uint32_t pattern_seed,
                                  uint32_t snake_prev_start,
                                  uint32_t snake_prev_count,
                                  const float color[3]);
void db_vk_update_ema_fallback(uint32_t gpu_count,
                               const uint32_t *frame_work_units,
                               double frame_ms, double *ema_ms_per_work_unit);
void db_vk_scheduler_update_frame_pacing(double frame_ms, double *frame_ema_ms,
                                         double *frame_jitter_ema_ms);
double db_vk_scheduler_percentile_sorted(const double *samples, size_t count,
                                         double pct);
void db_vk_cleanup_runtime(const db_vk_cleanup_ctx_t *ctx);
void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx);

// NOLINTEND(misc-include-cleaner)

#endif
