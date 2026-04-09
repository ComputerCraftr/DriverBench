#ifndef DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_INTERNAL_H
#define DRIVERBENCH_RENDERER_OPENGL_GL1_5_GLES1_1_INTERNAL_H

#include "renderer_opengl_gl1_5_gles1_1_damage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../config/benchmark_config.h"
#include "../renderer_benchmark_gradient.h"
#include "../renderer_gl_common.h"
#include "../renderer_gl_proc_runtime_internal.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_emit.h"
#include "../renderer_snake_shape_common.h"

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define DB_GL1_GRADIENT_DIRTY_RANGE_CAP 2U
#define DB_GL1_SNAKE_COMPACT_HEALTH_LOG_INTERVAL 120U
#define DB_GL1_GRADIENT_REPLAY_ROW_CAP 4U
#define DB_GL1_SNAKE_COMPACT_RECT_LIMIT 2048U
#define DB_GL1_SNAKE_DEBUG_FRAME_LIMIT 120U
#define ES_STRIDE_BYTES ((int)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((int)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    db_snake_compact_block_t *prev_draw_blocks;
    size_t prev_draw_block_count;
    size_t draw_block_capacity;
    int replay_mode;
} db_gl1_snake_replay_t;

typedef struct {
    uint64_t attempt_frames;
    uint64_t success_frames;
    uint64_t fallback_frames;
    uint64_t full_recovery_frames;
} db_gl1_snake_compact_health_t;

typedef struct {
    uint64_t backing_incremental_frames;
    uint64_t backing_rebuild_frames;
    uint64_t texture_partial_upload_frames;
    uint64_t texture_full_upload_frames;
    uint64_t recovery_from_shadow_frames;
} db_gl1_shadow_sync_stats_t;

enum {
    DB_GL1_SNAKE_REPLAY_NONE = 0,
    DB_GL1_SNAKE_REPLAY_COMPACT = 1,
    DB_GL1_SNAKE_REPLAY_FULL_RECOVERY_REQUIRED = 2,
};

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;
    db_gl_vertex_init_t vertex;
    db_gl_viewport_cache_t viewport;
    int is_es_context;
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_gl1_snake_replay_t snake_replay;
    db_history_snake_scratch_t snake_scratch;
    db_history_snake_backbuffer_state_t snake_backbuffer_state;
    int backbuffer_valid;
    db_gl1_snake_compact_health_t snake_compact_health;
    db_gl1_shadow_sync_stats_t snake_shadow_stats;
    db_gl_buffer_cache_t buffers;
    db_gl_upload_stream_t vertex_stream;
    db_gl_compact_vbo_state_t compact_vbo;
    float *snake_color_state;
    size_t snake_color_capacity;
    uint32_t *snake_shadow_rgba8;
    uint16_t *snake_shadow_rgba16f;
    size_t snake_shadow_pixel_capacity;
    uint32_t snake_shadow_pixel_width;
    uint32_t snake_shadow_pixel_height;
    db_damage_block_t *snake_shadow_upload_blocks;
    size_t snake_shadow_upload_block_capacity;
    db_gl_shadow_present_state_t snake_shadow_present;
    db_gl_shadow_present_texture_format_t snake_shadow_backing_format;
    int snake_shadow_present_logged;
    unsigned int snake_shadow_logged_fallback_mask;
    float *gradient_row_y_ndc;
    uint32_t gradient_row_y_ndc_rows;
    int gradient_row_y_ndc_viewport_h;
    int bands_x_cache_viewport_w;
    uint32_t bands_x_cache_cols;
    uint32_t bands_x_cache_count;
    int bands_x_cache_px[BENCH_BANDS + 1U];
    float bands_x_cache_ndc[BENCH_BANDS + 1U];
    int client_arrays_configured;
    int vbo_arrays_configured;
} renderer_state_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    int rebuild_current_frame;
    int force_full_upload;
} db_gl1_snake_frame_state_t;

typedef enum {
    DB_GL1_SHADOW_UPLOAD_INTENT_NONE = 0,
    DB_GL1_SHADOW_UPLOAD_INTENT_PARTIAL_BLOCKS = 1,
    DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD = 2,
} db_gl1_shadow_upload_intent_t;

typedef struct {
    float *dst_vertices;
    size_t rect_capacity;
    size_t rect_count;
    size_t rect_float_count;
    size_t stride;
    int viewport_h;
    uint32_t rows;
    int can_use_cached_rows;
} db_gl1_gradient_rect_emit_ctx_t;

typedef struct {
    float x0;
    float x1;
    float y0;
    float y1;
} db_gl1_rect_bounds_t;

enum {
    DB_GL1_SNAKE_SHADOW_LOG_COMPACT_CAPACITY = 1U << 0,
    DB_GL1_SNAKE_SHADOW_LOG_SHAPE_FALLBACK = 1U << 1,
    DB_GL1_SNAKE_SHADOW_LOG_INVALID_RECOVERY = 1U << 2,
    DB_GL1_SNAKE_SHADOW_LOG_AUTHORITATIVE_REBUILD = 1U << 3,
};

extern renderer_state_t g_gl1_state;
#define g_state g_gl1_state

void db_gl1_log_compact_reject(const char *path, const char *reason,
                               size_t range_count, size_t value, size_t limit);
void db_gl1_log_shadow_fallback_once(unsigned int reason_mask,
                                     const char *reason);
void db_gl1_record_compact_health(int dirty_backbuffer_mode,
                                  int used_compact_draw, int used_fallback_draw,
                                  db_gl1_snake_frame_mode_t frame_mode,
                                  size_t compact_block_count,
                                  size_t damage_block_count,
                                  const char *fallback_reason);
void db_gl1_invalidate_array_pointer_cache(void);
void db_gl1_emit_gradient_row_block(uint32_t row_start, uint32_t row_count,
                                    const double *row_rgb, void *user_data);
int db_gl1_has_snake_color_state(void);
int db_gl1_shadow_backing_uses_rgba16f(void);
db_gl1_rect_bounds_t db_gl1_rect_bounds_from_grid_tile(size_t row, size_t col);
void db_gl1_emit_snake_compact_rect(float *dst_unit, size_t stride,
                                    size_t row_start, size_t row_count,
                                    size_t col_start, size_t col_count,
                                    const float *tile_color);
void db_gl1_get_snake_color_bits(uint32_t row, uint32_t col, void *user_data,
                                 uint32_t *color_bits);
void db_gl1_init_snake_color_state_from_vertices(void);
void db_gl1_ensure_shadow_framebuffer_capacity(uint32_t pixel_width,
                                               uint32_t pixel_height);
void db_gl1_rebuild_shadow_framebuffer_full(
    uint32_t pixel_width, uint32_t pixel_height,
    const db_benchmark_pixel_surface_t *mirror_surface);
void db_gl1_update_shadow_framebuffer_from_snake_step(
    const db_gl1_snake_frame_state_t *snake_frame, uint32_t pixel_width,
    uint32_t pixel_height, const db_benchmark_pixel_surface_t *mirror_surface);
size_t db_gl1_build_shadow_upload_blocks_from_damage_blocks(
    const db_grid_block_t *damage_blocks, size_t damage_block_count,
    uint32_t pixel_width, uint32_t pixel_height);
size_t db_gl1_build_shadow_upload_blocks_from_compact_blocks(
    const db_snake_compact_block_t *compact_blocks, size_t compact_block_count,
    uint32_t pixel_width, uint32_t pixel_height);
size_t db_gl1_build_shadow_repair_blocks(
    const db_snake_compact_block_t *compact_blocks, size_t compact_block_count,
    const db_grid_block_t *damage_blocks, size_t damage_block_count,
    uint32_t pixel_width, uint32_t pixel_height);
int db_gl1_draw_shadow_framebuffer_once(const db_damage_block_t *blocks,
                                        size_t block_count,
                                        uint32_t pixel_width,
                                        uint32_t pixel_height);
void db_gl1_refresh_tile_positions_for_viewport(int viewport_w, int viewport_h);
void db_gl1_refresh_gradient_row_ndc_cache(int viewport_h);
void db_gl1_refresh_bands_x_cache(uint32_t cols, uint32_t band_count,
                                  int viewport_w);
void db_gl1_seed_backbuffer_clear_cb(const float *rgba, void *user_data);
void db_gl1_refresh_capability_mode(void);
void db_gl1_log_backbuffer_strategy(void);
size_t db_gl1_snake_compact_rect_capacity(void);
int db_gl1_init_runtime_metadata_only(
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride);
int db_init_vertices_for_mode(const db_benchmark_runtime_init_t *runtime_state,
                              size_t vertex_stride);
void db_render_snake_step(const db_snake_plan_t *plan,
                          const db_snake_region_t *region, uint32_t shape_kind,
                          uint32_t pattern_seed, uint32_t shape_index,
                          const double *target_rgb,
                          int force_full_fill_on_phase_complete);
size_t db_gl1_build_gradient_compact_row_vertices(
    const db_grid_block_t *dirty_blocks, size_t dirty_count, uint32_t head_row,
    int direction_down, uint32_t cycle_index, int viewport_w, int viewport_h,
    float *dst_vertices, size_t dst_float_capacity);
void db_gl1_configure_client_arrays_if_needed(void);
void db_gl1_configure_vbo_arrays_if_needed(void);
int db_gl1_draw_compact_blocks_from_snake_colors_once(
    const db_snake_compact_block_t *rects, size_t rect_count);
void db_gl1_draw_gradient_dirty_blocks_mesh(
    const db_grid_block_t *dirty_blocks, size_t dirty_count, uint32_t head_row,
    int direction_down, uint32_t cycle_index, int viewport_w, int viewport_h);
void db_gl1_draw_bands_compact(uint32_t cols, uint32_t band_count,
                               uint32_t frame_index, int viewport_w,
                               int viewport_h);
void db_gl1_prepare_snake_frame_state(db_gl1_snake_frame_state_t *state,
                                      uint32_t preserved_framebuffer_count,
                                      int dirty_backbuffer_mode);
void db_gl1_render_snake_draw_pass(
    const db_gl1_snake_frame_state_t *snake_frame, int dirty_backbuffer_mode,
    int viewport_w, int viewport_h);
void db_gl1_render_gradient_frame(int viewport_w, int viewport_h,
                                  uint32_t preserved_framebuffer_count);
void db_gl1_render_bands_frame(int viewport_w, int viewport_h,
                               int dirty_backbuffer_mode);
#endif
