#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_geometry.h"
#include "../renderer_benchmark_gradient.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_collect.h"
#include "../renderer_snake_emit.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_snake_types.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define DB_CAP_MODE_CPU_OFFSCREEN_BO "cpu_offscreen_bo"
#define DB_CAP_MODE_CPU_OFFSCREEN_BO_HDR "cpu_offscreen_bo_hdr_rgba16f"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels_rgba8;
    uint16_t *pixels_rgba16f;
    int is_hdr_float_bo;
} db_cpu_bo_t;

typedef struct {
    db_cpu_bo_t bo;
    db_damage_block_t *damage_blocks;
    size_t damage_block_capacity;
    size_t damage_block_count;
    db_history_snake_scratch_t snake_scratch;
    db_renderer_frame_stats_t frame;
    int initialized;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;
} db_cpu_renderer_state_t;

static db_cpu_renderer_state_t g_state = {0};

static void db_cpu_bo_fill_damage_block_rgb(db_cpu_bo_t *bo, uint32_t row_start,
                                            uint32_t row_count,
                                            uint32_t col_start,
                                            uint32_t col_count,
                                            const double *rgb) {
    if ((bo == NULL) || (rgb == NULL) || (row_start >= bo->height) ||
        (row_count == 0U) || (col_count == 0U)) {
        return;
    }
    if (col_start >= bo->width) {
        return;
    }
    db_rgb_pixels_fill_damage_block_f64(
        bo->width, bo->height, bo->pixels_rgba8, bo->pixels_rgba16f,
        bo->is_hdr_float_bo, row_start, row_count, col_start, col_count, rgb);
}

static void db_cpu_set_full_damage(const db_cpu_bo_t *bo) {
    if ((bo == NULL) || (g_state.damage_blocks == NULL) ||
        (g_state.damage_block_capacity == 0U) || (bo->height == 0U) ||
        (bo->width == 0U)) {
        g_state.damage_block_count = 0U;
        return;
    }
    g_state.damage_blocks[0] = (db_damage_block_t){
        .row_start = 0U,
        .row_count = bo->height,
        .col_start = 0U,
        .col_count = bo->width,
    };
    g_state.damage_block_count = 1U;
}

static void db_cpu_publish_grid_blocks(const db_grid_block_t *blocks,
                                       size_t block_count) {
    if ((blocks == NULL) || (block_count == 0U)) {
        g_state.damage_block_count = 0U;
        return;
    }
    if ((g_state.damage_blocks == NULL) ||
        (g_state.damage_block_capacity == 0U)) {
        g_state.damage_block_count = 0U;
        return;
    }
    g_state.damage_block_count = db_damage_blocks_from_grid_blocks_or_full(
        blocks, block_count, g_state.bo.height, g_state.bo.width,
        g_state.damage_blocks, g_state.damage_block_capacity);
}

static void
db_cpu_apply_gradient_damage_block(db_cpu_bo_t *bo,
                                   const db_grid_block_t *block,
                                   const db_gradient_state_t *state) {
    if ((bo == NULL) || (block == NULL) || (state == NULL) ||
        (block->row_count == 0U)) {
        return;
    }
    db_gradient_row_segment_iter_t iter = {0};
    if (db_gradient_row_segment_iter_init(block, state->head_row,
                                          state->direction_down,
                                          state->cycle_index, &iter) == 0) {
        return;
    }
    db_gradient_row_segment_t segment = {0};
    while (db_gradient_row_segment_iter_next(&iter, &segment) != 0) {
        db_cpu_bo_fill_damage_block_rgb(
            bo, segment.block.row_start, segment.block.row_count,
            segment.block.col_start, segment.block.col_count, segment.rgb);
    }
}

static void db_cpu_apply_gradient_damage_blocks(
    db_cpu_bo_t *bo, const db_gradient_damage_plan_t *plan,
    const db_grid_block_t *damage_blocks, size_t damage_block_count) {
    if ((bo == NULL) || (plan == NULL) || (damage_blocks == NULL) ||
        (damage_block_count == 0U)) {
        return;
    }
    for (size_t block_index = 0U; block_index < damage_block_count;
         block_index++) {
        const db_grid_block_t *const block = &damage_blocks[block_index];
        if ((block->row_count == 0U) || (block->col_count == 0U)) {
            continue;
        }
        db_cpu_apply_gradient_damage_block(bo, block, &plan->render_state);
    }
}

static void db_render_bands(db_cpu_bo_t *bo, uint32_t frame_index) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        double band_rgb[3] = {0.0, 0.0, 0.0};
        db_band_color_rgb3(band, BENCH_BANDS, frame_index, band_rgb);
        const uint32_t x0 = (band * cols) / BENCH_BANDS;
        const uint32_t x1 = ((band + 1U) * cols) / BENCH_BANDS;
        if ((x1 <= x0) || (x0 >= cols)) {
            continue;
        }
        const uint32_t x_end = db_u32_min(x1, cols);
        const uint32_t span_cols = x_end - x0;
        if (span_cols == 0U) {
            continue;
        }
        db_cpu_bo_fill_damage_block_rgb(bo, 0U, rows, x0, span_cols, band_rgb);
    }
}

static void db_render_snake_step(db_cpu_bo_t *bo, const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 const db_snake_shape_cache_t *shape_cache,
                                 const db_snake_active_tile_scratch_t *scratch,
                                 const double *target_rgb,
                                 int force_full_fill_on_phase_complete) {
    if ((bo == NULL) || (plan == NULL) || (region == NULL) ||
        (target_rgb == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }
    if ((force_full_fill_on_phase_complete != 0) &&
        (plan->phase_completed != 0)) {
        db_rgb_pixels_fill_solid_f64(bo->width, bo->height, bo->pixels_rgba8,
                                     bo->pixels_rgba16f, bo->is_hdr_float_bo,
                                     target_rgb);
        return;
    }
    const db_snake_rgb_sink_t sink = {
        .kind = DB_SNAKE_RGB_SINK_PIXEL_SURFACE_DIRECT,
        .logical_cols = bo->width,
        .logical_rows = bo->height,
        .pixel_surface =
            {
                .pixel_width = bo->width,
                .pixel_height = bo->height,
                .pixels_rgba8 = bo->pixels_rgba8,
                .pixels_rgba16f = bo->pixels_rgba16f,
                .uses_rgba16f = bo->is_hdr_float_bo,
            },
        .tile_rgb_f32 = NULL,
        .tile_count = 0U,
    };
    db_snake_emit_step_rgb(plan, region, shape_cache, target_rgb,
                           force_full_fill_on_phase_complete, scratch, &sink);
}

void db_renderer_cpu_renderer_init_with_hdr_float_bo(int use_hdr_float_bo) {
    if (g_state.initialized != 0) {
        return;
    }
    db_benchmark_runtime_init_t init_state = {0};
    if (!db_init_benchmark_runtime_common(BACKEND_NAME, &init_state)) {
        db_failf(BACKEND_NAME, "cpu renderer init failed");
    }

    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint64_t pixel_count = (uint64_t)grid_cols * (uint64_t)grid_rows;
    const uint64_t max_rgba8_pixel_count = SIZE_MAX / sizeof(uint32_t);
    if ((pixel_count == 0U) || (pixel_count > max_rgba8_pixel_count)) {
        db_failf(BACKEND_NAME, "invalid offscreen BO size: %ux%u", grid_cols,
                 grid_rows);
    }

    db_cpu_bo_t bo = {
        .width = grid_cols,
        .height = grid_rows,
        .pixels_rgba8 = NULL,
        .pixels_rgba16f = NULL,
        .is_hdr_float_bo = (use_hdr_float_bo != 0) ? 1 : 0,
    };
    if (bo.is_hdr_float_bo != 0) {
        bo.pixels_rgba16f = (uint16_t *)db_alloc_aligned_array_or_fail(
            BACKEND_NAME, "pixels_rgba16f",
            (size_t)pixel_count * DB_RGBA16F_CHANNELS_PER_PIXEL,
            sizeof(uint16_t), DB_CACHELINE_ALIGNMENT_BYTES);
    } else {
        bo.pixels_rgba8 = (uint32_t *)db_alloc_aligned_array_or_fail(
            BACKEND_NAME, "pixels_rgba8", (size_t)pixel_count, sizeof(uint32_t),
            DB_CACHELINE_ALIGNMENT_BYTES);
    }
    double seed_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(&init_state, seed_rgb);
    db_rgb_pixels_fill_solid_f64(bo.width, bo.height, bo.pixels_rgba8,
                                 bo.pixels_rgba16f, bo.is_hdr_float_bo,
                                 seed_rgb);
    db_snake_shape_row_bounds_t *snake_row_bounds = NULL;
    size_t snake_row_bounds_capacity = 0U;
    db_grid_block_t *snake_damage_blocks = NULL;
    size_t snake_compact_block_capacity = 0U;
    db_damage_block_t *damage_blocks = NULL;
    size_t damage_block_capacity = 4U;
    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(init_state.pattern);
    if (pattern_flags.is_snake_shapes != 0) {
        snake_row_bounds =
            (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_row_bounds", (size_t)grid_rows,
                sizeof(*snake_row_bounds));
        snake_row_bounds_capacity = (size_t)grid_rows;
    }
    if (pattern_flags.is_snake_history_texture != 0) {
        snake_compact_block_capacity =
            db_snake_scratch_capacity_from_work_units(
                init_state.work_unit_count);
        snake_damage_blocks = (db_grid_block_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "snake_damage_blocks", snake_compact_block_capacity,
            sizeof(*snake_damage_blocks));
        damage_block_capacity = snake_compact_block_capacity;
        damage_blocks = (db_damage_block_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "damage_blocks", damage_block_capacity,
            sizeof(*damage_blocks));
    } else {
        damage_blocks = (db_damage_block_t *)db_alloc_array_or_fail(
            BACKEND_NAME, "damage_blocks", damage_block_capacity,
            sizeof(*damage_blocks));
    }

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.runtime = init_state;
    g_state.runtime_flags = db_history_runtime_mode_flags(&g_state.runtime);
    g_state.bo = bo;
    g_state.runtime.snake.shape_index = 0U;
    g_state.snake_scratch.damage.blocks = snake_damage_blocks;
    g_state.snake_scratch.damage.capacity = snake_compact_block_capacity;
    g_state.snake_scratch.compact.blocks = NULL;
    g_state.snake_scratch.compact.capacity = 0U;
    g_state.snake_scratch.shape.row_bounds = snake_row_bounds;
    g_state.snake_scratch.shape.row_bounds_capacity = snake_row_bounds_capacity;
    g_state.damage_blocks = damage_blocks;
    g_state.damage_block_capacity = damage_block_capacity;
    db_history_snake_active_cache_init(&g_state.snake_scratch, BACKEND_NAME,
                                       BENCH_SNAKE_PHASE_WINDOW_TILES, 3U);
}

void db_renderer_cpu_renderer_render_frame(uint32_t frame_index) {
    if (g_state.initialized == 0) {
        return;
    }

    db_cpu_bo_t *write_bo = &g_state.bo;
    g_state.damage_block_count = 0U;
    if (g_state.runtime_flags.is_bands != 0) {
        db_render_bands(write_bo, frame_index);
        db_cpu_set_full_damage(write_bo);
    } else if (g_state.runtime_flags.is_snake_history_texture != 0) {
        const db_history_snake_step_eval_t eval =
            db_history_eval_snake_step_from_runtime(&g_state.runtime);
        const db_snake_plan_t plan = eval.plan;
        const db_snake_step_target_t target = eval.target;
        const db_snake_shape_kind_t shape_kind = eval.shape_kind;
        db_snake_shape_cache_t shape_cache = {0};
        const db_snake_shape_cache_t *shape_cache_ptr = NULL;
        if (eval.is_shapes_mode != 0) {
            if ((g_state.snake_scratch.shape.row_bounds != NULL) &&
                (g_state.snake_scratch.shape.row_bounds_capacity > 0U) &&
                (db_snake_shape_cache_init_from_index(
                     &shape_cache, g_state.snake_scratch.shape.row_bounds,
                     g_state.snake_scratch.shape.row_bounds_capacity,
                     g_state.runtime.pattern_seed, plan.active_shape_index,
                     DB_U32_SALT_PALETTE, &target.region, shape_kind) != 0)) {
                shape_cache_ptr = &shape_cache;
            }
        }
        const db_snake_active_tile_scratch_t scratch = {
            .active_tile_indices =
                g_state.snake_scratch.shape.active_tile_indices,
            .active_tile_valid = g_state.snake_scratch.shape.active_tile_valid,
            .active_prior_rgb = g_state.snake_scratch.shape.active_prior_rgb,
            .active_tile_capacity =
                g_state.snake_scratch.shape.active_tile_capacity,
        };
        db_render_snake_step(write_bo, &plan, &target.region, shape_cache_ptr,
                             &scratch, target.target_rgb,
                             target.force_full_fill_on_phase_complete);
        if ((target.force_full_fill_on_phase_complete != 0) &&
            (plan.phase_completed != 0)) {
            db_cpu_set_full_damage(write_bo);
        } else {
            const db_grid_block_t *blocks = NULL;
            size_t block_count = 0U;
            if ((g_state.snake_scratch.damage.blocks != NULL) &&
                (db_snake_collect_damage_blocks_for_plan(
                     &target.region, &plan, shape_cache_ptr,
                     g_state.snake_scratch.damage.blocks,
                     g_state.snake_scratch.damage.capacity,
                     &block_count) != 0)) {
                blocks = g_state.snake_scratch.damage.blocks;
                db_cpu_publish_grid_blocks(blocks, block_count);
            } else {
                DB_LOG_CAPACITY_EXCEEDED_ONCE(
                    BACKEND_NAME, "cpu_snake_damage_blocks",
                    db_snake_plan_upload_range_capacity_needed(&plan),
                    g_state.snake_scratch.damage.capacity);
                db_cpu_set_full_damage(write_bo);
            }
        }
        db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        const db_gradient_damage_plan_t plan =
            db_history_eval_gradient_step_from_runtime(&g_state.runtime);
        const int force_full_gradient_frame = (g_state.frame.frame_index == 0U);
        if (force_full_gradient_frame != 0) {
            const db_grid_block_t full_block =
                db_grid_block_full(write_bo->height, write_bo->width);
            db_cpu_apply_gradient_damage_block(write_bo, &full_block,
                                               &plan.render_state);
            db_cpu_set_full_damage(write_bo);
        } else {
            db_grid_block_t gradient_dirty_blocks[2U] = {
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
            };
            const size_t gradient_dirty_count =
                db_gradient_collect_dirty_blocks(
                    &plan, write_bo->height, write_bo->width,
                    gradient_dirty_blocks,
                    sizeof(gradient_dirty_blocks) /
                        sizeof(gradient_dirty_blocks[0]));
            if (gradient_dirty_count == 0U) {
                db_cpu_set_full_damage(write_bo);
                const db_grid_block_t full_block =
                    db_grid_block_full(write_bo->height, write_bo->width);
                db_cpu_apply_gradient_damage_block(write_bo, &full_block,
                                                   &plan.render_state);
            } else {
                db_cpu_apply_gradient_damage_blocks(write_bo, &plan,
                                                    gradient_dirty_blocks,
                                                    gradient_dirty_count);
                db_cpu_publish_grid_blocks(gradient_dirty_blocks,
                                           gradient_dirty_count);
            }
        }
        db_history_apply_gradient_step_to_runtime(&g_state.runtime, &plan);
    }

    db_history_finalize_frame(&g_state.frame, &g_state.runtime, write_bo->width,
                              write_bo->height);
}

const uint32_t *db_renderer_cpu_renderer_pixels_rgba8(uint32_t *out_width,
                                                      uint32_t *out_height) {
    if (g_state.initialized == 0) {
        return NULL;
    }
    if (out_width != NULL) {
        *out_width = g_state.bo.width;
    }
    if (out_height != NULL) {
        *out_height = g_state.bo.height;
    }
    if (g_state.bo.is_hdr_float_bo == 0) {
        return g_state.bo.pixels_rgba8;
    }
    return NULL;
}

uint32_t db_renderer_cpu_renderer_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, g_state.initialized);
}

const char *db_renderer_cpu_renderer_capability_mode(void) {
    if (g_state.bo.is_hdr_float_bo != 0) {
        return DB_CAP_MODE_CPU_OFFSCREEN_BO_HDR;
    }
    return DB_CAP_MODE_CPU_OFFSCREEN_BO;
}

int db_renderer_cpu_renderer_bo_uses_rgba16f(void) {
    return (g_state.initialized != 0) && (g_state.bo.is_hdr_float_bo != 0);
}

const uint16_t *db_renderer_cpu_renderer_pixels_rgba16f(uint32_t *out_width,
                                                        uint32_t *out_height) {
    if (g_state.initialized == 0) {
        return NULL;
    }
    if (out_width != NULL) {
        *out_width = g_state.bo.width;
    }
    if (out_height != NULL) {
        *out_height = g_state.bo.height;
    }
    if (g_state.bo.is_hdr_float_bo == 0) {
        return NULL;
    }
    return g_state.bo.pixels_rgba16f;
}

uint64_t db_renderer_cpu_renderer_state_hash(void) {
    return g_state.frame.state_hash;
}

const db_damage_block_t *
db_renderer_cpu_renderer_damage_blocks(size_t *out_count) {
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (g_state.initialized == 0) {
        return NULL;
    }
    if (out_count != NULL) {
        *out_count = g_state.damage_block_count;
    }
    return g_state.damage_blocks;
}

void db_renderer_cpu_renderer_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.damage_blocks);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    free(g_state.bo.pixels_rgba8);
    free(g_state.bo.pixels_rgba16f);
    g_state = (db_cpu_renderer_state_t){0};
}
