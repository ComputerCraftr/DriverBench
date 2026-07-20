#ifndef DRIVERBENCH_VK_RUNTIME_INTERNAL_H
#define DRIVERBENCH_VK_RUNTIME_INTERNAL_H
#include "core/db_progress_policy.h"
#include "core/db_render_ir.h"
#include "vk_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#define DEFAULT_EMA_MS_PER_WORK_UNIT 0.2
#define EMA_KEEP 0.9
#define EMA_NEW 0.1
#define DB_VK_PERCENTILE_P50 50.0
#define DB_VK_PERCENTILE_P95 95.0
#define DB_VK_PERCENTILE_P99 99.0

void db_vk_release_output_hash_readback_buffer(void);
void db_vk_release_rebuild_upload_buffer(void);
void db_vk_prepare_raster_seed_upload(
    const db_render_ir_external_binding_t *binding);
void db_vk_record_raster_seed_upload(
    VkCommandBuffer command_buffer, VkBackingTargetState *target,
    const db_render_ir_external_binding_t *binding);
void db_vk_ensure_output_hash_readback_buffer(size_t required_bytes);
uint64_t db_vk_compute_output_hash_from_image(VkImage image,
                                              VkImageLayout old_layout,
                                              VkExtent2D extent);
int db_vk_dual_metrics_enabled(void);
void db_vk_record_render_frame_duration(double frame_ms);
void db_vk_recreate_swapchain_and_backing_targets_with_reset(void);
VkResult db_vk_wait_fence(VkDevice device, VkFence fence,
                          db_progress_policy_id_t policy_id,
                          const char *operation);
VkResult db_vk_acquire_next_image(VkDevice device, VkSwapchainKHR swapchain,
                                  VkSemaphore semaphore,
                                  uint32_t *out_image_index);

#endif
