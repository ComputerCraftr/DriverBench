#ifndef DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_RUNTIME_INTERNAL_H
#define DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_RUNTIME_INTERNAL_H

#include "renderer_vulkan_1_2_multi_gpu_init_internal.h"

#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define DB_VK_RENDER_FRAME_SAMPLE_INIT_CAPACITY 1024U
#define DB_VK_PERCENTILE_P50 50.0
#define DB_VK_PERCENTILE_P95 95.0
#define DB_VK_PERCENTILE_P99 99.0

extern const float db_vk_shader_ignored_color_rgb[3];

void db_vk_release_output_hash_readback_buffer(void);
void db_vk_ensure_output_hash_readback_buffer(size_t required_bytes);
uint64_t db_vk_compute_output_hash_from_image(VkImage image,
                                              VkImageLayout old_layout,
                                              VkExtent2D extent);
db_vk_grid_row_block_draw_req_t
db_vk_gradient_row_block_req(const db_grid_block_t *block, db_pattern_t pattern,
                             const db_gradient_state_t *state,
                             uint32_t frame_index);
void db_vk_draw_gradient_block_segments(const db_vk_owner_draw_ctx_t *draw_ctx,
                                        const db_grid_block_t *block,
                                        db_pattern_t pattern,
                                        const db_gradient_state_t *state,
                                        uint32_t frame_index);
int db_vk_dual_metrics_enabled(void);
uint32_t db_vk_metrics_sample_capacity_hint(void);
void db_vk_record_render_frame_sample(double frame_ms);
void db_vk_recreate_swapchain_and_history_targets_with_reset(void);

#endif
