#include "../../config/benchmark_config.h"
#include "../../core/db_alloc_policy.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_geometry.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_emit.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_snake_types.h"
#include "renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderer_opengl_gl1_5_gles1_1_internal.h"
#include <stddef.h>
#include <stdint.h>

void db_gl1_log_compact_reject(const char *path, const char *reason,
                               size_t range_count, size_t value, size_t limit) {
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

void db_gl1_log_shadow_fallback_once(unsigned int reason_mask,
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

void db_gl1_record_compact_health(int dirty_backbuffer_mode,
                                  int used_compact_draw, int used_fallback_draw,
                                  db_gl1_snake_frame_mode_t frame_mode,
                                  size_t compact_block_count,
                                  size_t damage_block_count,
                                  const char *fallback_reason) {
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

void db_gl1_invalidate_array_pointer_cache(void) {
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
}

void db_gl1_emit_gradient_row_block(uint32_t row_start, uint32_t row_count,
                                    const double *row_rgb, void *user_data) {
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

int db_gl1_has_snake_color_state(void) {
    return (g_state.snake_color_state != NULL) &&
           (g_state.snake_color_capacity >=
            ((size_t)g_state.runtime.work_unit_count *
             DB_GL_COLOR_COMPONENT_COUNT));
}

int db_gl1_shadow_backing_uses_rgba16f(void) {
    return (g_state.snake_shadow_backing_format ==
            DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
               ? 1
               : 0;
}

db_gl1_rect_bounds_t db_gl1_rect_bounds_from_grid_tile(size_t row, size_t col) {
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

void db_gl1_emit_snake_compact_rect(float *dst_unit, size_t stride,
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

void db_gl1_get_snake_color_bits(uint32_t row, uint32_t col, void *user_data,
                                 uint32_t *color_bits) {
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

void db_gl1_init_snake_color_state_from_vertices(void) {
    if (db_gl1_has_snake_color_state() == 0) {
        return;
    }
    static const double base_rgb[3] = {BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                                       BENCH_GRID_PHASE0_B};
    float base_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
    db_rgb_f64_to_f32_rgb3(base_rgb, base_rgb_f32);
    for (uint32_t tile_index = 0U; tile_index < g_state.runtime.work_unit_count;
         tile_index++) {
        float *tile_color = db_gl1_snake_tile_color_ptr(tile_index);
        if (tile_color == NULL) {
            continue;
        }
        db_copy_f32_rgb3(tile_color, base_rgb_f32);
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

void db_gl1_ensure_shadow_framebuffer_capacity(uint32_t pixel_width,
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

void db_gl1_rebuild_shadow_framebuffer_full(uint32_t pixel_width,
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

void db_gl1_update_shadow_framebuffer_from_snake_step(
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

size_t db_gl1_build_shadow_upload_blocks_from_damage_blocks(
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

int db_gl1_draw_shadow_framebuffer_once(const db_damage_block_t *blocks,
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

void db_gl1_refresh_tile_positions_for_viewport(int viewport_w,
                                                int viewport_h) {
    if ((viewport_w <= 0) || (viewport_h <= 0) ||
        (g_state.vertex.vertices == NULL)) {
        return;
    }
    const uint32_t viewport_w_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w);
    const uint32_t viewport_h_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t tile_index = 0U; tile_index < g_state.vertex.work_unit_count;
         tile_index++) {
        db_damage_block_t pixel_block = {0U, 0U, 0U, 0U};
        if (db_grid_tile_to_pixel_block(cols, rows, tile_index, viewport_w_u32,
                                        viewport_h_u32, &pixel_block) == 0) {
            continue;
        }
        const uint32_t x0_px = pixel_block.col_start;
        const uint32_t x1_px =
            db_damage_block_col_end_or_fail("gl1_tile_x1_px", &pixel_block);
        const uint32_t y0_px =
            viewport_h_u32 -
            db_damage_block_row_end_or_fail("gl1_tile_y0_px", &pixel_block);
        const uint32_t y1_px = viewport_h_u32 - pixel_block.row_start;

        const float x0 = db_pixel_coord_to_ndc_f32(x0_px, viewport_w_u32);
        const float x1 = db_pixel_coord_to_ndc_f32(x1_px, viewport_w_u32);
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

void db_gl1_refresh_gradient_row_ndc_cache(int viewport_h) {
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

void db_gl1_refresh_bands_x_cache(uint32_t cols, uint32_t band_count,
                                  int viewport_w) {
    if ((viewport_w <= 0) || (cols == 0U) || (band_count == 0U) ||
        (band_count > BENCH_BANDS)) {
        return;
    }
    const uint32_t viewport_w_u32 =
        db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w);
    if ((g_state.bands_x_cache_viewport_w == viewport_w) &&
        (g_state.bands_x_cache_cols == cols) &&
        (g_state.bands_x_cache_count == band_count)) {
        return;
    }
    for (uint32_t band = 0U; band <= band_count; band++) {
        const uint32_t tile_x = db_checked_u64_to_u32(
            BACKEND_NAME, "bands_x_cache_tile_x",
            ((uint64_t)band * (uint64_t)cols) / (uint64_t)band_count);
        int x_px = db_checked_u32_to_i32(
            BACKEND_NAME, "bands_x_cache_x_px",
            db_grid_axis_edge_to_pixel_coord(cols, tile_x, viewport_w_u32));
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
        g_state.bands_x_cache_ndc[band] = db_pixel_coord_to_ndc_f32(
            db_checked_int_to_u32(BACKEND_NAME, "bands_x_cache_x_px", x_px),
            viewport_w_u32);
    }
    g_state.bands_x_cache_viewport_w = viewport_w;
    g_state.bands_x_cache_cols = cols;
    g_state.bands_x_cache_count = band_count;
}

void db_gl1_seed_backbuffer_clear_cb(const float *rgba, void *user_data) {
    (void)user_data;
    if (rgba == NULL) {
        return;
    }
    db_gl_clear_color_rgba(rgba[0], rgba[1], rgba[2], 1.0F);
    db_gl_clear_color_buffer();
}

void db_gl1_refresh_capability_mode(void) {
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

void db_gl1_log_backbuffer_strategy(void) {
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

size_t db_gl1_snake_compact_rect_capacity(void) {
    return DB_GL1_SNAKE_COMPACT_RECT_LIMIT;
}

int db_gl1_init_runtime_metadata_only(
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

int db_init_vertices_for_mode(const db_benchmark_runtime_init_t *runtime_state,
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

void db_render_snake_step(const db_snake_plan_t *plan,
                          const db_snake_region_t *region, uint32_t shape_kind,
                          uint32_t pattern_seed, uint32_t shape_index,
                          const double *target_rgb,
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
