#include "renderer_gl_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../config/benchmark_config.h"
#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
#include "../core/db_hash.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_common.h"
#include "renderer_snake_common.h"
#include "renderer_snake_shape_common.h"

static int db_gl_upload_ranges_can_merge(const db_gl_upload_range_t *left,
                                         const db_gl_upload_range_t *right) {
    if ((left == NULL) || (right == NULL) || (left->size_bytes == 0U) ||
        (right->size_bytes == 0U)) {
        return 0;
    }
    if ((left->src_offset_bytes > (SIZE_MAX - left->size_bytes)) ||
        (left->dst_offset_bytes > (SIZE_MAX - left->size_bytes))) {
        return 0;
    }
    const size_t left_src_end = left->src_offset_bytes + left->size_bytes;
    const size_t left_dst_end = left->dst_offset_bytes + left->size_bytes;
    if ((left_src_end != right->src_offset_bytes) ||
        (left_dst_end != right->dst_offset_bytes)) {
        return 0;
    }

    int same_delta = 0;
    if ((left->dst_offset_bytes >= left->src_offset_bytes) &&
        (right->dst_offset_bytes >= right->src_offset_bytes)) {
        same_delta = ((left->dst_offset_bytes - left->src_offset_bytes) ==
                      (right->dst_offset_bytes - right->src_offset_bytes));
    } else if ((left->dst_offset_bytes <= left->src_offset_bytes) &&
               (right->dst_offset_bytes <= right->src_offset_bytes)) {
        same_delta = ((left->src_offset_bytes - left->dst_offset_bytes) ==
                      (right->src_offset_bytes - right->dst_offset_bytes));
    }
    return same_delta;
}

db_gl_upload_range_t db_gl_upload_full_range(size_t total_bytes) {
    return (db_gl_upload_range_t){
        .dst_offset_bytes = 0U,
        .src_offset_bytes = 0U,
        .size_bytes = total_bytes,
    };
}

size_t db_gl_collect_row_upload_ranges(
    uint32_t row_unit_width, uint32_t row_count_total, size_t unit_stride_bytes,
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_dirty_row_range_t *out_rows, db_gl_upload_range_t *out_ranges,
    size_t out_capacity) {
    if ((row_unit_width == 0U) || (row_count_total == 0U) ||
        (unit_stride_bytes == 0U) || (out_ranges == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }

    if (unit_stride_bytes > (SIZE_MAX / (size_t)row_unit_width)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)row_unit_width * unit_stride_bytes;
    if (row_bytes > (SIZE_MAX / (size_t)row_count_total)) {
        return 0U;
    }

    if (dirty_count == 0U) {
        return 0U;
    }
    DB_LOG_CAPACITY_EXCEEDED_ONCE("renderer_gl_common",
                                  "collect_row_upload_ranges", dirty_count,
                                  out_capacity);

    size_t span_count = 0U;
    int has_pending = 0;
    db_gl_upload_range_t pending_range = {0U, 0U, 0U};
    db_dirty_row_range_t pending_rows = {0U, 0U};
    for (size_t i = 0U; (i < dirty_count) && (span_count < out_capacity); i++) {
        const uint32_t row_start = dirty_ranges[i].row_start;
        const uint32_t row_count = dirty_ranges[i].row_count;
        if ((row_start >= row_count_total) || (row_count == 0U)) {
            continue;
        }
        const uint32_t clamped_count =
            db_u32_min(row_count, row_count_total - row_start);
        if (clamped_count == 0U) {
            continue;
        }
        const size_t byte_offset = row_bytes * (size_t)row_start;
        const size_t byte_size = row_bytes * (size_t)clamped_count;
        const db_gl_upload_range_t candidate = (db_gl_upload_range_t){
            .dst_offset_bytes = byte_offset,
            .src_offset_bytes = byte_offset,
            .size_bytes = byte_size,
        };
        const db_dirty_row_range_t candidate_rows = (db_dirty_row_range_t){
            .row_start = row_start, .row_count = clamped_count};

        if (has_pending == 0) {
            pending_range = candidate;
            pending_rows = candidate_rows;
            has_pending = 1;
            continue;
        }
        if (db_gl_upload_ranges_can_merge(&pending_range, &candidate) != 0) {
            pending_range.size_bytes += candidate.size_bytes;
            pending_rows.row_count += candidate_rows.row_count;
            continue;
        }

        if (out_rows != NULL) {
            out_rows[span_count] = pending_rows;
        }
        out_ranges[span_count] = pending_range;
        span_count++;

        pending_range = candidate;
        pending_rows = candidate_rows;
        has_pending = 1;
    }

    if ((has_pending != 0) && (span_count < out_capacity)) {
        if (out_rows != NULL) {
            out_rows[span_count] = pending_rows;
        }
        out_ranges[span_count] = pending_range;
        span_count++;
    }
    return span_count;
}

size_t db_gl_collect_span_upload_ranges(
    uint32_t row_unit_width, size_t dst_unit_stride_bytes,
    size_t src_unit_stride_bytes, const db_snake_col_span_t *spans,
    size_t span_count, db_gl_upload_range_t *out_ranges, size_t out_capacity) {
    if ((row_unit_width == 0U) || (dst_unit_stride_bytes == 0U) ||
        (src_unit_stride_bytes == 0U) || (spans == NULL) ||
        (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }

    size_t upload_count = 0U;
    DB_LOG_CAPACITY_EXCEEDED_ONCE("renderer_gl_common",
                                  "collect_span_upload_ranges", span_count,
                                  out_capacity);
    int has_pending = 0;
    db_gl_upload_range_t pending_range = {0U, 0U, 0U};
    for (size_t i = 0U; i < span_count; i++) {
        const uint32_t row = spans[i].row;
        const uint32_t col_start = spans[i].col_start;
        const uint32_t col_end = spans[i].col_end;
        if ((col_end <= col_start) || (col_end > row_unit_width)) {
            continue;
        }
        const uint32_t span_units = col_end - col_start;
        const uint32_t first_unit = (row * row_unit_width) + col_start;
        const size_t dst_offset = (size_t)first_unit * dst_unit_stride_bytes;
        const size_t src_offset = (size_t)first_unit * src_unit_stride_bytes;
        const size_t span_bytes = (size_t)span_units * dst_unit_stride_bytes;
        const db_gl_upload_range_t candidate = (db_gl_upload_range_t){
            .dst_offset_bytes = dst_offset,
            .src_offset_bytes = src_offset,
            .size_bytes = span_bytes,
        };
        if (has_pending == 0) {
            pending_range = candidate;
            has_pending = 1;
            continue;
        }
        if (db_gl_upload_ranges_can_merge(&pending_range, &candidate) != 0) {
            pending_range.size_bytes += candidate.size_bytes;
            continue;
        }
        if (upload_count >= out_capacity) {
            break;
        }
        out_ranges[upload_count++] = pending_range;
        pending_range = candidate;
        has_pending = 1;
    }

    if ((has_pending != 0) && (upload_count < out_capacity)) {
        out_ranges[upload_count++] = pending_range;
    }
    return upload_count;
}

size_t
db_gl_collect_damage_upload_ranges(const db_gl_damage_upload_plan_t *plan,
                                   db_gl_upload_range_t *out_ranges,
                                   size_t out_capacity) {
    if ((plan == NULL) || (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }
    if (plan->force_full_upload != 0) {
        if (plan->total_bytes == 0U) {
            return 0U;
        }
        out_ranges[0] = db_gl_upload_full_range(plan->total_bytes);
        return 1U;
    }
    if ((plan->spans != NULL) && (plan->span_count > 0U)) {
        return db_gl_collect_span_upload_ranges(
            plan->row_unit_width, plan->unit_stride_bytes,
            plan->unit_stride_bytes, plan->spans, plan->span_count, out_ranges,
            out_capacity);
    }
    if ((plan->dirty_rows == NULL) || (plan->dirty_row_count == 0U)) {
        return 0U;
    }
    return db_gl_collect_row_upload_ranges(
        plan->row_unit_width, plan->row_count_total, plan->unit_stride_bytes,
        plan->dirty_rows, plan->dirty_row_count, NULL, out_ranges,
        out_capacity);
}

size_t
db_gl_collect_pattern_upload_ranges(const db_gl_pattern_upload_collect_t *ctx,
                                    db_gl_upload_range_t *out_ranges,
                                    size_t out_capacity) {
    if ((ctx == NULL) || (out_ranges == NULL) || (out_capacity == 0U)) {
        return 0U;
    }

    db_gl_damage_upload_plan_t upload_plan = {
        .row_unit_width = ctx->cols,
        .row_count_total = ctx->rows,
        .unit_stride_bytes = ctx->upload_tile_bytes,
        .total_bytes = ctx->upload_bytes,
        .force_full_upload = 0,
        .dirty_rows = NULL,
        .dirty_row_count = 0U,
        .spans = NULL,
        .span_count = 0U,
    };

    if (ctx->use_damage_row_ranges != 0) {
        upload_plan.dirty_rows = ctx->damage_row_ranges;
        upload_plan.dirty_row_count = ctx->damage_row_count;
        upload_plan.force_full_upload = ctx->force_full_upload;
        return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                  out_capacity);
    }

    if ((ctx->pattern == DB_PATTERN_SNAKE_GRID) ||
        (ctx->pattern == DB_PATTERN_SNAKE_RECT) ||
        (ctx->pattern == DB_PATTERN_SNAKE_SHAPES)) {
        const int is_grid = (ctx->pattern == DB_PATTERN_SNAKE_GRID);
        const db_snake_plan_t empty_plan = {0};
        const db_snake_plan_t *plan =
            (ctx->snake_plan != NULL) ? ctx->snake_plan : &empty_plan;
        if (ctx->force_full_upload != 0) {
            upload_plan.force_full_upload = 1;
            return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                      1U);
        }
        const db_snake_region_t region =
            (is_grid != 0)
                ? (db_snake_region_t){
                      .x = 0U,
                      .y = 0U,
                      .width = ctx->cols,
                      .height = ctx->rows,
                      .color_r = 0.0,
                      .color_g = 0.0,
                      .color_b = 0.0,
                  }
                : db_snake_region_from_index(ctx->pattern_seed,
                                             plan->active_shape_index);
        if ((region.width == 0U) || (region.height == 0U) ||
            (ctx->snake_scratch == NULL) ||
            (ctx->snake_scratch->spans == NULL)) {
            return 0U;
        }
        const uint32_t settled_count =
            (is_grid != 0) ? plan->prev_count : ctx->snake_prev_count;
        const size_t max_ranges =
            (size_t)settled_count + (size_t)plan->batch_size;
        if ((max_ranges == 0U) ||
            (max_ranges > ctx->snake_scratch->span_capacity)) {
            return 0U;
        }
        db_snake_shape_cache_t shape_cache = {0};
        const db_snake_shape_cache_t *shape_cache_ptr = NULL;
        if ((ctx->pattern == DB_PATTERN_SNAKE_SHAPES) &&
            (ctx->snake_scratch->row_bounds != NULL)) {
            const db_snake_shape_kind_t shape_kind =
                db_snake_shapes_kind_from_index(ctx->pattern_seed,
                                                plan->active_shape_index,
                                                DB_U32_SALT_PALETTE);
            if (db_snake_shape_cache_init_from_index(
                    &shape_cache, ctx->snake_scratch->row_bounds,
                    ctx->snake_scratch->row_bounds_capacity, ctx->pattern_seed,
                    plan->active_shape_index, DB_U32_SALT_PALETTE, &region,
                    shape_kind) != 0) {
                shape_cache_ptr = &shape_cache;
            }
        }
        const size_t span_count = db_snake_collect_damage_spans(
            ctx->snake_scratch->spans, max_ranges, &region,
            (is_grid != 0) ? plan->prev_start : ctx->snake_prev_start,
            settled_count, plan->active_cursor, plan->batch_size,
            shape_cache_ptr);
        upload_plan.spans = ctx->snake_scratch->spans;
        upload_plan.span_count = span_count;
        return db_gl_collect_damage_upload_ranges(
            &upload_plan, out_ranges,
            (max_ranges < out_capacity) ? max_ranges : out_capacity);
    }

    if ((ctx->pattern == DB_PATTERN_GRADIENT_SWEEP) ||
        (ctx->pattern == DB_PATTERN_GRADIENT_FILL)) {
        upload_plan.dirty_rows = ctx->damage_row_ranges;
        upload_plan.dirty_row_count = ctx->damage_row_count;
        upload_plan.force_full_upload = ctx->force_full_upload;
        return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges,
                                                  out_capacity);
    }

    upload_plan.force_full_upload = 1;
    return db_gl_collect_damage_upload_ranges(&upload_plan, out_ranges, 1U);
}

size_t db_gl_for_each_upload_row_span(const char *backend_name,
                                      uint32_t row_unit_width,
                                      const db_gl_upload_range_t *ranges,
                                      size_t range_count,
                                      db_gl_upload_row_span_apply_fn_t apply_fn,
                                      void *user_data) {
    if ((backend_name == NULL) || (row_unit_width == 0U) || (ranges == NULL) ||
        (range_count == 0U) || (apply_fn == NULL)) {
        return 0U;
    }
    const size_t row_bytes = (size_t)db_checked_mul_u32(
        backend_name, "upload_row_bytes", row_unit_width, 4U);
    if (row_bytes == 0U) {
        return 0U;
    }

    size_t applied_count = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->size_bytes % row_bytes) != 0U) ||
            ((range->dst_offset_bytes % row_bytes) != 0U)) {
            continue;
        }
        const db_gl_upload_row_span_t span = {
            .range = *range,
            .rows =
                (db_dirty_row_range_t){
                    .row_start = db_checked_size_to_u32(
                        backend_name, "upload_row_start",
                        range->dst_offset_bytes / row_bytes),
                    .row_count =
                        db_checked_size_to_u32(backend_name, "upload_row_count",
                                               range->size_bytes / row_bytes),
                },
        };
        apply_fn(&span, user_data);
        applied_count++;
    }
    return applied_count;
}

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

void db_gl_compact_vbo_free(db_gl_compact_vbo_state_t *compact) {
    if (compact == NULL) {
        return;
    }
    free(compact->scratch_vertices);
    *compact = (db_gl_compact_vbo_state_t){0};
}

int db_gl_compact_copy_ranges_from_vertices(
    const db_gl_upload_range_t *ranges, size_t range_count,
    const float *source_vertices, size_t upload_bytes, size_t vertex_stride,
    db_gl_compact_vbo_state_t *compact, size_t *out_compact_bytes) {
    if ((ranges == NULL) || (range_count == 0U) || (source_vertices == NULL) ||
        (vertex_stride == 0U) || (compact == NULL) ||
        (out_compact_bytes == NULL) || (compact->scratch_vertices == NULL)) {
        return 0;
    }

    const size_t bytes_per_vertex = vertex_stride * sizeof(float);
    const size_t bytes_per_tile =
        (size_t)DB_RECT_VERTEX_COUNT * bytes_per_vertex;
    if ((bytes_per_vertex == 0U) || (bytes_per_tile == 0U)) {
        return 0;
    }
    size_t compact_bytes = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % bytes_per_tile) != 0U) ||
            ((range->size_bytes % bytes_per_tile) != 0U)) {
            continue;
        }
        if ((range->src_offset_bytes > upload_bytes) ||
            (range->size_bytes > (upload_bytes - range->src_offset_bytes))) {
            return 0;
        }
        if (compact_bytes > (SIZE_MAX - range->size_bytes)) {
            return 0;
        }
        compact_bytes += range->size_bytes;
    }
    if ((compact_bytes == 0U) ||
        (compact_bytes > compact->vbo_capacity_bytes) ||
        ((compact_bytes / sizeof(float)) > compact->scratch_float_capacity)) {
        return 0;
    }

    uint8_t *const compact_dst = (uint8_t *)compact->scratch_vertices;
    const uint8_t *const source_bytes = (const uint8_t *)source_vertices;
    size_t compact_cursor = 0U;
    for (size_t i = 0U; i < range_count; i++) {
        const db_gl_upload_range_t *range = &ranges[i];
        if ((range->size_bytes == 0U) ||
            ((range->src_offset_bytes % bytes_per_tile) != 0U) ||
            ((range->size_bytes % bytes_per_tile) != 0U)) {
            continue;
        }
        db_copy_bytes(compact_dst + compact_cursor,
                      source_bytes + range->src_offset_bytes,
                      range->size_bytes);
        compact_cursor += range->size_bytes;
    }

    *out_compact_bytes = compact_bytes;
    return 1;
}

void db_gl_draw_dirty_ranges_common(const char *backend_name,
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

size_t db_gl_copy_upload_ranges(const db_gl_upload_range_t *source_ranges,
                                size_t source_count,
                                db_gl_upload_range_t *out_ranges,
                                size_t out_capacity) {
    if ((source_ranges == NULL) || (out_ranges == NULL) ||
        (out_capacity == 0U)) {
        return 0U;
    }
    DB_LOG_CAPACITY_EXCEEDED_ONCE("renderer_gl_common", "copy_upload_ranges",
                                  source_count, out_capacity);
    size_t out_count = 0U;
    const size_t copy_limit =
        (source_count < out_capacity) ? source_count : out_capacity;
    size_t index = 0U;
    while ((index < copy_limit) && (source_ranges[index].size_bytes != 0U)) {
        index++;
    }
    if (index == copy_limit) {
        db_copy_bytes(out_ranges, source_ranges,
                      copy_limit * sizeof(*source_ranges));
        return copy_limit;
    }
    if (index > 0U) {
        db_copy_bytes(out_ranges, source_ranges,
                      index * sizeof(*source_ranges));
        out_count = index;
    }
    for (; (index < source_count) && (out_count < out_capacity); index++) {
        if (source_ranges[index].size_bytes == 0U) {
            continue;
        }
        out_ranges[out_count++] = source_ranges[index];
    }
    return out_count;
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
        db_set_rect_unit_rgb(unit, vertex_stride,
                             DB_VERTEX_POSITION_FLOAT_COUNT,
                             BENCH_GRID_PHASE0_R_F, BENCH_GRID_PHASE0_G_F,
                             BENCH_GRID_PHASE0_B_F);
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
        double color_r_value = 0.0;
        double color_g_value = 0.0;
        double color_b_value = 0.0;
        db_band_color_rgb(band, band_count, frame_index, &color_r_value,
                          &color_g_value, &color_b_value);
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_rgb_f64_to_f32_triplet(color_r_value, color_g_value, color_b_value,
                                  &color_r, &color_g, &color_b);

        for (uint32_t row = 0U; row < rows; row++) {
            const uint32_t first_tile = (row * cols) + col_start;
            db_set_rect_tile_range_rgb(verts, first_tile, col_end - col_start,
                                       stride_floats, color_offset_floats,
                                       color_r, color_g, color_b);
        }
    }
}
