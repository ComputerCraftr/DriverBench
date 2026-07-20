#ifndef DRIVERBENCH_CORE_RENDER_RESULT_H
#define DRIVERBENCH_CORE_RENDER_RESULT_H

#include "core/db_conformance_cache.h"
#include "db_conformance.h"

#include <stdint.h>

typedef enum {
    DB_RENDER_TARGET_NONE = 0,
    DB_RENDER_TARGET_CPU_SURFACE,
    DB_RENDER_TARGET_GL1_DIRECT_WINDOW,
    DB_RENDER_TARGET_GL1_PERSISTENT_FBO,
    DB_RENDER_TARGET_GL1_CPU_UPLOAD,
    DB_RENDER_TARGET_GL3_PERSISTENT_FBO,
    DB_RENDER_TARGET_VULKAN_PERSISTENT_IMAGE,
} db_render_target_strategy_t;

typedef enum {
    DB_RENDER_OPERATION_NONE = 0,
    DB_RENDER_OPERATION_CPU_NATIVE,
    DB_RENDER_OPERATION_GL1_FIXED_FUNCTION,
    DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT,
    DB_RENDER_OPERATION_GL1_ROW_FILL,
    DB_RENDER_OPERATION_GL1_CPU_UPLOAD,
    DB_RENDER_OPERATION_GL3_INSTANCED_SOLID,
    DB_RENDER_OPERATION_GL3_SEMANTIC_GRADIENT,
    DB_RENDER_OPERATION_GL3_EXACT_LOOKUP,
    DB_RENDER_OPERATION_GL3_ROW_FILL,
    DB_RENDER_OPERATION_VULKAN_INSTANCED_SOLID,
    DB_RENDER_OPERATION_VULKAN_SEMANTIC_GRADIENT,
    DB_RENDER_OPERATION_VULKAN_EXACT_LOOKUP,
    DB_RENDER_OPERATION_VULKAN_ROW_FILL,
} db_render_operation_path_t;

typedef struct {
    db_render_target_strategy_t target_strategy;
    db_render_operation_path_t solid_path;
    db_render_operation_path_t gradient_path;
    uint32_t solid_commands;
    uint32_t gradient_commands;
    uint32_t solid_draws;
    uint32_t gradient_draws;
    uint32_t fallback_instances;
    uint32_t replay_stream_count;
    uint32_t lookup_words;
    uint64_t cpu_pixels_written;
    uint64_t uploaded_bytes;
    uint64_t surface_restoration_bytes;
    uint64_t encoded_span_bytes;
    uint64_t lookup_upload_bytes;
    uint64_t command_upload_bytes;
    db_gradient_implementation_t gradient_implementation;
    db_qualification_source_t qualification_source;
    db_conformance_cache_status_t cache_status;
    uint32_t qualification_lane_count;
    const char *qualification_reason;
    const char *strategy_reason;
    uint64_t strategy_generation;
    uint64_t qualification_generation;
    uint64_t target_generation;
    int qualified;
    int diagnostic_forced;
} db_render_execution_report_t;

typedef struct {
    uint64_t full_present_frames;
    uint64_t dirty_geometry_frames;
    uint64_t shadow_fallback_frames;
    uint64_t replay_only_frames;
} db_renderer_draw_path_stats_t;

typedef struct {
    uint64_t state_hash;
    uint64_t full_draw_frames;
    uint64_t dirty_draw_frames;
    db_renderer_draw_path_stats_t draw_paths;
    uint32_t frame_index;
} db_renderer_frame_stats_t;

typedef struct {
    int success;
    db_renderer_draw_path_stats_t draw_paths;
    uint64_t backing_generation;
    uint64_t working_hash;
    int working_hash_valid;
    db_render_execution_report_t execution;
} db_render_result_t;

const char *db_render_target_strategy_name(db_render_target_strategy_t value);
const char *db_render_operation_path_name(db_render_operation_path_t value);

static inline db_render_result_t db_render_result_success(void) {
    return (db_render_result_t){.success = 1};
}

#endif
