#include "db_frame_plan.h"
#include "db_render_result.h"

const char *db_frame_plan_status_name(db_frame_plan_status_t status) {
    switch (status) {
    case DB_FRAME_PLAN_OK:
        return "ok";
    case DB_FRAME_PLAN_INVALID:
        return "invalid";
    case DB_FRAME_PLAN_CAPACITY:
        return "capacity";
    case DB_FRAME_PLAN_ARITHMETIC_OVERFLOW:
        return "arithmetic_overflow";
    case DB_FRAME_PLAN_CHECKPOINT_REQUIRED:
        return "checkpoint_required";
    case DB_FRAME_PLAN_CHECKPOINT_UNAVAILABLE:
        return "checkpoint_unavailable";
    }
    return "unknown";
}

const char *db_render_target_strategy_name(db_render_target_strategy_t value) {
    switch (value) {
    case DB_RENDER_TARGET_NONE:
        return "none";
    case DB_RENDER_TARGET_CPU_SURFACE:
        return "cpu_surface";
    case DB_RENDER_TARGET_GL1_DIRECT_WINDOW:
        return "gl1_direct_window";
    case DB_RENDER_TARGET_GL1_PERSISTENT_FBO:
        return "gl1_persistent_fbo";
    case DB_RENDER_TARGET_GL1_CPU_UPLOAD:
        return "gl1_cpu_upload";
    case DB_RENDER_TARGET_GL3_PERSISTENT_FBO:
        return "gl3_persistent_fbo";
    case DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE:
        return "vulkan_persistent_image";
    }
    return "unknown";
}

const char *db_render_operation_path_name(db_render_operation_path_t value) {
    switch (value) {
    case DB_RENDER_OPERATION_NONE:
        return "none";
    case DB_RENDER_OPERATION_CPU_NATIVE:
        return "cpu_native";
    case DB_RENDER_OPERATION_GL1_FIXED_FUNCTION:
        return "gl1_fixed_function";
    case DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT:
        return "gl1_interpolated_gradient";
    case DB_RENDER_OPERATION_GL1_ROW_FILL:
        return "gl1_row_fill";
    case DB_RENDER_OPERATION_GL1_CPU_UPLOAD:
        return "gl1_cpu_upload";
    case DB_RENDER_OPERATION_GL3_INSTANCED_SOLID:
        return "gl3_instanced_solid";
    case DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT:
        return "gl3_semantic_gradient";
    case DB_RENDER_OPERATION_GL3_EXACT_LOOKUP:
        return "gl3_exact_lookup";
    case DB_RENDER_OPERATION_GL3_ROW_FILL:
        return "gl3_row_fill";
    case DB_RENDER_OPERATION_VULKAN_INSTANCED_SOLID:
        return "vulkan_instanced_solid";
    case DB_RENDER_OPERATION_VULKAN_SEMANTIC_GRADIENT:
        return "vulkan_semantic_gradient";
    case DB_RENDER_OPERATION_VULKAN_EXACT_LOOKUP:
        return "vulkan_exact_lookup";
    case DB_RENDER_OPERATION_VULKAN_ROW_FILL:
        return "vulkan_row_fill";
    }
    return "unknown";
}
