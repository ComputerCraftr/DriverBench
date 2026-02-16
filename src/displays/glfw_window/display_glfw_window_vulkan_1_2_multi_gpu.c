#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../../core/db_core.h"
#include "../../renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.h"
#include "../bench_config.h"
#include "display_glfw_window_common.h"

#define BACKEND_NAME "display_glfw_window_vulkan_1_2_multi_gpu"
#define RENDERER_NAME "renderer_vulkan_1_2_multi_gpu"
#define REMOTE_DISPLAY_OVERRIDE_ENV "DRIVERBENCH_ALLOW_REMOTE_DISPLAY"

int main(void) {
    db_validate_runtime_environment(BACKEND_NAME, REMOTE_DISPLAY_OVERRIDE_ENV);

    GLFWwindow *window = db_glfw_create_no_api_window(
        BACKEND_NAME, "Vulkan 1.2 opportunistic multi-GPU (device groups)",
        BENCH_WINDOW_WIDTH_PX, BENCH_WINDOW_HEIGHT_PX);

    int exit_code = db_renderer_vulkan_1_2_multi_gpu_run(window);
    db_glfw_destroy_window(window);
    return exit_code;
}
