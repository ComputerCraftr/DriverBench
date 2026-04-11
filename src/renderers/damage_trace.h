#ifndef DRIVERBENCH_RENDERER_DAMAGE_TRACE_H
#define DRIVERBENCH_RENDERER_DAMAGE_TRACE_H

#include "../core/db_frame_plan.h"
#include "core/db_render_types.h"

typedef enum {
    DB_DAMAGE_TRACE_BACKEND_CPU = 0,
    DB_DAMAGE_TRACE_BACKEND_GL1,
    DB_DAMAGE_TRACE_BACKEND_GL3,
    DB_DAMAGE_TRACE_BACKEND_VULKAN,
    DB_DAMAGE_TRACE_BACKEND_DISPLAY,
} db_damage_trace_backend_t;

typedef enum {
    DB_DAMAGE_TRACE_STAGE_LOGICAL = 0,
    DB_DAMAGE_TRACE_STAGE_NORMALIZED,
    DB_DAMAGE_TRACE_STAGE_RENDERER_WRITE,
    DB_DAMAGE_TRACE_STAGE_SHADOW_WRITE,
    DB_DAMAGE_TRACE_STAGE_STAGING_WRITE,
    DB_DAMAGE_TRACE_STAGE_UPLOAD,
    DB_DAMAGE_TRACE_STAGE_TEXTURE_IMAGE,
    DB_DAMAGE_TRACE_STAGE_RENDER_TARGET,
    DB_DAMAGE_TRACE_STAGE_PRESENT,
} db_damage_trace_stage_t;

typedef enum {
    DB_DAMAGE_TRACE_BUFFER_NONE = 0,
    DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
    DB_DAMAGE_TRACE_BUFFER_CPU_SURFACE,
    DB_DAMAGE_TRACE_BUFFER_GL1_BACKBUFFER,
    DB_DAMAGE_TRACE_BUFFER_GL1_SHADOW,
    DB_DAMAGE_TRACE_BUFFER_PBO_UNPACK,
    DB_DAMAGE_TRACE_BUFFER_PBO_PACK,
    DB_DAMAGE_TRACE_BUFFER_GL_TEXTURE,
    DB_DAMAGE_TRACE_BUFFER_GL_FBO,
    DB_DAMAGE_TRACE_BUFFER_GL_DEFAULT_FRAMEBUFFER,
    DB_DAMAGE_TRACE_BUFFER_VK_STAGING,
    DB_DAMAGE_TRACE_BUFFER_VK_IMAGE,
    DB_DAMAGE_TRACE_BUFFER_VK_SWAPCHAIN,
} db_damage_trace_buffer_t;

typedef enum {
    DB_DAMAGE_TRACE_OP_SEED = 0,
    DB_DAMAGE_TRACE_OP_REBUILD,
    DB_DAMAGE_TRACE_OP_INCREMENTAL,
    DB_DAMAGE_TRACE_OP_DRAW,
    DB_DAMAGE_TRACE_OP_COPY,
    DB_DAMAGE_TRACE_OP_BLIT,
    DB_DAMAGE_TRACE_OP_MAP_WRITE,
    DB_DAMAGE_TRACE_OP_SUBDATA,
    DB_DAMAGE_TRACE_OP_UPLOAD,
    DB_DAMAGE_TRACE_OP_FALLBACK,
    DB_DAMAGE_TRACE_OP_READBACK,
    DB_DAMAGE_TRACE_OP_PRESENT,
} db_damage_trace_operation_t;

typedef enum {
    DB_DAMAGE_TRACE_SPACE_GRID = 0,
    DB_DAMAGE_TRACE_SPACE_PIXEL,
} db_damage_trace_space_t;

typedef enum {
    DB_DAMAGE_TRACE_RESULT_EXECUTED = 0,
    DB_DAMAGE_TRACE_RESULT_SKIPPED,
    DB_DAMAGE_TRACE_RESULT_FALLBACK,
    DB_DAMAGE_TRACE_RESULT_FAILED,
} db_damage_trace_result_t;

typedef struct {
    uint32_t frame_index;
    db_damage_trace_backend_t backend;
    db_damage_trace_stage_t stage;
    db_damage_trace_operation_t operation;
    db_damage_trace_buffer_t source;
    db_damage_trace_buffer_t destination;
    uint32_t source_index;
    uint32_t destination_index;
    db_damage_trace_space_t space;
    uint32_t width;
    uint32_t height;
    db_pixel_format_t pixel_format;
    size_t row_stride_bytes;
    const db_damage_block_t *blocks;
    size_t block_count;
    size_t transfer_offset_bytes;
    size_t transfer_size_bytes;
    uint64_t source_hash;
    uint64_t destination_hash;
    db_damage_trace_result_t result;
    const char *mode;
    const char *reason;
    const char *target;
    uint32_t target_generation;
    const char *present_method;
} db_damage_trace_event_t;

typedef enum {
    DB_TARGET_LIFECYCLE_CREATE = 0,
    DB_TARGET_LIFECYCLE_RECREATE,
    DB_TARGET_LIFECYCLE_INVALIDATE,
    DB_TARGET_LIFECYCLE_REBUILD,
    DB_TARGET_LIFECYCLE_DESTROY,
} db_target_lifecycle_action_t;

typedef struct {
    db_damage_trace_backend_t backend;
    db_target_lifecycle_action_t action;
    const char *target;
    uint64_t target_id;
    uint32_t generation;
    uint32_t old_width;
    uint32_t old_height;
    uint32_t new_width;
    uint32_t new_height;
    db_pixel_format_t format;
    const char *cause;
    int valid_before;
    int valid_after;
} db_target_lifecycle_event_t;

typedef struct {
    uint64_t sequence;
    uint64_t covered_units;
    uint64_t duplicate_units;
    uint64_t union_hash;
    db_damage_block_t bounds;
    size_t valid_block_count;
    size_t rejected_block_count;
    int truncated;
} db_damage_trace_summary_t;

int db_damage_trace_level(void);
int db_damage_trace_enabled(void);
size_t db_damage_trace_detail_count(size_t block_count, int trace_level);
uint64_t db_damage_trace_surface_hash(const db_pixel_surface_t *surface);
uint64_t
db_damage_trace_surface_hash_oriented(const db_pixel_surface_t *surface,
                                      int rows_bottom_to_top);
db_damage_trace_summary_t
db_damage_trace_summarize(const db_damage_trace_event_t *event);
db_damage_trace_summary_t
db_damage_trace_emit(const db_damage_trace_event_t *event);
db_damage_trace_summary_t
db_damage_trace_emit_grid(const db_damage_trace_event_t *event,
                          const db_grid_block_t *blocks, size_t block_count);
db_damage_trace_summary_t
db_damage_trace_emit_colored_grid(const db_damage_trace_event_t *event,
                                  const db_colored_f64_block_t *blocks,
                                  size_t block_count);
void db_damage_trace_emit_frame_plan(db_damage_trace_backend_t backend,
                                     const char *target,
                                     uint32_t target_generation,
                                     const db_frame_plan_t *plan);
void db_damage_trace_emit_target_lifecycle(
    const db_target_lifecycle_event_t *event);

#endif
