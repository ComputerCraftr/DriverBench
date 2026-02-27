#include "renderer_opengl_gl1_5_gles1_1.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/gltypes.h>
#elifdef DB_HAS_OPENGL_DESKTOP
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#else
#include <GLES/gl.h>
#endif

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define DB_NDC_TO_VIEWPORT_HALF_F 0.5F
#define DB_GL1_GRADIENT_DIRTY_RANGE_CAP 2U
#define DB_GL1_GRADIENT_MESH_ROW_THRESHOLD 8U
#define DB_GL1_GRADIENT_REPLAY_ROW_CAP 4U
#define ES_STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    db_gl_upload_range_t *curr_upload_ranges;
    db_gl_upload_range_t *prev_upload_ranges;
    size_t prev_upload_count;
} db_gl1_snake_replay_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    uint64_t state_hash;
    uint64_t full_draw_frames;
    uint64_t dirty_draw_frames;
    uint32_t frame_index;
    db_benchmark_runtime_init_t runtime;
    db_gl_vertex_init_t vertex;
    int is_es_context;
    // Double-buffered preserved-backbuffer support: replay prior frame damage
    // onto the next backbuffer (which may contain N-1) before current damage.
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_gl1_snake_replay_t snake_replay;
    db_snake_col_span_t *snake_spans;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    size_t snake_scratch_capacity;
    GLint last_viewport_w;
    GLint last_viewport_h;
    int backbuffer_valid;
    GLuint vbo;
    unsigned int bound_array_buffer;
    int client_arrays_configured;
    int rect_clear_scope_active;
    int vbo_arrays_configured;
} renderer_state_t;

typedef struct {
    uint32_t total_rows;
    GLint viewport_h;
    GLint viewport_w;
} db_gl1_gradient_gpu_apply_ctx_t;

typedef struct {
    uint32_t cols;
} db_gl1_gradient_mesh_apply_ctx_t;

static renderer_state_t g_state = {0};

static const char *db_gl1_cap_upload_string(void) {
    return db_gl_cap_upload_mode_from_probe((g_state.vbo != 0U) ? 1 : 0,
                                            &g_state.vertex.upload);
}

static const char *db_gl1_cap_draw_mode_for_pattern(void) {
    if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL) ||
        (g_state.runtime.pattern == DB_PATTERN_BANDS)) {
        return DB_GL_CAP_DRAW_FF_RECT_FILL;
    }
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) {
        return DB_GL_CAP_DRAW_HISTORY_DIRTY;
    }
    return DB_GL_CAP_DRAW_TILES_FULL;
}

static const char *db_gl1_cap_upload_mode_for_pattern(void) {
    if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL) ||
        (g_state.runtime.pattern == DB_PATTERN_BANDS)) {
        return DB_GL_CAP_UPLOAD_NONE;
    }
    return db_gl1_cap_upload_string();
}

static int db_gl1_cap_backbuffer_replay_for_pattern(void) {
    if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        return g_state.runtime.backbuffer_draw_full == 0;
    }
    return 0;
}

static void db_gl1_refresh_capability_mode(void) {
    db_gl_capability_mode_compose(g_state.capability_mode,
                                  sizeof(g_state.capability_mode),
                                  db_gl1_cap_draw_mode_for_pattern(),
                                  db_gl1_cap_upload_mode_for_pattern(),
                                  db_gl1_cap_backbuffer_replay_for_pattern());
}

static int db_init_vertices_for_mode(size_t vertex_stride) {
    db_benchmark_runtime_init_t runtime_state = {0};
    db_gl_vertex_init_t init_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &runtime_state)) {
        return 0;
    }
    if (!db_init_vertices_for_runtime_common_with_stride(
            BACKEND_NAME, &init_state, &runtime_state, vertex_stride)) {
        return 0;
    }

    g_state.vertex = init_state;
    g_state.runtime = runtime_state;
    return 1;
}

static void db_render_snake_step(const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 uint32_t shape_kind, uint32_t pattern_seed,
                                 uint32_t shape_index, float target_r,
                                 float target_g, float target_b,
                                 int full_fill_on_phase_completed) {
    if ((plan == NULL) || (region == NULL)) {
        return;
    }
    if (plan->batch_size > BENCH_SNAKE_PHASE_WINDOW_TILES) {
        failf("snake batch size %u exceeds BENCH_SNAKE_PHASE_WINDOW_TILES=%u",
              plan->batch_size, BENCH_SNAKE_PHASE_WINDOW_TILES);
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }
    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_fill_grid_all_rgb_stride(
            g_state.vertex.vertices, g_state.runtime.work_unit_count,
            g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
            target_r, target_g, target_b);
        return;
    }
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES) {
        if ((g_state.snake_row_bounds != NULL) &&
            (db_snake_shape_cache_init_from_index(
                 &shape_cache, g_state.snake_row_bounds,
                 g_state.snake_row_bounds_capacity, pattern_seed, shape_index,
                 DB_U32_SALT_PALETTE, region,
                 (db_snake_shape_kind_t)shape_kind) != 0)) {
            shape_cache_ptr = &shape_cache;
        }
    }
    float prior_rgb[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0F};
    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const size_t prior_base = (size_t)update_index * 3U;
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
        const uint32_t row = tile_index / db_grid_cols_effective();
        const uint32_t col = tile_index % db_grid_cols_effective();
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        prior_rgb[prior_base] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        prior_rgb[prior_base + 1U] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        prior_rgb[prior_base + 2U] = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
        const uint32_t row = tile_index / db_grid_cols_effective();
        const uint32_t col = tile_index % db_grid_cols_effective();
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, target_r, target_g,
                             target_b);
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        const uint32_t tile_index = db_snake_tile_index_from_step(region, step);
        const uint32_t row = tile_index / db_grid_cols_effective();
        const uint32_t col = tile_index % db_grid_cols_effective();
        if (shape_cache_ptr != NULL) {
            const int inside =
                db_snake_shape_cache_contains_tile(shape_cache_ptr, row, col);
            if (inside == 0) {
                continue;
            }
        }
        const float blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);

        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        const size_t prior_base = (size_t)update_index * 3U;
        const float prior_r = prior_rgb[prior_base];
        const float prior_g = prior_rgb[prior_base + 1U];
        const float prior_b = prior_rgb[prior_base + 2U];
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r, &out_g, &out_b);
        db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
    }
}

static size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

static void db_gl1_set_gradient_grid_row_rgb(uint32_t row, uint32_t cols,
                                             float row_r, float row_g,
                                             float row_b) {
    if (cols == 0U) {
        return;
    }
    for (uint32_t col = 0U; col < cols; col++) {
        const uint32_t tile_index = (row * cols) + col;
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, row_r, row_g,
                             row_b);
    }
}

static void db_gl1_write_gradient_row_color_to_mesh(uint32_t row, float row_r,
                                                    float row_g, float row_b,
                                                    void *user_data) {
    db_gl1_gradient_mesh_apply_ctx_t *ctx =
        (db_gl1_gradient_mesh_apply_ctx_t *)user_data;
    if (ctx == NULL || ctx->cols == 0U) {
        return;
    }
    const uint32_t rows = db_grid_rows_effective();
    if (rows == 0U) {
        return;
    }
    db_gl1_set_gradient_grid_row_rgb(row % rows, ctx->cols, row_r, row_g,
                                     row_b);
}

static int db_gl1_row_range_to_copy_rect(uint32_t row_start, uint32_t row_count,
                                         uint32_t total_rows,
                                         GLint viewport_width,
                                         GLint viewport_height, GLint *x_out,
                                         GLint *y_out, GLsizei *width_out,
                                         GLsizei *height_out) {
    if ((row_count == 0U) || (total_rows == 0U) || (row_start >= total_rows) ||
        (viewport_width <= 0) || (viewport_height <= 0) || (x_out == NULL) ||
        (y_out == NULL) || (width_out == NULL) || (height_out == NULL)) {
        return 0;
    }

    const uint32_t row_end = db_u32_min(total_rows, row_start + row_count);
    if (row_end <= row_start) {
        return 0;
    }

    // Compute bounds in top-origin tile space -> top-origin pixel space.
    // py_top/py_bottom are measured from the TOP of the framebuffer.
    GLint py_top = (GLint)(((uint64_t)row_start * (uint64_t)viewport_height) /
                           (uint64_t)total_rows);
    GLint py_bottom = (GLint)(((uint64_t)row_end * (uint64_t)viewport_height) /
                              (uint64_t)total_rows);

    // Ensure final row reaches the bottom edge.
    if (row_end == total_rows) {
        py_bottom = viewport_height;
    }

    if (py_top < 0) {
        py_top = 0;
    }
    if (py_bottom > viewport_height) {
        py_bottom = viewport_height;
    }
    if (py_bottom <= py_top) {
        return 0;
    }

    // Convert to OpenGL scissor space (bottom-origin).
    GLint rect_y = viewport_height - py_bottom;
    GLsizei rect_h = (GLsizei)(py_bottom - py_top);

    if (rect_y < 0) {
        rect_h = (GLsizei)(rect_h + rect_y);
        rect_y = 0;
    }
    if ((rect_y + (GLint)rect_h) > viewport_height) {
        rect_h = (GLsizei)(viewport_height - rect_y);
    }
    if (rect_h <= 0) {
        return 0;
    }

    *x_out = 0;
    *y_out = rect_y;
    *width_out = (GLsizei)viewport_width;
    *height_out = rect_h;
    return 1;
}

static void db_gl1_configure_client_arrays_if_needed(void) {
    if (g_state.client_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached(0U, &g_state.bound_array_buffer);

    const GLsizei client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const GLint client_color_components = (g_state.is_es_context != 0)
                                              ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                              : DB_VERTEX_COLOR_FLOAT_COUNT;

    glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT, client_stride,
                    &g_state.vertex.vertices[0]);
    glColorPointer(client_color_components, GL_FLOAT, client_stride,
                   &g_state.vertex.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);

    g_state.client_arrays_configured = 1;
    g_state.vbo_arrays_configured = 0;
}

static void db_gl1_configure_vbo_arrays_if_needed(void) {
    if (g_state.vbo == 0U || g_state.vbo_arrays_configured != 0) {
        return;
    }
    (void)db_gl_bind_array_buffer_cached((unsigned int)g_state.vbo,
                                         &g_state.bound_array_buffer);

    const GLsizei vbo_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const GLint vbo_color_components = (g_state.is_es_context != 0)
                                           ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                           : DB_VERTEX_COLOR_FLOAT_COUNT;

    glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT, vbo_stride,
                    db_gl_vbo_offset_ptr(0U));
    glColorPointer(
        vbo_color_components, GL_FLOAT, vbo_stride,
        db_gl_vbo_offset_ptr(sizeof(float) * DB_VERTEX_POSITION_FLOAT_COUNT));

    g_state.vbo_arrays_configured = 1;
    g_state.client_arrays_configured = 0;
}

static void db_gl1_draw_solid_rect_pixels(GLint rect_x, GLint rect_y,
                                          GLsizei rect_width,
                                          GLsizei rect_height, GLint viewport_w,
                                          GLint viewport_h, float color_r,
                                          float color_g, float color_b) {
    if ((rect_width <= 0) || (rect_height <= 0) || (viewport_w <= 0) ||
        (viewport_h <= 0)) {
        return;
    }

    // Clamp to framebuffer.
    GLint x0 = rect_x;
    GLint y0 = rect_y;
    GLint x1 = rect_x + (GLint)rect_width;
    GLint y1 = rect_y + (GLint)rect_height;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > viewport_w) {
        x1 = viewport_w;
    }
    if (y1 > viewport_h) {
        y1 = viewport_h;
    }
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }

    glScissor(x0, y0, x1 - x0, y1 - y0);
    glClearColor(color_r, color_g, color_b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void db_gl1_draw_gradient_row_color(uint32_t row, float row_r,
                                           float row_g, float row_b,
                                           void *user_data) {
    db_gl1_gradient_gpu_apply_ctx_t *ctx =
        (db_gl1_gradient_gpu_apply_ctx_t *)user_data;
    if (ctx == NULL) {
        return;
    }
    GLint rect_x = 0;
    GLint rect_y = 0;
    GLsizei rect_width = 0;
    GLsizei rect_height = 0;
    if (db_gl1_row_range_to_copy_rect(row, 1U, ctx->total_rows, ctx->viewport_w,
                                      ctx->viewport_h, &rect_x, &rect_y,
                                      &rect_width, &rect_height) == 0) {
        return;
    }
    db_gl1_draw_solid_rect_pixels(rect_x, rect_y, rect_width, rect_height,
                                  ctx->viewport_w, ctx->viewport_h, row_r,
                                  row_g, row_b);
}

static void
db_gl1_draw_gradient_dirty_rows_gpu(const db_dirty_row_range_t *dirty_ranges,
                                    size_t dirty_count, uint32_t head_row,
                                    int direction_down, uint32_t cycle_index,
                                    GLint viewport_w, GLint viewport_h) {
    if (viewport_w <= 0 || viewport_h <= 0) {
        return;
    }
    const uint32_t total_rows = db_grid_rows_effective();

    db_gl1_gradient_gpu_apply_ctx_t apply_ctx = {
        .total_rows = total_rows,
        .viewport_h = viewport_h,
        .viewport_w = viewport_w,
    };
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count_raw = dirty_ranges[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= total_rows)) {
            continue;
        }
        const uint32_t row_end = db_u32_min(
            total_rows, db_checked_add_u32(BACKEND_NAME, "row_end", row_start,
                                           row_count_raw));
        const uint32_t row_count =
            db_checked_sub_u32(BACKEND_NAME, "row_count", row_end, row_start);
        if (row_count == 0U) {
            continue;
        }
        db_for_each_gradient_row_color(
            row_start, row_count, head_row, direction_down, cycle_index,
            db_gl1_draw_gradient_row_color, &apply_ctx);
    }
}

static size_t db_gl1_gradient_dirty_ranges_to_upload_ranges(
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_gl_upload_range_t *upload_ranges, size_t upload_capacity) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if (cols == 0U) {
        return 0U;
    }
    const size_t row_upload_bytes = (size_t)cols * DB_RECT_VERTEX_COUNT *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
    size_t out_count = 0U;
    for (size_t i = 0U; i < dirty_count && out_count < upload_capacity; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count_raw = dirty_ranges[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= rows)) {
            continue;
        }
        const uint32_t row_end =
            db_u32_min(rows, db_checked_add_u32(BACKEND_NAME, "row_end",
                                                row_start, row_count_raw));
        const uint32_t row_count =
            db_checked_sub_u32(BACKEND_NAME, "row_count", row_end, row_start);
        if (row_count == 0U) {
            continue;
        }
        upload_ranges[out_count].src_offset_bytes =
            (size_t)row_start * row_upload_bytes;
        upload_ranges[out_count].dst_offset_bytes =
            upload_ranges[out_count].src_offset_bytes;
        upload_ranges[out_count].size_bytes =
            (size_t)row_count * row_upload_bytes;
        out_count++;
    }
    return out_count;
}

static size_t
db_gl1_gradient_dirty_row_total(const db_dirty_row_range_t *dirty_ranges,
                                size_t dirty_count) {
    size_t total_rows = 0U;
    for (size_t i = 0U; i < dirty_count; i++) {
        total_rows += (size_t)dirty_ranges[i].row_count;
    }
    return total_rows;
}

static int
db_gl1_gradient_should_use_mesh(const db_dirty_row_range_t *dirty_ranges,
                                size_t dirty_count) {
    const size_t dirty_row_total =
        db_gl1_gradient_dirty_row_total(dirty_ranges, dirty_count);
    return (dirty_row_total > 0U) &&
           (dirty_row_total <= DB_GL1_GRADIENT_MESH_ROW_THRESHOLD);
}

static void
db_gl1_draw_gradient_dirty_rows_mesh(const db_dirty_row_range_t *dirty_ranges,
                                     size_t dirty_count, uint32_t head_row,
                                     int direction_down, uint32_t cycle_index) {
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    db_gl1_gradient_mesh_apply_ctx_t apply_ctx = {.cols = cols};
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count_raw = dirty_ranges[i].row_count;
        if ((row_count_raw == 0U) || (row_start >= rows)) {
            continue;
        }
        const uint32_t row_end =
            db_u32_min(rows, db_checked_add_u32(BACKEND_NAME, "row_end",
                                                row_start, row_count_raw));
        const uint32_t row_count =
            db_checked_sub_u32(BACKEND_NAME, "row_count", row_end, row_start);
        if (row_count == 0U) {
            continue;
        }
        db_for_each_gradient_row_color(
            row_start, row_count, head_row, direction_down, cycle_index,
            db_gl1_write_gradient_row_color_to_mesh, &apply_ctx);
    }

    db_gl_upload_range_t upload_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
        {0U, 0U, 0U}, {0U, 0U, 0U}, {0U, 0U, 0U}, {0U, 0U, 0U}};
    const size_t upload_count = db_gl1_gradient_dirty_ranges_to_upload_ranges(
        dirty_ranges, dirty_count, upload_ranges,
        DB_GL1_GRADIENT_REPLAY_ROW_CAP);
    if (upload_count == 0U) {
        return;
    }

    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    const size_t bytes_per_vertex =
        g_state.vertex.vertex_stride * sizeof(float);
    if (g_state.vbo != 0U) {
        db_gl_upload_ranges_target(g_state.vertex.vertices, upload_bytes,
                                   upload_ranges, upload_count,
                                   DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                                   g_state.vertex.upload.use_persistent_upload,
                                   g_state.vertex.upload.persistent_mapped_ptr,
                                   g_state.vertex.upload.use_map_range_upload,
                                   g_state.vertex.upload.use_map_buffer_upload);
        db_gl1_configure_vbo_arrays_if_needed();
    } else {
        db_gl1_configure_client_arrays_if_needed();
    }
    for (size_t i = 0U; i < upload_count; i++) {
        const db_gl_upload_range_t *range = &upload_ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % bytes_per_vertex) != 0U) ||
            ((range->size_bytes % bytes_per_vertex) != 0U)) {
            continue;
        }
        const size_t first_vertex = range->src_offset_bytes / bytes_per_vertex;
        const size_t vertex_count = range->size_bytes / bytes_per_vertex;
        if ((first_vertex + vertex_count) >
            (size_t)g_state.vertex.draw_vertex_count) {
            continue;
        }
        const GLuint first_vertex_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "first_vertex", first_vertex);
        const GLuint vertex_count_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "vertex_count", vertex_count);
        glDrawArrays(GL_TRIANGLES,
                     db_checked_u32_to_i32(BACKEND_NAME, "first_vertex",
                                           first_vertex_u32),
                     db_checked_u32_to_i32(BACKEND_NAME, "vertex_count",
                                           vertex_count_u32));
    }
}

static void
db_gl1_draw_gradient_dirty_rows_hybrid(const db_dirty_row_range_t *dirty_ranges,
                                       size_t dirty_count, uint32_t head_row,
                                       int direction_down, uint32_t cycle_index,
                                       GLint viewport_w, GLint viewport_h) {
    const int use_mesh =
        db_gl1_gradient_should_use_mesh(dirty_ranges, dirty_count);
    if (use_mesh != 0) {
        const int reopen_scissor = (g_state.rect_clear_scope_active != 0);
        if (reopen_scissor != 0) {
            glDisable(GL_SCISSOR_TEST);
        }
        db_gl1_draw_gradient_dirty_rows_mesh(
            dirty_ranges, dirty_count, head_row, direction_down, cycle_index);
        if (reopen_scissor != 0) {
            glEnable(GL_SCISSOR_TEST);
        }
        return;
    }
    db_gl1_draw_gradient_dirty_rows_gpu(dirty_ranges, dirty_count, head_row,
                                        direction_down, cycle_index, viewport_w,
                                        viewport_h);
}

static void db_gl1_draw_bands_gpu(uint32_t cols, uint32_t band_count,
                                  uint32_t frame_index, int viewport_width_px,
                                  int viewport_height_px) {
    if (band_count == 0U) {
        return;
    }

    // Draw bands as solid rects using scissor + clear.
    const int viewport_w = viewport_width_px;
    const int viewport_h = viewport_height_px;
    if (viewport_w <= 0 || viewport_h <= 0) {
        return;
    }

    for (uint32_t band = 0U; band < band_count; band++) {
        // Compute band edges in *tile-space* first (matches CPU
        // partitioning).
        const uint32_t tile_x0 = (uint32_t)(((uint64_t)band * (uint64_t)cols) /
                                            (uint64_t)band_count);
        const uint32_t tile_x1 =
            (uint32_t)(((uint64_t)(band + 1U) * (uint64_t)cols) /
                       (uint64_t)band_count);

        // Map tile-space edges to framebuffer pixels (HiDPI-safe).
        GLint x0 = (GLint)(((uint64_t)tile_x0 * (uint64_t)viewport_w) /
                           (uint64_t)cols);
        GLint x1 = (GLint)(((uint64_t)tile_x1 * (uint64_t)viewport_w) /
                           (uint64_t)cols);

        // Ensure the last band reaches the right edge (avoid division
        // gaps).
        if (band + 1U == band_count) {
            x1 = (GLint)viewport_w;
        }

        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band, band_count, frame_index, &color_r, &color_g,
                          &color_b);

        // Clamp and draw.
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > (GLint)viewport_w) {
            x1 = (GLint)viewport_w;
        }
        const GLsizei rect_w = (GLsizei)(x1 - x0);
        if (rect_w <= 0) {
            continue;
        }

        db_gl1_draw_solid_rect_pixels(x0, 0, rect_w, (GLsizei)viewport_h,
                                      viewport_w, viewport_h, color_r, color_g,
                                      color_b);
    }
}

static size_t db_collect_gl1_damage_ranges(
    const db_snake_plan_t *plan, uint32_t snake_prev_start,
    uint32_t snake_prev_count, int force_full_upload,
    const db_dirty_row_range_t *gradient_dirty_ranges,
    size_t gradient_dirty_count, db_gl_upload_range_t *range_storage,
    size_t range_capacity) {
    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    const size_t upload_tile_bytes =
        db_rect_tile_bytes(g_state.vertex.vertex_stride);
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    db_gl_upload_range_t upload_ranges[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    db_gl_pattern_upload_collect_t collect_ctx = {
        .pattern = g_state.runtime.pattern,
        .cols = cols,
        .rows = rows,
        .upload_bytes = upload_bytes,
        .upload_tile_bytes = upload_tile_bytes,
        .force_full_upload = force_full_upload,
        .snake_plan = plan,
        .snake_prev_start = snake_prev_start,
        .snake_prev_count = snake_prev_count,
        .pattern_seed = g_state.runtime.pattern_seed,
        .snake_spans = g_state.snake_spans,
        .snake_scratch_capacity = g_state.snake_scratch_capacity,
        .snake_row_bounds = g_state.snake_row_bounds,
        .snake_row_bounds_capacity = g_state.snake_row_bounds_capacity,
        .damage_row_ranges = gradient_dirty_ranges,
        .damage_row_count = gradient_dirty_count,
    };
    db_gl_upload_range_t *local_range_storage = upload_ranges;
    size_t local_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) {
        local_range_storage = g_state.snake_replay.curr_upload_ranges;
        local_range_capacity = g_state.snake_scratch_capacity;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        local_range_capacity = DB_GL1_GRADIENT_DIRTY_RANGE_CAP;
    } else {
        local_range_capacity = 1U;
    }
    if ((range_storage != NULL) && (range_capacity > 0U)) {
        local_range_storage = range_storage;
        local_range_capacity = range_capacity;
    }
    return db_gl_collect_pattern_upload_ranges(
        &collect_ctx, local_range_storage, local_range_capacity);
}

static size_t db_gl1_append_nonzero_ranges(const db_dirty_row_range_t *ranges,
                                           size_t range_count,
                                           db_dirty_row_range_t *out_ranges,
                                           size_t out_capacity,
                                           size_t out_count) {
    for (size_t index = 0U; index < range_count && out_count < out_capacity;
         index++) {
        if (ranges[index].row_count == 0U) {
            continue;
        }
        out_ranges[out_count++] = ranges[index];
    }
    return out_count;
}

static size_t db_gl1_build_curr_draw_ranges(
    const db_dirty_row_range_t *skipped_ranges, size_t skipped_count,
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_dirty_row_range_t *out_ranges, size_t out_capacity) {
    size_t out_count = db_gl1_append_nonzero_ranges(
        skipped_ranges, skipped_count, out_ranges, out_capacity, 0U);
    out_count = db_gl1_append_nonzero_ranges(
        dirty_ranges, dirty_count, out_ranges, out_capacity, out_count);
    return out_count;
}

static void
db_gl1_persist_gradient_replay_frame(const db_dirty_row_range_t *persist_ranges,
                                     size_t persist_count, uint32_t head_row,
                                     int direction_down, uint32_t cycle_index) {
    const size_t limited_count =
        (persist_count < DB_GL1_GRADIENT_REPLAY_ROW_CAP)
            ? persist_count
            : DB_GL1_GRADIENT_REPLAY_ROW_CAP;
    db_dirty_row_range_t persist_snapshot[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
        {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
    for (size_t i = 0U; i < limited_count; i++) {
        persist_snapshot[i] = persist_ranges[i];
    }
    for (size_t i = 0U; i < DB_GL1_GRADIENT_REPLAY_ROW_CAP; i++) {
        g_state.gradient_prev_frame.draw_rows[i] =
            (db_dirty_row_range_t){0U, 0U};
    }
    for (size_t i = 0U; i < limited_count; i++) {
        g_state.gradient_prev_frame.draw_rows[i] = persist_snapshot[i];
    }
    g_state.gradient_prev_frame.draw_count = limited_count;
    g_state.gradient_prev_frame.state.head_row = head_row;
    g_state.gradient_prev_frame.state.direction_down = direction_down;
    g_state.gradient_prev_frame.state.cycle_index = cycle_index;
}

static void
db_upload_vbo_damage_ranges(const db_gl_upload_range_t *range_storage,
                            size_t upload_range_count) {
    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    if (upload_range_count == 0U) {
        return;
    }

    db_gl_upload_ranges_target(g_state.vertex.vertices, upload_bytes,
                               range_storage, upload_range_count,
                               DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                               g_state.vertex.upload.use_persistent_upload,
                               g_state.vertex.upload.persistent_mapped_ptr,
                               g_state.vertex.upload.use_map_range_upload,
                               g_state.vertex.upload.use_map_buffer_upload);
}

static void db_gl1_dirty_ranges_draw(const db_gl_upload_range_t *ranges,
                                     size_t range_count) {
    const size_t bytes_per_vertex =
        g_state.vertex.vertex_stride * sizeof(float);
    if (bytes_per_vertex == 0U) {
        return;
    }
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % bytes_per_vertex) != 0U) ||
            ((range->size_bytes % bytes_per_vertex) != 0U)) {
            continue;
        }
        const size_t first_vertex = range->src_offset_bytes / bytes_per_vertex;
        const size_t vertex_count = range->size_bytes / bytes_per_vertex;
        if ((first_vertex + vertex_count) >
            (size_t)g_state.vertex.draw_vertex_count) {
            continue;
        }
        const GLuint first_vertex_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "first_vertex", first_vertex);
        const GLuint vertex_count_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "vertex_count", vertex_count);
        glDrawArrays(GL_TRIANGLES,
                     db_checked_u32_to_i32(BACKEND_NAME, "first_vertex",
                                           first_vertex_u32),
                     db_checked_u32_to_i32(BACKEND_NAME, "vertex_count",
                                           vertex_count_u32));
    }
}

// Draws using the currently-bound arrays, assumed to be VBO-backed.
static void db_gl1_dirty_ranges_draw_vbo(const db_gl_upload_range_t *ranges,
                                         size_t range_count) {
    const size_t bytes_per_vertex =
        g_state.vertex.vertex_stride * sizeof(float);
    if (bytes_per_vertex == 0U) {
        return;
    }

    // Assumes ARRAY_BUFFER is bound to the renderer VBO and vertex/color
    // pointers are configured as VBO offsets.
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % bytes_per_vertex) != 0U) ||
            ((range->size_bytes % bytes_per_vertex) != 0U)) {
            continue;
        }
        const size_t first_vertex = range->src_offset_bytes / bytes_per_vertex;
        const size_t vertex_count = range->size_bytes / bytes_per_vertex;
        if ((first_vertex + vertex_count) >
            (size_t)g_state.vertex.draw_vertex_count) {
            continue;
        }

        const GLuint first_vertex_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "first_vertex", first_vertex);
        const GLuint vertex_count_u32 =
            db_checked_size_to_u32(BACKEND_NAME, "vertex_count", vertex_count);

        glDrawArrays(GL_TRIANGLES,
                     db_checked_u32_to_i32(BACKEND_NAME, "first_vertex",
                                           first_vertex_u32),
                     db_checked_u32_to_i32(BACKEND_NAME, "vertex_count",
                                           vertex_count_u32));
    }
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    g_state.is_es_context =
        db_gl_is_es_context((const char *)glGetString(GL_VERSION));
    g_state.vertex.vertex_stride = (g_state.is_es_context != 0)
                                       ? DB_ES_VERTEX_FLOAT_STRIDE
                                       : DB_VERTEX_FLOAT_STRIDE;

    if (!db_init_vertices_for_mode(g_state.vertex.vertex_stride)) {
        failf("failed to allocate benchmark vertex buffers");
    }
    g_state.snake_replay.curr_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_count = 0U;
    g_state.snake_spans = NULL;
    g_state.snake_row_bounds = NULL;
    g_state.snake_row_bounds_capacity = 0U;
    g_state.snake_scratch_capacity = 0U;
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) {
        const size_t scratch_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        g_state.snake_replay.curr_upload_ranges =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_upload_ranges", scratch_capacity,
                sizeof(*g_state.snake_replay.curr_upload_ranges));
        g_state.snake_replay.prev_upload_ranges =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_prev_upload_ranges", scratch_capacity,
                sizeof(*g_state.snake_replay.prev_upload_ranges));
        g_state.snake_spans = (db_snake_col_span_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_spans", scratch_capacity,
            sizeof(*g_state.snake_spans));
        if (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES) {
            g_state.snake_row_bounds =
                (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                    sizeof(*g_state.snake_row_bounds));
            g_state.snake_row_bounds_capacity =
                (size_t)db_grid_rows_effective();
        }
        g_state.snake_scratch_capacity = scratch_capacity;
    }

    db_gl_upload_probe_result_t probe_result = {0};

    g_state.vertex.upload = (db_gl_upload_probe_result_t){0};
    g_state.vbo = 0U;
    g_state.backbuffer_valid = 0;
    for (size_t i = 0U; i < DB_GL1_GRADIENT_REPLAY_ROW_CAP; i++) {
        g_state.gradient_prev_frame.draw_rows[i] =
            (db_dirty_row_range_t){0U, 0U};
    }
    g_state.gradient_prev_frame.draw_count = 0U;
    g_state.gradient_prev_frame.state.head_row = 0U;
    g_state.gradient_prev_frame.state.direction_down = 1;
    g_state.gradient_prev_frame.state.cycle_index = 0U;

    g_state.capability_mode[0] = '\0';
    db_gl1_refresh_capability_mode();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    db_gl1_configure_client_arrays_if_needed();

    if (db_gl_context_supports_vbo() != 0) {
        const size_t probe_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.vbo = (GLuint)vbo_u32;
        }
        if (g_state.vbo != 0U) {
            if (db_gl_bind_array_buffer_cached((unsigned int)g_state.vbo,
                                               &g_state.bound_array_buffer) ==
                0) {
                db_gl_vbo_delete_if_valid((unsigned int)g_state.vbo);
                g_state.vbo = 0U;
            }
        }
        if (g_state.vbo != 0U) {
            db_gl_probe_upload_capabilities(
                probe_bytes, g_state.vertex.vertices, &probe_result);
            g_state.vertex.upload = probe_result;
            g_state.vbo_arrays_configured = 0;
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_refresh_capability_mode();
            infof("using capability mode: %s",
                  db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            return;
        }
    }

    db_gl1_refresh_capability_mode();
    infof("using capability mode: %s",
          db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(uint32_t frame_index,
                                                   int viewport_width_px,
                                                   int viewport_height_px,
                                                   int double_buffered) {
    db_snake_plan_t plan = {0};
    uint32_t snake_prev_start = 0U;
    uint32_t snake_prev_count = 0U;
    int force_full_upload = 0;
    db_dirty_row_range_t gradient_dirty_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] =
        {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
    size_t gradient_dirty_count = 0U;
    uint32_t gradient_render_head_row = 0U;
    int gradient_render_direction_down = 1;
    uint32_t gradient_render_cycle_index = 0U;

    // If the platform backend has not yet reported a drawable size, fall
    // back to the configured tile grid size so pixel-space operations
    // (viewport/scissor/history allocation) still function.
    if (viewport_width_px <= 0 || viewport_height_px <= 0) {
        viewport_width_px = db_checked_u32_to_i32(
            BACKEND_NAME, "viewport_width_px", db_grid_cols_effective());
        viewport_height_px = db_checked_u32_to_i32(
            BACKEND_NAME, "viewport_height_px", db_grid_rows_effective());
    }

    const int viewport_changed =
        (g_state.last_viewport_w != viewport_width_px) ||
        (g_state.last_viewport_h != viewport_height_px);

    if (viewport_changed != 0) {
        // Keep GL viewport in sync with drawable pixels (HiDPI-safe)
        // without querying GL state.
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
        g_state.last_viewport_w = viewport_width_px;
        g_state.last_viewport_h = viewport_height_px;

        // Any backbuffer assumptions are invalid after resize.
        g_state.backbuffer_valid = 0;
        g_state.snake_replay.prev_upload_count = 0U;
    }

    // Preserved-backbuffer assumption for snake paths: dirty draws operate
    // directly on the default framebuffer.

    // Use a known viewport size without querying GL state.
    // Pixel-space ops must use drawable pixels; tile math stays on grid
    // cols/rows.
    const GLint viewport_w = (GLint)viewport_width_px;
    const GLint viewport_h = (GLint)viewport_height_px;
    const int is_snake_pattern =
        db_pattern_uses_history_texture(g_state.runtime.pattern);
    const int is_gradient_pattern =
        (g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL);
    const int is_bands_pattern = (g_state.runtime.pattern == DB_PATTERN_BANDS);
    const int use_frame_solid_rect_scope =
        (is_gradient_pattern != 0) || (is_bands_pattern != 0);

    if (use_frame_solid_rect_scope != 0) {
        glEnable(GL_SCISSOR_TEST);
        g_state.rect_clear_scope_active = 1;
    }

    if (is_snake_pattern != 0) {
        const int can_replay_snake =
            (double_buffered != 0) &&
            (g_state.runtime.backbuffer_draw_full == 0) &&
            (g_state.backbuffer_valid != 0) &&
            (g_state.snake_replay.prev_upload_count > 0U);
        if (can_replay_snake != 0) {
            if (g_state.vbo != 0U) {
                db_upload_vbo_damage_ranges(
                    g_state.snake_replay.prev_upload_ranges,
                    g_state.snake_replay.prev_upload_count);
                db_gl1_configure_vbo_arrays_if_needed();
                db_gl1_dirty_ranges_draw_vbo(
                    g_state.snake_replay.prev_upload_ranges,
                    g_state.snake_replay.prev_upload_count);
            } else {
                db_gl1_configure_client_arrays_if_needed();
                db_gl1_dirty_ranges_draw(
                    g_state.snake_replay.prev_upload_ranges,
                    g_state.snake_replay.prev_upload_count);
            }
        }

        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const int is_shapes =
            (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES);
        if (is_grid == 0) {
            snake_prev_start = g_state.runtime.snake.prev_start;
            snake_prev_count = g_state.runtime.snake.prev_count;
        }
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake.shape_index, g_state.runtime.snake.cursor,
            g_state.runtime.snake.prev_start, g_state.runtime.snake.prev_count,
            g_state.runtime.mode_phase_flag, g_state.runtime.bench_speed_step);
        plan = db_snake_plan_next_step(&request);
        db_snake_step_target_t target = db_snake_step_target_from_plan(
            is_grid, g_state.runtime.pattern_seed, &plan);
        const db_snake_shape_kind_t shape_kind =
            (is_shapes != 0) ? target.shape_kind : DB_SNAKE_SHAPE_RECT;
        if (target.has_next_mode_phase_flag != 0) {
            g_state.runtime.mode_phase_flag = target.next_mode_phase_flag;
        }
        if (is_grid == 0) {
            if (target.has_next_shape_index != 0) {
                g_state.runtime.snake.shape_index = target.next_shape_index;
            }
            if (plan.wrapped != 0) {
                g_state.runtime.snake.prev_count = 0U;
            }
        }
        db_render_snake_step(
            &plan, &target.region, shape_kind, g_state.runtime.pattern_seed,
            plan.active_shape_index, target.target_r, target.target_g,
            target.target_b, target.full_fill_on_phase_completed);
        if ((g_state.runtime.backbuffer_draw_full == 0) &&
            (g_state.backbuffer_valid == 0)) {
            force_full_upload = 1;
        }
        g_state.runtime.snake.prev_start = plan.next_prev_start;
        g_state.runtime.snake.prev_count = plan.next_prev_count;
        g_state.runtime.snake.cursor = plan.next_cursor;
    } else if (is_gradient_pattern != 0) {
        // Gradient draws solid rect rows directly to the default framebuffer.
        // Upload mode is none (no VBO updates). Backbuffer replay depends on
        // double buffering.
        const db_gradient_damage_plan_t gradient_plan =
            db_gradient_step_from_runtime(g_state.runtime.pattern,
                                          g_state.runtime.gradient.head_row,
                                          g_state.runtime.mode_phase_flag,
                                          g_state.runtime.gradient.cycle_index,
                                          g_state.runtime.bench_speed_step);

        // Render MUST use the plan's render_* state. The plan's next_*
        // state is only applied to the runtime AFTER we draw, matching the
        // CPU renderer semantics.
        gradient_render_head_row = gradient_plan.render_state.head_row;
        gradient_render_direction_down =
            gradient_plan.render_state.direction_down;
        gradient_render_cycle_index = gradient_plan.render_state.cycle_index;

        // Collect dirty ranges from the plan.
        gradient_dirty_count = db_gradient_collect_dirty_ranges(
            &gradient_plan, gradient_dirty_ranges);

        // When bench_speed_step > 1, the head can advance by multiple rows.
        // The plan's dirty ranges cover the *new* sweep window, but rows
        // that were traversed between render_head_row and next_head_row can
        // be skipped unless we repaint them as solid per-row colors.
        db_dirty_row_range_t skipped_ranges[2] = {{0U, 0U}, {0U, 0U}};
        size_t skipped_count = 0U;

        const uint32_t rows = db_grid_rows_effective();
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
                    // Downwards: skipped rows are render+1 ..
                    // render+skipped.
                    const uint32_t start_row = (render_head + 1U) % rows;
                    skipped_ranges[0].row_start = start_row;
                    skipped_ranges[0].row_count =
                        db_u32_min(rows - start_row, skipped_rows);
                    skipped_ranges[1].row_start = 0U;
                    skipped_ranges[1].row_count =
                        skipped_rows - skipped_ranges[0].row_count;
                    skipped_count =
                        (skipped_ranges[1].row_count > 0U) ? 2U : 1U;
                } else {
                    // Upwards: skipped rows are render-1 .. render-skipped.
                    // Express in forward ranges (increasing row indices).
                    if (render_head >= skipped_rows) {
                        // No wrap past 0: [render-skipped .. render-1]
                        skipped_ranges[0].row_start =
                            render_head - skipped_rows;
                        skipped_ranges[0].row_count = skipped_rows;
                        skipped_count = (skipped_rows > 0U) ? 1U : 0U;
                    } else {
                        // Wraps past 0: [0 .. render-1] and
                        // [rows-underflow .. rows-1]
                        const uint32_t underflow = skipped_rows - render_head;
                        skipped_ranges[0].row_start = 0U;
                        skipped_ranges[0].row_count = render_head;
                        skipped_ranges[1].row_start = rows - underflow;
                        skipped_ranges[1].row_count = underflow;
                        skipped_count = 0U;
                        if (skipped_ranges[0].row_count > 0U) {
                            skipped_count++;
                        }
                        if (skipped_ranges[1].row_count > 0U) {
                            skipped_count++;
                        }
                    }
                }
            }
        }

        // Gradient patterns: damage-only updates directly on the default
        // framebuffer/backbuffer.
        //
        // Double-buffered caveat: on frame N we draw into backbuffer B, but
        // on frame N+1 we draw into the other backbuffer A which typically
        // still contains frame N-1. To make A match frame N before applying
        // the new damage, we replay the prior frame's damage ranges
        // (computed in tile space) onto A first.
        if ((viewport_w > 0) && (viewport_h > 0)) {
            const uint32_t rows = db_grid_rows_effective();
            if (rows > 0U) {
                if (g_state.runtime.backbuffer_draw_full != 0) {
                    db_dirty_row_range_t full_range[2] = {{0U, 0U}, {0U, 0U}};
                    full_range[0].row_start = 0U;
                    full_range[0].row_count = rows;
                    db_gl1_draw_gradient_dirty_rows_hybrid(
                        full_range, 1U, gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index, viewport_w, viewport_h);
                    g_state.backbuffer_valid = 1;
                    for (size_t i = 0U; i < DB_GL1_GRADIENT_REPLAY_ROW_CAP;
                         i++) {
                        g_state.gradient_prev_frame.draw_rows[i] =
                            (db_dirty_row_range_t){0U, 0U};
                    }
                    g_state.gradient_prev_frame.draw_rows[0] = full_range[0];
                    g_state.gradient_prev_frame.draw_count = 1U;
                    g_state.gradient_prev_frame.state.head_row =
                        gradient_render_head_row;
                    g_state.gradient_prev_frame.state.direction_down =
                        gradient_render_direction_down;
                    g_state.gradient_prev_frame.state.cycle_index =
                        gradient_render_cycle_index;
                    g_state.full_draw_frames++;
                } else {
                    // Build the set of row ranges we will actually draw this
                    // frame (skipped traversed rows first, then the plan's
                    // dirty ranges).
                    db_dirty_row_range_t
                        curr_draw_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
                            {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
                    size_t curr_draw_count = db_gl1_build_curr_draw_ranges(
                        skipped_ranges, skipped_count, gradient_dirty_ranges,
                        gradient_dirty_count, curr_draw_ranges,
                        DB_GL1_GRADIENT_REPLAY_ROW_CAP);
                    // On resize/first frame we must fully seed the backbuffer.
                    const int need_full_seed = (g_state.backbuffer_valid == 0);
                    const db_dirty_row_range_t *persist_ranges =
                        curr_draw_ranges;
                    size_t persist_count = curr_draw_count;
                    db_gradient_state_t persist_state = {
                        .head_row = gradient_render_head_row,
                        .cycle_index = gradient_render_cycle_index,
                        .direction_down = gradient_render_direction_down,
                    };

                    if (need_full_seed != 0) {
                        db_dirty_row_range_t full_range[2] = {{0U, 0U},
                                                              {0U, 0U}};
                        full_range[0].row_start = 0U;
                        full_range[0].row_count = rows;
                        db_gl1_draw_gradient_dirty_rows_hybrid(
                            full_range, 1U, gradient_render_head_row,
                            gradient_render_direction_down,
                            gradient_render_cycle_index, viewport_w,
                            viewport_h);
                        g_state.backbuffer_valid = 1;

                        // For double-buffer replay next frame, treat the seed
                        // as a full-range draw.
                        curr_draw_ranges[0] = full_range[0];
                        persist_count = 1U;
                        g_state.full_draw_frames++;
                    } else {
                        // Double-buffered: replay prior frame's damage first so
                        // the current backbuffer matches the last displayed
                        // frame.
                        const int has_replay =
                            (double_buffered != 0) &&
                            (g_state.gradient_prev_frame.draw_count > 0U);
                        const int replay_uses_mesh =
                            (has_replay != 0) &&
                            db_gl1_gradient_should_use_mesh(
                                g_state.gradient_prev_frame.draw_rows,
                                g_state.gradient_prev_frame.draw_count);
                        const int has_current = (curr_draw_count > 0U);
                        const int current_uses_mesh =
                            (has_current != 0) &&
                            db_gl1_gradient_should_use_mesh(curr_draw_ranges,
                                                            curr_draw_count);
                        if ((g_state.runtime.pattern ==
                             DB_PATTERN_GRADIENT_SWEEP) &&
                            (has_replay != 0) && (has_current == 0)) {
                            // At bottom bounce, sweep can be replay-only for
                            // one frame. Keep replay ranges for the next
                            // frame so double-buffer persistence does not drop
                            // to empty across the direction flip.
                            persist_ranges =
                                g_state.gradient_prev_frame.draw_rows;
                            persist_count =
                                g_state.gradient_prev_frame.draw_count;
                            persist_state = g_state.gradient_prev_frame.state;
                        }

                        // Fast path: when both replay and current updates are
                        // rect-driven, use the rect path directly for both.
                        if ((has_replay != 0) && (has_current != 0) &&
                            (replay_uses_mesh == 0) &&
                            (current_uses_mesh == 0)) {
                            db_gl1_draw_gradient_dirty_rows_gpu(
                                g_state.gradient_prev_frame.draw_rows,
                                g_state.gradient_prev_frame.draw_count,
                                g_state.gradient_prev_frame.state.head_row,
                                g_state.gradient_prev_frame.state
                                    .direction_down,
                                g_state.gradient_prev_frame.state.cycle_index,
                                viewport_w, viewport_h);
                            db_gl1_draw_gradient_dirty_rows_gpu(
                                curr_draw_ranges, curr_draw_count,
                                gradient_render_head_row,
                                gradient_render_direction_down,
                                gradient_render_cycle_index, viewport_w,
                                viewport_h);
                        } else {
                            if (has_replay != 0) {
                                db_gl1_draw_gradient_dirty_rows_hybrid(
                                    g_state.gradient_prev_frame.draw_rows,
                                    g_state.gradient_prev_frame.draw_count,
                                    g_state.gradient_prev_frame.state.head_row,
                                    g_state.gradient_prev_frame.state
                                        .direction_down,
                                    g_state.gradient_prev_frame.state
                                        .cycle_index,
                                    viewport_w, viewport_h);
                            }

                            // Apply this frame's damage.
                            if (has_current != 0) {
                                db_gl1_draw_gradient_dirty_rows_hybrid(
                                    curr_draw_ranges, curr_draw_count,
                                    gradient_render_head_row,
                                    gradient_render_direction_down,
                                    gradient_render_cycle_index, viewport_w,
                                    viewport_h);
                            }
                        }
                        g_state.dirty_draw_frames++;
                    }

                    // Persist the damage we drew this frame for the next
                    // double-buffer replay.
                    db_gl1_persist_gradient_replay_frame(
                        persist_ranges, persist_count, persist_state.head_row,
                        persist_state.direction_down,
                        persist_state.cycle_index);
                }
            }
        }

        // Apply step AFTER rendering (CPU renderer ordering).
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_plan);
    } else if (is_bands_pattern != 0) {
        // Bands are solid rect fills directly to the default framebuffer.
        db_gl1_draw_bands_gpu(db_grid_cols_effective(), BENCH_BANDS,
                              frame_index, viewport_width_px,
                              viewport_height_px);
        g_state.full_draw_frames++;
    }

    if (is_snake_pattern != 0) {
        db_gl_upload_range_t draw_ranges[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
            {0U, 0U, 0U}};
        size_t draw_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
        if ((g_state.snake_replay.curr_upload_ranges != NULL) &&
            (g_state.snake_scratch_capacity > 0U)) {
            draw_range_capacity = g_state.snake_scratch_capacity;
        } else {
            draw_range_capacity = 1U;
        }
        db_gl_upload_range_t *range_storage = draw_ranges;
        if ((draw_range_capacity == g_state.snake_scratch_capacity) &&
            (g_state.snake_replay.curr_upload_ranges != NULL)) {
            range_storage = g_state.snake_replay.curr_upload_ranges;
        }
        size_t draw_range_count = db_collect_gl1_damage_ranges(
            &plan, snake_prev_start, snake_prev_count, force_full_upload,
            gradient_dirty_ranges, gradient_dirty_count, range_storage,
            draw_range_capacity);
        const int allow_empty_dirty_draw =
            (is_snake_pattern != 0) &&
            (g_state.runtime.backbuffer_draw_full == 0);
        if ((draw_range_count == 0U) && (allow_empty_dirty_draw == 0)) {
            const size_t upload_bytes =
                (size_t)g_state.vertex.draw_vertex_count *
                g_state.vertex.vertex_stride * sizeof(float);
            range_storage[0] = (db_gl_upload_range_t){0U, 0U, upload_bytes};
            draw_range_count = 1U;
        }
        const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
        if (g_state.runtime.backbuffer_draw_full != 0) {
            range_storage[0] = (db_gl_upload_range_t){0U, 0U, upload_bytes};
            draw_range_count = 1U;
        }
        const int draw_is_full_mesh =
            (draw_range_count == 1U) &&
            (range_storage[0].src_offset_bytes == 0U) &&
            (range_storage[0].size_bytes == upload_bytes);

        if (draw_range_count == 0U) {
            // No damage this frame; preserve backbuffer contents.
        } else if (draw_is_full_mesh != 0) {
            if (g_state.vbo != 0U) {
                db_upload_vbo_damage_ranges(range_storage, draw_range_count);
                db_gl1_configure_vbo_arrays_if_needed();
            } else {
                db_gl1_configure_client_arrays_if_needed();
            }
            glDrawArrays(GL_TRIANGLES, 0,
                         (GLsizei)db_gl_draw_vertex_count_i32(
                             BACKEND_NAME, g_state.vertex.draw_vertex_count));
            g_state.full_draw_frames++;
        } else {
            // Preserved-backbuffer dirty redraw path: upload+draw changed
            // ranges.
            if (g_state.vbo != 0U) {
                db_upload_vbo_damage_ranges(range_storage, draw_range_count);
                db_gl1_configure_vbo_arrays_if_needed();
                db_gl1_dirty_ranges_draw_vbo(range_storage, draw_range_count);
            } else {
                db_gl1_configure_client_arrays_if_needed();
                db_gl1_dirty_ranges_draw(range_storage, draw_range_count);
            }
            g_state.dirty_draw_frames++;
        }
        if ((draw_range_count > 0U) &&
            (g_state.snake_replay.prev_upload_ranges != NULL) &&
            (draw_range_count <= g_state.snake_scratch_capacity)) {
            for (size_t i = 0U; i < draw_range_count; i++) {
                g_state.snake_replay.prev_upload_ranges[i] = range_storage[i];
            }
            g_state.snake_replay.prev_upload_count = draw_range_count;
        } else if (draw_range_count > 0U) {
            g_state.snake_replay.prev_upload_count = 0U;
        }
        // Keep previous replay ranges across zero-damage frames so the
        // next backbuffer can still be brought forward in double-buffered
        // preserved-backbuffer mode.
        g_state.backbuffer_valid = 1;
    }
    if (use_frame_solid_rect_scope != 0) {
        glDisable(GL_SCISSOR_TEST);
        g_state.rect_clear_scope_active = 0;
    }

    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame_index++;
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_bind_array_buffer_cached((unsigned int)g_state.vbo,
                                             &g_state.bound_array_buffer);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.vbo != 0U) {
        db_gl_vbo_delete_if_valid((unsigned int)g_state.vbo);
        g_state.vbo = 0U;
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    free(g_state.snake_replay.curr_upload_ranges);
    free(g_state.snake_replay.prev_upload_ranges);
    free(g_state.snake_spans);
    free(g_state.snake_row_bounds);
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
    return g_state.state_hash;
}

void db_renderer_opengl_gl1_5_gles1_1_draw_stats(uint64_t *full_draw_frames,
                                                 uint64_t *dirty_draw_frames) {
    if (full_draw_frames != NULL) {
        *full_draw_frames = g_state.full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = g_state.dirty_draw_frames;
    }
}
