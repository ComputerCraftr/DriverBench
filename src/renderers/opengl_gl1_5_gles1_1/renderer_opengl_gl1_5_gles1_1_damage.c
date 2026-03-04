#include "renderer_opengl_gl1_5_gles1_1_damage.h"

#include <stddef.h>
#include <stdint.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"

size_t
db_gl1_collect_pattern_damage_ranges(const db_gl1_damage_collect_ctx_t *ctx,
                                     db_gl_upload_range_t *range_storage,
                                     size_t range_capacity) {
    if (ctx == NULL) {
        return 0U;
    }
    db_gl_upload_range_t upload_ranges[BENCH_SNAKE_PHASE_WINDOW_TILES] = {
        {0U, 0U, 0U}};
    db_gl_pattern_upload_collect_t collect_ctx = {
        .pattern = ctx->pattern,
        .cols = ctx->cols,
        .rows = ctx->rows,
        .upload_bytes = ctx->upload_bytes,
        .upload_tile_bytes = ctx->upload_tile_bytes,
        .force_full_upload = ctx->force_full_upload,
        .snake_plan = ctx->snake_plan,
        .snake_prev_start =
            (ctx->snake_plan != NULL) ? ctx->snake_plan->prev_start : 0U,
        .snake_prev_count =
            (ctx->snake_plan != NULL) ? ctx->snake_plan->prev_count : 0U,
        .pattern_seed = ctx->pattern_seed,
        .snake_scratch = ctx->snake_scratch,
        .damage_row_ranges = ctx->damage_row_ranges,
        .damage_row_count = ctx->damage_row_count,
    };
    db_gl_upload_range_t *local_range_storage = upload_ranges;
    size_t local_range_capacity = BENCH_SNAKE_PHASE_WINDOW_TILES;
    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(ctx->pattern);
    if (pattern_flags.is_snake_history_texture != 0) {
        local_range_storage = ctx->default_history_range_storage;
        local_range_capacity = (ctx->snake_scratch != NULL)
                                   ? ctx->snake_scratch->span_capacity
                                   : 0U;
    } else if (pattern_flags.is_gradient != 0) {
        local_range_capacity = ctx->gradient_dirty_range_cap;
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

void db_gl1_upload_vbo_damage_ranges(const float *vertices, size_t upload_bytes,
                                     const db_gl_upload_probe_result_t *upload,
                                     const db_gl_upload_range_t *range_storage,
                                     size_t upload_range_count) {
    if ((upload == NULL) || (upload_range_count == 0U)) {
        return;
    }
    db_gl_upload_ranges_target(
        vertices, upload_bytes, range_storage, upload_range_count,
        DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER, 0U, upload->use_persistent_upload,
        upload->persistent_mapped_ptr, upload->use_map_range_upload,
        upload->use_map_buffer_upload);
}

void db_gl1_draw_dirty_ranges_common(const char *backend_name,
                                     size_t vertex_stride,
                                     uint32_t draw_vertex_count,
                                     const db_gl_upload_range_t *ranges,
                                     size_t range_count) {
    const size_t bytes_per_vertex = vertex_stride * sizeof(float);
    if ((ranges == NULL) || (bytes_per_vertex == 0U)) {
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
        if ((first_vertex + vertex_count) > (size_t)draw_vertex_count) {
            continue;
        }

        const unsigned int first_vertex_u32 =
            db_checked_size_to_u32(backend_name, "first_vertex", first_vertex);
        const unsigned int vertex_count_u32 =
            db_checked_size_to_u32(backend_name, "vertex_count", vertex_count);
        db_gl_draw_arrays_triangles(
            db_checked_u32_to_i32(backend_name, "first_vertex",
                                  first_vertex_u32),
            db_checked_u32_to_i32(backend_name, "vertex_count",
                                  vertex_count_u32));
    }
}
