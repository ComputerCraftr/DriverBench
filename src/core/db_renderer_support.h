#ifndef DRIVERBENCH_CORE_RENDERER_SUPPORT_H
#define DRIVERBENCH_CORE_RENDERER_SUPPORT_H

#include "db_numeric.h"
#include "db_render_result.h"
#include "db_trace.h"
#include <stdint.h>

typedef struct {
    int uses_dirty_backbuffer_mode;
    int uses_ff_rect_draw_mode;
    int uses_history_pipeline;
} db_renderer_pipeline_flags_t;

typedef struct {
    db_renderer_pipeline_flags_t pipeline;
} db_renderer_mode_flags_t;

typedef struct {
    uint32_t work_unit_count;
    uint32_t grid_cols;
    uint32_t grid_rows;
    double seed_rgba_f64[4];
    int backbuffer_draw_full;
    int backbuffer_replay_enabled;
    db_renderer_pipeline_flags_t pipeline;
    db_trace_config_t trace;
} db_renderer_execution_config_t;

typedef struct {
    int is_valid;
    int read_index;
} db_renderer_pair_state_t;

static inline void
db_renderer_pair_state_seed(db_renderer_pair_state_t *state) {
    if (state != NULL) {
        *state = (db_renderer_pair_state_t){.is_valid = 1, .read_index = 0};
    }
}

static inline void db_renderer_record_draw_stats_for_work(
    uint64_t *full_draw_frames, uint64_t *dirty_draw_frames,
    int frame_full_draw, int frame_dirty_draw, uint32_t work_units_drawn) {
    if (work_units_drawn == 0U) {
        return;
    }
    if (frame_full_draw != 0) {
        (*full_draw_frames)++;
    }
    if (frame_dirty_draw != 0) {
        (*dirty_draw_frames)++;
    }
}

static inline void db_renderer_record_draw_path(
    db_renderer_draw_path_stats_t *stats, int full_present, int dirty_geometry,
    int shadow_fallback, int replay_only, uint32_t work_units_drawn) {
    if ((stats == NULL) || (work_units_drawn == 0U)) {
        return;
    }
    stats->full_present_frames += (uint64_t)DB_BOOL(full_present);
    stats->dirty_geometry_frames += (uint64_t)DB_BOOL(dirty_geometry);
    stats->shadow_fallback_frames += (uint64_t)DB_BOOL(shadow_fallback);
    stats->replay_only_frames += (uint64_t)DB_BOOL(replay_only);
}

static inline void
db_renderer_copy_draw_path_stats(const db_renderer_frame_stats_t *frame,
                                 db_renderer_draw_path_stats_t *out) {
    if ((frame == NULL) || (out == NULL)) {
        return;
    }
    *out = frame->draw_paths;
    if ((out->full_present_frames == 0U) &&
        (out->dirty_geometry_frames == 0U) &&
        (out->shadow_fallback_frames == 0U) &&
        (out->replay_only_frames == 0U)) {
        out->full_present_frames = frame->full_draw_frames;
        out->dirty_geometry_frames = frame->dirty_draw_frames;
    }
}

#endif
