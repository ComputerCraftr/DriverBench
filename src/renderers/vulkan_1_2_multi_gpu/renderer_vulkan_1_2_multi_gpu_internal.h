#ifndef DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_INTERNAL_H
#define DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_INTERNAL_H

// NOLINTBEGIN(misc-include-cleaner)

#include <stdint.h>

#include <vulkan/vulkan.h>

#include "../../config/benchmark_config.h"
#include "../renderer_benchmark_common.h"
#include "renderer_vulkan_1_2_multi_gpu.h"

#define MAX_BAND_OWNER BENCH_BANDS
#define MAX_GPU_COUNT 8U
#define MAX_INSTANCE_EXTS 16U
#define QUAD_VERT_FLOAT_COUNT 12U
#define TIMESTAMP_QUERIES_PER_GPU 2U
#define TIMESTAMP_QUERY_COUNT (MAX_GPU_COUNT * TIMESTAMP_QUERIES_PER_GPU)

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
    int32_t mode_phase_flag;
    uint32_t palette_cycle;
    uint32_t pattern_seed;
    uint32_t render_mode;
    uint32_t snake_batch_size;
    uint32_t snake_cursor;
    uint32_t snake_rect_index;
    int32_t snake_phase_completed;
    uint32_t viewport_height;
    uint32_t viewport_width;
} PushConstants;

typedef struct {
    VkPhysicalDeviceGroupProperties grp;
    uint32_t presentableMask;
} DeviceGroupInfo;

typedef struct {
    uint32_t phys_count;
    VkPhysicalDevice phys[MAX_GPU_COUNT];
    uint32_t group_count;
    VkPhysicalDeviceGroupProperties groups[MAX_GPU_COUNT];
    int have_group;
    uint32_t chosen_count;
    VkPhysicalDevice chosen_phys[MAX_GPU_COUNT];
    uint32_t present_mask;
    VkPhysicalDevice present_phys;
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
    const uint32_t *work_owner;
    const double *ema_ms_per_work_unit;
    double timestamp_period_ns;
} db_vk_state_init_ctx_t;

typedef struct {
    uint64_t bench_frames;
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
    uint64_t frame_index;
    uint32_t gpu_count;
    int gpu_timing_enabled;
    uint32_t gradient_window_rows;
    int have_group;
    int have_prev_timing_frame;
    int history_descriptor_index;
    int history_read_index;
    VkRenderPass history_render_pass;
    VkSampler history_sampler;
    HistoryTargetState history_targets[2];
    VkSemaphore image_available;
    VkFence in_flight;
    int initialized;
    VkInstance instance;
    const char *log_backend_name;
    double next_progress_log_due_ms;
    db_benchmark_runtime_init_t runtime;
    VkPhysicalDevice present_phys;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkPresentModeKHR present_mode;
    uint8_t prev_frame_owner_used[MAX_GPU_COUNT];
    uint32_t prev_frame_work_units[MAX_GPU_COUNT];
    VkQueue queue;
    VkSemaphore render_done;
    VkRenderPass render_pass;
    DeviceSelectionState selection;
    int snake_reset_pending;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surface_format;
    SwapchainState swapchain_state;
    double timestamp_period_ns;
    VkQueryPool timing_query_pool;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    uint32_t work_owner[MAX_BAND_OWNER];
    db_vk_wsi_config_t wsi_config;
} renderer_state_t;

typedef struct {
    uint32_t candidate_owner;
    uint32_t span_units;
    uint32_t row_start;
    uint32_t row_end;
    const float *color;
    uint32_t render_mode;
    uint32_t gradient_head_row;
    int mode_phase_flag;
    uint32_t snake_cursor;
    uint32_t snake_batch_size;
    uint32_t snake_rect_index;
    int snake_phase_completed;
    uint32_t palette_cycle;
} db_vk_grid_row_block_draw_req_t;

typedef struct {
    float ndc_x0;
    float ndc_y0;
    float ndc_x1;
    float ndc_y1;
    const float *color;
    uint32_t render_mode;
    uint32_t gradient_head_row;
    int mode_phase_flag;
    uint32_t snake_cursor;
    uint32_t snake_batch_size;
    uint32_t snake_rect_index;
    int snake_phase_completed;
    uint32_t palette_cycle;
} db_vk_draw_dynamic_req_t;

typedef struct {
    VkCommandBuffer cmd;
    VkPipelineLayout layout;
    VkExtent2D extent;
    int have_group;
    uint32_t active_gpu_count;
    uint64_t frame_start_ns;
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
} db_vk_owner_draw_ctx_t;

typedef struct {
    VkDevice device;
    VkFence in_flight;
    VkSemaphore image_available;
    VkSemaphore render_done;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
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
                                           VkSurfaceKHR surface);
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
    HistoryTargetState history_targets[2], int *history_read_index);
void db_vk_draw_owner_grid_row_block(
    const db_vk_owner_draw_ctx_t *ctx,
    const db_vk_grid_row_block_draw_req_t *req);
uint32_t db_vk_select_owner_for_work(uint32_t candidate_owner,
                                     uint32_t gpu_count, uint32_t work_units,
                                     uint64_t frame_start_ns,
                                     uint64_t budget_ns, uint64_t safety_ns,
                                     const double *ema_ms_per_unit);
void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                              VkQueryPool query_pool, uint32_t owner,
                              uint8_t *owner_started);
void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                            VkQueryPool query_pool, uint32_t owner,
                            uint8_t *owner_finished);
void db_vk_draw_snake_grid_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                uint32_t work_unit_count, const float color[3]);
void db_vk_draw_rect_snake_plan(const db_vk_owner_draw_ctx_t *ctx,
                                const db_snake_plan_t *plan,
                                uint32_t pattern_seed,
                                uint32_t snake_prev_start,
                                uint32_t snake_prev_count,
                                int snake_reset_pending, const float color[3]);
void db_vk_update_ema_fallback(db_pattern_t pattern, uint32_t gpu_count,
                               const uint32_t *work_owner,
                               const uint32_t *grid_tiles_per_gpu,
                               uint32_t grid_tiles_drawn, double frame_ms,
                               double *ema_ms_per_work_unit);
void db_vk_cleanup_runtime(const db_vk_cleanup_ctx_t *ctx);
void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx);
void db_vk_init_impl(const db_vk_wsi_config_t *wsi_config);
db_vk_frame_result_t db_vk_render_frame_impl(void);
void db_vk_shutdown_impl(void);
const char *db_vk_capability_mode_impl(void);
uint32_t db_vk_work_unit_count_impl(void);

// NOLINTEND(misc-include-cleaner)

#endif
