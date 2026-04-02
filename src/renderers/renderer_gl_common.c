#include "renderer_gl_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../config/benchmark_config.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_common.h"

size_t db_gl_compact_vbo_total_bytes(size_t base_vbo_bytes) {
    if (base_vbo_bytes > (SIZE_MAX / 2U)) {
        return 0U;
    }
    return base_vbo_bytes * 2U;
}

void db_gl_compact_vbo_init_or_fail(const char *backend_name,
                                    db_gl_compact_vbo_state_t *compact,
                                    size_t base_vbo_bytes,
                                    size_t vertex_stride) {
    if ((backend_name == NULL) || (compact == NULL)) {
        db_failf("renderer_gl_common",
                 "db_gl_compact_vbo_init_or_fail: invalid arguments");
    }
    if (vertex_stride == 0U) {
        db_failf(backend_name, "compact vertex_stride is zero");
    }
    *compact = (db_gl_compact_vbo_state_t){0};
    compact->vbo_capacity_bytes = base_vbo_bytes;
    compact->vbo_offset_bytes = base_vbo_bytes;
    compact->scratch_float_capacity = base_vbo_bytes / sizeof(float);
    compact->first_vertex =
        compact->vbo_offset_bytes / (vertex_stride * sizeof(float));
    compact->scratch_vertices = (float *)db_alloc_array_or_fail(
        backend_name, "compact_vbo_scratch", compact->scratch_float_capacity,
        sizeof(float));
}

void db_gl_compact_vbo_init_standalone_or_fail(
    const char *backend_name, db_gl_compact_vbo_state_t *compact,
    size_t compact_vbo_bytes, size_t vertex_stride) {
    if ((backend_name == NULL) || (compact == NULL)) {
        db_failf(
            "renderer_gl_common",
            "db_gl_compact_vbo_init_standalone_or_fail: invalid arguments");
    }
    if ((vertex_stride == 0U) || (compact_vbo_bytes == 0U)) {
        db_failf(backend_name, "compact standalone VBO layout is invalid");
    }
    *compact = (db_gl_compact_vbo_state_t){
        .vbo_capacity_bytes = compact_vbo_bytes,
        .vbo_offset_bytes = 0U,
        .scratch_float_capacity = compact_vbo_bytes / sizeof(float),
        .first_vertex = 0U,
    };
    compact->scratch_vertices = (float *)db_alloc_array_or_fail(
        backend_name, "compact_vbo_scratch", compact->scratch_float_capacity,
        sizeof(float));
}

void db_gl_compact_vbo_free(db_gl_compact_vbo_state_t *compact) {
    if (compact == NULL) {
        return;
    }
    free(compact->scratch_vertices);
    *compact = (db_gl_compact_vbo_state_t){0};
}

int db_init_grid_vertices_common(db_gl_vertex_init_t *out_state,
                                 db_pattern_t pattern, size_t vertex_stride) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(pattern);
    if ((tile_count_u64 == 0U) || (tile_count_u64 > UINT32_MAX)) {
        return 0;
    }

    const uint64_t vertex_count_u64 = tile_count_u64 * DB_RECT_VERTEX_COUNT;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * (uint64_t)vertex_stride;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_tile_count", tile_count_u64);
    float *vertices = calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }
    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);
        const size_t base =
            (size_t)tile_index * DB_RECT_VERTEX_COUNT * vertex_stride;
        float *unit = &vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, vertex_stride);
        db_set_rect_unit_rgb(
            unit, vertex_stride, DB_VERTEX_POSITION_FLOAT_COUNT,
            (const float[3]){BENCH_GRID_PHASE0_R_F, BENCH_GRID_PHASE0_G_F,
                             BENCH_GRID_PHASE0_B_F});
        if (vertex_stride == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, vertex_stride,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   BENCH_CLEAR_COLOR_A_F);
        }
    }

    *out_state = (db_gl_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->vertex_stride = vertex_stride;
    out_state->work_unit_count = tile_count;
    out_state->draw_vertex_count = db_checked_u64_to_u32(
        DB_BENCH_COMMON_BACKEND, "grid_draw_vertex_count", vertex_count_u64);
    return 1;
}

int db_init_vertices_for_pattern_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    db_pattern_t pattern, size_t vertex_stride) {
    const int initialized =
        db_init_grid_vertices_common(out_state, pattern, vertex_stride);
    if (initialized == 0) {
        db_failf(backend_name, "benchmark mode '%s' initialization failed",
                 db_pattern_mode_name(pattern));
    }
    out_state->pattern = pattern;
    return 1;
}

int db_init_vertices_for_runtime_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride) {
    if (runtime_state == NULL) {
        return 0;
    }
    if (!db_init_vertices_for_pattern_common_with_stride(
            backend_name, out_state, runtime_state->pattern, vertex_stride)) {
        return 0;
    }

    out_state->pattern = runtime_state->pattern;
    out_state->work_unit_count = runtime_state->work_unit_count;
    out_state->draw_vertex_count = runtime_state->draw_vertex_count;
    out_state->vertex_stride = vertex_stride;
    return 1;
}

void db_update_grid_vertices_for_bands_rgb_stride(
    float *verts, uint32_t cols, uint32_t rows, uint32_t band_count,
    uint32_t frame_index, size_t stride_floats, size_t color_offset_floats) {
    if ((verts == NULL) || (cols == 0U) || (rows == 0U) || (band_count == 0U)) {
        return;
    }
    for (uint32_t band = 0U; band < band_count; band++) {
        const uint32_t col_start = (band * cols) / band_count;
        const uint32_t col_end = ((band + 1U) * cols) / band_count;
        if ((col_end <= col_start) || (col_start >= cols)) {
            continue;
        }
        double color_rgb[3] = {0.0, 0.0, 0.0};
        db_band_color_rgb3(band, band_count, frame_index, color_rgb);
        float color_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(color_rgb, color_rgb_f32);

        for (uint32_t row = 0U; row < rows; row++) {
            const uint32_t first_tile = (row * cols) + col_start;
            db_set_rect_tile_range_rgb(verts, first_tile, col_end - col_start,
                                       stride_floats, color_offset_floats,
                                       color_rgb_f32);
        }
    }
}
