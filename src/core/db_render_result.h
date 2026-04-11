#ifndef DRIVERBENCH_CORE_RENDER_RESULT_H
#define DRIVERBENCH_CORE_RENDER_RESULT_H

#include <stdint.h>

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
} db_render_result_t;

static inline db_render_result_t db_render_result_success(void) {
    return (db_render_result_t){.success = 1};
}

#endif
