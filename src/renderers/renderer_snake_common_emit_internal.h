#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_EMIT_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_EMIT_INTERNAL_H

#include "renderer_snake_collect.h"

static inline db_snake_plan_t db_snake_plan_next_step_for_region(
    const db_snake_region_t *region, uint32_t active_shape_index,
    uint32_t active_cursor, uint32_t prev_start, uint32_t prev_count,
    int phase_flag, uint32_t cursor_step, int toggle_clearing_on_complete,
    int advance_shape_index_on_complete) {
    db_snake_plan_t plan = {0};
    plan.active_shape_index = active_shape_index;
    plan.active_cursor = active_cursor;
    plan.prev_start = prev_start;
    plan.prev_count = prev_count;
    plan.phase_flag = phase_flag;
    if (region == NULL) {
        plan.next_cursor = active_cursor;
        plan.next_prev_start = prev_start;
        plan.next_prev_count = prev_count;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = active_shape_index;
        return plan;
    }
    const uint32_t target_tile_count =
        db_checked_mul_u32(DB_SNAKE_COMMON_BACKEND, "snake_target_tile_count",
                           region->width, region->height);
    plan.target_tile_count = target_tile_count;
    if (target_tile_count == 0U) {
        plan.next_cursor = active_cursor;
        plan.next_prev_start = prev_start;
        plan.next_prev_count = prev_count;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = active_shape_index;
        return plan;
    }

    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(target_tile_count);
    const uint32_t cursor_step_effective = db_u32_max(cursor_step, 1U);

    if (plan.active_cursor == DB_SNAKE_CURSOR_PRE_ENTRY) {
        plan.active_cursor = 0U;
        plan.batch_size = 0U;
        plan.phase_completed = 0;
        plan.next_cursor = 0U;
        plan.next_prev_start = 0U;
        plan.next_prev_count = 0U;
        plan.next_phase_flag = phase_flag;
        plan.next_shape_index = plan.active_shape_index;
        plan.wrapped = 0;
        return plan;
    }

    plan.batch_size = tiles_per_step;
    plan.phase_completed = (plan.active_cursor >= target_tile_count) ? 1 : 0;
    plan.next_shape_index = plan.active_shape_index;
    plan.wrapped = 0;
    plan.next_prev_start = plan.active_cursor;
    uint32_t advanced_count = 0U;
    plan.next_phase_flag = phase_flag;
    if (plan.phase_completed != 0) {
        plan.next_cursor = DB_SNAKE_CURSOR_PRE_ENTRY;
        if (toggle_clearing_on_complete != 0) {
            plan.next_phase_flag = !phase_flag;
        }
        if (advance_shape_index_on_complete != 0) {
            plan.next_shape_index = plan.active_shape_index + 1U;
            if (plan.next_shape_index < plan.active_shape_index) {
                plan.wrapped = 1;
            }
        }
    } else {
        plan.next_cursor =
            db_checked_add_u32(DB_SNAKE_COMMON_BACKEND, "snake_next_cursor",
                               plan.active_cursor, cursor_step_effective);
        if (plan.next_cursor > target_tile_count) {
            plan.next_cursor = target_tile_count;
        }
        advanced_count =
            db_checked_sub_u32(DB_SNAKE_COMMON_BACKEND, "snake_advanced_count",
                               plan.next_cursor, plan.active_cursor);
    }
    plan.next_prev_count =
        plan.phase_completed ? 0U : db_u32_max(plan.batch_size, advanced_count);
    return plan;
}

static inline db_snake_plan_t
db_snake_plan_next_step(const db_snake_plan_request_t *request) {
    db_snake_plan_t plan = {0};
    if (request == NULL) {
        return plan;
    }

    if (request->is_grid_mode != 0) {
        const db_snake_region_t grid_region = {
            .x = 0U,
            .y = 0U,
            .width = db_snake_grid_cols_effective(),
            .height = db_snake_grid_rows_effective(),
            .color_rgb = {0.0, 0.0, 0.0},
        };
        return db_snake_plan_next_step_for_region(
            &grid_region, 0U, request->cursor, request->prev_start,
            request->prev_count, request->phase_flag, request->speed_step, 1,
            0);
    }

    const db_snake_region_t region =
        db_snake_region_from_index(request->seed, request->shape_index);
    return db_snake_plan_next_step_for_region(
        &region, request->shape_index, request->cursor, request->prev_start,
        request->prev_count, 0, request->speed_step, 0, 1);
}

static inline double db_window_blend_factor(uint32_t window_index,
                                            uint32_t window_size) {
    const uint32_t span = db_u32_max(window_size, 1U);
    if (span <= 1U) {
        return 1.0;
    }
    return ((double)((span - 1U) - window_index)) / (double)(span - 1U);
}

static inline db_snake_active_batch_t db_snake_bind_active_batch_scratch(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_active_tile_scratch_t *scratch,
    uint32_t *fallback_tile_indices, uint8_t *fallback_tile_valid,
    double *fallback_prior_rgb) {
    if ((plan == NULL) || (region == NULL) || (region->width == 0U) ||
        (region->height == 0U) || (fallback_tile_indices == NULL) ||
        (fallback_tile_valid == NULL) || (fallback_prior_rgb == NULL)) {
        return (db_snake_active_batch_t){0};
    }
    const uint32_t batch_limit =
        db_snake_plan_active_batch_limit(plan, BENCH_SNAKE_PHASE_WINDOW_TILES);
    db_snake_active_batch_t batch = {
        .active_tile_indices = fallback_tile_indices,
        .active_tile_valid = fallback_tile_valid,
        .active_prior_rgb = fallback_prior_rgb,
        .batch_limit = batch_limit,
    };
    if ((scratch != NULL) && (scratch->active_tile_indices != NULL) &&
        (scratch->active_tile_valid != NULL) &&
        (scratch->active_prior_rgb != NULL) &&
        (scratch->active_tile_capacity >= batch_limit)) {
        batch.active_tile_indices = scratch->active_tile_indices;
        batch.active_tile_valid = scratch->active_tile_valid;
        batch.active_prior_rgb = scratch->active_prior_rgb;
    }
    return batch;
}

static inline void db_snake_prepare_active_batch(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache_ptr, uint32_t cols, uint32_t rows,
    db_snake_active_batch_t *batch) {
    if ((plan == NULL) || (region == NULL) || (batch == NULL) || (cols == 0U) ||
        (rows == 0U) || (region->width == 0U) || (region->height == 0U) ||
        (batch->active_tile_indices == NULL) ||
        (batch->active_tile_valid == NULL)) {
        return;
    }
    for (uint32_t update_index = 0U; update_index < batch->batch_limit;
         update_index++) {
        batch->active_tile_valid[update_index] = 0U;
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_active_tile(plan, region, shape_cache_ptr,
                                              update_index, cols, rows,
                                              &tile) == 0) {
            continue;
        }
        batch->active_tile_indices[update_index] = tile.tile_index;
        batch->active_tile_valid[update_index] = 1U;
    }
}

static inline void db_snake_blend_active_rgb(const db_snake_plan_t *plan,
                                             uint32_t update_index,
                                             const double *prior_rgb,
                                             const double *target_rgb,
                                             double *out_rgb) {
    if ((plan == NULL) || (prior_rgb == NULL) || (target_rgb == NULL) ||
        (out_rgb == NULL)) {
        return;
    }
    const double blend_factor =
        db_window_blend_factor(update_index, plan->batch_size);
    db_blend_rgb3(prior_rgb, target_rgb, blend_factor, out_rgb);
}

static inline int db_snake_rgb_sink_read_tile(const db_snake_rgb_sink_t *sink,
                                              uint32_t tile_index,
                                              double *out_rgb) {
    if ((sink == NULL) || (out_rgb == NULL)) {
        return 0;
    }
    if (sink->kind == DB_SNAKE_RGB_SINK_TILE_RGB_F32) {
        if ((sink->tile_rgb_f32 == NULL) || (tile_index >= sink->tile_count)) {
            return 0;
        }
        db_rgb_f32_to_f64_rgb3(&sink->tile_rgb_f32[(size_t)tile_index *
                                                   DB_VERTEX_COLOR_FLOAT_COUNT],
                               out_rgb);
        return 1;
    }
    if ((sink->pixel_surface.pixel_width == 0U) ||
        (sink->pixel_surface.pixel_height == 0U)) {
        return 0;
    }
    if (sink->kind == DB_SNAKE_RGB_SINK_PIXEL_SURFACE_DIRECT) {
        db_rgb_pixels_read_index_f64(sink->pixel_surface.pixels_rgba8,
                                     sink->pixel_surface.pixels_rgba16f,
                                     sink->pixel_surface.uses_rgba16f,
                                     (size_t)tile_index, out_rgb);
        return 1;
    }
    db_damage_block_t pixel_block = {0U, 0U, 0U, 0U};
    if (db_grid_tile_to_pixel_block(sink->logical_cols, sink->logical_rows,
                                    tile_index, sink->pixel_surface.pixel_width,
                                    sink->pixel_surface.pixel_height,
                                    &pixel_block) == 0) {
        return 0;
    }
    db_rgb_pixels_read_index_f64(sink->pixel_surface.pixels_rgba8,
                                 sink->pixel_surface.pixels_rgba16f,
                                 sink->pixel_surface.uses_rgba16f,
                                 ((size_t)pixel_block.row_start *
                                  (size_t)sink->pixel_surface.pixel_width) +
                                     (size_t)pixel_block.col_start,
                                 out_rgb);
    return 1;
}

static inline void db_snake_rgb_sink_write_tile(const db_snake_rgb_sink_t *sink,
                                                uint32_t tile_index,
                                                const double *rgb) {
    if ((sink == NULL) || (rgb == NULL)) {
        return;
    }
    if (sink->kind == DB_SNAKE_RGB_SINK_TILE_RGB_F32) {
        if ((sink->tile_rgb_f32 == NULL) || (tile_index >= sink->tile_count)) {
            return;
        }
        float rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(rgb, rgb_f32);
        db_copy_f32_rgb3(&sink->tile_rgb_f32[(size_t)tile_index *
                                             DB_VERTEX_COLOR_FLOAT_COUNT],
                         rgb_f32);
        return;
    }
    if ((sink->pixel_surface.pixel_width == 0U) ||
        (sink->pixel_surface.pixel_height == 0U)) {
        return;
    }
    if (sink->kind == DB_SNAKE_RGB_SINK_PIXEL_SURFACE_DIRECT) {
        db_rgb_pixels_write_index_f64(sink->pixel_surface.pixels_rgba8,
                                      sink->pixel_surface.pixels_rgba16f,
                                      sink->pixel_surface.uses_rgba16f,
                                      (size_t)tile_index, rgb);
        if (sink->mirror_pixel_surface_enabled != 0) {
            db_rgb_pixels_write_index_f64(
                sink->mirror_pixel_surface.pixels_rgba8,
                sink->mirror_pixel_surface.pixels_rgba16f,
                sink->mirror_pixel_surface.uses_rgba16f, (size_t)tile_index,
                rgb);
        }
        return;
    }
    db_damage_block_t pixel_block = {0U, 0U, 0U, 0U};
    if (db_grid_tile_to_pixel_block(sink->logical_cols, sink->logical_rows,
                                    tile_index, sink->pixel_surface.pixel_width,
                                    sink->pixel_surface.pixel_height,
                                    &pixel_block) == 0) {
        return;
    }
    db_rgb_pixels_fill_damage_block_f64(
        sink->pixel_surface.pixel_width, sink->pixel_surface.pixel_height,
        sink->pixel_surface.pixels_rgba8, sink->pixel_surface.pixels_rgba16f,
        sink->pixel_surface.uses_rgba16f, pixel_block.row_start,
        pixel_block.row_count, pixel_block.col_start, pixel_block.col_count,
        rgb);
    if (sink->mirror_pixel_surface_enabled != 0) {
        db_rgb_pixels_fill_damage_block_f64(
            sink->mirror_pixel_surface.pixel_width,
            sink->mirror_pixel_surface.pixel_height,
            sink->mirror_pixel_surface.pixels_rgba8,
            sink->mirror_pixel_surface.pixels_rgba16f,
            sink->mirror_pixel_surface.uses_rgba16f, pixel_block.row_start,
            pixel_block.row_count, pixel_block.col_start, pixel_block.col_count,
            rgb);
    }
}

static inline void db_snake_rgb_sink_fill_all(const db_snake_rgb_sink_t *sink,
                                              const double *rgb) {
    if ((sink == NULL) || (rgb == NULL)) {
        return;
    }
    if (sink->kind == DB_SNAKE_RGB_SINK_TILE_RGB_F32) {
        if (sink->tile_rgb_f32 == NULL) {
            return;
        }
        float rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(rgb, rgb_f32);
        for (uint32_t tile_index = 0U; tile_index < sink->tile_count;
             tile_index++) {
            db_copy_f32_rgb3(&sink->tile_rgb_f32[(size_t)tile_index *
                                                 DB_VERTEX_COLOR_FLOAT_COUNT],
                             rgb_f32);
        }
        return;
    }
    db_rgb_pixels_fill_solid_f64(
        sink->pixel_surface.pixel_width, sink->pixel_surface.pixel_height,
        sink->pixel_surface.pixels_rgba8, sink->pixel_surface.pixels_rgba16f,
        sink->pixel_surface.uses_rgba16f, rgb);
    if (sink->mirror_pixel_surface_enabled != 0) {
        db_rgb_pixels_fill_solid_f64(sink->mirror_pixel_surface.pixel_width,
                                     sink->mirror_pixel_surface.pixel_height,
                                     sink->mirror_pixel_surface.pixels_rgba8,
                                     sink->mirror_pixel_surface.pixels_rgba16f,
                                     sink->mirror_pixel_surface.uses_rgba16f,
                                     rgb);
    }
}

static inline void db_snake_emit_step_rgb(
    const db_snake_plan_t *plan, const db_snake_region_t *region,
    const db_snake_shape_cache_t *shape_cache_ptr, const double *target_rgb,
    int force_full_fill_on_phase_complete,
    const db_snake_active_tile_scratch_t *scratch,
    const db_snake_rgb_sink_t *sink) {
    if ((plan == NULL) || (region == NULL) || (target_rgb == NULL) ||
        (sink == NULL) || (sink->logical_cols == 0U) ||
        (sink->logical_rows == 0U) || (region->width == 0U) ||
        (region->height == 0U)) {
        return;
    }
    if ((force_full_fill_on_phase_complete != 0) &&
        (plan->phase_completed != 0)) {
        db_snake_rgb_sink_fill_all(sink, target_rgb);
        return;
    }
    const uint32_t logical_cols = sink->logical_cols;
    const uint32_t logical_rows = sink->logical_rows;
    uint32_t active_tile_indices_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    uint8_t active_tile_valid_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    double prior_rgb_local[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0};
    db_snake_active_batch_t batch = db_snake_bind_active_batch_scratch(
        plan, region, scratch, active_tile_indices_local,
        active_tile_valid_local, prior_rgb_local);
    db_snake_prepare_active_batch(plan, region, shape_cache_ptr, logical_cols,
                                  logical_rows, &batch);
    const uint32_t batch_limit = batch.batch_limit;
    uint32_t *const active_tile_indices = batch.active_tile_indices;
    double *const active_prior_rgb = batch.active_prior_rgb;
    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        if (batch.active_tile_valid[update_index] == 0U) {
            continue;
        }
        if (db_snake_rgb_sink_read_tile(
                sink, active_tile_indices[update_index],
                &active_prior_rgb[(size_t)update_index * 3U]) == 0) {
            batch.active_tile_valid[update_index] = 0U;
        }
    }
    const uint32_t prev_count = plan->prev_count;
    for (uint32_t prev_offset = 0U; prev_offset < prev_count; prev_offset++) {
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_prev_tile(plan, region, shape_cache_ptr,
                                            prev_offset, logical_cols,
                                            logical_rows, &tile) == 0) {
            continue;
        }
        db_snake_rgb_sink_write_tile(sink, tile.tile_index, target_rgb);
    }
    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        if (batch.active_tile_valid[update_index] == 0U) {
            continue;
        }
        double out_rgb[3] = {0.0, 0.0, 0.0};
        const double *const prior_rgb =
            &active_prior_rgb[(size_t)update_index * 3U];
        db_snake_blend_active_rgb(plan, update_index, prior_rgb, target_rgb,
                                  out_rgb);
        db_snake_rgb_sink_write_tile(sink, active_tile_indices[update_index],
                                     out_rgb);
    }
}

static inline void db_grid_target_color_rgb3(int phase_flag, double *out_rgb) {
    if (out_rgb == NULL) {
        return;
    }
    static const double phase0_rgb[3] = {
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B};
    static const double phase1_rgb[3] = {
        BENCH_GRID_PHASE1_R, BENCH_GRID_PHASE1_G, BENCH_GRID_PHASE1_B};
    if (phase_flag != 0) {
        db_copy_f64_rgb3(out_rgb, phase0_rgb);
        return;
    }
    db_copy_f64_rgb3(out_rgb, phase1_rgb);
}

static inline db_snake_step_target_t
db_snake_step_target_from_plan(int is_grid_mode, uint32_t pattern_seed,
                               const db_snake_plan_t *plan) {
    db_snake_step_target_t result = {0};
    if (plan == NULL) {
        return result;
    }
    if (is_grid_mode != 0) {
        result.region = (db_snake_region_t){
            .x = 0U,
            .y = 0U,
            .width = db_snake_grid_cols_effective(),
            .height = db_snake_grid_rows_effective(),
            .color_rgb = {0.0, 0.0, 0.0},
        };
        double target_rgb[3] = {0.0, 0.0, 0.0};
        db_grid_target_color_rgb3(plan->phase_flag, target_rgb);
        db_copy_f64_rgb3(result.target_rgb, target_rgb);
        result.force_full_fill_on_phase_complete = 1;
        result.has_next_phase_flag = 1;
        result.next_phase_flag = plan->next_phase_flag;
        return result;
    }

    result.region =
        db_snake_region_from_index(pattern_seed, plan->active_shape_index);
    db_copy_f64_rgb3(result.target_rgb, result.region.color_rgb);
    result.shape_kind = db_snake_shapes_kind_from_index(
        pattern_seed, plan->active_shape_index, DB_U32_SALT_PALETTE);
    result.has_next_shape_index = 1;
    result.next_shape_index = plan->next_shape_index;
    return result;
}

static inline db_snake_step_eval_t
db_snake_step_eval_from_runtime(db_pattern_t pattern, uint32_t pattern_seed,
                                uint32_t shape_index, uint32_t cursor,
                                uint32_t prev_start, uint32_t prev_count,
                                int phase_flag, uint32_t bench_speed_step) {
    db_snake_step_eval_t result = {0};
    result.is_grid_mode = (pattern == DB_PATTERN_SNAKE_GRID);
    result.is_shapes_mode = (pattern == DB_PATTERN_SNAKE_SHAPES);
    const db_snake_plan_request_t request = db_snake_plan_request_make(
        result.is_grid_mode, pattern_seed, shape_index, cursor, prev_start,
        prev_count, phase_flag, bench_speed_step);
    result.plan = db_snake_plan_next_step(&request);
    result.target = db_snake_step_target_from_plan(result.is_grid_mode,
                                                   pattern_seed, &result.plan);
    result.shape_kind = (result.is_shapes_mode != 0) ? result.target.shape_kind
                                                     : DB_SNAKE_SHAPE_RECT;
    return result;
}

#endif
