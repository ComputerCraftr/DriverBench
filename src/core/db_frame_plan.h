#ifndef DRIVERBENCH_CORE_FRAME_PLAN_H
#define DRIVERBENCH_CORE_FRAME_PLAN_H

#include "db_render_ir.h"
#include "db_render_types.h"
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
    DB_FRAME_PLAN_OK = 0,
    DB_FRAME_PLAN_INVALID = 1,
    DB_FRAME_PLAN_CAPACITY = 2,
    DB_FRAME_PLAN_ARITHMETIC_OVERFLOW = 3,
    DB_FRAME_PLAN_CHECKPOINT_REQUIRED = 4,
    DB_FRAME_PLAN_CHECKPOINT_UNAVAILABLE = 5,
} db_frame_plan_status_t;

const char *db_frame_plan_status_name(db_frame_plan_status_t status);

typedef struct {
    db_pixel_format_t checkpoint_format;
    uint32_t checkpoint_width;
    uint32_t checkpoint_height;
    size_t checkpoint_row_stride_bytes;
    size_t checkpoint_allocation_bytes;
    uint64_t committed_revision;
    uint64_t requirements_token;
    uint32_t frame_index;
    int checkpoint_required;
} db_frame_requirements_t;

typedef struct {
    uint64_t binding_token;
    uint64_t resource_generation;
    uint64_t content_revision;
    uint32_t width;
    uint32_t height;
    db_pixel_format_t format;
    int valid;
} db_frame_checkpoint_binding_t;

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
    uint64_t preparation_token;
    uint64_t checkpoint_binding_token;
    uint32_t presentation_replay_depth;
    int rebuild_required;
    db_frame_rebuild_reason_t rebuild_reason;
    int force_full_upload;
    int full_draw_required;
    int seeded_background;
    int seeded_shadow_ring;
    const char *shadow_seed_source_label;

    db_render_ir_view_t update_ir;
    db_render_ir_view_t rebuild_ir;
    db_render_ir_external_binding_view_t external_bindings;
    db_render_ir_external_binding_t external_binding_storage[1];
    uint64_t update_ir_hash;
    uint64_t rebuild_ir_hash;
    db_render_ir_metadata_t update_metadata;
    db_render_ir_metadata_t rebuild_metadata;

} db_frame_plan_t;

typedef struct {
    uint32_t pixel_width;
    uint32_t pixel_height;
    int force_rebuild;
    int force_full_draw;
    db_frame_rebuild_reason_t rebuild_reason;
    uint64_t preparation_token;
    uint32_t presentation_replay_depth;
} db_frame_plan_request_t;

#endif
