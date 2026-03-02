#ifndef RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H
#define RENDERER_OPENGL_GL1_5_GLES1_1_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "../../config/benchmark_config.h"
#include "../renderer_gl_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

typedef struct {
    const char *backend_name;
    uint32_t pattern;
    uint32_t cols;
    uint32_t rows;
    size_t upload_bytes;
    size_t upload_tile_bytes;
    int force_full_upload;
    const db_snake_plan_t *snake_plan;
    uint32_t pattern_seed;
    db_snake_col_span_t *snake_spans;
    size_t snake_scratch_capacity;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    const db_dirty_row_range_t *damage_row_ranges;
    size_t damage_row_count;
    db_gl_upload_range_t *default_history_range_storage;
    size_t gradient_dirty_range_cap;
} db_gl1_damage_collect_ctx_t;

size_t
db_gl1_collect_pattern_damage_ranges(const db_gl1_damage_collect_ctx_t *ctx,
                                     db_gl_upload_range_t *range_storage,
                                     size_t range_capacity);

void db_gl1_upload_vbo_damage_ranges(const float *vertices, size_t upload_bytes,
                                     const db_gl_upload_probe_result_t *upload,
                                     const db_gl_upload_range_t *range_storage,
                                     size_t upload_range_count);

void db_gl1_draw_dirty_ranges_common(const char *backend_name,
                                     size_t vertex_stride,
                                     uint32_t draw_vertex_count,
                                     const db_gl_upload_range_t *ranges,
                                     size_t range_count);

#endif
