#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_geometry.h"
#include "../renderer_benchmark_gradient.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_gl_common.h"
#include "../renderer_gl_proc_runtime_internal.h"
#include "../renderer_snake_types.h"
#include "renderer_opengl_gl1_5_gles1_1_internal.h"
#include "renderer_opengl_gl1_5_gles1_1_util.h"
#include <stddef.h>
#include <stdint.h>

size_t db_gl1_build_gradient_compact_row_vertices(
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

void db_gl1_configure_client_arrays_if_needed(void) {
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

void db_gl1_configure_vbo_arrays_if_needed(void) {
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
        db_checked_size_to_i32(BACKEND_NAME, first_vertex_label,
                               g_state.compact_vbo.first_vertex),
        db_gl_draw_vertex_count_i32(BACKEND_NAME,
                                    db_checked_size_to_u32(BACKEND_NAME,
                                                           "draw_vertex_count",
                                                           draw_vertex_count)));
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
        0, db_gl_draw_vertex_count_i32(
               BACKEND_NAME,
               db_checked_size_to_u32(BACKEND_NAME, "draw_vertex_count",
                                      draw_vertex_count)));
    g_state.client_arrays_configured = 0;
    g_state.vbo_arrays_configured = 0;
    db_gl1_invalidate_array_pointer_cache();
}

int db_gl1_draw_compact_blocks_from_snake_colors_once(
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

void db_gl1_draw_gradient_dirty_blocks_mesh(
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

void db_gl1_draw_bands_compact(uint32_t cols, uint32_t band_count,
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
            const uint32_t tile_x0 = db_checked_u64_to_u32(
                BACKEND_NAME, "gl1_band_tile_x0",
                ((uint64_t)band * (uint64_t)cols) / (uint64_t)band_count);
            const uint32_t tile_x1 =
                db_checked_u64_to_u32(BACKEND_NAME, "gl1_band_tile_x1",
                                      ((uint64_t)(band + 1U) * (uint64_t)cols) /
                                          (uint64_t)band_count);
            x0 = db_checked_u32_to_i32(BACKEND_NAME, "gl1_band_x0_px",
                                       db_grid_axis_edge_to_pixel_coord(
                                           cols, tile_x0, viewport_w_u32));
            x1 = db_checked_u32_to_i32(BACKEND_NAME, "gl1_band_x1_px",
                                       db_grid_axis_edge_to_pixel_coord(
                                           cols, tile_x1, viewport_w_u32));
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
