#ifndef DRIVERBENCH_GL1_INTERNAL_H
#define DRIVERBENCH_GL1_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "../../core/db_frame_plan.h"
#include "../gl_common.h"
#include "core/db_format_contract.h"
#include "core/db_render_types.h"

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
} gl1_telemetry_t;

typedef struct {
    db_renderer_execution_config_t runtime;
    db_gl_viewport_cache_t viewport;
    gl1_backing_storage_t backing;
    gl1_upload_workspace_t upload;
    gl1_presentation_state_t presentation;
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
int db_gl1_present_backing(db_pixel_block_view_t blocks_view,
                           uint32_t pixel_width, uint32_t pixel_height);
void db_gl1_refresh_capability_mode(void);
int db_gl1_init_runtime(const db_renderer_execution_config_t *runtime_state);
void db_gl1_render_geometry_to_backing(const db_frame_plan_t *plan,
                                       int viewport_width, int viewport_height);

#endif
