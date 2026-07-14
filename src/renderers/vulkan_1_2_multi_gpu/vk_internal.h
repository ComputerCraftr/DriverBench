#ifndef DRIVERBENCH_VK_INTERNAL_H
#define DRIVERBENCH_VK_INTERNAL_H

#include "core/db_renderer_support.h"
#include "vk_diagnostics.h"
#include "vk_renderer.h"
#include <stddef.h>
#include <stdint.h>

#define DB_VK_MAX_LANES 8U
#define MAX_GPU_COUNT DB_VK_MAX_LANES
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
#define DB_VK_LANE_SLOT_COUNT 2U
#define DB_VK_MAX_PIECES_PER_FRAME 8192U
#define DB_VK_LOOKUP_WORD_CAPACITY ((size_t)DB_VK_MAX_PIECES_PER_FRAME * 2U)
#define DB_VK_MAX_DAMAGE_RECTS 8192U
#define DB_VK_MAX_BATCHES_PER_LANE 4U
#define DB_VK_DAMAGE_HISTORY_LENGTH 8U
#define DB_VK_CALIBRATION_PAIR_COUNT 16U
#define DB_VK_CALIBRATION_WARMUP_COUNT 4U
#define DB_VK_SPLIT_SEARCH_SHARE_COUNT 5U
#define DB_VK_SPLIT_SAMPLES_PER_SHARE 5U
#define DB_VK_SPLIT_INVALID_RETRY_LIMIT 3U
#define DB_VK_HDR_DEVICE_SCORE_BONUS_SHIFT 48U
#define DB_VK_SPLIT_SEARCH_SAMPLE_COUNT                                        \
    (DB_VK_SPLIT_SEARCH_SHARE_COUNT * DB_VK_SPLIT_SAMPLES_PER_SHARE)

typedef enum {
    DB_VK_COMPOSE_SAMPLE_NEAREST = 0,
    DB_VK_COMPOSE_SAMPLE_CONVERT = 1,
} db_vk_compose_mode_t;

typedef enum {
    DB_VK_SCHEDULING_PRIMARY_ONLY = 0,
    DB_VK_SCHEDULING_STABLE_ROWS = 1,
    DB_VK_SCHEDULING_GREEDY_DAMAGE_CHUNKS = 2,
    DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS = 3,
} db_vk_scheduling_policy_t;

typedef struct {
    VkRect2D source_rect;
    VkRect2D destination_rect;
    db_render_ir_rect_t logical_rect;
    db_render_ir_command_range_t command_range;
    uint32_t instance_first;
    uint32_t instance_count;
    uint32_t piece_index;
    db_vk_compose_mode_t compose_mode;
} db_vk_present_piece_t;

typedef struct {
    float rect[4];
    float start_color[4];
    float end_color[4];
    float gradient[4];
} db_vk_ir_execute_instance_t;

typedef struct {
    uint32_t piece_first;
    uint32_t piece_count;
    uint32_t lane;
    uint32_t slot;
    uint32_t batch;
} db_vk_lane_assignment_t;

typedef struct {
    uint32_t scheduling_epoch;
    uint32_t content_generation;
    db_vk_present_piece_t *pieces;
    size_t piece_count;
    db_vk_lane_assignment_t *assignments;
    size_t assignment_count;
    db_vk_scheduling_policy_t policy;
    int primary_only_fallback;
} db_vk_execution_plan_t;

typedef struct {
    double primary_ms;
    double candidate_ms;
    uint64_t primary_state_hash;
    uint64_t candidate_state_hash;
    uint64_t primary_working_hash;
    uint64_t candidate_working_hash;
    uint64_t primary_uncertainty_ns;
    uint64_t candidate_uncertainty_ns;
    int calibrated;
} db_vk_calibration_pair_t;

typedef struct {
    double median_improvement;
    double primary_p95_ms;
    double candidate_p95_ms;
    int complete;
    int hashes_match;
    int timing_confident;
    int activate;
} db_vk_calibration_result_t;

typedef enum {
    DB_VK_MULTI_GPU_CLOSED = 0,
    DB_VK_MULTI_GPU_WARMING = 1,
    DB_VK_MULTI_GPU_CALIBRATING = 2,
    DB_VK_MULTI_GPU_VALIDATED = 3,
    DB_VK_MULTI_GPU_ACTIVE = 4,
} db_vk_multi_gpu_phase_t;

typedef struct {
    db_vk_multi_gpu_phase_t phase;
    uint32_t warmup_count;
    uint32_t pair_count;
    db_vk_calibration_pair_t pairs[DB_VK_CALIBRATION_PAIR_COUNT];
    db_vk_calibration_result_t result;
} db_vk_calibration_state_t;

typedef struct {
    uint64_t host_critical_path_ns;
    uint64_t worker_gpu_ns;
    uint64_t primary_gpu_ns;
    uint64_t handoff_ns;
    uint64_t uncertainty_ns;
    int calibrated;
    int valid;
} db_vk_split_sample_t;

typedef struct {
    db_vk_split_sample_t samples[DB_VK_SPLIT_SEARCH_SAMPLE_COUNT];
    uint32_t shares_bps[DB_VK_SPLIT_SEARCH_SHARE_COUNT];
    uint32_t valid_count[DB_VK_SPLIT_SEARCH_SHARE_COUNT];
    uint32_t invalid_count[DB_VK_SPLIT_SEARCH_SHARE_COUNT];
    uint8_t warmed[DB_VK_SPLIT_SEARCH_SHARE_COUNT];
    uint32_t sample_count;
    uint32_t share_index;
    uint32_t selected_share_bps;
    int complete;
} db_vk_split_search_t;

typedef struct {
    float hdr_output_enabled;
} db_vk_present_push_constants_t;

typedef struct {
    VkPhysicalDeviceGroupProperties grp;
    uint32_t presentable_mask;
    uint32_t hdr_presentable_mask;
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

typedef enum {
    DB_VK_EXTERNAL_OWNERSHIP_EXTERNAL = 0,
    DB_VK_EXTERNAL_OWNERSHIP_FOREIGN = 1,
} db_vk_external_ownership_domain_t;

typedef enum {
    DB_VK_TRANSPORT_UNSUPPORTED = 0,
    DB_VK_TRANSPORT_DEVICE_GROUP_PEER_IMAGE = 1,
    DB_VK_TRANSPORT_OPAQUE_FD_IMAGE = 2,
    DB_VK_TRANSPORT_DMA_BUF_IMAGE = 3,
    DB_VK_TRANSPORT_DMA_BUF_BUFFER = 4,
} db_vk_transport_kind_t;

typedef struct {
    VkPhysicalDevice phys;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceDriverProperties driver_properties;
    uint8_t device_uuid[VK_UUID_SIZE];
    uint32_t queue_family_index;
    int supports_graphics;
    int supports_present;
    int supports_external_memory_interop;
    int supports_external_semaphore_interop;
    int supports_external_image_export;
    int supports_external_image_import;
    int supports_dma_buf;
    int supports_drm_modifier;
    int supports_dma_buf_buffer;
    int supports_sync_fd;
    int supports_foreign_queue;
    int supports_calibrated_timestamps_khr;
    int supports_calibrated_timestamps_ext;
    int supports_time_domain_monotonic_raw;
    int supports_time_domain_monotonic;
    int supports_hdr10_format;
    int supports_hdr10_colorspace;
    int supports_hdr10_surface_pair;
    int supports_hdr_metadata;
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
    db_vk_transport_kind_t transport;
    db_vk_external_ownership_domain_t ownership_domain;
    uint64_t drm_modifier;
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
    VkImageLayout *image_layouts;
} SwapchainState;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    uint32_t memory_type_index;
    uint32_t memory_heap_index;
    int layout_initialized;
} VkBackingTargetState;

typedef enum {
    DB_VK_EXTERNAL_SLOT_UNUSED = 0,
    DB_VK_EXTERNAL_SLOT_WORKER_OWNED = 1,
    DB_VK_EXTERNAL_SLOT_READY = 2,
    DB_VK_EXTERNAL_SLOT_PRIMARY_OWNED = 3,
    DB_VK_EXTERNAL_SLOT_REUSABLE = 4,
} db_vk_external_slot_phase_t;

typedef enum {
    DB_VK_SYNC_IDLE = 0,
    DB_VK_SYNC_SIGNAL_SUBMITTED = 1,
    DB_VK_SYNC_FD_EXPORTED = 2,
    DB_VK_SYNC_FD_IMPORTED = 3,
    DB_VK_SYNC_WAIT_SUBMITTED = 4,
    DB_VK_SYNC_PAYLOAD_CONSUMED = 5,
} db_vk_sync_state_t;

typedef struct {
    int device_group_peer_read;
    int opaque_identity_compatible;
    int opaque_external_image;
    int dma_buf_external_image;
    int dma_buf_modifier_compatible;
    int dma_buf_external_buffer;
    int sync_fd_semaphore;
    int external_domain_supported;
    int foreign_domain_required;
    int foreign_domain_supported_by_both;
} db_vk_transport_capabilities_t;

typedef struct {
    db_vk_transport_kind_t transport;
    db_vk_external_ownership_domain_t ownership_domain;
    int supported;
} db_vk_transport_profile_t;

typedef struct {
    uint32_t piece_index;
    VkRect2D destination_rect;
    uint32_t row_pitch;
    uint32_t segment_index;
    VkDeviceSize byte_offset;
    VkDeviceSize byte_size;
    db_pixel_format_t format;
    uint32_t content_generation;
    uint32_t scheduling_epoch;
    uint64_t frame;
} db_vk_shared_piece_layout_t;

typedef struct {
    VkDeviceSize segment_base_alignment;
    VkDeviceSize segment_range;
    VkDeviceSize total_size;
    uint32_t segment_count;
    size_t layout_count;
    size_t rerouted_piece_count;
} db_vk_shared_buffer_plan_t;

typedef struct {
    int32_t origin[2];
    uint32_t extent[2];
    uint32_t row_words;
    uint32_t word_offset;
    uint32_t rgba16f;
} db_vk_buffer_push_t;

typedef struct {
    VkBackingTargetState worker_target;
    VkImage primary_alias_image;
    VkDeviceMemory primary_alias_memory;
    VkImageView primary_alias_view;
    VkDescriptorSet primary_descriptor_set;
    VkBuffer worker_shared_buffer;
    VkDeviceMemory worker_shared_memory;
    VkBuffer primary_shared_buffer;
    VkDeviceMemory primary_shared_memory;
    VkDescriptorSet worker_pack_descriptor_set;
    VkDescriptorSet primary_unpack_descriptor_set;
    VkDeviceSize shared_buffer_size;
    VkSemaphore worker_ready;
    VkSemaphore primary_ready;
    VkSemaphore primary_reusable;
    VkSemaphore worker_reusable;
    VkDeviceSize allocation_size;
    VkDeviceSize worker_requirement_size;
    VkDeviceSize worker_requirement_alignment;
    uint32_t worker_requirement_type_bits;
    int worker_dedicated_required;
    int worker_dedicated_preferred;
    int primary_dedicated_required;
    int primary_dedicated_preferred;
    db_vk_external_slot_phase_t phase;
    db_vk_sync_state_t ready_sync_state;
    db_vk_sync_state_t reusable_sync_state;
    db_vk_external_ownership_domain_t ownership_domain;
    uint32_t slot_generation;
    uint32_t content_generation;
    uint32_t scheduling_epoch;
    uint64_t last_applied_frame;
    uint32_t valid_piece_first;
    uint32_t valid_piece_count;
    int initialized;
    int quarantined;
} db_vk_lane_slot_t;

typedef struct {
    VkPhysicalDevice phys;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    VkQueryPool timing_query_pool;
    double timestamp_period_ns;
    uint32_t timestamp_valid_bits;
    VkRenderPass render_pass;
    VkSampler pack_sampler;
    VkDescriptorSetLayout pack_descriptor_set_layout;
    VkDescriptorPool pack_descriptor_pool;
    VkPipelineLayout pack_pipeline_layout;
    VkPipeline pack_pipeline;
    VkDescriptorSetLayout unpack_descriptor_set_layout;
    VkDescriptorPool unpack_descriptor_pool;
    VkPipelineLayout unpack_pipeline_layout;
    VkPipeline unpack_pipeline;
    db_vk_transport_kind_t transport;
    uint64_t drm_modifier;
    db_vk_lane_slot_t slots[DB_VK_LANE_SLOT_COUNT];
    uint32_t scheduling_epoch;
    uint32_t active_slot;
    int initialized;
    int active;
    int calibrated_timestamps_enabled;
} db_vk_independent_lane_runtime_t;

typedef struct db_vk_state_init_ctx db_vk_state_init_ctx_t;

typedef struct {
    uint32_t span_units;
    uint32_t owner;
    uint32_t first_instance;
    uint32_t instance_count;
    VkRect2D scissor;
    db_grid_block_t block;
} db_vk_grid_row_block_draw_req_t;

typedef struct {
    VkCommandBuffer cmd;
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
    const uint8_t *owner_enabled;
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
    VkBuffer instance_buffer;
    VkDeviceMemory instance_memory;
    void *instance_mapped;
    VkBuffer lookup_buffer;
    VkDeviceMemory lookup_memory;
    void *lookup_mapped;
    VkBuffer hash_readback_buffer;
    VkDeviceMemory hash_readback_memory;
    VkPipeline pipeline;
    VkPipeline present_pipeline;
    VkPipeline composition_pipeline;
    VkPipelineLayout pipeline_layout;
    SwapchainState *swapchain_state;
    VkBackingTargetState *backing_targets;
    VkRenderPass render_pass;
    VkRenderPass backing_render_pass;
    VkCommandPool command_pool;
    VkQueryPool timing_query_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkSampler backing_sampler;
    VkInstance instance;
    VkSurfaceKHR surface;
} db_vk_cleanup_ctx_t;

static inline const char *
db_vk_capability_draw_mode_name(const db_renderer_execution_config_t *config) {
    if ((config == NULL) || (config->backbuffer_draw_full != 0)) {
        return DB_CAP_MODE_VK_DRAW_TILES_FULL;
    }
    const int uses_history = config->pipeline.uses_history_pipeline;
    return (uses_history != 0) ? DB_CAP_MODE_VK_DRAW_HISTORY_DIRTY
                               : DB_CAP_MODE_VK_DRAW_TILES_FULL;
}

static inline const char *
db_vk_scheduler_mode_name(db_vk_execution_mode_t execution_mode) {
    switch (execution_mode) {
    case DB_VK_EXECUTION_MODE_SINGLE_GPU:
        return DB_CAP_MODE_VK_SCHED_SINGLE_GPU;
    case DB_VK_EXECUTION_MODE_DEVICE_GROUP:
        return DB_CAP_MODE_VK_SCHED_DEVICE_GROUP;
    case DB_VK_EXECUTION_MODE_INDEPENDENT_DEVICES:
        return DB_CAP_MODE_VK_SCHED_INDEPENDENT_MULTI_DEVICE;
    }
    return "unknown";
}

static inline const char *
db_vk_scheduler_mode_name_effective(db_vk_execution_mode_t execution_mode,
                                    uint32_t active_lane_count) {
    if (active_lane_count <= 1U) {
        return DB_CAP_MODE_VK_SCHED_SINGLE_GPU;
    }
    return db_vk_scheduler_mode_name(execution_mode);
}

static inline uint32_t db_vk_normalize_gpu_count(uint32_t gpu_count) {
    if (gpu_count == 0U) {
        return 1U;
    }
    return DB_MIN(gpu_count, MAX_GPU_COUNT);
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

uint32_t db_vk_build_device_group_mask(uint32_t device_count);
VkSurfaceFormatKHR
db_vk_choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count);
typedef struct {
    VkSurfaceFormatKHR surface_format;
    db_native_output_capability_t capability;
    int hdr_enabled;
} db_vk_surface_format_selection_t;
db_vk_surface_format_selection_t db_vk_resolve_surface_format_for_output(
    const VkSurfaceFormatKHR *formats, uint32_t count,
    db_output_format_request_t request, int metadata_supported);
VkPresentModeKHR db_vk_choose_present_mode(VkPhysicalDevice present_phys,
                                           VkSurfaceKHR surface,
                                           int vsync_enabled);
void db_vk_create_backing_target(VkPhysicalDevice phys, VkDevice device,
                                 VkFormat format, VkExtent2D extent,
                                 VkRenderPass render_pass,
                                 uint32_t device_group_mask,
                                 VkBackingTargetState *out_target);
void db_vk_destroy_backing_target(VkDevice device,
                                  VkBackingTargetState *target);
void db_vk_create_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                  VkPhysicalDevice present_phys,
                                  VkDevice device, VkSurfaceKHR surface,
                                  VkSurfaceFormatKHR fmt,
                                  VkPresentModeKHR present_mode,
                                  VkRenderPass render_pass,
                                  SwapchainState *out_state);
void db_vk_update_backing_descriptor(VkDevice device,
                                     VkDescriptorSet descriptor_set,
                                     VkSampler sampler, VkImageView image_view);
uint32_t db_vk_choose_primary_physical_index_for_output(
    const DeviceSelectionState *selection,
    db_output_format_request_t output_request);
DeviceSelectionState
db_vk_select_devices_and_group(VkInstance instance, VkSurfaceKHR surface,
                               db_output_format_request_t output_request,
                               VkFormat working_format);
void db_vk_recreate_swapchain_state(const db_vk_wsi_config_t *wsi_config,
                                    VkPhysicalDevice present_phys,
                                    VkDevice device, VkSurfaceKHR surface,
                                    VkSurfaceFormatKHR surface_format,
                                    VkPresentModeKHR present_mode,
                                    VkRenderPass render_pass,
                                    SwapchainState *state);
void db_vk_recreate_backing_target(VkPhysicalDevice phys, VkDevice device,
                                   VkFormat format, VkExtent2D extent,
                                   VkRenderPass render_pass,
                                   uint32_t device_group_mask,
                                   VkBackingTargetState *backing_targets);
void db_vk_draw_owner_grid_row_block(
    const db_vk_owner_draw_ctx_t *ctx,
    const db_vk_grid_row_block_draw_req_t *req);
uint32_t db_vk_select_owner_for_work(uint32_t gpu_count, uint32_t work_units,
                                     uint64_t budget_ns, uint64_t safety_ns,
                                     const double *ema_ms_per_unit,
                                     const uint32_t *frame_work_units);
int db_vk_multi_gpu_measured_benefit(double primary_mean_ms,
                                     double candidate_mean_ms,
                                     double primary_p95_ms,
                                     double candidate_p95_ms);
db_vk_calibration_result_t
db_vk_evaluate_calibration(const db_vk_calibration_pair_t *pairs,
                           size_t pair_count);
const char *db_vk_multi_gpu_phase_name(db_vk_multi_gpu_phase_t phase);
int db_vk_multi_gpu_phase_transition_valid(db_vk_multi_gpu_phase_t from,
                                           db_vk_multi_gpu_phase_t to);
void db_vk_calibration_state_open(db_vk_calibration_state_t *state);
void db_vk_calibration_state_record(db_vk_calibration_state_t *state,
                                    const db_vk_calibration_pair_t *pair);
uint32_t db_vk_import_memory_type_bits(uint32_t exported_fd_type_bits,
                                       uint32_t alias_requirement_type_bits);
int db_vk_external_interop_usable(int platform_supported, int external_memory,
                                  int external_semaphore, int external_image);
int db_vk_buffer_transport_create(db_vk_independent_lane_runtime_t *runtime,
                                  uint32_t lane_index);
void db_vk_buffer_transport_destroy(db_vk_independent_lane_runtime_t *runtime);
db_vk_transport_profile_t
db_vk_negotiate_transport(const db_vk_transport_capabilities_t *capabilities);
int db_vk_build_shared_buffer_plan(const db_vk_execution_plan_t *execution_plan,
                                   db_pixel_format_t format,
                                   VkDeviceSize segment_base_alignment,
                                   VkDeviceSize max_segment_range,
                                   db_vk_shared_piece_layout_t *layouts,
                                   size_t layout_capacity,
                                   db_vk_shared_buffer_plan_t *out_plan);
int db_vk_build_execution_plan(
    const db_frame_plan_t *frame_plan, uint32_t lane_count,
    db_vk_scheduling_policy_t policy, const double *ema_ms_per_work_unit,
    uint32_t scheduling_epoch, uint32_t content_generation,
    db_vk_present_piece_t *pieces, size_t piece_capacity,
    db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    db_vk_execution_plan_t *out_plan);
int db_vk_build_execution_plan_for_gradient_path(
    const db_frame_plan_t *frame_plan, uint32_t lane_count,
    db_vk_scheduling_policy_t policy, const double *ema_ms_per_work_unit,
    uint32_t scheduling_epoch, uint32_t content_generation,
    db_vk_present_piece_t *pieces, size_t piece_capacity,
    db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    db_vk_execution_plan_t *out_plan, int semantic_gradient);
size_t db_vk_frame_rect_count(const db_frame_plan_t *plan);
int db_vk_frame_rect_at(const db_frame_plan_t *plan, size_t index,
                        db_render_ir_fill_t *fill);
size_t db_vk_write_frame_instances(const db_frame_plan_t *plan,
                                   db_vk_ir_execute_instance_t *instances,
                                   size_t instance_capacity);
size_t db_vk_write_frame_instances_for_gradient_path(
    const db_frame_plan_t *plan, db_vk_ir_execute_instance_t *instances,
    size_t instance_capacity, int semantic_gradient);
size_t db_vk_write_frame_instances_for_implementation(
    const db_frame_plan_t *plan, db_vk_ir_execute_instance_t *instances,
    size_t instance_capacity, uint32_t *lookup_words, size_t lookup_capacity,
    size_t *lookup_word_count, db_pixel_format_t working_format,
    db_gradient_implementation_t implementation);
int db_vk_build_execution_plan_with_worker_share(
    const db_frame_plan_t *frame_plan, uint32_t lane_count,
    db_vk_scheduling_policy_t policy, uint32_t worker_share_bps,
    const double *ema_ms_per_work_unit, uint32_t scheduling_epoch,
    uint32_t content_generation, db_vk_present_piece_t *pieces,
    size_t piece_capacity, db_vk_lane_assignment_t *assignments,
    size_t assignment_capacity, db_vk_execution_plan_t *out_plan);
uint32_t db_vk_split_search_next_share(const db_vk_split_search_t *search);
void db_vk_split_search_record(db_vk_split_search_t *search,
                               const db_vk_split_sample_t *sample);
uint64_t db_vk_timestamp_delta(uint64_t begin, uint64_t end,
                               uint32_t valid_bits);
int db_vk_timestamp_deviation_acceptable(uint64_t deviation_ns,
                                         uint64_t critical_path_ns);
int db_vk_sync_state_transition_valid(db_vk_sync_state_t from,
                                      db_vk_sync_state_t to);
int db_vk_slot_result_is_current(const db_vk_lane_slot_t *slot,
                                 const db_vk_execution_plan_t *plan,
                                 uint64_t frame_index);
int db_vk_create_external_image(VkPhysicalDevice phys, VkDevice device,
                                VkFormat format, VkExtent2D extent,
                                VkRenderPass render_pass,
                                db_vk_transport_kind_t transport,
                                uint64_t drm_modifier, uint32_t lane_index,
                                uint32_t worker_memory_type,
                                db_vk_lane_slot_t *slot);
int db_vk_import_external_image(VkPhysicalDevice primary_phys,
                                VkDevice primary_device, VkDevice worker_device,
                                VkFormat format, VkExtent2D extent,
                                uint32_t lane_index,
                                db_vk_transport_kind_t transport,
                                db_vk_lane_slot_t *slot);
void db_vk_independent_lanes_init(void);
void db_vk_device_group_lanes_init(void);
void db_vk_device_group_lanes_shutdown(void);
int db_vk_device_group_peer_read_usable(VkPeerMemoryFeatureFlags features);
uint32_t db_vk_device_group_record(const db_frame_plan_t *plan,
                                   const db_vk_execution_plan_t *execution_plan,
                                   VkCommandBuffer command_buffer,
                                   uint32_t *frame_work_units,
                                   uint8_t *frame_owner_used,
                                   uint8_t *frame_owner_finished);
uint32_t
db_vk_independent_lanes_submit(const db_frame_plan_t *plan,
                               const db_vk_execution_plan_t *execution_plan,
                               VkSemaphore *primary_wait_semaphores,
                               VkPipelineStageFlags *primary_wait_stages,
                               VkSemaphore *primary_signal_semaphores);
uint64_t db_vk_independent_lane_timing_ns(uint32_t lane_index);
void db_vk_independent_lanes_record_composition(
    VkCommandBuffer command_buffer,
    const db_vk_execution_plan_t *execution_plan,
    VkBackingTargetState *destination_target);
void db_vk_independent_lanes_export_reusable(void);
void db_vk_independent_lanes_shutdown(void);
void db_vk_independent_lane_quarantine(uint32_t lane_index, const char *reason);
void db_vk_calibration_run_after_live(const db_frame_plan_t *plan);
void db_vk_calibration_shutdown(void);
void db_vk_owner_timing_begin(VkCommandBuffer cmd, int timing_enabled,
                              VkQueryPool query_pool, uint32_t owner,
                              uint8_t *owner_started);
void db_vk_owner_timing_end(VkCommandBuffer cmd, int timing_enabled,
                            VkQueryPool query_pool, uint32_t owner,
                            uint8_t *owner_finished);
void db_vk_update_ema_fallback(uint32_t gpu_count,
                               const uint32_t *frame_work_units,
                               double frame_ms, double *ema_ms_per_work_unit);
void db_vk_scheduler_update_frame_pacing(double frame_ms, double *frame_ema_ms,
                                         double *frame_jitter_ema_ms);
double db_vk_scheduler_percentile_sorted(const double *samples, size_t count,
                                         double pct);
void db_vk_cleanup_runtime(const db_vk_cleanup_ctx_t *ctx);
void db_vk_publish_initialized_state(const db_vk_state_init_ctx_t *ctx);
void db_vk_resolve_gradient_qualification(void);

#endif
