#ifndef DRIVERBENCH_VK_H
#define DRIVERBENCH_VK_H

#include <stdint.h>

#include <vulkan/vulkan_core.h>

#include "../gl_common.h"
#include "core/db_frame_contracts.h"
#include "core/db_renderer_runtime_contract.h"

typedef enum {
    DB_VK_FRAME_OK = 0,
    DB_VK_FRAME_RETRY = 1,
    DB_VK_FRAME_STOP = 2,
} db_vk_frame_result_t;

typedef struct {
    const char *backend_name;
    VkResult (*create_window_surface)(VkInstance instance, void *window_handle,
                                      VkSurfaceKHR *surface);
    void (*get_framebuffer_size)(void *window_handle, int *width, int *height);
    const char *const *(*get_required_instance_extensions)(uint32_t *count);
    void *window_handle;
} db_vk_wsi_config_t;

#include "core/db_frame_plan.h"

db_native_output_capability_t
db_vk_init(const db_vk_wsi_config_t *wsi_config, int vsync_enabled,
           const db_renderer_runtime_contract_t *resolved_runtime);
db_vk_frame_result_t db_vk_render_frame(const db_frame_plan_t *plan);
void db_vk_shutdown(void);
const char *db_vk_capability_mode(void);
uint32_t db_vk_work_unit_count(void);
uint64_t db_vk_state_hash(void);
uint64_t db_vk_output_hash(void);
void db_vk_set_output_hash_enabled(int enabled);
void db_vk_draw_stats(db_renderer_draw_path_stats_t *stats);
void db_vk_execution_report(db_render_execution_report_t *report);
const db_renderer_qualification_ops_t *db_vk_qualification_ops(void);
double db_vk_last_render_critical_ms(void);
void db_vk_set_present_metrics(double frame_ema_ms, double jitter_ema_ms,
                               double p50_ms, double p95_ms, double p99_ms,
                               uint32_t window_sample_count,
                               uint32_t window_capacity, uint64_t total_samples,
                               uint64_t retries);
void db_vk_set_render_metrics(double p50_ms, double p95_ms, double p99_ms,
                              uint32_t window_sample_count,
                              uint32_t window_capacity, uint64_t total_samples);

#endif
