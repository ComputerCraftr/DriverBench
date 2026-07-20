#ifndef DRIVERBENCH_GL1_INTERNAL_H
#define DRIVERBENCH_GL1_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_qualification_contracts.h"
#include "../../core/db_render_ir_snapshot.h"
#include "../../core/db_renderer_diagnostics.h"
#include "../../core/db_replay_policy.h"
#include "../gl_common.h"
#include "../gl_hash_readback.h"
#include "core/db_format_contract.h"
#include "core/db_geometry.h"
#include "core/db_log.h"
#include "core/db_render_ir.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_support.h"

typedef enum {
    GL1_STRATEGY_UNRESOLVED = 0,
    GL1_STRATEGY_DIRECT_WINDOW,
    GL1_STRATEGY_PERSISTENT_FBO,
    GL1_STRATEGY_CPU_UPLOAD,
} gl1_strategy_t;

typedef struct {
    gl1_strategy_t strategy;
    unsigned int fbo;
    unsigned int direct_fbo;
    db_gl_upload_stream_t vertex_stream;
    db_gl_framebuffer_hash_scratch_t hash_scratch;
    float *vertices;
    size_t vertex_capacity;
    size_t storage_bytes;
    uint32_t generation;
    uint32_t width;
    uint32_t height;
    int valid;
    db_renderer_applied_selection_t applied;
} gl1_native_target_t;

typedef struct {
    db_render_ir_snapshot_t update;
    uint32_t frame_index;
    uint64_t target_generation;
    uint32_t width;
    uint32_t height;
    db_pixel_format_t format;
    int replay_boundary;
    int valid;
} gl1_replay_entry_t;

typedef struct {
    db_replay_policy_t policy;
    gl1_replay_entry_t entries[DB_REPLAY_CAPACITY_MAX + 1U];
    uint32_t next_entry;
    uint32_t pending_entry;
    uint64_t target_generation;
    db_render_target_strategy_t committed_strategy;
    uint64_t committed_target_generation;
    size_t allocation_bytes;
    int direct_window_lineage_valid;
    int available;
} gl1_replay_history_t;

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define runtime_failf(...) DB_RUNTIME_FAIL(BACKEND_NAME, __VA_ARGS__)
#define g_state g_gl1_state

typedef struct {
    uint64_t backing_incremental_frames;
    uint64_t backing_rebuild_frames;
    uint64_t texture_partial_upload_frames;
    uint64_t texture_full_upload_frames;
} gl1_backing_stats_t;

typedef struct {
    void *pixels;
    size_t pixel_capacity;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t generation;
    db_gl_shadow_present_texture_format_t texture_format;
    db_display_resolved_format_config_t format;
} gl1_backing_storage_t;

typedef struct {
    db_damage_block_t *blocks;
    size_t capacity;
} gl1_upload_workspace_t;

typedef struct {
    db_gl_shadow_present_state_t shadow;
    int config_logged;
    int current_present_full;
} gl1_presentation_state_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    gl1_backing_stats_t backing;
    db_render_execution_report_t execution;
} gl1_telemetry_t;

typedef struct {
    db_renderer_execution_config_t runtime;
    db_renderer_diagnostic_config_t diagnostics;
    db_gl_viewport_cache_t viewport;
    gl1_backing_storage_t backing;
    gl1_upload_workspace_t upload;
    gl1_presentation_state_t presentation;
    gl1_native_target_t native;
    gl1_replay_history_t replay;
    gl1_telemetry_t telemetry;
} renderer_state_t;

extern renderer_state_t g_gl1_state;

static inline db_pixel_surface_t gl1_backing_surface(uint32_t pixel_width,
                                                     uint32_t pixel_height) {
    return (db_pixel_surface_t){
        .pixel_width = pixel_width,
        .pixel_height = pixel_height,
        .pixels = g_state.backing.pixels,
        .format = (g_state.backing.texture_format ==
                   DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                      ? DB_PIXEL_FORMAT_RGBA16F
                      : DB_PIXEL_FORMAT_RGBA8,
    };
}

int db_gl1_backing_uses_rgba16f(void);
void db_gl1_ensure_backing_capacity(uint32_t pixel_width,
                                    uint32_t pixel_height);
void db_gl1_rebuild_backing(const db_frame_plan_t *plan, uint32_t pixel_width,
                            uint32_t pixel_height);
void db_gl1_update_backing(const db_frame_plan_t *plan, uint32_t pixel_width,
                           uint32_t pixel_height);
int db_gl1_present_backing(const db_frame_plan_t *plan,
                           db_pixel_block_view_t blocks_view,
                           uint32_t pixel_width, uint32_t pixel_height);
void db_gl1_refresh_capability_mode(void);
int db_gl1_init_runtime(const db_renderer_execution_config_t *runtime_state);
int db_gl1_native_init(void);
const db_renderer_qualification_ops_t *db_gl1_native_qualification_ops(void);
int db_gl1_native_render(const db_frame_plan_t *plan, uint32_t logical_width,
                         const db_renderer_target_t *target,
                         uint32_t logical_height, int presentation_fbo,
                         int viewport_width, int viewport_height);
void db_gl1_native_shutdown(void);
int db_gl1_replay_init(void);
void db_gl1_replay_shutdown(void);
void db_gl1_replay_reset(void);
void db_gl1_replay_discard_pending(void);
size_t db_gl1_replay_collect(const db_frame_plan_t *plan,
                             db_render_ir_view_t *views, size_t view_capacity,
                             int *use_rebuild);
int db_gl1_replay_prepare(const db_frame_plan_t *plan, uint32_t width,
                          uint32_t height, db_pixel_format_t format,
                          uint64_t target_generation, int replay_boundary);
void db_gl1_replay_publish_pending(void);
void db_gl1_replay_prepare_boundary(void);
void db_gl1_render_geometry_to_backing(const db_frame_plan_t *plan,
                                       int viewport_width, int viewport_height);

#endif
