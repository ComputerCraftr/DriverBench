#ifndef DRIVERBENCH_CORE_DB_RENDERER_LOG_H
#define DRIVERBENCH_CORE_DB_RENDERER_LOG_H

#include "core/db_conformance.h"
#include "core/db_conformance_cache.h"
#include "core/db_core.h"
#include "core/db_log.h"
#include "core/db_render_result.h"
#include <stdint.h>

static inline void db_renderer_log_final_summary(
    const char *api_name, const char *renderer_name, const char *backend,
    uint64_t frames, uint32_t work_unit_count, double elapsed_ms,
    void (*draw_stats)(db_renderer_draw_path_stats_t *),
    void (*execution_report)(db_render_execution_report_t *)) {
    db_renderer_draw_path_stats_t stats = {0};
    if (draw_stats != NULL) {
        draw_stats(&stats);
    }
    const db_log_field_t fields[] = {
        DB_LOG_U64("full_present_frames", stats.full_present_frames),
        DB_LOG_U64("dirty_geometry_frames", stats.dirty_geometry_frames),
        DB_LOG_U64("shadow_fallback_frames", stats.shadow_fallback_frames),
        DB_LOG_U64("replay_only_frames", stats.replay_only_frames),
    };
    db_log_info(backend, "draw_stats", fields, DB_LOG_FIELD_COUNT(fields));
    if (execution_report != NULL) {
        db_render_execution_report_t execution = {0};
        execution_report(&execution);
        const db_log_field_t execution_fields[] = {
            DB_LOG_TOKEN("target_strategy", db_render_target_strategy_name(
                                                execution.target_strategy)),
            DB_LOG_TOKEN("solid_path",
                         db_render_operation_path_name(execution.solid_path)),
            DB_LOG_TOKEN("gradient_path", db_render_operation_path_name(
                                              execution.gradient_path)),
            DB_LOG_TOKEN("gradient_implementation",
                         db_gradient_implementation_name(
                             execution.gradient_implementation)),
            DB_LOG_TOKEN(
                "qualification_source",
                db_qualification_source_name(execution.qualification_source)),
            DB_LOG_TOKEN("cache_status", db_conformance_cache_status_name(
                                             execution.cache_status)),
            DB_LOG_U64("qualification_lane_count",
                       execution.qualification_lane_count),
            DB_LOG_TOKEN("qualification_reason",
                         (execution.qualification_reason != NULL)
                             ? execution.qualification_reason
                             : "none"),
            DB_LOG_TOKEN("strategy_reason", (execution.strategy_reason != NULL)
                                                ? execution.strategy_reason
                                                : "none"),
            DB_LOG_U64("strategy_generation", execution.strategy_generation),
            DB_LOG_U64("qualification_generation",
                       execution.qualification_generation),
            DB_LOG_U64("target_generation", execution.target_generation),
            DB_LOG_U64("solid_commands", execution.solid_commands),
            DB_LOG_U64("gradient_commands", execution.gradient_commands),
            DB_LOG_U64("solid_draws", execution.solid_draws),
            DB_LOG_U64("gradient_draws", execution.gradient_draws),
            DB_LOG_U64("fallback_instances", execution.fallback_instances),
            DB_LOG_U64("replay_stream_count", execution.replay_stream_count),
            DB_LOG_U64("lookup_words", execution.lookup_words),
            DB_LOG_U64("cpu_pixels_written", execution.cpu_pixels_written),
            DB_LOG_U64("uploaded_bytes", execution.uploaded_bytes),
            DB_LOG_U64("surface_restoration_bytes",
                       execution.surface_restoration_bytes),
            DB_LOG_U64("encoded_span_bytes", execution.encoded_span_bytes),
            DB_LOG_U64("gradient_lookup_bytes", execution.lookup_upload_bytes),
            DB_LOG_U64("command_vbo_bytes", execution.command_upload_bytes),
            DB_LOG_BOOL("qualified", execution.qualified),
            DB_LOG_BOOL("diagnostic_forced", execution.diagnostic_forced),
        };
        db_log_info(backend, "renderer_execution", execution_fields,
                    DB_LOG_FIELD_COUNT(execution_fields));
    }
    db_benchmark_log_final(api_name, renderer_name, backend, frames,
                           work_unit_count, elapsed_ms);
}

#endif
