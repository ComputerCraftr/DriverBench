#include "renderer_opengl_gl1_5_gles1_1.h"
#include "renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderer_opengl_gl1_5_gles1_1_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_alloc_policy.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_api.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_viewport_common.h"

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
    // Runtime mode and benchmark state
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;

    // Vertex and viewport state
    db_gl_vertex_init_t vertex;
    db_gl_viewport_cache_t viewport;
    int is_es_context;

    // History/replay state
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_gl1_snake_replay_t snake_replay;
    db_history_snake_scratch_t snake_scratch;
    db_history_snake_backbuffer_state_t snake_backbuffer_state;
    int backbuffer_valid;
    db_gl1_snake_compact_health_t snake_compact_health;
    db_gl1_shadow_sync_stats_t snake_shadow_stats;

    // GL objects and caches
    db_gl_buffer_cache_t buffers;
    db_gl_compact_vbo_state_t compact_vbo;

    // CPU shadow state for compact snake upload path
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

    // Vertex attribute cache flags
    int client_arrays_configured;
    int vbo_arrays_configured;
} renderer_state_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
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

static renderer_state_t g_state = {0};

enum {
    DB_GL1_SNAKE_SHADOW_LOG_COMPACT_CAPACITY = 1U << 0,
    DB_GL1_SNAKE_SHADOW_LOG_SHAPE_FALLBACK = 1U << 1,
    DB_GL1_SNAKE_SHADOW_LOG_INVALID_RECOVERY = 1U << 2,
    DB_GL1_SNAKE_SHADOW_LOG_AUTHORITATIVE_REBUILD = 1U << 3,
};

static void db_gl1_log_compact_reject(const char *path, const char *reason,
                                      size_t range_count, size_t value,
                                      size_t limit) {
    if ((path == NULL) || (reason == NULL) ||
        (g_state.runtime_flags.is_snake_grid == 0) ||
        (g_state.runtime_flags.uses_dirty_backbuffer_mode == 0) ||
        (g_state.frame.frame_index >= DB_GL1_SNAKE_DEBUG_FRAME_LIMIT)) {
        return;
    }
    db_infof(BACKEND_NAME,
             "snake_grid frame[%u] compact_reject path=%s reason=%s "
             "range_count=%zu value=%zu limit=%zu",
             g_state.frame.frame_index, path, reason, range_count, value,
             limit);
}

static void db_gl1_log_shadow_fallback_once(unsigned int reason_mask,
                                            const char *reason) {
    if ((reason == NULL) ||
        (g_state.runtime_flags.uses_dirty_backbuffer_mode == 0) ||
        ((g_state.snake_shadow_logged_fallback_mask & reason_mask) != 0U)) {
        return;
    }
    g_state.snake_shadow_logged_fallback_mask |= reason_mask;
    db_infof(BACKEND_NAME, "snake shadow_fb_fallback reason=%s", reason);
}

static int db_gl1_should_log_compact_event(void) {
    const uint32_t frame_index = g_state.frame.frame_index;
    return (frame_index < DB_GL1_SNAKE_DEBUG_FRAME_LIMIT) ||
           ((frame_index % DB_GL1_SNAKE_COMPACT_HEALTH_LOG_INTERVAL) == 0U);
}

static void db_gl1_log_compact_fallback_event(
    const char *reason, db_gl1_snake_frame_mode_t frame_mode,
    size_t compact_block_count, size_t damage_block_count) {
    if ((reason == NULL) ||
        (g_state.runtime_flags.uses_dirty_backbuffer_mode == 0) ||
        (db_gl1_should_log_compact_event() == 0)) {
        return;
    }
    db_infof(
        BACKEND_NAME,
        "snake dirty_mode_compact_fallback frame[%u] reason=%s frame_mode=%s "
        "compact_blocks=%zu damage_blocks=%zu "
        "requested_backbuffer_draw_full=%d",
        g_state.frame.frame_index, reason,
        (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED)
            ? "full_recovery_required"
            : "compact",
        compact_block_count, damage_block_count,
        g_state.runtime.backbuffer_draw_full);
}

static void db_gl1_record_compact_health(
    int dirty_backbuffer_mode, int used_compact_draw, int used_fallback_draw,
    db_gl1_snake_frame_mode_t frame_mode, size_t compact_block_count,
    size_t damage_block_count, const char *fallback_reason) {
    if (dirty_backbuffer_mode == 0) {
        return;
    }
    g_state.snake_compact_health.attempt_frames++;
    if (used_compact_draw != 0) {
        g_state.snake_compact_health.success_frames++;
    }
    if (used_fallback_draw != 0) {
        g_state.snake_compact_health.fallback_frames++;
        db_gl1_log_compact_fallback_event(fallback_reason, frame_mode,
                                          compact_block_count,
                                          damage_block_count);
    }
    if (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) {
        g_state.snake_compact_health.full_recovery_frames++;
    }

    if (db_gl1_should_log_compact_event() == 0) {
        return;
    }
    const uint64_t attempts = g_state.snake_compact_health.attempt_frames;
    const uint64_t fallbacks = g_state.snake_compact_health.fallback_frames;
    const uint64_t successes = g_state.snake_compact_health.success_frames;
    if (attempts == 0U) {
        return;
    }
    if ((attempts >= 16U) && (fallbacks > (attempts / 2U))) {
        db_infof(
            BACKEND_NAME,
            "snake dirty_mode_compact_health frame[%u] compact_success=%llu "
            "compact_fallback=%llu full_recovery=%llu attempts=%llu",
            g_state.frame.frame_index, (unsigned long long)successes,
            (unsigned long long)fallbacks,
            (unsigned long long)
                g_state.snake_compact_health.full_recovery_frames,
            (unsigned long long)attempts);
    }
}

static void db_gl1_invalidate_array_pointer_cache(void) {
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
}

static void db_gl1_emit_gradient_row_block(uint32_t row_start,
                                           uint32_t row_count,
                                           const double *row_rgb,
                                           void *user_data) {
    db_gl1_gradient_rect_emit_ctx_t *ctx =
        (db_gl1_gradient_rect_emit_ctx_t *)user_data;
    if ((ctx == NULL) || (row_count == 0U) ||
        (ctx->rect_count >= ctx->rect_capacity) || (row_rgb == NULL)) {
        return;
    }
    const uint32_t row_end = db_checked_add_u32(
        BACKEND_NAME, "gradient_row_end", row_start, row_count);
    const uint32_t viewport_h_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_h", ctx->viewport_h);
    const float y0 =
        ctx->can_use_cached_rows
            ? g_state.gradient_row_y_ndc[row_end]
            : db_pixel_coord_to_ndc_f32(
                  viewport_h_u32 - db_grid_axis_edge_to_pixel_coord(
                                       ctx->rows, row_end, viewport_h_u32),
                  viewport_h_u32);
    const float y1 =
        ctx->can_use_cached_rows
            ? g_state.gradient_row_y_ndc[row_start]
            : db_pixel_coord_to_ndc_f32(
                  viewport_h_u32 - db_grid_axis_edge_to_pixel_coord(
                                       ctx->rows, row_start, viewport_h_u32),
                  viewport_h_u32);
    float row_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
    db_rgb_f64_to_f32_rgb3(row_rgb, row_rgb_f32);
    const size_t base = ctx->rect_count * ctx->rect_float_count;
    float *const unit = &ctx->dst_vertices[base];
    db_fill_rect_unit_pos(unit, -1.0F, y0, 1.0F, y1, ctx->stride);
    db_set_rect_unit_rgb(unit, ctx->stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                         row_rgb_f32);
    if (g_state.is_es_context != 0) {
        db_set_rect_unit_alpha(unit, ctx->stride, DB_GL_COLOR_A_OFFSET, 1.0F);
    }
    ctx->rect_count++;
}

static int db_gl1_has_snake_color_state(void) {
    return (g_state.snake_color_state != NULL) &&
           (g_state.snake_color_capacity >=
            ((size_t)g_state.runtime.work_unit_count *
             DB_GL_COLOR_COMPONENT_COUNT));
}

static int db_gl1_shadow_backing_uses_rgba16f(void) {
    return (g_state.snake_shadow_backing_format ==
            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
               ? 1
               : 0;
}

static db_gl1_rect_bounds_t db_gl1_rect_bounds_from_grid_tile(size_t row,
                                                              size_t col) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    db_gl1_rect_bounds_t bounds = {0.0F, 0.0F, 0.0F, 0.0F};
    if ((cols == 0U) || (rows == 0U)) {
        return bounds;
    }
    const double inv_cols = 1.0 / (double)cols;
    const double inv_rows = 1.0 / (double)rows;
    bounds.x0 = db_double_to_f32((2.0 * (double)col * inv_cols) - 1.0);
    bounds.x1 = db_double_to_f32((2.0 * (double)(col + 1U) * inv_cols) - 1.0);
    bounds.y1 = db_double_to_f32(1.0 - (2.0 * (double)row * inv_rows));
    bounds.y0 = db_double_to_f32(1.0 - (2.0 * (double)(row + 1U) * inv_rows));
    return bounds;
}

static void db_gl1_emit_snake_compact_rect(float *dst_unit, size_t stride,
                                           size_t row_start, size_t row_count,
                                           size_t col_start, size_t col_count,
                                           const float *tile_color) {
    const db_gl1_rect_bounds_t left_bounds =
        db_gl1_rect_bounds_from_grid_tile(row_start, col_start);
    const db_gl1_rect_bounds_t right_bounds = db_gl1_rect_bounds_from_grid_tile(
        row_start, col_start + col_count - 1U);
    const db_gl1_rect_bounds_t bottom_bounds =
        db_gl1_rect_bounds_from_grid_tile(row_start + row_count - 1U,
                                          col_start);
    db_fill_rect_unit_pos(dst_unit, left_bounds.x0, bottom_bounds.y0,
                          right_bounds.x1, left_bounds.y1, stride);
    db_set_rect_unit_rgb(dst_unit, stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                         tile_color);
    if (g_state.is_es_context != 0) {
        db_set_rect_unit_alpha(dst_unit, stride, DB_GL_COLOR_A_OFFSET, 1.0F);
    }
}

static void db_gl1_get_snake_color_bits(uint32_t row, uint32_t col,
                                        void *user_data, uint32_t *color_bits) {
    const size_t cols = (size_t)db_grid_cols_effective();
    const size_t tile_index = ((size_t)row * cols) + (size_t)col;
    const float *tile_color =
        &g_state.snake_color_state[tile_index * DB_GL_COLOR_COMPONENT_COUNT];
    (void)user_data;
    if (color_bits != NULL) {
        db_f32_rgb_to_bits_u32_rgb3(tile_color, color_bits);
    }
}

static float *db_gl1_snake_tile_color_ptr(uint32_t tile_index) {
    if (db_gl1_has_snake_color_state() == 0) {
        return NULL;
    }
    if ((size_t)tile_index >= (size_t)g_state.runtime.work_unit_count) {
        return NULL;
    }
    return &g_state.snake_color_state[(size_t)tile_index *
                                      DB_GL_COLOR_COMPONENT_COUNT];
}

static void db_gl1_init_snake_color_state_from_vertices(void) {
    if (db_gl1_has_snake_color_state() == 0) {
        return;
    }
    const float base_rgb[3] = {BENCH_GRID_PHASE0_R_F, BENCH_GRID_PHASE0_G_F,
                               BENCH_GRID_PHASE0_B_F};
    for (uint32_t tile_index = 0U; tile_index < g_state.runtime.work_unit_count;
         tile_index++) {
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color == NULL) {
            continue;
        }
        db_copy_f32_rgb3(tile_color, base_rgb);
    }
}

static int db_gl1_has_snake_shadow_pixels(void) {
    return ((db_gl1_has_snake_color_state() != 0) &&
            (g_state.snake_shadow_pixel_width > 0U) &&
            (g_state.snake_shadow_pixel_height > 0U) &&
            (g_state.snake_shadow_pixel_capacity >=
             ((size_t)g_state.snake_shadow_pixel_width *
              (size_t)g_state.snake_shadow_pixel_height)) &&
            (((db_gl1_shadow_backing_uses_rgba16f() != 0) &&
              (g_state.snake_shadow_rgba16f != NULL)) ||
             ((db_gl1_shadow_backing_uses_rgba16f() == 0) &&
              (g_state.snake_shadow_rgba8 != NULL))));
}

static void
db_gl1_shadow_fill_pixel_block_rgb_f64(const db_damage_block_t *block,
                                       const double *rgb) {
    if ((block == NULL) || (rgb == NULL) || (block->row_count == 0U) ||
        (block->col_count == 0U) || (g_state.snake_shadow_pixel_width == 0U) ||
        (g_state.snake_shadow_pixel_height == 0U)) {
        return;
    }
    db_rgb_pixels_fill_damage_block_f64(
        g_state.snake_shadow_pixel_width, g_state.snake_shadow_pixel_height,
        g_state.snake_shadow_rgba8, g_state.snake_shadow_rgba16f,
        db_gl1_shadow_backing_uses_rgba16f(), block->row_start,
        block->row_count, block->col_start, block->col_count, rgb);
}

static size_t db_gl1_next_shadow_pixel_capacity(size_t required_capacity) {
    return db_size_grow_capacity_3_2(g_state.snake_shadow_pixel_capacity,
                                     required_capacity, required_capacity);
}

static void db_gl1_reserve_shadow_framebuffer_capacity(size_t pixel_capacity) {
    const size_t pixel_alloc_capacity =
        db_gl1_next_shadow_pixel_capacity(pixel_capacity);
    if (db_gl1_shadow_backing_uses_rgba16f() != 0) {
        db_reserve_array_capacity_or_fail(
            (void **)&g_state.snake_shadow_rgba16f,
            &g_state.snake_shadow_pixel_capacity, pixel_alloc_capacity * 4U,
            pixel_alloc_capacity * 4U, sizeof(uint16_t), 0U, BACKEND_NAME,
            "snake_shadow_rgba16f");
        g_state.snake_shadow_pixel_capacity = pixel_alloc_capacity;
        return;
    }
    db_reserve_array_capacity_or_fail((void **)&g_state.snake_shadow_rgba8,
                                      &g_state.snake_shadow_pixel_capacity,
                                      pixel_alloc_capacity,
                                      pixel_alloc_capacity, sizeof(uint32_t),
                                      0U, BACKEND_NAME, "snake_shadow_rgba8");
}

static void db_gl1_ensure_shadow_framebuffer_capacity(uint32_t pixel_width,
                                                      uint32_t pixel_height) {
    const size_t pixel_capacity = (size_t)pixel_width * (size_t)pixel_height;
    if ((pixel_width == 0U) || (pixel_height == 0U) || (pixel_capacity == 0U)) {
        return;
    }
    db_gl_shadow_present_prepare_texture(
        &g_state.snake_shadow_present, BACKEND_NAME, pixel_width, pixel_height);
    if (g_state.snake_shadow_present_logged == 0) {
        db_gl_shadow_present_log_decision(BACKEND_NAME, "snake shadow present",
                                          1, 0, &g_state.snake_shadow_present);
        g_state.snake_shadow_present_logged = 1;
    }
    const int need_shadow_backing =
        ((db_gl1_shadow_backing_uses_rgba16f() != 0)
             ? (g_state.snake_shadow_rgba16f == NULL)
             : (g_state.snake_shadow_rgba8 == NULL));
    if ((need_shadow_backing != 0) ||
        (g_state.snake_shadow_pixel_capacity < pixel_capacity)) {
        db_gl1_reserve_shadow_framebuffer_capacity(pixel_capacity);
        g_state.snake_shadow_present.backing_valid = 0;
        g_state.snake_shadow_present.texture_valid = 0;
        g_state.snake_shadow_present.texture_needs_full_upload = 1;
    }
    g_state.snake_shadow_pixel_width = pixel_width;
    g_state.snake_shadow_pixel_height = pixel_height;
}

static void db_gl1_rebuild_shadow_framebuffer_full(uint32_t pixel_width,
                                                   uint32_t pixel_height) {
    if ((db_gl1_has_snake_color_state() == 0) ||
        (g_state.runtime.work_unit_count == 0U)) {
        return;
    }
    db_gl1_ensure_shadow_framebuffer_capacity(pixel_width, pixel_height);
    if (db_gl1_has_snake_shadow_pixels() == 0) {
        return;
    }
    const uint32_t cols = db_grid_cols_effective();
    if (cols == 0U) {
        return;
    }
    for (uint32_t tile_index = 0U; tile_index < g_state.runtime.work_unit_count;
         tile_index++) {
        db_damage_block_t pixel_block = {0U, 0U, 0U, 0U};
        if (db_grid_tile_to_pixel_block(
                db_grid_cols_effective(), db_grid_rows_effective(), tile_index,
                pixel_width, pixel_height, &pixel_block) == 0) {
            continue;
        }
        const float *tile_color =
            &g_state.snake_color_state[(size_t)tile_index *
                                       DB_GL_COLOR_COMPONENT_COUNT];
        const double tile_color_f64[3] = {(double)tile_color[0],
                                          (double)tile_color[1],
                                          (double)tile_color[2]};
        db_gl1_shadow_fill_pixel_block_rgb_f64(&pixel_block, tile_color_f64);
    }
    g_state.snake_shadow_present.backing_valid = 1;
    g_state.snake_shadow_present.texture_valid = 0;
    g_state.snake_shadow_present.texture_needs_full_upload = 1;
}

static void
db_gl1_apply_snake_step_to_shadow(const db_gl1_snake_frame_state_t *snake_frame,
                                  const db_snake_shape_cache_t *shape_cache_ptr,
                                  uint32_t pixel_width, uint32_t pixel_height) {
    if ((snake_frame == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }
    const db_snake_active_tile_scratch_t scratch = {
        .active_tile_indices = g_state.snake_scratch.shape.active_tile_indices,
        .active_tile_valid = g_state.snake_scratch.shape.active_tile_valid,
        .active_prior_rgb = g_state.snake_scratch.shape.active_prior_rgb,
        .active_tile_capacity =
            g_state.snake_scratch.shape.active_tile_capacity,
    };
    const db_snake_rgb_sink_t sink = {
        .kind = DB_SNAKE_RGB_SINK_PIXEL_SURFACE_PROJECTED,
        .logical_cols = db_grid_cols_effective(),
        .logical_rows = db_grid_rows_effective(),
        .pixel_surface =
            {
                .pixel_width = pixel_width,
                .pixel_height = pixel_height,
                .pixels_rgba8 = g_state.snake_shadow_rgba8,
                .pixels_rgba16f = g_state.snake_shadow_rgba16f,
                .uses_rgba16f = db_gl1_shadow_backing_uses_rgba16f(),
            },
        .tile_rgb_f32 = NULL,
        .tile_count = 0U,
    };
    db_snake_emit_step_rgb(
        &snake_frame->plan, &snake_frame->target.region, shape_cache_ptr,
        snake_frame->target.target_rgb,
        snake_frame->target.force_full_fill_on_phase_complete, &scratch, &sink);
}

static void db_gl1_update_shadow_framebuffer_from_snake_step(
    const db_gl1_snake_frame_state_t *snake_frame, uint32_t pixel_width,
    uint32_t pixel_height) {
    if ((snake_frame == NULL) || (pixel_width == 0U) || (pixel_height == 0U)) {
        return;
    }
    db_gl1_ensure_shadow_framebuffer_capacity(pixel_width, pixel_height);
    if (db_gl1_has_snake_shadow_pixels() == 0) {
        return;
    }
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (g_state.runtime_flags.is_snake_shapes != 0) {
        if ((g_state.snake_scratch.shape.row_bounds != NULL) &&
            (db_snake_shape_cache_init_from_index(
                 &shape_cache, g_state.snake_scratch.shape.row_bounds,
                 g_state.snake_scratch.shape.row_bounds_capacity,
                 g_state.runtime.pattern_seed,
                 snake_frame->plan.active_shape_index, DB_U32_SALT_PALETTE,
                 &snake_frame->target.region,
                 snake_frame->target.shape_kind) != 0)) {
            shape_cache_ptr = &shape_cache;
        }
    }
    db_gl1_apply_snake_step_to_shadow(snake_frame, shape_cache_ptr, pixel_width,
                                      pixel_height);
    g_state.snake_shadow_present.backing_valid = 1;
    g_state.snake_shadow_present.texture_valid = 0;
    g_state.snake_shadow_present.texture_needs_full_upload = 0;
    g_state.snake_shadow_stats.backing_incremental_frames++;
}

static size_t db_gl1_build_shadow_upload_blocks_from_damage_blocks(
    const db_grid_block_t *damage_blocks, size_t damage_block_count,
    uint32_t pixel_width, uint32_t pixel_height) {
    if ((damage_blocks == NULL) ||
        (g_state.snake_shadow_upload_blocks == NULL)) {
        return 0U;
    }
    if (damage_block_count > g_state.snake_shadow_upload_block_capacity) {
        return 0U;
    }
    size_t out_count = 0U;
    for (size_t i = 0U; i < damage_block_count; i++) {
        if (out_count >= g_state.snake_shadow_upload_block_capacity) {
            return 0U;
        }
        if (db_grid_block_to_pixel_block(
                db_grid_cols_effective(), db_grid_rows_effective(),
                &damage_blocks[i], pixel_width, pixel_height,
                &g_state.snake_shadow_upload_blocks[out_count]) != 0) {
            out_count++;
        }
    }
    return out_count;
}

static int db_gl1_draw_shadow_framebuffer_once(const db_damage_block_t *blocks,
                                               size_t block_count,
                                               uint32_t pixel_width,
                                               uint32_t pixel_height) {
    db_gl1_ensure_shadow_framebuffer_capacity(pixel_width, pixel_height);
    if (db_gl1_has_snake_shadow_pixels() == 0) {
        return 0;
    }
    (void)db_gl_bind_array_buffer_cached(0U,
                                         &g_state.buffers.bound_array_buffer);
    const db_gl_shadow_present_frame_t present_frame = {
        .state = &g_state.snake_shadow_present,
        .backend = BACKEND_NAME,
        .pixel_width = pixel_width,
        .pixel_height = pixel_height,
        .selected_pixels = (db_gl1_shadow_backing_uses_rgba16f() != 0)
                               ? (const void *)g_state.snake_shadow_rgba16f
                               : (const void *)g_state.snake_shadow_rgba8,
        .damage_blocks = blocks,
        .damage_block_count = block_count,
        .prepare_upload_target_fn = NULL,
        .prepare_upload_target_user_data = NULL,
    };
    db_gl_shadow_present_frame(&present_frame);
    db_gl1_invalidate_array_pointer_cache();
    return 1;
}

static void db_gl1_refresh_tile_positions_for_viewport(int viewport_w,
                                                       int viewport_h) {
    if ((viewport_w <= 0) || (viewport_h <= 0) ||
        (g_state.vertex.vertices == NULL)) {
        return;
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t tile_index = 0U; tile_index < g_state.vertex.work_unit_count;
         tile_index++) {
        db_damage_block_t pixel_block = {0U, 0U, 0U, 0U};
        if (db_grid_tile_to_pixel_block(
                cols, rows, tile_index, (uint32_t)viewport_w,
                (uint32_t)viewport_h, &pixel_block) == 0) {
            continue;
        }
        const uint32_t x0_px = pixel_block.col_start;
        const uint32_t x1_px =
            db_damage_block_col_end_or_fail("gl1_tile_x1_px", &pixel_block);
        const uint32_t viewport_h_u32 =
            db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
        const uint32_t y0_px =
            viewport_h_u32 -
            db_damage_block_row_end_or_fail("gl1_tile_y0_px", &pixel_block);
        const uint32_t y1_px = viewport_h_u32 - pixel_block.row_start;

        const float x0 = db_pixel_coord_to_ndc_f32(
            x0_px,
            db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w));
        const float x1 = db_pixel_coord_to_ndc_f32(
            x1_px,
            db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w));
        const float y0 = db_pixel_coord_to_ndc_f32(y0_px, viewport_h_u32);
        const float y1 = db_pixel_coord_to_ndc_f32(y1_px, viewport_h_u32);

        const size_t base = (size_t)tile_index * DB_RECT_VERTEX_COUNT *
                            g_state.vertex.vertex_stride;
        db_fill_rect_unit_pos(&g_state.vertex.vertices[base], x0, y0, x1, y1,
                              g_state.vertex.vertex_stride);
    }

    if (g_state.buffers.vbo != 0U) {
        const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
        db_gl_upload_buffer(g_state.vertex.vertices, upload_bytes,
                            g_state.vertex.upload.use_persistent_upload,
                            g_state.vertex.upload.persistent_mapped_ptr,
                            g_state.vertex.upload.use_map_range_upload,
                            g_state.vertex.upload.use_map_buffer_upload);
    }
}

static void db_gl1_refresh_gradient_row_ndc_cache(int viewport_h) {
    if ((viewport_h <= 0) || (g_state.gradient_row_y_ndc == NULL)) {
        return;
    }
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t viewport_h_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
    if (rows == 0U) {
        return;
    }
    if ((g_state.gradient_row_y_ndc_rows == rows) &&
        (g_state.gradient_row_y_ndc_viewport_h == viewport_h)) {
        return;
    }
    for (uint32_t row = 0U; row <= rows; row++) {
        const uint32_t y_top_px =
            db_grid_axis_edge_to_pixel_coord(rows, row, viewport_h_u32);
        const uint32_t y_px = viewport_h_u32 - y_top_px;
        g_state.gradient_row_y_ndc[row] =
            db_pixel_coord_to_ndc_f32(y_px, viewport_h_u32);
    }
    g_state.gradient_row_y_ndc_rows = rows;
    g_state.gradient_row_y_ndc_viewport_h = viewport_h;
}

static void db_gl1_refresh_bands_x_cache(uint32_t cols, uint32_t band_count,
                                         int viewport_w) {
    if ((viewport_w <= 0) || (cols == 0U) || (band_count == 0U) ||
        (band_count > BENCH_BANDS)) {
        return;
    }
    if ((g_state.bands_x_cache_viewport_w == viewport_w) &&
        (g_state.bands_x_cache_cols == cols) &&
        (g_state.bands_x_cache_count == band_count)) {
        return;
    }
    for (uint32_t band = 0U; band <= band_count; band++) {
        const uint32_t tile_x = (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                                           (uint64_t)band_count);
        int x_px = (int)db_grid_axis_edge_to_pixel_coord(cols, tile_x,
                                                         (uint32_t)viewport_w);
        if (band == band_count) {
            x_px = viewport_w;
        }
        if (x_px < 0) {
            x_px = 0;
        }
        if (x_px > viewport_w) {
            x_px = viewport_w;
        }
        g_state.bands_x_cache_px[band] = x_px;
        g_state.bands_x_cache_ndc[band] =
            db_pixel_coord_to_ndc_f32((uint32_t)x_px, (uint32_t)viewport_w);
    }
    g_state.bands_x_cache_viewport_w = viewport_w;
    g_state.bands_x_cache_cols = cols;
    g_state.bands_x_cache_count = band_count;
}

static void db_gl1_seed_backbuffer_clear_cb(const float *rgba,
                                            void *user_data) {
    (void)user_data;
    if (rgba == NULL) {
        return;
    }
    db_gl_clear_color_rgba(rgba[0], rgba[1], rgba[2], 1.0F);
    db_gl_clear_color_buffer();
}

static void db_gl1_refresh_capability_mode(void) {
    const int uses_history_draw =
        (g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.runtime_flags.uses_dirty_backbuffer_mode != 0);
    const char *draw_mode = db_gl_capability_mode_draw_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode, uses_history_draw, 0);
    const char *upload_mode = db_gl_capability_mode_upload_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode,
        db_gl_capability_mode_upload_from_probe(
            (g_state.buffers.vbo != 0U) ? 1 : 0, &g_state.vertex.upload));
    db_gl_capability_mode_compose(
        g_state.capability_mode, sizeof(g_state.capability_mode), draw_mode,
        upload_mode, db_runtime_backbuffer_replay_enabled(&g_state.runtime));
}

static void db_gl1_log_backbuffer_strategy(void) {
    if (g_state.runtime_flags.is_snake_history_texture == 0) {
        return;
    }
    if (g_state.runtime.backbuffer_draw_full != 0) {
        db_infof(BACKEND_NAME,
                 "snake draw uses full redraw mode; preserved-backbuffer "
                 "replay is disabled");
        return;
    }
    if (db_runtime_backbuffer_replay_enabled(&g_state.runtime) != 0) {
        db_infof(BACKEND_NAME,
                 "snake draw uses preserved-backbuffer replay for dirty "
                 "frames");
        return;
    }
    db_infof(BACKEND_NAME,
             "snake draw uses CPU shadow framebuffer when preserved-backbuffer "
             "replay is disabled");
}

static size_t db_gl1_snake_compact_rect_capacity(void) {
    return DB_GL1_SNAKE_COMPACT_RECT_LIMIT;
}

static int db_gl1_init_runtime_metadata_only(
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride) {
    if (runtime_state == NULL) {
        return 0;
    }

    g_state.runtime = *runtime_state;
    g_state.vertex = (db_gl_vertex_init_t){
        .vertices = NULL,
        .vertex_stride = vertex_stride,
        .pattern = runtime_state->pattern,
        .work_unit_count = runtime_state->work_unit_count,
        .draw_vertex_count = runtime_state->draw_vertex_count,
        .upload = (db_gl_upload_probe_result_t){0},
    };
    return 1;
}

static int
db_init_vertices_for_mode(const db_benchmark_runtime_init_t *runtime_state,
                          size_t vertex_stride) {
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_runtime_common_with_stride(
            BACKEND_NAME, &init_state, runtime_state, vertex_stride)) {
        return 0;
    }

    g_state.vertex = init_state;
    g_state.runtime = *runtime_state;
    return 1;
}

static void db_render_snake_step(const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 uint32_t shape_kind, uint32_t pattern_seed,
                                 uint32_t shape_index, const double *target_rgb,
                                 int force_full_fill_on_phase_complete) {
    if ((plan == NULL) || (region == NULL) || (target_rgb == NULL)) {
        return;
    }
    if (plan->batch_size > BENCH_SNAKE_PHASE_WINDOW_TILES) {
        failf("snake batch size %u exceeds BENCH_SNAKE_PHASE_WINDOW_TILES=%u",
              plan->batch_size, BENCH_SNAKE_PHASE_WINDOW_TILES);
    }
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (g_state.runtime_flags.is_snake_shapes != 0) {
        if ((g_state.snake_scratch.shape.row_bounds != NULL) &&
            (db_snake_shape_cache_init_from_index(
                 &shape_cache, g_state.snake_scratch.shape.row_bounds,
                 g_state.snake_scratch.shape.row_bounds_capacity, pattern_seed,
                 shape_index, DB_U32_SALT_PALETTE, region,
                 (db_snake_shape_kind_t)shape_kind) != 0)) {
            shape_cache_ptr = &shape_cache;
        }
    }
    const db_snake_active_tile_scratch_t scratch = {
        .active_tile_indices = g_state.snake_scratch.shape.active_tile_indices,
        .active_tile_valid = g_state.snake_scratch.shape.active_tile_valid,
        .active_prior_rgb = g_state.snake_scratch.shape.active_prior_rgb,
        .active_tile_capacity =
            g_state.snake_scratch.shape.active_tile_capacity,
    };
    const db_snake_rgb_sink_t sink = {
        .kind = DB_SNAKE_RGB_SINK_TILE_RGB_F32,
        .logical_cols = db_grid_cols_effective(),
        .logical_rows = db_grid_rows_effective(),
        .pixel_surface = {0},
        .tile_rgb_f32 = g_state.snake_color_state,
        .tile_count = g_state.runtime.work_unit_count,
    };
    db_snake_emit_step_rgb(plan, region, shape_cache_ptr, target_rgb,
                           force_full_fill_on_phase_complete, &scratch, &sink);
}

static size_t db_gl1_build_gradient_compact_row_vertices(
    const db_grid_block_t *dirty_blocks, size_t dirty_count, uint32_t head_row,
    int direction_down, uint32_t cycle_index, int viewport_w, int viewport_h,
    float *dst_vertices, size_t dst_float_capacity) {
    if ((dirty_blocks == NULL) || (dirty_count == 0U) || (viewport_w <= 0) ||
        (viewport_h <= 0) || (dst_vertices == NULL) ||
        (dst_float_capacity == 0U)) {
        return 0U;
    }
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return 0U;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if (rect_float_count == 0U) {
        return 0U;
    }
    const size_t rect_capacity = dst_float_capacity / rect_float_count;
    if (rect_capacity == 0U) {
        return 0U;
    }
    const int can_use_cached_rows =
        (g_state.gradient_row_y_ndc != NULL) &&
        (g_state.gradient_row_y_ndc_rows == rows) &&
        (g_state.gradient_row_y_ndc_viewport_h == viewport_h);

    db_gl1_gradient_rect_emit_ctx_t emit_ctx = {
        .dst_vertices = dst_vertices,
        .rect_capacity = rect_capacity,
        .rect_count = 0U,
        .rect_float_count = rect_float_count,
        .stride = stride,
        .viewport_h = viewport_h,
        .rows = rows,
        .can_use_cached_rows = can_use_cached_rows,
    };
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_blocks[i].row_start;
        const uint32_t row_count_raw = dirty_blocks[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= rows) ||
            (emit_ctx.rect_count >= rect_capacity)) {
            continue;
        }
        const uint32_t row_end =
            db_u32_min(rows, db_checked_add_u32(BACKEND_NAME, "row_end",
                                                row_start, row_count_raw));
        if (row_end <= row_start) {
            continue;
        }
        const db_grid_block_t block = {
            .row_start = row_start,
            .row_count = row_end - row_start,
            .col_start = dirty_blocks[i].col_start,
            .col_count = dirty_blocks[i].col_count,
        };
        db_gradient_row_segment_iter_t iter = {0};
        db_gradient_row_segment_t segment = {0};
        if (db_gradient_row_segment_iter_init(&block, head_row, direction_down,
                                              cycle_index, &iter) == 0) {
            continue;
        }
        while (db_gradient_row_segment_iter_next(&iter, &segment) != 0) {
            db_gl1_emit_gradient_row_block(segment.block.row_start,
                                           segment.block.row_count, segment.rgb,
                                           &emit_ctx);
            if (emit_ctx.rect_count >= rect_capacity) {
                break;
            }
        }
        if (emit_ctx.rect_count >= rect_capacity) {
            break;
        }
    }
    return emit_ctx.rect_count;
}

static void db_gl1_configure_client_arrays_if_needed(void) {
    if (g_state.client_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(0U,
                                         &g_state.buffers.bound_array_buffer);

    const int client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int client_color_components = (g_state.is_es_context != 0)
                                            ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                            : DB_VERTEX_COLOR_FLOAT_COUNT;

    db_gl_set_vertex_pointer_2f(client_stride, &g_state.vertex.vertices[0]);
    db_gl_set_color_pointer_f(
        client_color_components, client_stride,
        &g_state.vertex.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);

    g_state.client_arrays_configured = 1;
    g_state.vbo_arrays_configured = 0;
}

static void db_gl1_configure_vbo_arrays_if_needed(void) {
    if (g_state.buffers.vbo == 0U || g_state.vbo_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(g_state.buffers.vbo,
                                         &g_state.buffers.bound_array_buffer);

    const int vbo_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int vbo_color_components = (g_state.is_es_context != 0)
                                         ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                         : DB_VERTEX_COLOR_FLOAT_COUNT;

    db_gl_set_vertex_pointer_2f(vbo_stride, db_gl_vbo_offset_ptr(0U));
    db_gl_set_color_pointer_f(
        vbo_color_components, vbo_stride,
        db_gl_vbo_offset_ptr(sizeof(float) * DB_VERTEX_POSITION_FLOAT_COUNT));

    g_state.vbo_arrays_configured = 1;
    g_state.client_arrays_configured = 0;
}

static void db_gl1_draw_compact_scratch_vbo(const char *first_vertex_label,
                                            size_t draw_vertex_count) {
    if ((first_vertex_label == NULL) || (draw_vertex_count == 0U)) {
        return;
    }
    db_gl1_configure_vbo_arrays_if_needed();
    db_gl_draw_arrays_triangles(
        db_checked_u32_to_i32(
            BACKEND_NAME, first_vertex_label,
            db_checked_size_to_u32(BACKEND_NAME, first_vertex_label,
                                   g_state.compact_vbo.first_vertex)),
        db_gl_draw_vertex_count_i32(BACKEND_NAME, draw_vertex_count));
}

static void db_gl1_draw_compact_scratch_client(size_t draw_vertex_count) {
    if (draw_vertex_count == 0U) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(0U,
                                         &g_state.buffers.bound_array_buffer);
    const int client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const int client_color_components = (g_state.is_es_context != 0)
                                            ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                            : DB_VERTEX_COLOR_FLOAT_COUNT;
    db_gl_set_vertex_pointer_2f(client_stride,
                                &g_state.compact_vbo.scratch_vertices[0]);
    db_gl_set_color_pointer_f(
        client_color_components, client_stride,
        &g_state.compact_vbo.scratch_vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
    db_gl_draw_arrays_triangles(
        0, db_gl_draw_vertex_count_i32(BACKEND_NAME, draw_vertex_count));
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    db_gl1_invalidate_array_pointer_cache();
}

static int db_gl1_draw_compact_blocks_from_snake_colors_once(
    const db_snake_compact_block_t *rects, size_t rect_count) {
    if ((rects == NULL) || (rect_count == 0U) ||
        (g_state.compact_vbo.scratch_vertices == NULL) ||
        (g_state.compact_vbo.vbo_capacity_bytes == 0U) ||
        (db_gl1_has_snake_color_state() == 0)) {
        db_gl1_log_compact_reject("compact_snake_ranges", "precondition",
                                  rect_count, 0U, 0U);
        return 0;
    }

    const size_t stride = g_state.vertex.vertex_stride;
    const size_t bytes_per_vertex = stride * sizeof(float);
    const size_t tile_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    const size_t tile_bytes = db_rect_tile_bytes(stride);
    if ((bytes_per_vertex == 0U) || (tile_float_count == 0U)) {
        db_gl1_log_compact_reject("compact_snake_ranges", "zero_stride",
                                  rect_count, bytes_per_vertex,
                                  tile_float_count);
        return 0;
    }
    if (tile_bytes == 0U) {
        db_gl1_log_compact_reject("compact_snake_ranges", "zero_tile_bytes",
                                  rect_count, 0U, 0U);
        return 0;
    }
    const size_t rect_capacity =
        g_state.compact_vbo.scratch_float_capacity / tile_float_count;
    if (rect_capacity == 0U) {
        db_gl1_log_compact_reject("compact_snake_ranges", "zero_rect_capacity",
                                  rect_count, 0U, 0U);
        return 0;
    }
    const size_t capped_rect_capacity =
        (rect_capacity < DB_GL1_SNAKE_COMPACT_RECT_LIMIT)
            ? rect_capacity
            : DB_GL1_SNAKE_COMPACT_RECT_LIMIT;
    if (rect_count > capped_rect_capacity) {
        db_gl1_log_compact_reject("compact_snake_ranges",
                                  "rect_capacity_collect_failed", rect_count,
                                  rect_count, capped_rect_capacity);
        return 0;
    }
    for (size_t i = 0U; i < rect_count; i++) {
        const db_snake_compact_block_t *rect = &rects[i];
        const size_t tile_index =
            (rect->row_start * (size_t)db_grid_cols_effective()) +
            rect->col_start;
        const float *tile_color =
            &g_state
                 .snake_color_state[tile_index * DB_GL_COLOR_COMPONENT_COUNT];
        const size_t dst_base = i * tile_float_count;
        db_gl1_emit_snake_compact_rect(
            &g_state.compact_vbo.scratch_vertices[dst_base], stride,
            rect->row_start, rect->row_count, rect->col_start, rect->col_count,
            tile_color);
    }
    const size_t compact_vertex_count =
        rect_count * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = compact_vertex_count * bytes_per_vertex;
    DB_LOG_CAPACITY_EXCEEDED_ONCE(BACKEND_NAME, "compact_bytes", compact_bytes,
                                  g_state.compact_vbo.vbo_capacity_bytes);
    if ((compact_bytes == 0U) ||
        (compact_bytes > g_state.compact_vbo.vbo_capacity_bytes)) {
        db_gl1_log_compact_reject("compact_snake_ranges", "compact_bytes",
                                  rect_count, compact_bytes,
                                  g_state.compact_vbo.vbo_capacity_bytes);
        return 0;
    }

    if ((g_state.buffers.vbo != 0U) &&
        (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                       &g_state.vertex.upload,
                                       compact_bytes) != 0)) {
        db_gl1_draw_compact_scratch_vbo("snake_compact_first_vertex",
                                        compact_vertex_count);
        return 1;
    }

    db_gl1_draw_compact_scratch_client(compact_vertex_count);
    return 1;
}

static void db_gl1_draw_gradient_dirty_blocks_mesh(
    const db_grid_block_t *dirty_blocks, size_t dirty_count, uint32_t head_row,
    int direction_down, uint32_t cycle_index, int viewport_w, int viewport_h) {
    if ((viewport_w <= 0) || (viewport_h <= 0) ||
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        return;
    }

    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if ((rect_float_count == 0U) ||
        (g_state.compact_vbo.scratch_float_capacity < rect_float_count)) {
        return;
    }
    const size_t rect_count = db_gl1_build_gradient_compact_row_vertices(
        dirty_blocks, dirty_count, head_row, direction_down, cycle_index,
        viewport_w, viewport_h, g_state.compact_vbo.scratch_vertices,
        g_state.compact_vbo.scratch_float_capacity);
    if (rect_count == 0U) {
        return;
    }
    const size_t draw_vertex_count = rect_count * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = draw_vertex_count * stride * sizeof(float);

    if (g_state.buffers.vbo != 0U) {
        if (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                          &g_state.vertex.upload,
                                          compact_bytes) == 0) {
            return;
        }
        db_gl1_draw_compact_scratch_vbo("gradient_compact_first_vertex",
                                        draw_vertex_count);
    } else {
        db_gl1_draw_compact_scratch_client(draw_vertex_count);
    }
}

static void db_gl1_draw_bands_compact(uint32_t cols, uint32_t band_count,
                                      uint32_t frame_index, int viewport_w,
                                      int viewport_h) {
    if ((cols == 0U) || (band_count == 0U) || (viewport_w <= 0) ||
        (viewport_h <= 0) || (g_state.compact_vbo.scratch_vertices == NULL)) {
        return;
    }
    const size_t stride = g_state.vertex.vertex_stride;
    const size_t rect_float_count = (size_t)DB_RECT_VERTEX_COUNT * stride;
    if ((rect_float_count == 0U) ||
        ((size_t)band_count >
         (g_state.compact_vbo.scratch_float_capacity / rect_float_count))) {
        return;
    }
    db_gl1_refresh_bands_x_cache(cols, band_count, viewport_w);
    const int use_cached_x = (band_count <= BENCH_BANDS) &&
                             (g_state.bands_x_cache_viewport_w == viewport_w) &&
                             (g_state.bands_x_cache_cols == cols) &&
                             (g_state.bands_x_cache_count == band_count);
    const uint32_t viewport_w_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w);
    const uint32_t viewport_h_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
    const float y0_ndc = db_pixel_coord_to_ndc_f32(0U, viewport_h_u32);
    const float y1_ndc =
        db_pixel_coord_to_ndc_f32(viewport_h_u32, viewport_h_u32);

    size_t rect_count = 0U;
    for (uint32_t band = 0U; band < band_count; band++) {
        int x0 = 0;
        int x1 = 0;
        float x0_ndc = 0.0F;
        float x1_ndc = 0.0F;
        if (use_cached_x != 0) {
            x0 = g_state.bands_x_cache_px[band];
            x1 = g_state.bands_x_cache_px[band + 1U];
            x0_ndc = g_state.bands_x_cache_ndc[band];
            x1_ndc = g_state.bands_x_cache_ndc[band + 1U];
        } else {
            const uint32_t tile_x0 =
                (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                           (uint64_t)band_count);
            const uint32_t tile_x1 =
                (uint32_t)(((uint64_t)(band + 1U) * (uint64_t)cols) /
                           (uint64_t)band_count);
            x0 = (int)db_grid_axis_edge_to_pixel_coord(cols, tile_x0,
                                                       viewport_w_u32);
            x1 = (int)db_grid_axis_edge_to_pixel_coord(cols, tile_x1,
                                                       viewport_w_u32);
            if (band + 1U == band_count) {
                x1 = viewport_w;
            }
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > viewport_w) {
                x1 = viewport_w;
            }
            x0_ndc = db_pixel_coord_to_ndc_f32(
                db_checked_int_to_u32(BACKEND_NAME, "band_x0_px", x0),
                viewport_w_u32);
            x1_ndc = db_pixel_coord_to_ndc_f32(
                db_checked_int_to_u32(BACKEND_NAME, "band_x1_px", x1),
                viewport_w_u32);
        }
        const int rect_w = x1 - x0;
        if (rect_w <= 0) {
            continue;
        }

        double color_rgb[3] = {0.0, 0.0, 0.0};
        db_band_color_rgb3(band, band_count, frame_index, color_rgb);
        const size_t base = rect_count * rect_float_count;
        float *const unit = &g_state.compact_vbo.scratch_vertices[base];
        float color_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(color_rgb, color_rgb_f32);
        db_fill_rect_unit_pos(unit, x0_ndc, y0_ndc, x1_ndc, y1_ndc, stride);
        db_set_rect_unit_rgb(unit, stride, DB_VERTEX_POSITION_FLOAT_COUNT,
                             color_rgb_f32);
        if (g_state.is_es_context != 0) {
            db_set_rect_unit_alpha(unit, stride, DB_GL_COLOR_A_OFFSET, 1.0F);
        }
        rect_count++;
    }
    if (rect_count == 0U) {
        return;
    }

    const size_t draw_vertex_count = rect_count * (size_t)DB_RECT_VERTEX_COUNT;
    const size_t compact_bytes = draw_vertex_count * stride * sizeof(float);
    if ((g_state.buffers.vbo != 0U) &&
        (db_gl_upload_compact_prepared(&g_state.compact_vbo,
                                       &g_state.vertex.upload,
                                       compact_bytes) != 0)) {
        db_gl1_draw_compact_scratch_vbo("bands_compact_first_vertex",
                                        draw_vertex_count);
        return;
    }
    db_gl1_draw_compact_scratch_client(draw_vertex_count);
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);
    db_gl_set_dither_enabled(0);

    g_state.is_es_context = db_gl_is_es_context(db_gl_get_version_string());
    g_state.vertex.vertex_stride = (g_state.is_es_context != 0)
                                       ? DB_ES_VERTEX_FLOAT_STRIDE
                                       : DB_VERTEX_FLOAT_STRIDE;
    db_benchmark_runtime_init_t runtime_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &runtime_state)) {
        failf("failed to initialize benchmark runtime");
    }
    const db_history_pattern_mode_flags_t runtime_flags =
        db_history_runtime_mode_flags(&runtime_state);
    const int use_snake_compact_only_init =
        (runtime_flags.is_snake_history_texture != 0);
    if ((use_snake_compact_only_init != 0)
            ? (!db_gl1_init_runtime_metadata_only(&runtime_state,
                                                  g_state.vertex.vertex_stride))
            : (!db_init_vertices_for_mode(&runtime_state,
                                          g_state.vertex.vertex_stride))) {
        failf("failed to allocate benchmark vertex buffers");
    }
    g_state.snake_replay.prev_draw_blocks = NULL;
    g_state.snake_replay.prev_draw_block_count = 0U;
    g_state.snake_replay.draw_block_capacity = 0U;
    g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
    g_state.snake_scratch.damage.blocks = NULL;
    g_state.snake_scratch.damage.capacity = 0U;
    g_state.snake_scratch.compact.blocks = NULL;
    g_state.snake_scratch.compact.capacity = 0U;
    g_state.snake_scratch.shape.row_bounds = NULL;
    g_state.snake_scratch.shape.row_bounds_capacity = 0U;
    g_state.snake_color_state = NULL;
    g_state.snake_color_capacity = 0U;
    g_state.snake_shadow_rgba8 = NULL;
    g_state.snake_shadow_rgba16f = NULL;
    g_state.snake_shadow_pixel_capacity = 0U;
    g_state.snake_shadow_pixel_width = 0U;
    g_state.snake_shadow_pixel_height = 0U;
    g_state.snake_shadow_upload_blocks = NULL;
    g_state.snake_shadow_upload_block_capacity = 0U;
    g_state.snake_replay.prev_draw_blocks = NULL;
    g_state.snake_replay.prev_draw_block_count = 0U;
    g_state.snake_replay.draw_block_capacity = 0U;
    g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
    g_state.snake_shadow_present = (db_gl_shadow_present_state_t){0};
    g_state.snake_shadow_backing_format = DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8;
    g_state.snake_shadow_logged_fallback_mask = 0U;
    g_state.gradient_row_y_ndc = NULL;
    g_state.gradient_row_y_ndc_rows = 0U;
    g_state.gradient_row_y_ndc_viewport_h = 0;
    g_state.bands_x_cache_viewport_w = 0;
    g_state.bands_x_cache_cols = 0U;
    g_state.bands_x_cache_count = 0U;
    db_history_snake_backbuffer_state_reset(&g_state.snake_backbuffer_state, 0);
    g_state.runtime_flags = runtime_flags;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl_shadow_present_init_runtime(&g_state.snake_shadow_present, 1, 1);
        g_state.snake_shadow_backing_format =
            g_state.snake_shadow_present.selected_texture_format;
        g_state.snake_shadow_present_logged = 0;
        db_history_snake_active_cache_init(&g_state.snake_scratch, BACKEND_NAME,
                                           BENCH_SNAKE_PHASE_WINDOW_TILES,
                                           DB_GL_COLOR_COMPONENT_COUNT);
        g_state.snake_color_capacity = (size_t)g_state.runtime.work_unit_count *
                                       DB_GL_COLOR_COMPONENT_COUNT;
        g_state.snake_color_state = (float *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_color_state", g_state.snake_color_capacity,
            sizeof(float));
        db_gl1_init_snake_color_state_from_vertices();
        const size_t snake_damage_block_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        const size_t snake_compact_rect_capacity =
            db_gl1_snake_compact_rect_capacity();
        const size_t snake_shadow_upload_block_capacity =
            (snake_damage_block_capacity > snake_compact_rect_capacity)
                ? snake_damage_block_capacity
                : snake_compact_rect_capacity;
        g_state.snake_scratch.damage.blocks =
            (db_grid_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_damage_blocks",
                snake_damage_block_capacity,
                sizeof(*g_state.snake_scratch.damage.blocks));
        g_state.snake_scratch.compact.blocks =
            (db_snake_compact_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_compact_blocks",
                snake_compact_rect_capacity,
                sizeof(*g_state.snake_scratch.compact.blocks));
        g_state.snake_scratch.damage.capacity = snake_damage_block_capacity;
        g_state.snake_scratch.compact.capacity = snake_compact_rect_capacity;
        g_state.snake_replay.prev_draw_blocks =
            (db_snake_compact_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_prev_draw_blocks",
                snake_compact_rect_capacity,
                sizeof(*g_state.snake_replay.prev_draw_blocks));
        g_state.snake_replay.draw_block_capacity = snake_compact_rect_capacity;
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_shadow_upload_blocks =
            (db_damage_block_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_shadow_upload_blocks",
                snake_shadow_upload_block_capacity,
                sizeof(*g_state.snake_shadow_upload_blocks));
        g_state.snake_shadow_upload_block_capacity =
            snake_shadow_upload_block_capacity;
        if (g_state.runtime_flags.is_snake_shapes != 0) {
            g_state.snake_scratch.shape.row_bounds =
                (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                    sizeof(*g_state.snake_scratch.shape.row_bounds));
            g_state.snake_scratch.shape.row_bounds_capacity =
                (size_t)db_grid_rows_effective();
        }
    }
    g_state.gradient_row_y_ndc = (float *)db_alloc_array_or_fail(
        BACKEND_NAME, "gradient_row_y_ndc",
        (size_t)db_grid_rows_effective() + 1U, sizeof(float));

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    g_state.buffers.vbo = 0U;
    g_state.backbuffer_valid = 0;
    db_history_gradient_replay_state_reset(&g_state.gradient_prev_frame);

    g_state.capability_mode[0] = '\0';
    db_gl1_refresh_capability_mode();

    db_gl_set_client_state_vertex_array_enabled(1);
    db_gl_set_client_state_color_array_enabled(1);
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    const size_t full_mesh_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
    const size_t compact_rect_capacity =
        (g_state.runtime_flags.is_snake_history_texture != 0)
            ? db_gl1_snake_compact_rect_capacity()
            : 0U;
    const size_t compact_only_vbo_bytes =
        compact_rect_capacity *
        db_rect_tile_bytes(g_state.vertex.vertex_stride);

    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (compact_only_vbo_bytes > 0U)) {
        db_gl_compact_vbo_init_standalone_or_fail(
            BACKEND_NAME, &g_state.compact_vbo, compact_only_vbo_bytes,
            g_state.vertex.vertex_stride);
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.buffers.vbo = vbo_u32;
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached(
                    g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_vbo_init_data(compact_only_vbo_bytes, NULL,
                                    GL_DYNAMIC_DRAW) == 0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            db_gl_context_probe_upload_capabilities(
                compact_only_vbo_bytes,
                (const void *)g_state.compact_vbo.scratch_vertices,
                &probe_result);
            g_state.vertex.upload = probe_result;
        }
        if (g_state.buffers.vbo != 0U) {
            g_state.vbo_arrays_configured = 0;
            g_state.client_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            db_log_renderer_capability_mode(
                BACKEND_NAME,
                db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            db_gl1_log_backbuffer_strategy();
            return;
        }
        db_gl_compact_vbo_free(&g_state.compact_vbo);
    } else {
        const size_t total_vbo_bytes =
            db_gl_compact_vbo_total_bytes(full_mesh_bytes);
        if (total_vbo_bytes == 0U) {
            failf("GL1 VBO size overflow");
        }
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.buffers.vbo = vbo_u32;
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached(
                    g_state.buffers.vbo, &g_state.buffers.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            if (db_gl_vbo_init_data(total_vbo_bytes, NULL, GL_DYNAMIC_DRAW) ==
                0) {
                db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
                g_state.buffers.vbo = 0U;
            }
        }
        if (g_state.buffers.vbo != 0U) {
            db_gl_context_probe_upload_capabilities(
                full_mesh_bytes, (const void *)g_state.vertex.vertices,
                &probe_result);
            g_state.vertex.upload = probe_result;
            db_gl_upload_buffer(g_state.vertex.vertices, full_mesh_bytes,
                                g_state.vertex.upload.use_persistent_upload,
                                g_state.vertex.upload.persistent_mapped_ptr,
                                g_state.vertex.upload.use_map_range_upload,
                                g_state.vertex.upload.use_map_buffer_upload);
            db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                           full_mesh_bytes,
                                           g_state.vertex.vertex_stride);
            g_state.vbo_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            db_log_renderer_capability_mode(
                BACKEND_NAME,
                db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            db_gl1_log_backbuffer_strategy();
            return;
        }
    }

    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.compact_vbo.scratch_vertices == NULL)) {
        db_gl_compact_vbo_init_or_fail(BACKEND_NAME, &g_state.compact_vbo,
                                       compact_only_vbo_bytes,
                                       g_state.vertex.vertex_stride);
    }

    db_gl1_configure_client_arrays_if_needed();
    db_gl1_refresh_capability_mode();
    db_log_renderer_capability_mode(
        BACKEND_NAME, db_renderer_opengl_gl1_5_gles1_1_capability_mode());
    db_gl1_log_backbuffer_strategy();
}

static void
db_gl1_render_snake_draw_pass(const db_gl1_snake_frame_state_t *snake_frame,
                              int dirty_backbuffer_mode, int viewport_w,
                              int viewport_h) {
    if (snake_frame == NULL) {
        return;
    }
    const int allow_empty_dirty_draw = dirty_backbuffer_mode;
    const int must_force_full_draw =
        (g_state.runtime.backbuffer_draw_full != 0);
    const db_grid_block_t *draw_damage_blocks = NULL;
    size_t draw_damage_block_count = 0U;
    const db_snake_compact_block_t *draw_compact_blocks = NULL;
    size_t draw_compact_block_count = 0U;
    db_gl1_snake_frame_mode_t frame_mode = DB_GL1_SNAKE_FRAME_MODE_COMPACT;
    db_gl1_shadow_upload_intent_t shadow_upload_intent =
        DB_GL1_SHADOW_UPLOAD_INTENT_NONE;
    const db_damage_block_t *shadow_upload_blocks = NULL;
    size_t shadow_upload_block_count = 0U;
    int shadow_used_recovery_source = 0;
    const int has_viewport = (viewport_w > 0) && (viewport_h > 0);
    const db_gl1_damage_collect_ctx_t collect_ctx = {
        .pattern = g_state.runtime.pattern,
        .cols = db_grid_cols_effective(),
        .rows = db_grid_rows_effective(),
        .force_full_upload =
            (must_force_full_draw != 0) ? 1 : snake_frame->force_full_upload,
        .snake_plan = &snake_frame->plan,
        .pattern_seed = g_state.runtime.pattern_seed,
        .snake_scratch = &g_state.snake_scratch,
        .get_color_bits = db_gl1_get_snake_color_bits,
        .color_user_data = NULL,
    };
    if (db_gl1_collect_current_snake_frame_blocks(
            &collect_ctx, g_state.snake_scratch.damage.blocks,
            g_state.snake_scratch.damage.capacity, &draw_damage_block_count,
            g_state.snake_scratch.compact.blocks,
            g_state.snake_scratch.compact.capacity, &draw_compact_block_count,
            &frame_mode) != 0) {
        if (draw_damage_block_count > 0U) {
            draw_damage_blocks = g_state.snake_scratch.damage.blocks;
        }
        if ((frame_mode == DB_GL1_SNAKE_FRAME_MODE_COMPACT) &&
            (draw_compact_block_count > 0U)) {
            draw_compact_blocks = g_state.snake_scratch.compact.blocks;
        }
    } else if ((g_state.snake_scratch.damage.blocks != NULL) &&
               (g_state.snake_scratch.damage.capacity > 0U)) {
        g_state.snake_scratch.damage.blocks[0] = db_grid_block_full(
            db_grid_rows_effective(), db_grid_cols_effective());
        draw_damage_blocks = g_state.snake_scratch.damage.blocks;
        draw_damage_block_count = 1U;
        frame_mode = DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED;
    }
    const int has_draw_work =
        ((draw_compact_blocks != NULL) && (draw_compact_block_count > 0U)) ||
        (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
        ((draw_damage_blocks != NULL) && (draw_damage_block_count > 0U) &&
         (allow_empty_dirty_draw == 0));

    if (has_viewport != 0) {
        const uint32_t viewport_w_u32 =
            db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w);
        const uint32_t viewport_h_u32 =
            db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
        db_gl1_ensure_shadow_framebuffer_capacity(viewport_w_u32,
                                                  viewport_h_u32);
        if ((g_state.snake_shadow_present.backing_valid == 0) ||
            (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
            ((draw_damage_blocks == NULL) && (has_draw_work != 0))) {
            db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                   viewport_h_u32);
            g_state.snake_shadow_stats.backing_rebuild_frames++;
        } else if ((draw_damage_blocks != NULL) &&
                   (draw_damage_block_count > 0U)) {
            db_gl1_update_shadow_framebuffer_from_snake_step(
                snake_frame, viewport_w_u32, viewport_h_u32);
        }
    }

    if (has_draw_work == 0) {
        // No damage this frame; preserve backbuffer contents.
    } else {
        int drew_frame = 0;
        int used_compact_draw = 0;
        int used_fallback_draw = 0;
        const char *fallback_reason = NULL;
        if ((draw_compact_blocks != NULL) && (draw_compact_block_count > 0U)) {
            drew_frame = db_gl1_draw_compact_blocks_from_snake_colors_once(
                draw_compact_blocks, draw_compact_block_count);
            if (drew_frame != 0) {
                used_compact_draw = 1;
            }
        }
        if (drew_frame == 0) {
            if ((snake_frame->force_full_upload != 0) &&
                (dirty_backbuffer_mode != 0)) {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_INVALID_RECOVERY,
                    "invalid_backbuffer_recovery");
                fallback_reason = "invalid_backbuffer_recovery";
            } else if (g_state.runtime_flags.is_snake_shapes != 0) {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_SHAPE_FALLBACK, "shape_fallback");
                fallback_reason = "shape_fallback";
            } else {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_COMPACT_CAPACITY,
                    "compact_capacity");
                fallback_reason = "compact_capacity";
            }
            if (has_viewport != 0) {
                const uint32_t viewport_w_u32 = db_checked_int_to_u32(
                    BACKEND_NAME, "viewport_w", viewport_w);
                const uint32_t viewport_h_u32 = db_checked_int_to_u32(
                    BACKEND_NAME, "viewport_h", viewport_h);
                if ((frame_mode ==
                     DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
                    (draw_damage_blocks == NULL) ||
                    (draw_damage_block_count == 0U)) {
                    db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                           viewport_h_u32);
                    g_state.snake_shadow_stats.backing_rebuild_frames++;
                    shadow_upload_intent =
                        DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD;
                } else {
                    shadow_upload_block_count =
                        db_gl1_build_shadow_upload_blocks_from_damage_blocks(
                            draw_damage_blocks, draw_damage_block_count,
                            viewport_w_u32, viewport_h_u32);
                    if (shadow_upload_block_count == 0U) {
                        db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                               viewport_h_u32);
                        g_state.snake_shadow_stats.backing_rebuild_frames++;
                        shadow_upload_intent =
                            DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD;
                    } else {
                        shadow_upload_blocks =
                            g_state.snake_shadow_upload_blocks;
                        shadow_upload_intent =
                            DB_GL1_SHADOW_UPLOAD_INTENT_PARTIAL_BLOCKS;
                    }
                }
                if (shadow_upload_intent ==
                    DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD) {
                    g_state.snake_shadow_present.texture_valid = 0;
                    g_state.snake_shadow_present.texture_needs_full_upload = 1;
                    g_state.snake_shadow_stats.texture_full_upload_frames++;
                } else if (shadow_upload_intent ==
                           DB_GL1_SHADOW_UPLOAD_INTENT_PARTIAL_BLOCKS) {
                    g_state.snake_shadow_stats.texture_partial_upload_frames++;
                }
                if (fallback_reason != NULL) {
                    shadow_used_recovery_source = 1;
                }
                drew_frame = db_gl1_draw_shadow_framebuffer_once(
                    shadow_upload_blocks, shadow_upload_block_count,
                    viewport_w_u32, viewport_h_u32);
                if (drew_frame != 0) {
                    used_fallback_draw = 1;
                }
            }
        }
        db_gl1_record_compact_health(dirty_backbuffer_mode, used_compact_draw,
                                     used_fallback_draw, frame_mode,
                                     draw_compact_block_count,
                                     draw_damage_block_count, fallback_reason);
        if ((used_fallback_draw != 0) && (shadow_used_recovery_source != 0)) {
            g_state.snake_shadow_stats.recovery_from_shadow_frames++;
        }
        if (drew_frame != 0) {
            db_history_record_mesh_draw_stats(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames,
                (dirty_backbuffer_mode == 0) ? 1 : 0, 1U);
        }
    }
    if (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) {
        g_state.snake_replay.replay_mode =
            DB_GL1_SNAKE_REPLAY_FULL_RECOVERY_REQUIRED;
        g_state.snake_replay.prev_draw_block_count = 0U;
    } else if ((draw_compact_blocks != NULL) &&
               (draw_compact_block_count > 0U) &&
               (g_state.snake_replay.prev_draw_blocks != NULL) &&
               (g_state.snake_replay.draw_block_capacity > 0U)) {
        const size_t copy_limit =
            (draw_compact_block_count <
             g_state.snake_replay.draw_block_capacity)
                ? draw_compact_block_count
                : g_state.snake_replay.draw_block_capacity;
        db_copy_bytes(g_state.snake_replay.prev_draw_blocks,
                      draw_compact_blocks,
                      copy_limit * sizeof(*draw_compact_blocks));
        g_state.snake_replay.prev_draw_block_count = copy_limit;
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_COMPACT;
    } else {
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
    }
    g_state.snake_backbuffer_state.backbuffer_valid = 1;
}

static void
db_gl1_prepare_snake_frame_state(db_gl1_snake_frame_state_t *state,
                                 uint32_t preserved_framebuffer_count,
                                 int dirty_backbuffer_mode, int has_viewport) {
    if (state == NULL) {
        return;
    }

    const db_history_snake_step_eval_t eval =
        db_history_eval_snake_step_from_runtime(&g_state.runtime);
    state->plan = eval.plan;
    state->target = eval.target;
    const db_snake_shape_kind_t shape_kind = eval.shape_kind;

    const db_history_snake_backbuffer_action_t history_action =
        db_history_eval_snake_backbuffer_action_io(
            dirty_backbuffer_mode, preserved_framebuffer_count,
            &g_state.snake_backbuffer_state.seed_frames_remaining,
            &g_state.snake_backbuffer_state.resync_frames_remaining,
            &g_state.snake_backbuffer_state.initial_seed_done,
            &g_state.snake_backbuffer_state.backbuffer_valid);

    if (db_history_run_seed_clear_if_needed(
            history_action.should_seed_now, &g_state.runtime,
            db_gl1_seed_backbuffer_clear_cb, NULL) != 0) {
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, 1U);
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
    }
    if (history_action.should_force_full_upload != 0) {
        state->force_full_upload = 1;
    }
    int collect_force_full_blocks = state->force_full_upload;
    if ((g_state.runtime_flags.is_snake_grid != 0) &&
        (state->force_full_upload != 0) && dirty_backbuffer_mode &&
        has_viewport) {
        // Grid fast-path on invalid backbuffer: clear to the pre-step base
        // phase, then redraw the full non-base set (settled + active).
        const int base_phase = (state->plan.phase_flag == 0) ? 1 : 0;
        double base_rgb[3] = {0.0, 0.0, 0.0};
        db_grid_target_color_rgb3(base_phase, base_rgb);
        float base_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(base_rgb, base_rgb_f32);
        db_gl_clear_color_rgba(base_rgb_f32[0], base_rgb_f32[1],
                               base_rgb_f32[2], 1.0F);
        db_gl_clear_color_buffer();
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, 1U);
        collect_force_full_blocks = 0;
    }
    state->force_full_upload = collect_force_full_blocks;
    const int can_replay_snake = db_history_can_replay_previous_damage(
        preserved_framebuffer_count, dirty_backbuffer_mode,
        g_state.snake_backbuffer_state.backbuffer_valid,
        (g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT) ? 1U
                                                                          : 0U);
    if (can_replay_snake != 0) {
        if ((g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT) &&
            (g_state.snake_replay.prev_draw_blocks != NULL) &&
            (g_state.snake_replay.prev_draw_block_count > 0U)) {
            (void)db_gl1_draw_compact_blocks_from_snake_colors_once(
                g_state.snake_replay.prev_draw_blocks,
                g_state.snake_replay.prev_draw_block_count);
        }
    }

    db_render_snake_step(&state->plan, &state->target.region, shape_kind,
                         g_state.runtime.pattern_seed,
                         state->plan.active_shape_index,
                         state->target.target_rgb,
                         state->target.force_full_fill_on_phase_complete);
    db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
}

static void db_gl1_render_gradient_frame(int viewport_w, int viewport_h,
                                         uint32_t preserved_framebuffer_count) {
    const db_gradient_damage_plan_t gradient_plan =
        db_history_eval_gradient_step_from_runtime(&g_state.runtime);

    // Render MUST use the plan's render_* state. The plan's next_* state is
    // only applied to the runtime AFTER we draw, matching CPU renderer
    // semantics.
    const uint32_t gradient_render_head_row =
        gradient_plan.render_state.head_row;
    const int gradient_render_direction_down =
        gradient_plan.render_state.direction_down;
    const uint32_t gradient_render_cycle_index =
        gradient_plan.render_state.cycle_index;
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t full_width_cols = db_grid_cols_effective();

    db_grid_block_t gradient_dirty_blocks[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
        {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}};
    const size_t gradient_dirty_count = db_gradient_collect_dirty_blocks(
        &gradient_plan, rows, full_width_cols, gradient_dirty_blocks,
        DB_GL1_GRADIENT_REPLAY_ROW_CAP);

    // When bench_speed_step > 1, the head can advance by multiple rows. Plan
    // dirty ranges cover the new sweep window, but traversed rows can be
    // skipped unless we repaint them as solid per-row colors.
    db_grid_block_t skipped_blocks[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
        {0U, 0U, 0U, 0U},
        {0U, 0U, 0U, 0U},
    };
    size_t skipped_count = 0U;

    const uint32_t render_head = gradient_plan.render_state.head_row;
    const uint32_t next_head = gradient_plan.next_state.head_row;
    if (rows > 0U) {
        uint32_t delta = 0U;
        if (gradient_render_direction_down != 0) {
            // Moving down => head increases modulo rows.
            delta = (next_head + rows - render_head) % rows;
        } else {
            // Moving up => head decreases modulo rows.
            delta = (render_head + rows - next_head) % rows;
        }

        // If head changed but delta==0, we wrapped a full cycle.
        // Conservatively repaint everything.
        if ((next_head != render_head) && (delta == 0U)) {
            delta = rows;
        }

        if (delta > 1U) {
            const uint32_t skipped_rows = delta - 1U;
            if (gradient_render_direction_down != 0) {
                // Downwards: skipped rows are render+1 .. render+skipped.
                const uint32_t start_row = (render_head + 1U) % rows;
                skipped_blocks[0].row_start = start_row;
                skipped_blocks[0].row_count =
                    db_u32_min(rows - start_row, skipped_rows);
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_blocks[1].row_start = 0U;
                skipped_blocks[1].row_count =
                    skipped_rows - skipped_blocks[0].row_count;
                skipped_blocks[1].col_start = 0U;
                skipped_blocks[1].col_count = full_width_cols;
                skipped_count = (skipped_blocks[1].row_count > 0U) ? 2U : 1U;
            } else if (render_head >= skipped_rows) {
                // No wrap past 0: [render-skipped .. render-1]
                skipped_blocks[0].row_start = render_head - skipped_rows;
                skipped_blocks[0].row_count = skipped_rows;
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_count = (skipped_rows > 0U) ? 1U : 0U;
            } else {
                // Wraps past 0: [0 .. render-1] and [rows-underflow .. rows-1]
                const uint32_t underflow = skipped_rows - render_head;
                skipped_blocks[0].row_start = 0U;
                skipped_blocks[0].row_count = render_head;
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_blocks[1].row_start = rows - underflow;
                skipped_blocks[1].row_count = underflow;
                skipped_blocks[1].col_start = 0U;
                skipped_blocks[1].col_count = full_width_cols;
                skipped_count = 0U;
                if (skipped_blocks[0].row_count > 0U) {
                    skipped_count++;
                }
                if (skipped_blocks[1].row_count > 0U) {
                    skipped_count++;
                }
            }
        }
    }

    // Gradient patterns: damage-only updates directly on the default
    // framebuffer/backbuffer.
    if ((viewport_w > 0) && (viewport_h > 0) && (rows > 0U)) {
        if (g_state.runtime.backbuffer_draw_full != 0) {
            db_grid_block_t full_blocks[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
            };
            full_blocks[0] = (db_grid_block_t){
                .row_start = 0U,
                .row_count = rows,
                .col_start = 0U,
                .col_count = full_width_cols,
            };
            const size_t full_count = 1U;
            db_gl1_draw_gradient_dirty_blocks_mesh(
                full_blocks, full_count, gradient_render_head_row,
                gradient_render_direction_down, gradient_render_cycle_index,
                viewport_w, viewport_h);
            g_state.backbuffer_valid = 1;
            const db_gradient_state_t render_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };
            db_history_gradient_replay_state_store(&g_state.gradient_prev_frame,
                                                   full_blocks, full_count,
                                                   &render_state);
            db_history_record_draw_stats_for_work(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames, 1, 0, 1U);
        } else {
            db_grid_block_t curr_draw_blocks[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U}};
            size_t curr_draw_count = db_gradient_build_curr_draw_blocks(
                skipped_blocks, skipped_count, gradient_dirty_blocks,
                gradient_dirty_count, full_width_cols, curr_draw_blocks,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            const int needs_full_seed = db_history_should_seed_full_on_invalid(
                g_state.backbuffer_valid);
            const db_grid_block_t *persist_blocks = curr_draw_blocks;
            size_t persist_count = curr_draw_count;
            db_gradient_state_t persist_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };

            if (needs_full_seed != 0) {
                g_state.backbuffer_valid = 1;
                curr_draw_count = 1U;
                curr_draw_blocks[0] = (db_grid_block_t){
                    .row_start = 0U,
                    .row_count = rows,
                    .col_start = 0U,
                    .col_count = full_width_cols,
                };
                db_gl1_draw_gradient_dirty_blocks_mesh(
                    curr_draw_blocks, curr_draw_count, gradient_render_head_row,
                    gradient_render_direction_down, gradient_render_cycle_index,
                    viewport_w, viewport_h);
                persist_count = curr_draw_count;
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 1, 0, 1U);
            } else {
                const int has_replay =
                    (preserved_framebuffer_count >= 2) &&
                    (g_state.gradient_prev_frame.draw_count > 0U);
                const int has_current = (curr_draw_count > 0U);
                db_grid_block_t replay_draw_blocks[4] = {{0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U}};
                const db_grid_block_t *replay_draw_ptr =
                    g_state.gradient_prev_frame.draw_blocks;
                size_t replay_draw_count =
                    g_state.gradient_prev_frame.draw_count;
                if ((has_replay != 0) && (has_current != 0)) {
                    replay_draw_count = db_gradient_subtract_replay_blocks(
                        g_state.gradient_prev_frame.draw_blocks,
                        g_state.gradient_prev_frame.draw_count,
                        curr_draw_blocks, curr_draw_count, replay_draw_blocks,
                        DB_GRADIENT_DRAW_RANGE_WORK_CAP);
                    replay_draw_ptr = replay_draw_blocks;
                }
                const int has_replay_draw = (replay_draw_count > 0U);
                if ((g_state.runtime_flags.is_gradient_sweep != 0) &&
                    (has_replay != 0) && (has_current == 0)) {
                    // At bottom bounce, sweep can be replay-only for one frame.
                    persist_blocks = g_state.gradient_prev_frame.draw_blocks;
                    persist_count = g_state.gradient_prev_frame.draw_count;
                    persist_state = g_state.gradient_prev_frame.state;
                }

                if (has_replay_draw != 0) {
                    db_gl1_draw_gradient_dirty_blocks_mesh(
                        replay_draw_ptr, replay_draw_count,
                        g_state.gradient_prev_frame.state.head_row,
                        g_state.gradient_prev_frame.state.direction_down,
                        g_state.gradient_prev_frame.state.cycle_index,
                        viewport_w, viewport_h);
                }
                if (has_current != 0) {
                    db_gl1_draw_gradient_dirty_blocks_mesh(
                        curr_draw_blocks, curr_draw_count,
                        gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index, viewport_w, viewport_h);
                }
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 0, 1,
                    ((has_replay_draw != 0) || (has_current != 0)) ? 1U : 0U);
            }

            // Persist the damage we drew this frame for next double-buffer
            // replay. `curr_draw_ranges` and replay-diff ranges are already
            // generated in normalized order, so avoid a second normalize pass.
            size_t persist_count_limited = persist_count;
            DB_LOG_CAPACITY_EXCEEDED_ONCE(
                BACKEND_NAME, "gradient_replay_persist_blocks", persist_count,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            if (persist_count_limited > DB_GL1_GRADIENT_REPLAY_ROW_CAP) {
                persist_count_limited = DB_GL1_GRADIENT_REPLAY_ROW_CAP;
            }
            db_history_gradient_replay_state_store(
                &g_state.gradient_prev_frame, persist_blocks,
                persist_count_limited, &persist_state);
        }
    }

    // Apply step AFTER rendering (CPU renderer ordering).
    db_history_apply_gradient_step_to_runtime(&g_state.runtime, &gradient_plan);
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(
    uint32_t frame_index, int viewport_width_px, int viewport_height_px,
    uint32_t preserved_framebuffer_count) {
    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(BACKEND_NAME, &viewport_width_px,
                                           &viewport_height_px,
                                           &g_state.viewport.last_viewport_w,
                                           &g_state.viewport.last_viewport_h);

    if (viewport_state.viewport_changed != 0) {
        size_t previous_replay_marker =
            (g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT)
                ? 1U
                : 0U;
        // Keep GL viewport in sync with drawable pixels (HiDPI-safe)
        // without querying GL state.
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
        db_gl1_refresh_tile_positions_for_viewport(viewport_width_px,
                                                   viewport_height_px);
        db_gl1_refresh_gradient_row_ndc_cache(viewport_height_px);
        db_history_invalidate_snake_backbuffer_on_resize(
            preserved_framebuffer_count, &g_state.backbuffer_valid,
            &previous_replay_marker, &g_state.snake_backbuffer_state);
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
        g_state.snake_shadow_present.backing_valid = 0;
        g_state.snake_shadow_present.texture_valid = 0;
        g_state.snake_shadow_present.texture_needs_full_upload = 1;
    }

    // When the display path advertises preserved backbuffer behavior, snake
    // dirty draws operate directly on the default framebuffer. Display-side
    // fallback can pass `preserved_framebuffer_count=0` to disable that
    // assumption.

    // Use a known viewport size without querying GL state.
    // Pixel-space ops must use drawable pixels; tile math stays on grid
    // cols/rows.
    const int viewport_w = viewport_state.viewport_width_px;
    const int viewport_h = viewport_state.viewport_height_px;
    const int has_viewport = viewport_state.has_viewport;
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl1_snake_frame_state_t snake_frame = {0};
        db_gl1_prepare_snake_frame_state(
            &snake_frame, preserved_framebuffer_count,
            g_state.runtime_flags.uses_dirty_backbuffer_mode, has_viewport);
        db_gl1_render_snake_draw_pass(
            &snake_frame, g_state.runtime_flags.uses_dirty_backbuffer_mode,
            viewport_w, viewport_h);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        db_gl1_render_gradient_frame(viewport_w, viewport_h,
                                     preserved_framebuffer_count);
    } else if (g_state.runtime_flags.is_bands != 0) {
        db_gl1_draw_bands_compact(db_grid_cols_effective(), BENCH_BANDS,
                                  frame_index, viewport_w, viewport_h);
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, BENCH_BANDS);
    }
    db_history_finalize_frame(&g_state.frame, &g_state.runtime,
                              db_grid_cols_effective(),
                              db_grid_rows_effective());
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached(
            g_state.buffers.vbo, &g_state.buffers.bound_array_buffer);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.buffers.vbo != 0U) {
        db_gl_vbo_delete_if_valid(g_state.buffers.vbo);
        g_state.buffers.vbo = 0U;
    }
    db_gl_set_client_state_vertex_array_enabled(0);
    db_gl_set_client_state_color_array_enabled(0);
    free(g_state.snake_replay.prev_draw_blocks);
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.compact.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    free(g_state.gradient_row_y_ndc);
    free(g_state.snake_color_state);
    free(g_state.snake_shadow_rgba8);
    free(g_state.snake_shadow_rgba16f);
    free(g_state.snake_shadow_upload_blocks);
    db_gl_shadow_present_shutdown(&g_state.snake_shadow_present);
    db_gl_compact_vbo_free(&g_state.compact_vbo);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    if (g_state.capability_mode[0] == '\0') {
        db_gl1_refresh_capability_mode();
    }
    return g_state.capability_mode;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, 1);
}

uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void) {
    return g_state.frame.state_hash;
}

void db_renderer_opengl_gl1_5_gles1_1_draw_stats(uint64_t *full_draw_frames,
                                                 uint64_t *dirty_draw_frames) {
    db_history_copy_draw_stats(&g_state.frame, full_draw_frames,
                               dirty_draw_frames);
    if ((g_state.runtime_flags.is_snake_history_texture != 0) &&
        (g_state.runtime_flags.uses_dirty_backbuffer_mode != 0)) {
        db_infof(
            BACKEND_NAME,
            "snake dirty_mode_stats: compact_success_frames=%llu "
            "shadow_fallback_frames=%llu full_recovery_frames=%llu "
            "compact_attempt_frames=%llu requested_backbuffer_draw_full=%d",
            (unsigned long long)g_state.snake_compact_health.success_frames,
            (unsigned long long)g_state.snake_compact_health.fallback_frames,
            (unsigned long long)
                g_state.snake_compact_health.full_recovery_frames,
            (unsigned long long)g_state.snake_compact_health.attempt_frames,
            g_state.runtime.backbuffer_draw_full);
        db_infof(
            BACKEND_NAME,
            "snake shadow_sync_stats: backing_incremental_frames=%llu "
            "backing_rebuild_frames=%llu texture_partial_upload_frames=%llu "
            "texture_full_upload_frames=%llu recovery_from_shadow_frames=%llu",
            (unsigned long long)
                g_state.snake_shadow_stats.backing_incremental_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.backing_rebuild_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.texture_partial_upload_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.texture_full_upload_frames,
            (unsigned long long)
                g_state.snake_shadow_stats.recovery_from_shadow_frames);
    }
}
