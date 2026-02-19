#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdint.h>

#include "../../core/db_core.h"
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#include "../bench_config.h"
#include "../display_env_common.h"
#include "../display_gl_runtime_common.h"
#include "display_glfw_window_common.h"

#define BACKEND_NAME "display_glfw_window_vulkan_1_2_multi_gpu"
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"

// NOLINTBEGIN(misc-include-cleaner)

static const char *const *
db_glfw_vk_required_instance_extensions(uint32_t *count, void *user_data) {
    (void)user_data;
    return glfwGetRequiredInstanceExtensions(count);
}

static VkResult db_glfw_vk_create_surface(VkInstance instance,
                                          void *window_handle,
                                          VkSurfaceKHR *surface,
                                          void *user_data) {
    (void)user_data;
    return glfwCreateWindowSurface(instance, (GLFWwindow *)window_handle, NULL,
                                   surface);
}

static void db_glfw_vk_get_framebuffer_size(void *window_handle, int *width,
                                            int *height, void *user_data) {
    (void)user_data;
    glfwGetFramebufferSize((GLFWwindow *)window_handle, width, height);
}

int main(void) {
    db_validate_runtime_environment(BACKEND_NAME, DB_ENV_ALLOW_REMOTE_DISPLAY);
    db_install_signal_handlers();
    const double fps_cap = db_glfw_resolve_fps_cap(BACKEND_NAME);
    const uint32_t frame_limit = db_glfw_resolve_frame_limit(BACKEND_NAME);

    GLFWwindow *window = db_glfw_create_no_api_window(
        BACKEND_NAME, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX);
    uint32_t runtime_api_version = VK_API_VERSION_1_0;
    const VkResult version_result =
        vkEnumerateInstanceVersion(&runtime_api_version);
    if (version_result != VK_SUCCESS) {
        runtime_api_version = VK_API_VERSION_1_0;
    }
    db_display_log_vulkan_runtime_api(BACKEND_NAME, runtime_api_version,
                                      "(selected by renderer)");

    const db_vk_wsi_config_t wsi_config = {
        .window_handle = window,
        .user_data = NULL,
        .get_required_instance_extensions =
            db_glfw_vk_required_instance_extensions,
        .create_window_surface = db_glfw_vk_create_surface,
        .get_framebuffer_size = db_glfw_vk_get_framebuffer_size,
    };
    db_renderer_vulkan_1_2_multi_gpu_init(&wsi_config);
    uint64_t frames = 0U;
    while (!glfwWindowShouldClose(window) && !db_should_stop()) {
        if ((frame_limit > 0U) && (frames >= frame_limit)) {
            break;
        }
        const double frame_start_s = db_glfw_time_seconds();
        db_glfw_poll_events();
        const db_vk_frame_result_t frame_result =
            db_renderer_vulkan_1_2_multi_gpu_render_frame();
        if (frame_result == DB_VK_FRAME_STOP) {
            break;
        }
        db_glfw_sleep_to_fps_cap(frame_start_s, fps_cap);
        frames++;
    }
    db_renderer_vulkan_1_2_multi_gpu_shutdown();
    db_glfw_destroy_window(window);
    return 0;
}
// NOLINTEND(misc-include-cleaner)
