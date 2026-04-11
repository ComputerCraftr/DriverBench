#include "gl_common.h"
#include "core/db_log.h"
#include "core/db_numeric.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/db_core.h"
#include "core/db_raster_geometry.h"
#include "core/db_render_types.h"
#include "core/db_renderer_support.h"

void db_gl_compact_vbo_init_or_fail(const char *backend_name,
                                    db_gl_compact_vbo_state_t *compact,
                                    size_t base_vbo_bytes,
                                    size_t vertex_stride) {
    if ((backend_name == NULL) || (compact == NULL)) {
        DB_RUNTIME_FAIL("renderer_gl_common",
                        "db_gl_compact_vbo_init_or_fail: invalid arguments");
    }
    if (vertex_stride == 0U) {
        DB_RUNTIME_FAIL(backend_name, "compact vertex_stride is zero");
    }
    *compact = (db_gl_compact_vbo_state_t){0};
    compact->vbo_capacity_bytes = base_vbo_bytes;
    compact->vbo_offset_bytes = base_vbo_bytes;
    compact->scratch_float_capacity = base_vbo_bytes / sizeof(float);
    compact->first_vertex =
        compact->vbo_offset_bytes / (vertex_stride * sizeof(float));
    compact->scratch_vertices = (float *)db_malloc_or_fail(
        backend_name, "compact_vbo_scratch", compact->scratch_float_capacity,
        sizeof(float));
}

void db_gl_compact_vbo_init_standalone_or_fail(
    const char *backend_name, db_gl_compact_vbo_state_t *compact,
    size_t compact_vbo_bytes, size_t vertex_stride) {
    if ((backend_name == NULL) || (compact == NULL)) {
        DB_RUNTIME_FAIL(
            "renderer_gl_common",
            "db_gl_compact_vbo_init_standalone_or_fail: invalid arguments");
    }
    if ((vertex_stride == 0U) || (compact_vbo_bytes == 0U)) {
        DB_RUNTIME_FAIL(backend_name,
                        "compact standalone VBO layout is invalid");
    }
    *compact = (db_gl_compact_vbo_state_t){
        .vbo_capacity_bytes = compact_vbo_bytes,
        .vbo_offset_bytes = 0U,
        .scratch_float_capacity = compact_vbo_bytes / sizeof(float),
        .first_vertex = 0U,
    };
    compact->scratch_vertices = (float *)db_malloc_or_fail(
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

int db_init_vertices_for_execution_config(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_renderer_execution_config_t *config, size_t vertex_stride) {
    if ((backend_name == NULL) || (out_state == NULL) || (config == NULL) ||
        (config->work_unit_count == 0U)) {
        return 0;
    }
    const uint64_t vertex_count =
        (uint64_t)config->work_unit_count * DB_RECT_VERTEX_COUNT;
    const uint64_t float_count = vertex_count * (uint64_t)vertex_stride;
    if ((vertex_count > UINT32_MAX) ||
        (float_count > (uint64_t)(SIZE_MAX / sizeof(float)))) {
        return 0;
    }
    float *vertices = calloc((size_t)float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }
    const uint32_t cols = config->grid_cols;
    const uint32_t rows = config->grid_rows;
    float rgba[4] = {0.0F};
    db_rgba_f64_to_f32_rgba4(config->seed_rgba_f64, rgba);
    for (uint32_t tile = 0U; tile < config->work_unit_count; tile++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_grid_tile_bounds_ndc_for_extent(cols, rows, tile, &x0, &y0, &x1,
                                           &y1);
        float *unit =
            &vertices[(size_t)tile * DB_RECT_VERTEX_COUNT * vertex_stride];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, vertex_stride);
        db_set_rect_unit_rgb(unit, vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT, rgba);
        if (vertex_stride == DB_ES_VERTEX_FLOAT_STRIDE) {
            db_set_rect_unit_alpha(unit, vertex_stride,
                                   DB_VERTEX_POSITION_FLOAT_COUNT +
                                       DB_VERTEX_COLOR_FLOAT_COUNT,
                                   rgba[3]);
        }
    }
    *out_state = (db_gl_vertex_init_t){
        .vertices = vertices,
        .vertex_stride = vertex_stride,
        .work_unit_count = config->work_unit_count,
        .draw_vertex_count = (uint32_t)vertex_count,
    };
    return 1;
}
