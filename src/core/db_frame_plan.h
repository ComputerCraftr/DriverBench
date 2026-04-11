#ifndef DRIVERBENCH_CORE_FRAME_PLAN_H
#define DRIVERBENCH_CORE_FRAME_PLAN_H

#include "db_geometry.h"
#include "db_render_types.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_FRAME_REBUILD_NONE = 0,
    DB_FRAME_REBUILD_INITIAL_TARGET = 1,
    DB_FRAME_REBUILD_RESIZE = 2,
    DB_FRAME_REBUILD_EXPLICIT = 3,
    DB_FRAME_REBUILD_SEED = 4,
    DB_FRAME_REBUILD_GEOMETRY_RECOVERY = 5,
} db_frame_rebuild_reason_t;

typedef enum {
    DB_FRAME_REBUILD_SEED_NONE = 0,
    DB_FRAME_REBUILD_SEED_GEOMETRY = 1,
    DB_FRAME_REBUILD_SEED_RASTER = 2,
} db_frame_rebuild_seed_kind_t;

typedef struct {
    db_frame_rebuild_seed_kind_t kind;
    db_colored_f64_block_view_t geometry;
    db_pixel_surface_t raster;
    uint64_t generation;
    uint64_t content_revision;
    uint32_t committed_frame_index;
    int committed_frame_valid;
} db_frame_rebuild_seed_t;

typedef struct {
    uint32_t frame_index;
    uint32_t simulation_tick_count;
    uint32_t simulation_chunk_count;
    uint32_t simulation_boundary_count;
    uint32_t simulation_terminal_item_count;
    uint32_t grid_cols;
    uint32_t grid_rows;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint64_t expected_state_hash;
    int rebuild_required;
    db_frame_rebuild_reason_t rebuild_reason;
    int force_full_upload;
    int full_draw_required;
    int seeded_background;
    int seeded_shadow_ring;
    int geometry_overflowed;
    const char *shadow_seed_source_label;

    db_frame_rebuild_seed_t rebuild_seed;
    db_geometry_execution_t geometry;
} db_frame_plan_t;

typedef struct {
    uint32_t pixel_width;
    uint32_t pixel_height;
    int force_rebuild;
    int force_full_draw;
    db_frame_rebuild_reason_t rebuild_reason;
} db_frame_plan_request_t;

#endif
