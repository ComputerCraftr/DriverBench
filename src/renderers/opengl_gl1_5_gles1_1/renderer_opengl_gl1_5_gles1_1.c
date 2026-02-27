#include "renderer_opengl_gl1_5_gles1_1.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
#define DB_CAP_MODE_OPENGL_CLIENT_ARRAY "opengl_client_array"
#define DB_CAP_MODE_OPENGL_GPU_HISTORY_DIRTY_DRAW                              \
    "opengl_gpu_history_dirty_draw"
#define DB_CAP_MODE_OPENGL_VBO "opengl_vbo"
#define DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER "opengl_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_VBO_MAP_RANGE "opengl_vbo_map_range"
#define DB_CAP_MODE_OPENGL_VBO_PERSISTENT "opengl_vbo_persistent"
#define ES_STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

enum {
    DB_GL1_QUAD_V0_X = 0,
    DB_GL1_QUAD_V0_Y = 1,
    DB_GL1_QUAD_V1_X = 2,
    DB_GL1_QUAD_V1_Y = 3,
    DB_GL1_QUAD_V2_X = 4,
    DB_GL1_QUAD_V2_Y = 5,
    DB_GL1_QUAD_V3_X = 6,
    DB_GL1_QUAD_V3_Y = 7,
};

typedef struct {
    uint64_t state_hash;
    uint32_t frame_index;
    db_benchmark_runtime_init_t runtime;
    db_gl_vertex_init_t vertex;
    int is_es_context;
    int snake_reset_pending;
    db_gl_upload_range_t *snake_upload_ranges;
    db_snake_col_span_t *snake_spans;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    size_t snake_scratch_capacity;
    GLfloat history_texcoords[8];
    GLfloat history_vertices[8];
    GLint history_height;
    GLint history_width;
    GLuint history_tex;
    int history_fallback_warned;
    int history_valid;
    GLuint vbo;
} renderer_state_t;

static renderer_state_t g_state = {0};

// NOLINTBEGIN(performance-no-int-to-ptr)
static const GLvoid *vbo_offset_ptr(size_t byte_offset) {
    return (const GLvoid *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static GLsizei db_draw_vertex_count_glsizei(void) {
    return (GLsizei)db_checked_u32_to_i32(BACKEND_NAME, "draw_vertex_count",
                                          g_state.vertex.draw_vertex_count);
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

static void db_blend_grid_row_to_color(uint32_t row, float target_r,
                                       float target_g, float target_b,
                                       float blend_factor) {
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t cols = db_grid_cols_effective();
    if ((rows == 0U) || (cols == 0U) || (blend_factor <= 0.0F)) {
        return;
    }
    if (blend_factor > 1.0F) {
        blend_factor = 1.0F;
    }

    const uint32_t row_wrapped = row % rows;
    for (uint32_t col = 0U; col < cols; col++) {
        const uint32_t tile_index = (row_wrapped * cols) + col;
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        const float prior_r = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        const float prior_g = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        const float prior_b = unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
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

static void db_apply_gradient_dirty_rows(uint32_t row_start, uint32_t row_count,
                                         uint32_t head_row, int direction_down,
                                         uint32_t cycle_index) {
    const uint32_t rows = db_grid_rows_effective();
    if ((rows == 0U) || (row_count == 0U)) {
        return;
    }
    for (uint32_t i = 0U; i < row_count; i++) {
        const uint32_t row = row_start + i;
        if (row >= rows) {
            break;
        }
        float row_r = 0.0F;
        float row_g = 0.0F;
        float row_b = 0.0F;
        db_gradient_row_color_rgb(row, head_row, direction_down, cycle_index,
                                  &row_r, &row_g, &row_b);
        db_blend_grid_row_to_color(row, row_r, row_g, row_b, 1.0F);
    }
}

static size_t db_rect_tile_bytes(size_t floats_per_vertex) {
    return (size_t)DB_RECT_VERTEX_COUNT * sizeof(float) * floats_per_vertex;
}

static void db_gl1_history_quad_init(void) {
    g_state.history_vertices[DB_GL1_QUAD_V0_X] = -1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V0_Y] = -1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V1_X] = 1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V1_Y] = -1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V2_X] = -1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V2_Y] = 1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V3_X] = 1.0F;
    g_state.history_vertices[DB_GL1_QUAD_V3_Y] = 1.0F;

    g_state.history_texcoords[DB_GL1_QUAD_V0_X] = 0.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V0_Y] = 0.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V1_X] = 1.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V1_Y] = 0.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V2_X] = 0.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V2_Y] = 1.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V3_X] = 1.0F;
    g_state.history_texcoords[DB_GL1_QUAD_V3_Y] = 1.0F;
}

static int db_gl1_ensure_history_textures(void) {
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) == 0) {
        return 0;
    }

    int viewport_w = 0;
    int viewport_h = 0;
    if (db_gl_get_viewport_size(&viewport_w, &viewport_h) == 0) {
        return 0;
    }
    if (g_state.history_tex == 0U) {
        if (db_gl_texture_create_rgba((unsigned int *)&g_state.history_tex,
                                      viewport_w, viewport_h, GL_RGBA,
                                      NULL) == 0) {
            return 0;
        }
        g_state.history_width = viewport_w;
        g_state.history_height = viewport_h;
        g_state.history_valid = 0;
        return 1;
    }
    if ((g_state.history_width != viewport_w) ||
        (g_state.history_height != viewport_h)) {
        if (db_gl_texture_allocate_rgba((unsigned int)g_state.history_tex,
                                        viewport_w, viewport_h, GL_RGBA,
                                        NULL) == 0) {
            return 0;
        }
        g_state.history_width = viewport_w;
        g_state.history_height = viewport_h;
        g_state.history_valid = 0;
    }
    return 1;
}

static void db_gl1_restore_history_to_framebuffer(void) {
    if ((g_state.history_tex == 0U) || (g_state.history_valid == 0)) {
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, g_state.history_vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, g_state.history_texcoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisable(GL_TEXTURE_2D);
}

static void db_gl1_capture_history_full_framebuffer(void) {
    if ((g_state.history_tex == 0U) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_state.history_width,
                        g_state.history_height);
    g_state.history_valid = 1;
}

static int db_gl1_history_range_to_copy_rect(const db_gl_upload_range_t *range,
                                             GLint viewport_w, GLint viewport_h,
                                             GLint *x_out, GLint *y_out,
                                             GLsizei *w_out, GLsizei *h_out) {
    if ((range == NULL) || (viewport_w <= 0) || (viewport_h <= 0) ||
        (x_out == NULL) || (y_out == NULL) || (w_out == NULL) ||
        (h_out == NULL)) {
        return 0;
    }
    const size_t bytes_per_vertex =
        g_state.vertex.vertex_stride * sizeof(float);
    if ((bytes_per_vertex == 0U) ||
        ((range->src_offset_bytes % bytes_per_vertex) != 0U) ||
        ((range->size_bytes % bytes_per_vertex) != 0U)) {
        return 0;
    }
    const size_t first_vertex = range->src_offset_bytes / bytes_per_vertex;
    const size_t vertex_count = range->size_bytes / bytes_per_vertex;
    if (vertex_count == 0U) {
        return 0;
    }
    const size_t draw_vertex_count = (size_t)g_state.vertex.draw_vertex_count;
    if ((first_vertex + vertex_count) > draw_vertex_count) {
        return 0;
    }

    float min_x = 1.0F;
    float min_y = 1.0F;
    float max_x = -1.0F;
    float max_y = -1.0F;
    for (size_t i = 0U; i < vertex_count; i++) {
        const size_t vertex_index = first_vertex + i;
        const float *vertex =
            &g_state.vertex
                 .vertices[vertex_index * g_state.vertex.vertex_stride];
        const float x_pos = vertex[0];
        const float y_pos = vertex[1];
        min_x = (x_pos < min_x) ? x_pos : min_x;
        min_y = (y_pos < min_y) ? y_pos : min_y;
        max_x = (x_pos > max_x) ? x_pos : max_x;
        max_y = (y_pos > max_y) ? y_pos : max_y;
    }

    if ((max_x < min_x) || (max_y < min_y)) {
        return 0;
    }
    const float fw = (float)viewport_w;
    const float fh = (float)viewport_h;
    const float px0_f = ((min_x + 1.0F) * 0.5F) * fw;
    const float py0_f = ((min_y + 1.0F) * 0.5F) * fh;
    const float px1_f = ((max_x + 1.0F) * 0.5F) * fw;
    const float py1_f = ((max_y + 1.0F) * 0.5F) * fh;

    GLint x0 = (GLint)floorf(px0_f);
    GLint y0 = (GLint)floorf(py0_f);
    GLint x1 = (GLint)ceilf(px1_f);
    GLint y1 = (GLint)ceilf(py1_f);

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
        return 0;
    }

    *x_out = x0;
    *y_out = y0;
    *w_out = (GLsizei)(x1 - x0);
    *h_out = (GLsizei)(y1 - y0);
    return 1;
}

static void db_gl1_capture_history_ranges(const db_gl_upload_range_t *ranges,
                                          size_t range_count,
                                          int force_full_capture) {
    if ((g_state.history_tex == 0U) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex);
    if ((force_full_capture != 0) || (ranges == NULL) || (range_count == 0U)) {
        db_gl1_capture_history_full_framebuffer();
        return;
    }
    for (size_t i = 0U; i < range_count; i++) {
        GLint rect_x = 0;
        GLint rect_y = 0;
        GLsizei rect_w = 0;
        GLsizei rect_h = 0;
        if (db_gl1_history_range_to_copy_rect(&ranges[i], g_state.history_width,
                                              g_state.history_height, &rect_x,
                                              &rect_y, &rect_w, &rect_h) == 0) {
            continue;
        }
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rect_x, rect_y, rect_x, rect_y,
                            rect_w, rect_h);
    }
    g_state.history_valid = 1;
}

static int db_gl1_row_range_to_copy_rect(uint32_t row_start, uint32_t row_count,
                                         GLint viewport_width,
                                         GLint viewport_height, GLint *x_out,
                                         GLint *y_out, GLsizei *width_out,
                                         GLsizei *height_out) {
    if ((row_count == 0U) || (viewport_width <= 0) || (viewport_height <= 0) ||
        (x_out == NULL) || (y_out == NULL) || (width_out == NULL) ||
        (height_out == NULL)) {
        return 0;
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    if ((cols == 0U) || (rows == 0U) || (row_start >= rows)) {
        return 0;
    }
    const uint32_t row_end = db_u32_min(rows, row_start + row_count);
    float min_y = 1.0F;
    float max_y = -1.0F;
    for (uint32_t row = row_start; row < row_end; row++) {
        const uint32_t tile_index = row * cols;
        const size_t tile_float_offset = (size_t)tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        const float *unit = &g_state.vertex.vertices[tile_float_offset];
        for (size_t v = 0U; v < DB_RECT_VERTEX_COUNT; v++) {
            const float y_pos = unit[(v * g_state.vertex.vertex_stride) + 1U];
            min_y = (y_pos < min_y) ? y_pos : min_y;
            max_y = (y_pos > max_y) ? y_pos : max_y;
        }
    }
    if (max_y < min_y) {
        return 0;
    }
    const float viewport_width_f = (float)viewport_width;
    const float viewport_height_f = (float)viewport_height;
    const GLint x0 = 0;
    const GLint x1 = viewport_width;
    GLint y0 = (GLint)floorf(((min_y + 1.0F) * DB_NDC_TO_VIEWPORT_HALF_F) *
                             viewport_height_f);
    GLint y1 = (GLint)ceilf(((max_y + 1.0F) * DB_NDC_TO_VIEWPORT_HALF_F) *
                            viewport_height_f);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > viewport_height) {
        y1 = viewport_height;
    }
    if ((x1 <= x0) || (y1 <= y0)) {
        return 0;
    }
    *x_out = x0;
    *y_out = y0;
    *width_out = (GLsizei)(x1 - x0);
    *height_out = (GLsizei)(y1 - y0);
    return 1;
}

static void db_gl1_scissor_clear_rect(GLint rect_x, GLint rect_y,
                                      GLsizei rect_width, GLsizei rect_height,
                                      float color_r, float color_g,
                                      float color_b) {
    if ((rect_width <= 0) || (rect_height <= 0)) {
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect_x, rect_y, rect_width, rect_height);
    glClearColor(color_r, color_g, color_b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

static void
db_gl1_draw_gradient_dirty_rows_gpu(const db_dirty_row_range_t dirty_ranges[2],
                                    size_t dirty_count, uint32_t head_row,
                                    int direction_down, uint32_t cycle_index) {
    int viewport_w = 0;
    int viewport_h = 0;
    if (db_gl_get_viewport_size(&viewport_w, &viewport_h) == 0) {
        return;
    }
    for (size_t i = 0U; i < dirty_count; i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count = dirty_ranges[i].row_count;
        if (row_count == 0U) {
            continue;
        }
        for (uint32_t row = 0U; row < row_count; row++) {
            const uint32_t abs_row = row_start + row;
            GLint rect_x = 0;
            GLint rect_y = 0;
            GLsizei rect_width = 0;
            GLsizei rect_height = 0;
            if (db_gl1_row_range_to_copy_rect(abs_row, 1U, viewport_w,
                                              viewport_h, &rect_x, &rect_y,
                                              &rect_width, &rect_height) == 0) {
                continue;
            }
            float row_color_r = 0.0F;
            float row_color_g = 0.0F;
            float row_color_b = 0.0F;
            db_gradient_row_color_rgb(abs_row, head_row, direction_down,
                                      cycle_index, &row_color_r, &row_color_g,
                                      &row_color_b);
            db_gl1_scissor_clear_rect(rect_x, rect_y, rect_width, rect_height,
                                      row_color_r, row_color_g, row_color_b);
        }
    }
}

static void db_gl1_draw_bands_gpu(float *verts, uint32_t cols, uint32_t rows,
                                  uint32_t band_count, uint32_t frame_index,
                                  size_t stride_floats,
                                  size_t color_offset_floats) {
    for (uint32_t band = 0U; band < band_count; band++) {
        const GLint x0 = (GLint)((cols * band) / band_count);
        const GLint x1 = (GLint)((cols * (band + 1U)) / band_count);

        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band, band_count, frame_index, &color_r, &color_g,
                          &color_b);

        db_gl1_scissor_clear_rect(x0, 0, (GLsizei)(x1 - x0), (GLsizei)rows,
                                  color_r, color_g, color_b);
    }
}

static void db_gl1_history_capture_gradient_dirty_rows(
    const db_dirty_row_range_t dirty_ranges[2], size_t dirty_count) {
    if ((g_state.history_tex == 0U) || (g_state.history_width <= 0) ||
        (g_state.history_height <= 0)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_state.history_tex);
    for (size_t i = 0U; i < dirty_count; i++) {
        GLint rect_x = 0;
        GLint rect_y = 0;
        GLsizei rect_width = 0;
        GLsizei rect_height = 0;
        if (db_gl1_row_range_to_copy_rect(
                dirty_ranges[i].row_start, dirty_ranges[i].row_count,
                g_state.history_width, g_state.history_height, &rect_x, &rect_y,
                &rect_width, &rect_height) == 0) {
            continue;
        }
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rect_x, rect_y, rect_x, rect_y,
                            rect_width, rect_height);
    }
    g_state.history_valid = 1;
}

static void db_bind_client_arrays_from_cpu_vertices(void) {
    // Client-array mode must ensure ARRAY_BUFFER is unbound so pointers are CPU
    // addresses.
    (void)db_gl_vbo_bind(0U);
    const GLsizei client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const GLint client_color_components = (g_state.is_es_context != 0)
                                              ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                              : DB_VERTEX_COLOR_FLOAT_COUNT;
    glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT, client_stride,
                    &g_state.vertex.vertices[0]);
    glColorPointer(client_color_components, GL_FLOAT, client_stride,
                   &g_state.vertex.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);
}

static size_t db_collect_gl1_damage_ranges(
    const db_snake_plan_t *plan, uint32_t snake_prev_start,
    uint32_t snake_prev_count, int force_full_upload,
    const db_dirty_row_range_t gradient_dirty_ranges[2],
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
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        local_range_storage = g_state.snake_upload_ranges;
        local_range_capacity = g_state.snake_scratch_capacity;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        local_range_capacity = 2U;
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

static void
db_upload_vbo_damage_ranges(const db_gl_upload_range_t *range_storage,
                            size_t upload_range_count) {
    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    if (upload_range_count > 0U) {
        db_gl_upload_ranges_target(g_state.vertex.vertices, upload_bytes,
                                   range_storage, upload_range_count,
                                   DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U,
                                   g_state.vertex.upload.use_persistent_upload,
                                   g_state.vertex.upload.persistent_mapped_ptr,
                                   g_state.vertex.upload.use_map_range_upload,
                                   g_state.vertex.upload.use_map_buffer_upload);
    }
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
    g_state.snake_upload_ranges = NULL;
    g_state.snake_spans = NULL;
    g_state.snake_row_bounds = NULL;
    g_state.snake_row_bounds_capacity = 0U;
    g_state.snake_scratch_capacity = 0U;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const size_t scratch_capacity =
            db_snake_scratch_capacity_from_work_units(
                g_state.runtime.work_unit_count);
        g_state.snake_upload_ranges =
            (db_gl_upload_range_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_upload_ranges", scratch_capacity,
                sizeof(*g_state.snake_upload_ranges));
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
    g_state.history_tex = 0U;
    g_state.history_valid = 0;
    g_state.history_width = 0;
    g_state.history_height = 0;
    db_gl1_history_quad_init();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    const GLsizei client_stride =
        (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
    const GLint client_color_components = (g_state.is_es_context != 0)
                                              ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                              : DB_VERTEX_COLOR_FLOAT_COUNT;
    glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT, client_stride,
                    &g_state.vertex.vertices[0]);
    glColorPointer(client_color_components, GL_FLOAT, client_stride,
                   &g_state.vertex.vertices[DB_VERTEX_POSITION_FLOAT_COUNT]);

    if (db_gl_context_supports_vbo() != 0) {
        const size_t probe_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
        unsigned int vbo_u32 = 0U;
        if (db_gl_vbo_create_or_zero(&vbo_u32) != 0) {
            g_state.vbo = (GLuint)vbo_u32;
        }
        if (g_state.vbo != 0U) {
            if (db_gl_vbo_bind((unsigned int)g_state.vbo) == 0) {
                db_gl_vbo_delete_if_valid((unsigned int)g_state.vbo);
                g_state.vbo = 0U;
            }
        }
        if (g_state.vbo != 0U) {
            db_gl_probe_upload_capabilities(
                probe_bytes, g_state.vertex.vertices, &probe_result);
            g_state.vertex.upload = probe_result;
            const GLsizei vbo_stride =
                (g_state.is_es_context != 0) ? ES_STRIDE_BYTES : STRIDE_BYTES;
            const GLint vbo_color_components =
                (g_state.is_es_context != 0) ? DB_ES_VERTEX_COLOR_FLOAT_COUNT
                                             : DB_VERTEX_COLOR_FLOAT_COUNT;
            glVertexPointer(DB_VERTEX_POSITION_FLOAT_COUNT, GL_FLOAT,
                            vbo_stride, vbo_offset_ptr(0));
            glColorPointer(
                vbo_color_components, GL_FLOAT, vbo_stride,
                vbo_offset_ptr(sizeof(float) * DB_VERTEX_POSITION_FLOAT_COUNT));
            infof("using capability mode: %s",
                  db_renderer_opengl_gl1_5_gles1_1_capability_mode());
            return;
        }
    }

    infof("using capability mode: %s",
          db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(uint32_t frame_index) {
    db_snake_plan_t plan = {0};
    uint32_t snake_prev_start = 0U;
    uint32_t snake_prev_count = 0U;
    int force_full_upload = 0;
    db_dirty_row_range_t gradient_dirty_ranges[2] = {{0U, 0U}, {0U, 0U}};
    size_t gradient_dirty_count = 0U;
    uint32_t gradient_render_head_row = 0U;
    int gradient_render_direction_down = 1;
    uint32_t gradient_render_cycle_index = 0U;
    const int history_available = db_gl1_ensure_history_textures();
    const int gpu_history_gradient_or_bands =
        (history_available != 0) &&
        ((g_state.runtime.pattern == DB_PATTERN_BANDS) ||
         (g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
         (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL));

    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID);
        const int is_shapes =
            (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES);
        if (is_grid == 0) {
            snake_prev_start = g_state.runtime.snake_prev_start;
            snake_prev_count = g_state.runtime.snake_prev_count;
        }
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.runtime.pattern_seed,
            g_state.runtime.snake_shape_index, g_state.runtime.snake_cursor,
            g_state.runtime.snake_prev_start, g_state.runtime.snake_prev_count,
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
            if (g_state.snake_reset_pending != 0) {
                db_fill_grid_all_rgb_stride(
                    g_state.vertex.vertices, g_state.runtime.work_unit_count,
                    g_state.vertex.vertex_stride,
                    DB_VERTEX_POSITION_FLOAT_COUNT, BENCH_GRID_PHASE0_R,
                    BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
                g_state.snake_reset_pending = 0;
                force_full_upload = 1;
            }
            if (target.has_next_shape_index != 0) {
                g_state.runtime.snake_shape_index = target.next_shape_index;
            }
            if (plan.wrapped != 0) {
                g_state.snake_reset_pending = 1;
                g_state.runtime.snake_prev_count = 0U;
            }
        }
        db_render_snake_step(
            &plan, &target.region, shape_kind, g_state.runtime.pattern_seed,
            plan.active_shape_index, target.target_r, target.target_g,
            target.target_b, target.full_fill_on_phase_completed);
        g_state.runtime.snake_prev_start = plan.next_prev_start;
        g_state.runtime.snake_prev_count = plan.next_prev_count;
        g_state.runtime.snake_cursor = plan.next_cursor;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const db_gradient_step_t gradient_step = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient_head_row,
            g_state.runtime.mode_phase_flag, g_state.runtime.gradient_cycle,
            g_state.runtime.bench_speed_step);
        const db_gradient_damage_plan_t *gradient_plan = &gradient_step.plan;
        gradient_dirty_count = db_gradient_collect_dirty_ranges(
            gradient_plan, gradient_dirty_ranges);
        gradient_render_head_row = gradient_plan->render_head_row;
        gradient_render_direction_down = gradient_step.render_direction_down;
        gradient_render_cycle_index = gradient_plan->render_cycle_index;
        if (gpu_history_gradient_or_bands == 0) {
            for (size_t i = 0U; i < gradient_dirty_count; i++) {
                db_apply_gradient_dirty_rows(
                    gradient_dirty_ranges[i].row_start,
                    gradient_dirty_ranges[i].row_count,
                    gradient_plan->render_head_row,
                    gradient_step.render_direction_down,
                    gradient_plan->render_cycle_index);
            }
        }
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_step);
    } else {
        if (gpu_history_gradient_or_bands == 0) {
            db_update_grid_vertices_for_bands_rgb_stride(
                g_state.vertex.vertices, db_grid_cols_effective(),
                db_grid_rows_effective(), BENCH_BANDS, frame_index,
                g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
        }
    }

    db_gl_upload_range_t draw_ranges[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    size_t draw_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        if ((g_state.snake_upload_ranges != NULL) &&
            (g_state.snake_scratch_capacity > 0U)) {
            draw_range_capacity = g_state.snake_scratch_capacity;
        }
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        draw_range_capacity = 2U;
    } else {
        draw_range_capacity = 1U;
    }
    db_gl_upload_range_t *range_storage = draw_ranges;
    if ((draw_range_capacity == g_state.snake_scratch_capacity) &&
        (g_state.snake_upload_ranges != NULL)) {
        range_storage = g_state.snake_upload_ranges;
    }
    size_t draw_range_count = db_collect_gl1_damage_ranges(
        &plan, snake_prev_start, snake_prev_count, force_full_upload,
        gradient_dirty_ranges, gradient_dirty_count, range_storage,
        draw_range_capacity);
    if (draw_range_count == 0U) {
        const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
        range_storage[0] = (db_gl_upload_range_t){0U, 0U, upload_bytes};
        draw_range_count = 1U;
    }

    if (history_available != 0) {
        const int seed_history_full_frame = (g_state.history_valid == 0);
        if ((gpu_history_gradient_or_bands != 0) &&
            (g_state.runtime.pattern == DB_PATTERN_BANDS)) {
            /*if (seed_history_full_frame == 0) {
                db_gl1_restore_history_to_framebuffer();
            }*/
            db_gl1_draw_bands_gpu(
                g_state.vertex.vertices, db_grid_cols_effective(),
                db_grid_rows_effective(), BENCH_BANDS, frame_index,
                g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
            // db_gl1_capture_history_full_framebuffer();
        } else if ((gpu_history_gradient_or_bands != 0) &&
                   ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                    (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL))) {
            if (seed_history_full_frame != 0) {
                db_gl1_scissor_clear_rect(
                    0, 0, g_state.history_width, g_state.history_height,
                    BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                    BENCH_GRID_PHASE0_B);
                db_gl1_draw_gradient_dirty_rows_gpu(
                    gradient_dirty_ranges, gradient_dirty_count,
                    gradient_render_head_row, gradient_render_direction_down,
                    gradient_render_cycle_index);
                db_gl1_capture_history_full_framebuffer();
            } else {
                db_gl1_restore_history_to_framebuffer();
                db_gl1_draw_gradient_dirty_rows_gpu(
                    gradient_dirty_ranges, gradient_dirty_count,
                    gradient_render_head_row, gradient_render_direction_down,
                    gradient_render_cycle_index);
                db_gl1_history_capture_gradient_dirty_rows(
                    gradient_dirty_ranges, gradient_dirty_count);
            }
        } else {
            if (seed_history_full_frame != 0) {
                const size_t upload_bytes =
                    (size_t)g_state.vertex.draw_vertex_count *
                    g_state.vertex.vertex_stride * sizeof(float);
                range_storage[0] = (db_gl_upload_range_t){0U, 0U, upload_bytes};
                draw_range_count = 1U;
            } else {
                db_gl1_restore_history_to_framebuffer();
            }
            g_state.history_fallback_warned = 0;
            db_bind_client_arrays_from_cpu_vertices();
            db_gl1_dirty_ranges_draw(range_storage, draw_range_count);
            const size_t upload_bytes =
                (size_t)g_state.vertex.draw_vertex_count *
                g_state.vertex.vertex_stride * sizeof(float);
            const int force_full_capture =
                (seed_history_full_frame != 0) ||
                ((draw_range_count == 1U) &&
                 (range_storage[0].src_offset_bytes == 0U) &&
                 (range_storage[0].size_bytes == upload_bytes));
            db_gl1_capture_history_ranges(range_storage, draw_range_count,
                                          force_full_capture);
        }
    } else {
        if ((db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) &&
            (g_state.history_fallback_warned == 0)) {
            infof("history texture unavailable; falling back to direct draw");
            g_state.history_fallback_warned = 1;
        }
        if (g_state.vbo == 0U) {
            db_bind_client_arrays_from_cpu_vertices();
        } else {
            db_upload_vbo_damage_ranges(range_storage, draw_range_count);
        }
        glDrawArrays(GL_TRIANGLES, 0, db_draw_vertex_count_glsizei());
    }
    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame_index++;
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        (void)db_gl_vbo_bind((unsigned int)g_state.vbo);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.vbo != 0U) {
        db_gl_vbo_delete_if_valid((unsigned int)g_state.vbo);
        g_state.vbo = 0U;
    }
    db_gl_texture_delete_if_valid((unsigned int *)&g_state.history_tex);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    free(g_state.snake_upload_ranges);
    free(g_state.snake_spans);
    free(g_state.snake_row_bounds);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
    if (db_pattern_uses_history_texture(g_state.runtime.pattern) != 0) {
        return DB_CAP_MODE_OPENGL_GPU_HISTORY_DIRTY_DRAW;
    }
    if (g_state.vbo == 0U) {
        return DB_CAP_MODE_OPENGL_CLIENT_ARRAY;
    }
    if (g_state.vertex.upload.use_persistent_upload != 0) {
        return DB_CAP_MODE_OPENGL_VBO_PERSISTENT;
    }
    if (g_state.vertex.upload.use_map_range_upload != 0) {
        return DB_CAP_MODE_OPENGL_VBO_MAP_RANGE;
    }
    if (g_state.vertex.upload.use_map_buffer_upload != 0) {
        return DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER;
    }
    return DB_CAP_MODE_OPENGL_VBO;
}

uint32_t db_renderer_opengl_gl1_5_gles1_1_work_unit_count(void) {
    return (g_state.runtime.work_unit_count != 0U)
               ? g_state.runtime.work_unit_count
               : BENCH_BANDS;
}

uint64_t db_renderer_opengl_gl1_5_gles1_1_state_hash(void) {
    return g_state.state_hash;
}
