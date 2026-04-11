#ifndef DRIVERBENCH_CORE_DB_RENDERER_LOG_H
#define DRIVERBENCH_CORE_DB_RENDERER_LOG_H

#include "core/db_core.h"
#include "core/db_log.h"
#include "core/db_render_result.h"

static inline void db_renderer_log_final_summary(
    const char *api_name, const char *renderer_name, const char *backend,
    uint64_t frames, uint32_t work_unit_count, double elapsed_ms,
    void (*draw_stats)(db_renderer_draw_path_stats_t *)) {
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
    db_benchmark_log_final(api_name, renderer_name, backend, frames,
                           work_unit_count, elapsed_ms);
}

#endif
