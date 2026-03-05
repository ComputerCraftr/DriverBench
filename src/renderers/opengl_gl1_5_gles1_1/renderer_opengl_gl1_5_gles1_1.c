#include "renderer_opengl_gl1_5_gles1_1.h"
#include "renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderer_opengl_gl1_5_gles1_1_primitives.h"
#include "renderer_opengl_gl1_5_gles1_1_ranges.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_viewport_common.h"

#define BACKEND_NAME "renderer_opengl_gl1_5_gles1_1"
#define DB_GL1_GRADIENT_DIRTY_RANGE_CAP 2U
// Crossover: above this many damaged rows, scissor/rect clears are typically
// cheaper than mesh upload+draw for gradient updates.
#define DB_GL1_GRADIENT_MESH_ROW_THRESHOLD 8U
#define DB_GL1_GRADIENT_REPLAY_ROW_CAP 4U
// Crossover: when upload ranges are highly fragmented, collapse to one span.
#define DB_GL1_SNAKE_RANGE_COLLAPSE_THRESHOLD 8U
// Crossover: prefer settled-span scissor clear earlier to reduce VBO traffic.
#define DB_GL1_SNAKE_SCISSOR_ROW_THRESHOLD 8U
#define ES_STRIDE_BYTES ((int)(sizeof(float) * DB_ES_VERTEX_FLOAT_STRIDE))
#define STRIDE_BYTES ((int)(sizeof(float) * DB_VERTEX_FLOAT_STRIDE))
#define failf(...) db_failf(BACKEND_NAME, __VA_ARGS__)
#define infof(...) db_infof(BACKEND_NAME, __VA_ARGS__)

typedef struct {
    db_gl_upload_range_t *curr_upload_ranges;
    db_gl_upload_range_t *prev_upload_ranges;
    size_t prev_upload_count;
} db_gl1_snake_replay_t;

typedef struct {
    char capability_mode[DB_GL_CAPABILITY_MODE_MAX];
    db_renderer_frame_stats_t frame;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;
    db_gl_vertex_init_t vertex;
    int is_es_context;
    // Double-buffered preserved-backbuffer support: replay prior frame damage
    // onto the next backbuffer (which may contain N-1) before current damage.
    db_gradient_backbuffer_replay_state_t gradient_prev_frame;
    db_gl1_snake_replay_t snake_replay;
    db_history_snake_scratch_t snake_scratch;
    db_history_snake_backbuffer_state_t snake_backbuffer_state;
    db_gl_viewport_cache_t viewport;
    int backbuffer_valid;
    db_gl_buffer_cache_t buffers;
    int client_arrays_configured;
    int vbo_arrays_configured;
} renderer_state_t;

typedef struct {
    uint32_t cols;
} db_gl1_gradient_mesh_apply_ctx_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_region_t target_region;
    double target_r;
    double target_g;
    double target_b;
    db_gl_upload_range_t *preview_ranges;
    size_t preview_capacity;
    size_t preview_count;
    int force_full_upload;
    int force_replay_full;
} db_gl1_snake_frame_state_t;

typedef struct {
    double color_b;
    double color_g;
    double color_r;
    int viewport_h;
    int viewport_w;
} db_gl1_scissor_fill_ctx_t;

static renderer_state_t g_state = {0};

static void db_gl1_seed_backbuffer_clear_cb(const float rgba[4],
                                            void *user_data) {
    (void)user_data;
    if (rgba == NULL) {
        return;
    }
    db_gl_set_scissor_enabled(0);
    db_gl_clear_color_rgb(rgba[0], rgba[1], rgba[2]);
    db_gl_clear_color_buffer();
}

static void db_gl1_apply_scissor_fill_rect(const db_gl_scissor_rect_t *rect,
                                           void *user_data) {
    const db_gl1_scissor_fill_ctx_t *ctx =
        (const db_gl1_scissor_fill_ctx_t *)user_data;
    if ((rect == NULL) || (ctx == NULL)) {
        return;
    }
    db_gl1_draw_solid_rect_pixels(
        rect->x_px, rect->y_px, rect->width_px, rect->height_px,
        ctx->viewport_w, ctx->viewport_h, db_double_to_f32(ctx->color_r),
        db_double_to_f32(ctx->color_g), db_double_to_f32(ctx->color_b));
}

static void db_gl1_refresh_capability_mode(void) {
    const char *draw_mode = db_gl_capability_mode_draw_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode,
        g_state.runtime_flags.is_snake_history_texture, 0);
    const char *upload_mode = db_gl_capability_mode_upload_select(
        g_state.runtime_flags.uses_ff_rect_draw_mode,
        db_gl_capability_mode_upload_from_probe(
            (g_state.buffers.vbo != 0U) ? 1 : 0, &g_state.vertex.upload));
    db_gl_capability_mode_compose(
        g_state.capability_mode, sizeof(g_state.capability_mode), draw_mode,
        upload_mode, db_runtime_backbuffer_replay_enabled(&g_state.runtime));
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
                                 uint32_t shape_index, double target_r,
                                 double target_g, double target_b,
                                 int force_full_fill_on_phase_complete) {
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
    if ((force_full_fill_on_phase_complete != 0) && (plan->phase_completed != 0)) {
        db_fill_grid_all_rgb_stride(
            g_state.vertex.vertices, g_state.runtime.work_unit_count,
            g_state.vertex.vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
            db_double_to_f32(target_r), db_double_to_f32(target_g),
            db_double_to_f32(target_b));
        return;
    }
    db_snake_shape_cache_t shape_cache = {0};
    const db_snake_shape_cache_t *shape_cache_ptr = NULL;
    if (g_state.runtime_flags.is_snake_shapes != 0) {
        if ((g_state.snake_scratch.row_bounds != NULL) &&
            (db_snake_shape_cache_init_from_index(
                 &shape_cache, g_state.snake_scratch.row_bounds,
                 g_state.snake_scratch.row_bounds_capacity, pattern_seed,
                 shape_index, DB_U32_SALT_PALETTE, region,
                 (db_snake_shape_kind_t)shape_kind) != 0)) {
            shape_cache_ptr = &shape_cache;
        }
    }
    const uint32_t cols = db_grid_cols_effective();
    const uint32_t rows = db_grid_rows_effective();
    const float target_r_f = db_double_to_f32(target_r);
    const float target_g_f = db_double_to_f32(target_g);
    const float target_b_f = db_double_to_f32(target_b);
    double prior_rgb[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0};
    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const size_t prior_base = (size_t)update_index * 3U;
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t tile_float_offset = (size_t)tile.tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        prior_rgb[prior_base] =
            (double)unit[DB_VERTEX_POSITION_FLOAT_COUNT + 0U];
        prior_rgb[prior_base + 1U] =
            (double)unit[DB_VERTEX_POSITION_FLOAT_COUNT + 1U];
        prior_rgb[prior_base + 2U] =
            (double)unit[DB_VERTEX_POSITION_FLOAT_COUNT + 2U];
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t tile_float_offset = (size_t)tile.tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        float *unit = &g_state.vertex.vertices[tile_float_offset];
        db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, target_r_f,
                             target_g_f, target_b_f);
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t tile_float_offset = (size_t)tile.tile_index *
                                         DB_RECT_VERTEX_COUNT *
                                         g_state.vertex.vertex_stride;
        const double blend_factor =
            db_window_blend_factor(update_index, plan->batch_size);

        float *unit = &g_state.vertex.vertices[tile_float_offset];
        const size_t prior_base = (size_t)update_index * 3U;
        const double prior_r = prior_rgb[prior_base];
        const double prior_g = prior_rgb[prior_base + 1U];
        const double prior_b = prior_rgb[prior_base + 2U];
        double out_r_value = 0.0;
        double out_g_value = 0.0;
        double out_b_value = 0.0;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend_factor, &out_r_value, &out_g_value, &out_b_value);
        const float out_r = db_double_to_f32(out_r_value);
        const float out_g = db_double_to_f32(out_g_value);
        const float out_b = db_double_to_f32(out_b_value);
        db_set_rect_unit_rgb(unit, g_state.vertex.vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, out_r, out_g,
                             out_b);
    }
}

static void db_gl1_set_gradient_grid_row_rgb(uint32_t row, uint32_t cols,
                                             double row_r, double row_g,
                                             double row_b) {
    if (cols == 0U) {
        return;
    }
    db_set_rect_tile_range_rgb(
        g_state.vertex.vertices, row * cols, cols, g_state.vertex.vertex_stride,
        DB_VERTEX_POSITION_FLOAT_COUNT, db_double_to_f32(row_r),
        db_double_to_f32(row_g), db_double_to_f32(row_b));
}

static void db_gl1_write_gradient_row_color_to_mesh(uint32_t row, double row_r,
                                                    double row_g, double row_b,
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

static void
db_gl1_draw_gradient_dirty_rows_mesh(const db_dirty_row_range_t *dirty_ranges,
                                     size_t dirty_count, uint32_t head_row,
                                     int direction_down, uint32_t cycle_index) {
    db_gl_set_scissor_enabled(0);
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
    const size_t upload_count = db_gl_collect_row_upload_ranges(
        cols, rows, db_rect_tile_bytes(g_state.vertex.vertex_stride),
        dirty_ranges, dirty_count, NULL, upload_ranges,
        DB_GL1_GRADIENT_REPLAY_ROW_CAP);
    if (upload_count == 0U) {
        return;
    }

    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    if (g_state.buffers.vbo != 0U) {
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
    db_gl1_draw_dirty_ranges_common(BACKEND_NAME, g_state.vertex.vertex_stride,
                                    g_state.vertex.draw_vertex_count,
                                    upload_ranges, upload_count);
}

static void
db_gl1_draw_gradient_dirty_rows_hybrid(const db_dirty_row_range_t *dirty_ranges,
                                       size_t dirty_count, uint32_t head_row,
                                       int direction_down, uint32_t cycle_index,
                                       int viewport_w, int viewport_h) {
    const int use_mesh = db_gl1_gradient_should_use_mesh(
        dirty_ranges, dirty_count, DB_GL1_GRADIENT_MESH_ROW_THRESHOLD);
    if (use_mesh != 0) {
        db_gl1_draw_gradient_dirty_rows_mesh(
            dirty_ranges, dirty_count, head_row, direction_down, cycle_index);
        return;
    }
    db_gl1_draw_gradient_dirty_rows_gpu(BACKEND_NAME, dirty_ranges, dirty_count,
                                        head_row, direction_down, cycle_index,
                                        viewport_w, viewport_h);
}

static size_t
db_collect_gl1_damage_ranges(const db_snake_plan_t *plan, int force_full_upload,
                             const db_dirty_row_range_t *gradient_dirty_ranges,
                             size_t gradient_dirty_count,
                             db_gl_upload_range_t *range_storage,
                             size_t range_capacity) {
    const db_gl1_damage_collect_ctx_t collect_ctx = {
        .backend_name = BACKEND_NAME,
        .pattern = g_state.runtime.pattern,
        .cols = db_grid_cols_effective(),
        .rows = db_grid_rows_effective(),
        .upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                        g_state.vertex.vertex_stride * sizeof(float),
        .upload_tile_bytes = db_rect_tile_bytes(g_state.vertex.vertex_stride),
        .force_full_upload = force_full_upload,
        .snake_plan = plan,
        .pattern_seed = g_state.runtime.pattern_seed,
        .snake_scratch = &g_state.snake_scratch,
        .damage_row_ranges = gradient_dirty_ranges,
        .damage_row_count = gradient_dirty_count,
        .default_history_range_storage =
            g_state.snake_replay.curr_upload_ranges,
        .gradient_dirty_range_cap = DB_GL1_GRADIENT_DIRTY_RANGE_CAP,
        .is_gradient_pattern = g_state.runtime_flags.is_gradient,
        .is_snake_history_texture_pattern =
            g_state.runtime_flags.is_snake_history_texture,
    };
    return db_gl1_collect_pattern_damage_ranges(&collect_ctx, range_storage,
                                                range_capacity);
}

static size_t db_gl1_build_snake_upload_ranges(const db_snake_plan_t *plan,
                                               int force_full_upload,
                                               db_gl_upload_range_t *ranges,
                                               size_t range_capacity,
                                               int allow_overdraw_collapse) {
    const size_t range_count = db_collect_gl1_damage_ranges(
        plan, force_full_upload, NULL, 0U, ranges, range_capacity);
    return db_gl1_optimize_upload_ranges(ranges, range_count,
                                         allow_overdraw_collapse,
                                         DB_GL1_SNAKE_RANGE_COLLAPSE_THRESHOLD);
}

static int
db_gl1_snake_should_use_scissor_for_settled(const db_gl_upload_range_t *ranges,
                                            size_t range_count) {
    if ((ranges == NULL) || (range_count == 0U)) {
        return 0;
    }
    const uint32_t cols = db_grid_cols_effective();
    if (cols == 0U) {
        return 0;
    }
    const size_t row_bytes =
        (size_t)cols * db_rect_tile_bytes(g_state.vertex.vertex_stride);
    if (row_bytes == 0U) {
        return 0;
    }
    size_t total_bytes = 0U;
    for (size_t index = 0U; index < range_count; index++) {
        if (ranges[index].size_bytes > (SIZE_MAX - total_bytes)) {
            total_bytes = SIZE_MAX;
            break;
        }
        total_bytes += ranges[index].size_bytes;
    }
    const size_t threshold_bytes =
        row_bytes * DB_GL1_SNAKE_SCISSOR_ROW_THRESHOLD;
    return total_bytes >= threshold_bytes;
}

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    db_gl_set_depth_test_enabled(0);
    db_gl_set_cull_face_enabled(0);

    g_state.is_es_context = db_gl_is_es_context(db_gl_get_version_string());
    g_state.vertex.vertex_stride = (g_state.is_es_context != 0)
                                       ? DB_ES_VERTEX_FLOAT_STRIDE
                                       : DB_VERTEX_FLOAT_STRIDE;

    if (!db_init_vertices_for_mode(g_state.vertex.vertex_stride)) {
        failf("failed to allocate benchmark vertex buffers");
    }
    g_state.snake_replay.curr_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_ranges = NULL;
    g_state.snake_replay.prev_upload_count = 0U;
    g_state.snake_scratch.spans = NULL;
    g_state.snake_scratch.row_bounds = NULL;
    g_state.snake_scratch.row_bounds_capacity = 0U;
    g_state.snake_scratch.span_capacity = 0U;
    db_history_snake_backbuffer_state_reset(&g_state.snake_backbuffer_state, 0);
    g_state.runtime_flags = db_history_runtime_mode_flags(&g_state.runtime);
    if (g_state.runtime_flags.is_snake_history_texture != 0) {
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
        g_state.snake_scratch.spans =
            (db_snake_col_span_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_spans", scratch_capacity,
                sizeof(*g_state.snake_scratch.spans));
        if (g_state.runtime_flags.is_snake_shapes != 0) {
            g_state.snake_scratch.row_bounds =
                (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                    BACKEND_NAME, "snake_row_bounds", db_grid_rows_effective(),
                    sizeof(*g_state.snake_scratch.row_bounds));
            g_state.snake_scratch.row_bounds_capacity =
                (size_t)db_grid_rows_effective();
        }
        g_state.snake_scratch.span_capacity = scratch_capacity;
    }

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

    if (db_gl_context_advertises_vbo() != 0) {
        const size_t probe_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                   g_state.vertex.vertex_stride * sizeof(float);
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
            db_gl_context_probe_upload_capabilities(
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

    db_gl1_configure_client_arrays_if_needed();
    db_gl1_refresh_capability_mode();
    infof("using capability mode: %s",
          db_renderer_opengl_gl1_5_gles1_1_capability_mode());
}

static void db_gl1_render_snake_draw_pass(
    const db_gl1_snake_frame_state_t *snake_frame, int allow_overdraw_collapse,
    int dirty_backbuffer_mode, int viewport_w, int viewport_h) {
    if (snake_frame == NULL) {
        return;
    }
    const db_snake_plan_t *plan = &snake_frame->plan;
    const db_snake_region_t *snake_target_region = &snake_frame->target_region;
    db_gl_upload_range_t *range_storage = snake_frame->preview_ranges;
    size_t draw_range_count = snake_frame->preview_count;
    const int is_grid_pattern = g_state.runtime_flags.is_snake_grid;
    const int is_grid_or_rect =
        (is_grid_pattern != 0) || (g_state.runtime_flags.is_snake_rect != 0);
    const int snake_transition_frame =
        (plan->phase_completed != 0) || (plan->phase_completed != 0) ||
        (plan->active_cursor == DB_SNAKE_CURSOR_PRE_ENTRY) ||
        (plan->next_cursor == DB_SNAKE_CURSOR_PRE_ENTRY);
    const int settled_scissor_heuristic_ok =
        (is_grid_pattern != 0) || db_gl1_snake_should_use_scissor_for_settled(
                                      range_storage, draw_range_count);
    const int has_span_scratch = (g_state.snake_scratch.spans != NULL) &&
                                 (g_state.snake_scratch.span_capacity > 0U);
    const int use_settled_scissor = db_history_should_use_snake_settled_scissor(
        (g_state.buffers.vbo != 0U), is_grid_or_rect, snake_transition_frame,
        snake_frame->force_full_upload, dirty_backbuffer_mode, has_span_scratch,
        settled_scissor_heuristic_ok);
    db_gl_upload_range_t active_ranges_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    db_gl_upload_range_t *active_ranges = active_ranges_local;
    size_t active_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if ((g_state.snake_replay.prev_upload_ranges != NULL) &&
        (g_state.snake_scratch.span_capacity > 0U)) {
        // Avoid truncation at high bench speed: use full scratch capacity.
        active_ranges = g_state.snake_replay.prev_upload_ranges;
        active_range_capacity = g_state.snake_scratch.span_capacity;
    }
    size_t active_range_count = 0U;
    int scissor_applied = 0;
    if (use_settled_scissor != 0) {
        db_gl_set_scissor_enabled(1);
        const size_t settled_span_count = db_snake_collect_damage_spans(
            g_state.snake_scratch.spans, g_state.snake_scratch.span_capacity,
            snake_target_region, plan->prev_start, plan->prev_count,
            plan->active_cursor, 0U, NULL);
        const db_gl1_scissor_fill_ctx_t scissor_ctx = {
            .color_b = snake_frame->target_b,
            .color_g = snake_frame->target_g,
            .color_r = snake_frame->target_r,
            .viewport_h = viewport_h,
            .viewport_w = viewport_w,
        };
        const size_t applied_rect_count =
            db_gl_for_each_span_scissor_rect_merged(
                g_state.snake_scratch.spans, settled_span_count,
                db_grid_cols_effective(), db_grid_rows_effective(), viewport_w,
                viewport_h, db_gl1_apply_scissor_fill_rect,
                (void *)&scissor_ctx);
        if (applied_rect_count > 0U) {
            scissor_applied = 1;
        }
        db_gl_set_scissor_enabled(0);
        active_range_count = db_gl1_build_snake_upload_ranges(
            plan, snake_frame->force_full_upload, active_ranges,
            active_range_capacity, allow_overdraw_collapse);
    }
    const int allow_empty_dirty_draw = dirty_backbuffer_mode;
    if ((draw_range_count == 0U) && (allow_empty_dirty_draw == 0)) {
        const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                    g_state.vertex.vertex_stride *
                                    sizeof(float);
        range_storage[0] = db_gl_upload_full_range(upload_bytes);
        draw_range_count = 1U;
    }
    const size_t upload_bytes = (size_t)g_state.vertex.draw_vertex_count *
                                g_state.vertex.vertex_stride * sizeof(float);
    if (g_state.runtime.backbuffer_draw_full != 0) {
        range_storage[0] = db_gl_upload_full_range(upload_bytes);
        draw_range_count = 1U;
    }
    const int draw_is_full_mesh = (draw_range_count == 1U) &&
                                  (range_storage[0].src_offset_bytes == 0U) &&
                                  (range_storage[0].size_bytes == upload_bytes);

    if (draw_range_count == 0U) {
        // No damage this frame; preserve backbuffer contents.
    } else if (draw_is_full_mesh != 0) {
        if (g_state.buffers.vbo != 0U) {
            db_gl1_upload_vbo_damage_ranges(
                g_state.vertex.vertices, upload_bytes, &g_state.vertex.upload,
                range_storage, draw_range_count);
            db_gl1_configure_vbo_arrays_if_needed();
        } else {
            db_gl1_configure_client_arrays_if_needed();
        }
        db_gl_draw_arrays_triangles(
            0, db_gl_draw_vertex_count_i32(BACKEND_NAME,
                                           g_state.vertex.draw_vertex_count));
        db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                     &g_state.frame.dirty_draw_frames, 1, 0);
    } else {
        // Preserved-backbuffer dirty redraw path: upload+draw changed ranges.
        const db_gl_upload_range_t *mesh_ranges =
            (use_settled_scissor != 0) ? active_ranges : range_storage;
        const size_t mesh_range_count =
            (use_settled_scissor != 0) ? active_range_count : draw_range_count;
        if ((g_state.buffers.vbo != 0U) && (mesh_range_count > 0U)) {
            db_gl1_upload_vbo_damage_ranges(
                g_state.vertex.vertices, upload_bytes, &g_state.vertex.upload,
                mesh_ranges, mesh_range_count);
            db_gl1_configure_vbo_arrays_if_needed();
            db_gl1_draw_dirty_ranges_common(BACKEND_NAME,
                                            g_state.vertex.vertex_stride,
                                            g_state.vertex.draw_vertex_count,
                                            mesh_ranges, mesh_range_count);
        } else if (mesh_range_count > 0U) {
            db_gl1_configure_client_arrays_if_needed();
            db_gl1_draw_dirty_ranges_common(BACKEND_NAME,
                                            g_state.vertex.vertex_stride,
                                            g_state.vertex.draw_vertex_count,
                                            mesh_ranges, mesh_range_count);
        }
        if ((mesh_range_count > 0U) || (scissor_applied != 0)) {
            db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                         &g_state.frame.dirty_draw_frames, 0,
                                         1);
        }
    }
    const int persist_full_mesh =
        (draw_is_full_mesh != 0) || (g_state.runtime.backbuffer_draw_full != 0);
    const db_gl_upload_range_t force_replay_full_range =
        db_gl_upload_full_range(upload_bytes);
    const db_gl_upload_range_t *persist_draw_ranges = range_storage;
    size_t persist_draw_count = draw_range_count;
    if (snake_frame->force_replay_full != 0) {
        persist_draw_ranges = &force_replay_full_range;
        persist_draw_count = 1U;
    }
    if ((snake_frame->force_replay_full == 0) && (persist_full_mesh == 0) &&
        (use_settled_scissor != 0)) {
        persist_draw_ranges = active_ranges;
        persist_draw_count = active_range_count;
    }
    if ((persist_draw_count > 0U) &&
        (g_state.snake_replay.prev_upload_ranges != NULL) &&
        (persist_draw_count <= g_state.snake_scratch.span_capacity)) {
        g_state.snake_replay.prev_upload_count =
            db_gl_copy_upload_ranges(persist_draw_ranges, persist_draw_count,
                                     g_state.snake_replay.prev_upload_ranges,
                                     g_state.snake_scratch.span_capacity);
    } else if (persist_draw_count > 0U) {
        g_state.snake_replay.prev_upload_count = 0U;
    }
    // Keep previous replay ranges across zero-damage frames so the next
    // backbuffer can still be brought forward in double-buffered
    // preserved-backbuffer mode.
    g_state.snake_backbuffer_state.backbuffer_valid = 1;
}

static void db_gl1_prepare_snake_frame_state(db_gl1_snake_frame_state_t *state,
                                             int is_double_buffered,
                                             int dirty_backbuffer_mode,
                                             int has_viewport,
                                             int allow_overdraw_collapse) {
    if (state == NULL) {
        return;
    }

    const db_history_snake_step_eval_t eval =
        db_history_eval_snake_step_from_runtime(&g_state.runtime);
    state->plan = eval.plan;
    db_snake_step_target_t target = eval.target;
    const db_snake_shape_kind_t shape_kind = eval.shape_kind;
    state->target_region = target.region;
    state->target_r = target.target_r;
    state->target_g = target.target_g;
    state->target_b = target.target_b;

    const db_history_snake_backbuffer_action_t history_action =
        db_history_eval_snake_backbuffer_action_io(
            dirty_backbuffer_mode, is_double_buffered,
            &g_state.snake_backbuffer_state.seed_frames_remaining,
            &g_state.snake_backbuffer_state.resync_frames_remaining,
            &g_state.snake_backbuffer_state.initial_seed_done,
            &g_state.snake_backbuffer_state.backbuffer_valid);

    if (db_history_run_seed_clear_if_needed(
            history_action.should_seed_now, &g_state.runtime,
            db_gl1_seed_backbuffer_clear_cb, NULL) != 0) {
        db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                     &g_state.frame.dirty_draw_frames, 1, 0);
        g_state.snake_replay.prev_upload_count = 0U;
    }
    if (history_action.should_force_full_upload != 0) {
        state->force_full_upload = 1;
    }

    if ((g_state.snake_replay.curr_upload_ranges != NULL) &&
        (g_state.snake_scratch.span_capacity > 0U)) {
        state->preview_ranges = g_state.snake_replay.curr_upload_ranges;
        state->preview_capacity = g_state.snake_scratch.span_capacity;
    }
    state->preview_count = db_gl1_build_snake_upload_ranges(
        &state->plan, state->force_full_upload, state->preview_ranges,
        state->preview_capacity, allow_overdraw_collapse);
    if ((g_state.runtime_flags.is_snake_grid != 0) &&
        (state->force_full_upload != 0) && dirty_backbuffer_mode &&
        has_viewport && (g_state.snake_scratch.spans != NULL)) {
        // Grid fast-path on invalid backbuffer: clear to the pre-step base
        // phase, then redraw the full non-base set (settled + active). If the
        // span set exceeds capacity, keep the normal full upload.
        const size_t needed_spans =
            db_snake_plan_span_capacity_needed(&state->plan);
        if (needed_spans <= state->preview_capacity) {
            double base_r = 0.0;
            double base_g = 0.0;
            double base_b = 0.0;
            const int base_phase = (state->plan.phase_flag == 0) ? 1 : 0;
            db_grid_target_color_rgb(base_phase, &base_r, &base_g, &base_b);
            db_gl_set_scissor_enabled(0);
            db_gl_clear_color_rgb(db_double_to_f32(base_r),
                                  db_double_to_f32(base_g),
                                  db_double_to_f32(base_b));
            db_gl_clear_color_buffer();
            db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                         &g_state.frame.dirty_draw_frames, 1,
                                         0);

            const size_t replay_span_count =
                db_snake_collect_damage_spans_for_plan(
                    g_state.snake_scratch.spans, state->preview_capacity,
                    &state->target_region, &state->plan, NULL);
            state->preview_count = db_gl_collect_span_upload_ranges(
                db_grid_cols_effective(),
                db_rect_tile_bytes(g_state.vertex.vertex_stride),
                db_rect_tile_bytes(g_state.vertex.vertex_stride),
                g_state.snake_scratch.spans, replay_span_count,
                state->preview_ranges, state->preview_capacity);
            state->preview_count = db_gl1_optimize_upload_ranges(
                state->preview_ranges, state->preview_count, 0,
                DB_GL1_SNAKE_RANGE_COLLAPSE_THRESHOLD);
            state->force_replay_full = 1;
        }
    }
    const int can_replay_snake = db_history_can_replay_previous_damage(
        is_double_buffered, dirty_backbuffer_mode,
        g_state.snake_backbuffer_state.backbuffer_valid,
        g_state.snake_replay.prev_upload_count);
    if (can_replay_snake != 0) {
        // Replay the full previously-drawn damage set onto the alternate
        // backbuffer. Do not cap to BENCH_SNAKE_PHASE_WINDOW_TILES here:
        // high speed can produce far more ranges.
        size_t replay_draw_count = g_state.snake_replay.prev_upload_count;
        DB_LOG_CAPACITY_EXCEEDED_ONCE(
            BACKEND_NAME, "snake_replay_prev_upload_ranges", replay_draw_count,
            g_state.snake_scratch.span_capacity);
        if (replay_draw_count > g_state.snake_scratch.span_capacity) {
            replay_draw_count = g_state.snake_scratch.span_capacity;
        }
        replay_draw_count = db_gl_coalesce_upload_ranges_in_place(
            g_state.snake_replay.prev_upload_ranges, replay_draw_count);
        if (replay_draw_count > 0U) {
            if (g_state.buffers.vbo != 0U) {
                db_gl1_upload_vbo_damage_ranges(
                    g_state.vertex.vertices,
                    (size_t)g_state.vertex.draw_vertex_count *
                        g_state.vertex.vertex_stride * sizeof(float),
                    &g_state.vertex.upload,
                    g_state.snake_replay.prev_upload_ranges, replay_draw_count);
                db_gl1_configure_vbo_arrays_if_needed();
                db_gl1_draw_dirty_ranges_common(
                    BACKEND_NAME, g_state.vertex.vertex_stride,
                    g_state.vertex.draw_vertex_count,
                    g_state.snake_replay.prev_upload_ranges, replay_draw_count);
            } else {
                db_gl1_configure_client_arrays_if_needed();
                db_gl1_draw_dirty_ranges_common(
                    BACKEND_NAME, g_state.vertex.vertex_stride,
                    g_state.vertex.draw_vertex_count,
                    g_state.snake_replay.prev_upload_ranges, replay_draw_count);
            }
        }
    }

    db_render_snake_step(
        &state->plan, &target.region, shape_kind, g_state.runtime.pattern_seed,
        state->plan.active_shape_index, target.target_r, target.target_g,
        target.target_b, target.force_full_fill_on_phase_complete);
    db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
}

static void db_gl1_render_gradient_frame(int viewport_w, int viewport_h,
                                         int is_double_buffered) {
    const db_gradient_damage_plan_t gradient_plan =
        db_gradient_step_from_runtime(g_state.runtime.pattern,
                                      g_state.runtime.gradient.head_row,
                                      g_state.runtime.gradient.direction_down,
                                      g_state.runtime.gradient.cycle_index,
                                      g_state.runtime.bench_speed_step);

    // Render MUST use the plan's render_* state. The plan's next_* state is
    // only applied to the runtime AFTER we draw, matching CPU renderer
    // semantics.
    const uint32_t gradient_render_head_row =
        gradient_plan.render_state.head_row;
    const int gradient_render_direction_down =
        gradient_plan.render_state.direction_down;
    const uint32_t gradient_render_cycle_index =
        gradient_plan.render_state.cycle_index;

    db_dirty_row_range_t gradient_dirty_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] =
        {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
    const size_t gradient_dirty_count =
        db_gradient_collect_dirty_ranges(&gradient_plan, gradient_dirty_ranges);

    // When bench_speed_step > 1, the head can advance by multiple rows. Plan
    // dirty ranges cover the new sweep window, but traversed rows can be
    // skipped unless we repaint them as solid per-row colors.
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
                // Downwards: skipped rows are render+1 .. render+skipped.
                const uint32_t start_row = (render_head + 1U) % rows;
                skipped_ranges[0].row_start = start_row;
                skipped_ranges[0].row_count =
                    db_u32_min(rows - start_row, skipped_rows);
                skipped_ranges[1].row_start = 0U;
                skipped_ranges[1].row_count =
                    skipped_rows - skipped_ranges[0].row_count;
                skipped_count = (skipped_ranges[1].row_count > 0U) ? 2U : 1U;
            } else if (render_head >= skipped_rows) {
                // No wrap past 0: [render-skipped .. render-1]
                skipped_ranges[0].row_start = render_head - skipped_rows;
                skipped_ranges[0].row_count = skipped_rows;
                skipped_count = (skipped_rows > 0U) ? 1U : 0U;
            } else {
                // Wraps past 0: [0 .. render-1] and [rows-underflow .. rows-1]
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

    // Gradient patterns: damage-only updates directly on the default
    // framebuffer/backbuffer.
    if ((viewport_w > 0) && (viewport_h > 0) && (rows > 0U)) {
        if (g_state.runtime.backbuffer_draw_full != 0) {
            db_dirty_row_range_t full_range[2] = {{0U, 0U}, {0U, 0U}};
            const size_t full_count =
                db_history_set_full_row_ranges(rows, full_range, 2U);
            db_gl1_draw_gradient_dirty_rows_hybrid(
                full_range, full_count, gradient_render_head_row,
                gradient_render_direction_down, gradient_render_cycle_index,
                viewport_w, viewport_h);
            g_state.backbuffer_valid = 1;
            const db_gradient_state_t render_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };
            db_history_gradient_replay_state_store(&g_state.gradient_prev_frame,
                                                   full_range, full_count,
                                                   &render_state);
            db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                         &g_state.frame.dirty_draw_frames, 1,
                                         0);
        } else {
            db_dirty_row_range_t
                curr_draw_ranges[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
                    {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
            size_t curr_draw_count = db_gradient_build_curr_draw_ranges(
                skipped_ranges, skipped_count, gradient_dirty_ranges,
                gradient_dirty_count, curr_draw_ranges,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            const int needs_full_seed = db_history_should_seed_full_on_invalid(
                g_state.backbuffer_valid);
            const db_dirty_row_range_t *persist_ranges = curr_draw_ranges;
            size_t persist_count = curr_draw_count;
            db_gradient_state_t persist_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };

            if (needs_full_seed != 0) {
                (void)db_history_apply_full_seed_rows_if_needed(
                    &g_state.backbuffer_valid, rows, curr_draw_ranges,
                    DB_GL1_GRADIENT_REPLAY_ROW_CAP, &curr_draw_count);
                db_gl1_draw_gradient_dirty_rows_hybrid(
                    curr_draw_ranges, curr_draw_count, gradient_render_head_row,
                    gradient_render_direction_down, gradient_render_cycle_index,
                    viewport_w, viewport_h);
                persist_count = curr_draw_count;
                db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                             &g_state.frame.dirty_draw_frames,
                                             1, 0);
            } else {
                const int has_replay =
                    (is_double_buffered != 0) &&
                    (g_state.gradient_prev_frame.draw_count > 0U);
                const int has_current = (curr_draw_count > 0U);
                db_dirty_row_range_t
                    replay_draw_ranges[DB_GRADIENT_DRAW_RANGE_WORK_CAP] = {
                        {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U},
                        {0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
                const db_dirty_row_range_t *replay_draw_ptr =
                    g_state.gradient_prev_frame.draw_rows;
                size_t replay_draw_count =
                    g_state.gradient_prev_frame.draw_count;
                if ((has_replay != 0) && (has_current != 0)) {
                    replay_draw_count = db_gradient_subtract_replay_ranges(
                        g_state.gradient_prev_frame.draw_rows,
                        g_state.gradient_prev_frame.draw_count,
                        curr_draw_ranges, curr_draw_count, replay_draw_ranges,
                        DB_GRADIENT_DRAW_RANGE_WORK_CAP);
                    replay_draw_ptr = replay_draw_ranges;
                }
                const int has_replay_draw = (replay_draw_count > 0U);
                const int replay_uses_mesh =
                    (has_replay_draw != 0) &&
                    db_gl1_gradient_should_use_mesh(
                        replay_draw_ptr, replay_draw_count,
                        DB_GL1_GRADIENT_MESH_ROW_THRESHOLD);
                const int current_uses_mesh =
                    (has_current != 0) &&
                    db_gl1_gradient_should_use_mesh(
                        curr_draw_ranges, curr_draw_count,
                        DB_GL1_GRADIENT_MESH_ROW_THRESHOLD);
                if ((g_state.runtime_flags.is_gradient_sweep != 0) &&
                    (has_replay != 0) && (has_current == 0)) {
                    // At bottom bounce, sweep can be replay-only for one frame.
                    persist_ranges = g_state.gradient_prev_frame.draw_rows;
                    persist_count = g_state.gradient_prev_frame.draw_count;
                    persist_state = g_state.gradient_prev_frame.state;
                }

                if ((has_replay_draw != 0) && (has_current != 0) &&
                    (replay_uses_mesh == 0) && (current_uses_mesh == 0)) {
                    db_gl1_draw_gradient_dirty_rows_gpu(
                        BACKEND_NAME, replay_draw_ptr, replay_draw_count,
                        g_state.gradient_prev_frame.state.head_row,
                        g_state.gradient_prev_frame.state.direction_down,
                        g_state.gradient_prev_frame.state.cycle_index,
                        viewport_w, viewport_h);
                    db_gl1_draw_gradient_dirty_rows_gpu(
                        BACKEND_NAME, curr_draw_ranges, curr_draw_count,
                        gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index, viewport_w, viewport_h);
                } else if ((has_replay_draw != 0) && (has_current != 0) &&
                           (replay_uses_mesh != 0) &&
                           (current_uses_mesh != 0)) {
                    db_gl1_draw_gradient_dirty_rows_mesh(
                        replay_draw_ptr, replay_draw_count,
                        g_state.gradient_prev_frame.state.head_row,
                        g_state.gradient_prev_frame.state.direction_down,
                        g_state.gradient_prev_frame.state.cycle_index);
                    db_gl1_draw_gradient_dirty_rows_mesh(
                        curr_draw_ranges, curr_draw_count,
                        gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index);
                } else {
                    if (has_replay_draw != 0) {
                        db_gl1_draw_gradient_dirty_rows_hybrid(
                            replay_draw_ptr, replay_draw_count,
                            g_state.gradient_prev_frame.state.head_row,
                            g_state.gradient_prev_frame.state.direction_down,
                            g_state.gradient_prev_frame.state.cycle_index,
                            viewport_w, viewport_h);
                    }
                    if (has_current != 0) {
                        db_gl1_draw_gradient_dirty_rows_hybrid(
                            curr_draw_ranges, curr_draw_count,
                            gradient_render_head_row,
                            gradient_render_direction_down,
                            gradient_render_cycle_index, viewport_w,
                            viewport_h);
                    }
                }
                db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                             &g_state.frame.dirty_draw_frames,
                                             0, 1);
            }

            // Persist the damage we drew this frame for next double-buffer
            // replay.
            DB_LOG_CAPACITY_EXCEEDED_ONCE(
                BACKEND_NAME, "gradient_replay_persist_rows", persist_count,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            db_history_gradient_replay_state_store(
                &g_state.gradient_prev_frame, persist_ranges, persist_count,
                &persist_state);
        }
    }

    // Apply step AFTER rendering (CPU renderer ordering).
    db_gradient_apply_step_to_runtime(&g_state.runtime, &gradient_plan);
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(uint32_t frame_index,
                                                   int viewport_width_px,
                                                   int viewport_height_px,
                                                   int double_buffered) {
    db_gl1_snake_frame_state_t snake_frame = {
        .plan = {0},
        .target_region = {0},
        .target_r = 0.0,
        .target_g = 0.0,
        .target_b = 0.0,
        .preview_ranges = NULL,
        .preview_capacity = 0U,
        .preview_count = 0U,
        .force_full_upload = 0,
        .force_replay_full = 0,
    };
    db_gl_upload_range_t snake_preview_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    snake_frame.preview_ranges = snake_preview_local;
    snake_frame.preview_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;

    const db_renderer_viewport_state_t viewport_state =
        db_renderer_resolve_viewport_state(BACKEND_NAME, &viewport_width_px,
                                           &viewport_height_px,
                                           &g_state.viewport.last_viewport_w,
                                           &g_state.viewport.last_viewport_h);

    if (viewport_state.viewport_changed != 0) {
        // Keep GL viewport in sync with drawable pixels (HiDPI-safe)
        // without querying GL state.
        db_gl_set_viewport_px(viewport_width_px, viewport_height_px);
        db_history_invalidate_snake_backbuffer_on_resize(
            double_buffered, &g_state.backbuffer_valid,
            &g_state.snake_replay.prev_upload_count,
            &g_state.snake_backbuffer_state);
    }

    // Preserved-backbuffer assumption for snake paths: dirty draws operate
    // directly on the default framebuffer.

    // Use a known viewport size without querying GL state.
    // Pixel-space ops must use drawable pixels; tile math stays on grid
    // cols/rows.
    const int viewport_w = viewport_state.viewport_width_px;
    const int viewport_h = viewport_state.viewport_height_px;
    const int has_viewport = viewport_state.has_viewport;
    const int is_double_buffered = (double_buffered != 0);
    const int allow_overdraw_collapse =
        ((g_state.runtime_flags.is_snake_history_texture != 0) &&
         (g_state.runtime_flags.is_snake_shapes == 0))
            ? 1
            : 0;

    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl1_prepare_snake_frame_state(
            &snake_frame, is_double_buffered,
            g_state.runtime_flags.uses_dirty_backbuffer_mode, has_viewport,
            allow_overdraw_collapse);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        db_gl1_render_gradient_frame(viewport_w, viewport_h,
                                     is_double_buffered);
    } else if (g_state.runtime_flags.is_bands != 0) {
        // Bands are solid rect fills directly to the default framebuffer.
        db_gl1_draw_bands_gpu(db_grid_cols_effective(), BENCH_BANDS,
                              frame_index, viewport_width_px,
                              viewport_height_px);
        db_history_record_draw_stats(&g_state.frame.full_draw_frames,
                                     &g_state.frame.dirty_draw_frames, 1, 0);
    }

    if (g_state.runtime_flags.is_snake_history_texture != 0) {
        db_gl1_render_snake_draw_pass(
            &snake_frame, allow_overdraw_collapse,
            g_state.runtime_flags.uses_dirty_backbuffer_mode, viewport_w,
            viewport_h);
    }
    g_state.frame.state_hash = db_benchmark_runtime_state_hash_cross_renderer(
        &g_state.runtime, g_state.frame.frame_index, db_grid_cols_effective(),
        db_grid_rows_effective());
    g_state.frame.frame_index++;
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
    free(g_state.snake_replay.curr_upload_ranges);
    free(g_state.snake_replay.prev_upload_ranges);
    free(g_state.snake_scratch.spans);
    free(g_state.snake_scratch.row_bounds);
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
    if (full_draw_frames != NULL) {
        *full_draw_frames = g_state.frame.full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = g_state.frame.dirty_draw_frames;
    }
}
