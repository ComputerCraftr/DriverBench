#ifndef DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_H
#define DRIVERBENCH_RENDERER_VULKAN_1_2_MULTI_GPU_H

#include <stdint.h>

#include <vulkan/vulkan.h>

typedef enum {
    DB_VK_FRAME_OK = 0,
    DB_VK_FRAME_RETRY = 1,
    DB_VK_FRAME_STOP = 2,
} db_vk_frame_result_t;

typedef struct {
    void *window_handle;
    void *user_data;
    const char *const *(*get_required_instance_extensions)(uint32_t *count,
                                                           void *user_data);
    VkResult (*create_window_surface)(VkInstance instance, void *window_handle,
                                      VkSurfaceKHR *surface, void *user_data);
    void (*get_framebuffer_size)(void *window_handle, int *width, int *height,
                                 void *user_data);
} db_vk_wsi_config_t;

void db_renderer_vulkan_1_2_multi_gpu_init(
    const db_vk_wsi_config_t *wsi_config);
db_vk_frame_result_t db_renderer_vulkan_1_2_multi_gpu_render_frame(void);
void db_renderer_vulkan_1_2_multi_gpu_shutdown(void);
const char *db_renderer_vulkan_1_2_multi_gpu_capability_mode(void);
uint32_t db_renderer_vulkan_1_2_multi_gpu_work_unit_count(void);

#endif
