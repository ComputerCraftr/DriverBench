#ifndef DRIVERBENCH_VK_DIAGNOSTICS_H
#define DRIVERBENCH_VK_DIAGNOSTICS_H

#include "core/db_log.h"
#include "core/db_numeric.h"
#include "core/db_trace.h"
#include <vulkan/vulkan_core.h>

static inline int db_vk_trace_level(void) {
    return db_trace_config_current().vulkan;
}

static inline VkClearValue
db_vk_clear_value_from_rgba_f64(const double rgba[4]) {
    VkClearValue clear = {0};
    db_rgba_f64_to_f32_rgba4(rgba, clear.color.float32);
    return clear;
}

static inline const char *db_vk_format_name(VkFormat format) {
    switch ((int)format) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "rgba16f";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "r8g8b8a8_unorm";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "b8g8r8a8_unorm";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return "a2r10g10b10_unorm";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "a2b10g10r10_unorm";
    case VK_FORMAT_UNDEFINED:
        return "undefined";
    default:
        return "other";
    }
}

static inline const char *db_vk_result_name(VkResult result) {
    switch ((int)result) {
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
    DB_RUNTIME_FAIL(backend_name, "%s failed: %s (%d) at %s:%d", expr,
                    db_vk_result_name(result), (int)result, file, line);
    __builtin_unreachable();
}

#define DB_VK_CHECK(backend_name, expression)                                  \
    do {                                                                       \
        const VkResult db_vk_check_result = (expression);                      \
        if (db_vk_check_result != VK_SUCCESS) {                                \
            db_vk_fail((backend_name), #expression, db_vk_check_result,        \
                       __FILE__, __LINE__);                                    \
        }                                                                      \
    } while (0)

#endif
