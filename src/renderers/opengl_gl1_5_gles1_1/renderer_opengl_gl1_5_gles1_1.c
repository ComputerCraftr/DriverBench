#include "renderer_opengl_gl1_5_gles1_1.h"

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
#include <GL/glext.h>
#else
#include <GLES/gl.h>
#endif

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define DB_CAP_MODE_OPENGL_CLIENT_ARRAY "opengl_client_array"
#define DB_CAP_MODE_OPENGL_VBO "opengl_vbo"
#define DB_CAP_MODE_OPENGL_VBO_MAP_BUFFER "opengl_vbo_map_buffer"
#define DB_CAP_MODE_OPENGL_VBO_MAP_RANGE "opengl_vbo_map_range"
#define DB_CAP_MODE_OPENGL_VBO_PERSISTENT "opengl_vbo_persistent"
#define ES_STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((GLsizei)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    uint64_t state_hash;
    uint64_t frame_index;
    db_benchmark_runtime_init_t runtime;
    db_gl_vertex_init_t vertex;
    int is_es_context;
    int snake_reset_pending;
    db_gl_upload_range_t *snake_upload_ranges;
    db_snake_col_span_t *snake_spans;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    size_t snake_scratch_capacity;
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

static void db_bind_client_arrays_from_cpu_vertices(void) {
    // Client-array mode must ensure ARRAY_BUFFER is unbound so pointers are CPU
    // addresses.
    glBindBuffer(GL_ARRAY_BUFFER, 0U);
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

static void
db_upload_vbo_damage_ranges(const db_snake_plan_t *plan,
                            uint32_t snake_prev_start,
                            uint32_t snake_prev_count, int force_full_upload,
                            const db_dirty_row_range_t gradient_dirty_ranges[2],
                            size_t gradient_dirty_count) {
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
    db_gl_upload_range_t *range_storage = upload_ranges;
    size_t range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if ((g_state.runtime.pattern == DB_PATTERN_SNAKE_GRID) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_RECT) ||
        (g_state.runtime.pattern == DB_PATTERN_SNAKE_SHAPES)) {
        range_storage = g_state.snake_upload_ranges;
        range_capacity = g_state.snake_scratch_capacity;
    } else if ((g_state.runtime.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.runtime.pattern == DB_PATTERN_GRADIENT_FILL)) {
        range_capacity = 2U;
    } else {
        range_capacity = 1U;
    }
    const size_t upload_range_count = db_gl_collect_pattern_upload_ranges(
        &collect_ctx, range_storage, range_capacity);
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

static void
db_apply_gl1_upload_mode(const db_snake_plan_t *plan, uint32_t snake_prev_start,
                         uint32_t snake_prev_count, int force_full_upload,
                         const db_dirty_row_range_t gradient_dirty_ranges[2],
                         size_t gradient_dirty_count) {
    if (g_state.vbo == 0U) {
        db_bind_client_arrays_from_cpu_vertices();
        return;
    }
    db_upload_vbo_damage_ranges(plan, snake_prev_start, snake_prev_count,
                                force_full_upload, gradient_dirty_ranges,
                                gradient_dirty_count);
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

    if (g_state.runtime.pattern == DB_PATTERN_BANDS) {
        db_fill_band_vertices_pos_rgb_stride(
            g_state.vertex.vertices, g_state.runtime.work_unit_count, 0.0,
            g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
    }

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

    if (db_gl_has_vbo_support() != 0) {
        const size_t probe_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
        glGenBuffers(1, &g_state.vbo);
        if (g_state.vbo != 0U) {
            glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
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

void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s) {
    db_snake_plan_t plan = {0};
    uint32_t snake_prev_start = 0U;
    uint32_t snake_prev_count = 0U;
    int force_full_upload = 0;
    db_dirty_row_range_t gradient_dirty_ranges[2] = {{0U, 0U}, {0U, 0U}};
    size_t gradient_dirty_count = 0U;

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
        for (size_t i = 0U; i < gradient_dirty_count; i++) {
            db_apply_gradient_dirty_rows(gradient_dirty_ranges[i].row_start,
                                         gradient_dirty_ranges[i].row_count,
                                         gradient_plan->render_head_row,
                                         gradient_step.render_direction_down,
                                         gradient_plan->render_cycle_index);
        }
        db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_step);
    } else {
        db_update_band_vertices_rgb_stride(
            g_state.vertex.vertices, g_state.runtime.work_unit_count, time_s,
            g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT);
    }

    db_apply_gl1_upload_mode(&plan, snake_prev_start, snake_prev_count,
                             force_full_upload, gradient_dirty_ranges,
                             gradient_dirty_count);

    glDrawArrays(GL_TRIANGLES, 0, db_draw_vertex_count_glsizei());
    g_state.state_hash = db_benchmark_runtime_state_hash(
        &g_state.runtime, g_state.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame_index++;
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {
    if (g_state.vertex.upload.persistent_mapped_ptr != NULL) {
        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        db_gl_unmap_current_array_buffer();
    }
    if (g_state.vbo != 0U) {
        glDeleteBuffers(1, &g_state.vbo);
        g_state.vbo = 0U;
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    free(g_state.snake_upload_ranges);
    free(g_state.snake_spans);
    free(g_state.snake_row_bounds);
    free(g_state.vertex.vertices);
    g_state = (renderer_state_t){0};
}

const char *db_renderer_opengl_gl1_5_gles1_1_capability_mode(void) {
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
