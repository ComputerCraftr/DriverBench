#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_alloc_policy.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../delta/renderer_frame_delta.h"
#include "../delta/renderer_frame_delta_producers.h"
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
#define DB_CAP_MODE_CPU_SURFACE "cpu_surface"
#define DB_CAP_MODE_CPU_SURFACE_HDR "cpu_surface_hdr_rgba16f"

typedef struct {
    db_damage_block_t *damage_blocks;
    size_t damage_block_capacity;
    size_t damage_block_count;
    uint32_t *replace_surface_rgba8;
    uint16_t *replace_surface_rgba16f;
    size_t replace_surface_pixel_capacity;
    int replace_surface_valid;
    db_history_snake_scratch_t snake_scratch;
    db_renderer_frame_stats_t frame;
    int initialized;
    int target_uses_rgba16f;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;
} db_cpu_renderer_state_t;

static db_cpu_renderer_state_t g_state = {0};

static uint32_t
db_cpu_surface_pixel_bytes(const db_benchmark_pixel_surface_t *surface) {
    if ((surface != NULL) && (surface->uses_rgba16f != 0)) {
        return (uint32_t)(sizeof(uint16_t) * DB_RGBA16F_CHANNELS_PER_PIXEL);
    }
    return 4U;
}

static int
db_cpu_surface_is_valid(const db_benchmark_pixel_surface_t *surface) {
    if ((surface == NULL) || (surface->pixel_width == 0U) ||
        (surface->pixel_height == 0U)) {
        return 0;
    }
    if (surface->uses_rgba16f != 0) {
        return (surface->pixels_rgba16f != NULL) ? 1 : 0;
    }
    return (surface->pixels_rgba8 != NULL) ? 1 : 0;
}

static void db_cpu_validate_render_surface_or_fail(
    const db_benchmark_pixel_surface_t *surface) {
    if (db_cpu_surface_is_valid(surface) == 0) {
        db_failf(BACKEND_NAME, "invalid cpu render target surface");
    }
    if (surface->pixel_width != db_grid_cols_effective()) {
        db_failf(BACKEND_NAME,
                 "cpu render surface width mismatch: got=%u expected=%u",
                 surface->pixel_width, db_grid_cols_effective());
    }
    if (surface->pixel_height != db_grid_rows_effective()) {
        db_failf(BACKEND_NAME,
                 "cpu render surface height mismatch: got=%u expected=%u",
                 surface->pixel_height, db_grid_rows_effective());
    }
    if (surface->uses_rgba16f != g_state.target_uses_rgba16f) {
        db_failf(
            BACKEND_NAME,
            "cpu render surface format mismatch: got_hdr=%d expected_hdr=%d",
            surface->uses_rgba16f, g_state.target_uses_rgba16f);
    }
}

static void db_cpu_surface_fill_damage_block_rgb(
    const db_benchmark_pixel_surface_t *surface, uint32_t row_start,
    uint32_t row_count, uint32_t col_start, uint32_t col_count,
    const double *rgb) {
    if ((surface == NULL) || (rgb == NULL) ||
        (row_start >= surface->pixel_height) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }
    if (col_start >= surface->pixel_width) {
        return;
    }
    db_rgb_pixels_fill_damage_block_f64(
        surface->pixel_width, surface->pixel_height, surface->pixels_rgba8,
        surface->pixels_rgba16f, surface->uses_rgba16f, row_start, row_count,
        col_start, col_count, rgb);
}

static void db_cpu_fill_surface_seed_background(
    const db_benchmark_pixel_surface_t *surface) {
    double seed_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(&g_state.runtime, seed_rgb);
    db_rgb_pixels_fill_solid_f64(surface->pixel_width, surface->pixel_height,
                                 surface->pixels_rgba8, surface->pixels_rgba16f,
                                 surface->uses_rgba16f, seed_rgb);
}

static db_benchmark_pixel_surface_t
db_cpu_replace_surface_or_fail(const db_benchmark_pixel_surface_t *surface) {
    if (surface == NULL) {
        db_failf(BACKEND_NAME, "missing replace surface");
    }
    const size_t pixel_capacity =
        (size_t)surface->pixel_width * (size_t)surface->pixel_height;
    if (pixel_capacity == 0U) {
        db_failf(BACKEND_NAME, "invalid replace surface capacity");
    }
    if (g_state.target_uses_rgba16f != 0) {
        db_reserve_array_capacity_or_fail(
            (void **)&g_state.replace_surface_rgba16f,
            &g_state.replace_surface_pixel_capacity,
            pixel_capacity * DB_RGBA16F_CHANNELS_PER_PIXEL,
            pixel_capacity * DB_RGBA16F_CHANNELS_PER_PIXEL, sizeof(uint16_t),
            0U, BACKEND_NAME, "replace_surface_rgba16f");
        return (db_benchmark_pixel_surface_t){
            .pixel_width = surface->pixel_width,
            .pixel_height = surface->pixel_height,
            .pixels_rgba8 = NULL,
            .pixels_rgba16f = g_state.replace_surface_rgba16f,
            .uses_rgba16f = 1,
        };
    }
    db_reserve_array_capacity_or_fail(
        (void **)&g_state.replace_surface_rgba8,
        &g_state.replace_surface_pixel_capacity, pixel_capacity, pixel_capacity,
        sizeof(uint32_t), 0U, BACKEND_NAME, "replace_surface_rgba8");
    return (db_benchmark_pixel_surface_t){
        .pixel_width = surface->pixel_width,
        .pixel_height = surface->pixel_height,
        .pixels_rgba8 = g_state.replace_surface_rgba8,
        .pixels_rgba16f = NULL,
        .uses_rgba16f = 0,
    };
}

static void
db_cpu_copy_surface_or_fail(const db_benchmark_pixel_surface_t *dst,
                            const db_benchmark_pixel_surface_t *src) {
    if ((dst == NULL) || (src == NULL) ||
        (dst->pixel_width != src->pixel_width) ||
        (dst->pixel_height != src->pixel_height) ||
        (dst->uses_rgba16f != src->uses_rgba16f)) {
        db_failf(BACKEND_NAME, "invalid surface copy request");
    }
    const size_t copy_bytes = (size_t)src->pixel_width *
                              (size_t)src->pixel_height *
                              (size_t)db_cpu_surface_pixel_bytes(src);
    if (src->uses_rgba16f != 0) {
        if ((dst->pixels_rgba16f == NULL) || (src->pixels_rgba16f == NULL)) {
            db_failf(BACKEND_NAME, "missing HDR surface pixels");
        }
        db_copy_bytes(dst->pixels_rgba16f, src->pixels_rgba16f, copy_bytes);
        return;
    }
    if ((dst->pixels_rgba8 == NULL) || (src->pixels_rgba8 == NULL)) {
        db_failf(BACKEND_NAME, "missing SDR surface pixels");
    }
    db_copy_bytes(dst->pixels_rgba8, src->pixels_rgba8, copy_bytes);
}

static void
db_cpu_set_full_damage(const db_benchmark_pixel_surface_t *surface) {
    if ((surface == NULL) || (g_state.damage_blocks == NULL) ||
        (g_state.damage_block_capacity == 0U) ||
        (surface->pixel_height == 0U) || (surface->pixel_width == 0U)) {
        g_state.damage_block_count = 0U;
        return;
    }
    g_state.damage_blocks[0] = (db_damage_block_t){
        .row_start = 0U,
        .row_count = surface->pixel_height,
        .col_start = 0U,
        .col_count = surface->pixel_width,
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
        blocks, block_count, db_grid_rows_effective(), db_grid_cols_effective(),
        g_state.damage_blocks, g_state.damage_block_capacity);
}

static void
db_cpu_apply_gradient_damage_block(const db_benchmark_pixel_surface_t *surface,
                                   const db_grid_block_t *block,
                                   const db_gradient_state_t *state) {
    if ((surface == NULL) || (block == NULL) || (state == NULL) ||
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
        db_cpu_surface_fill_damage_block_rgb(
            surface, segment.block.row_start, segment.block.row_count,
            segment.block.col_start, segment.block.col_count, segment.rgb);
    }
}

static void
db_cpu_apply_gradient_damage_blocks(const db_benchmark_pixel_surface_t *surface,
                                    const db_gradient_damage_plan_t *plan,
                                    const db_grid_block_t *damage_blocks,
                                    size_t damage_block_count) {
    if ((surface == NULL) || (plan == NULL) || (damage_blocks == NULL) ||
        (damage_block_count == 0U)) {
        return;
    }
    for (size_t block_index = 0U; block_index < damage_block_count;
         block_index++) {
        const db_grid_block_t *const block = &damage_blocks[block_index];
        if ((block->row_count == 0U) || (block->col_count == 0U)) {
            continue;
        }
        db_cpu_apply_gradient_damage_block(surface, block, &plan->render_state);
    }
}

static void db_render_bands(const db_benchmark_pixel_surface_t *surface,
                            uint32_t frame_index) {
    const uint32_t cols = surface->pixel_width;
    const uint32_t rows = surface->pixel_height;
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
        db_cpu_surface_fill_damage_block_rgb(surface, 0U, rows, x0, span_cols,
                                             band_rgb);
    }
}

static void db_render_snake_step(const db_benchmark_pixel_surface_t *surface,
                                 const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 const db_snake_shape_cache_t *shape_cache,
                                 const db_snake_active_tile_scratch_t *scratch,
                                 const double *target_rgb,
                                 int force_full_fill_on_phase_complete) {
    if ((surface == NULL) || (plan == NULL) || (region == NULL) ||
        (target_rgb == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }
    if ((force_full_fill_on_phase_complete != 0) &&
        (plan->phase_completed != 0)) {
        db_rgb_pixels_fill_solid_f64(
            surface->pixel_width, surface->pixel_height, surface->pixels_rgba8,
            surface->pixels_rgba16f, surface->uses_rgba16f, target_rgb);
        return;
    }
    const db_snake_rgb_sink_t sink = {
        .kind = DB_SNAKE_RGB_SINK_PIXEL_SURFACE_DIRECT,
        .logical_cols = surface->pixel_width,
        .logical_rows = surface->pixel_height,
        .pixel_surface = *surface,
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

    const uint32_t grid_rows = db_grid_rows_effective();
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
    }
    damage_blocks = (db_damage_block_t *)db_alloc_array_or_fail(
        BACKEND_NAME, "damage_blocks", damage_block_capacity,
        sizeof(*damage_blocks));

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.target_uses_rgba16f = (use_hdr_float_bo != 0) ? 1 : 0;
    g_state.runtime = init_state;
    g_state.runtime_flags = db_history_runtime_mode_flags(&g_state.runtime);
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

const db_damage_block_t *db_renderer_cpu_renderer_render_frame_to_surface_mode(
    uint32_t frame_index, const db_benchmark_pixel_surface_t *surface,
    db_cpu_render_target_mode_t target_mode, size_t *out_damage_count) {
    if (out_damage_count != NULL) {
        *out_damage_count = 0U;
    }
    if (g_state.initialized == 0) {
        return NULL;
    }

    db_cpu_validate_render_surface_or_fail(surface);
    const int use_replace_surface =
        (target_mode == DB_CPU_RENDER_TARGET_REPLACE_SURFACE) ? 1 : 0;
    db_benchmark_pixel_surface_t replace_surface = {0};
    const db_benchmark_pixel_surface_t *render_surface = surface;
    if (use_replace_surface != 0) {
        replace_surface = db_cpu_replace_surface_or_fail(surface);
        render_surface = &replace_surface;
        if (g_state.replace_surface_valid == 0) {
            db_cpu_fill_surface_seed_background(render_surface);
            g_state.replace_surface_valid = 1;
        }
    } else if (g_state.frame.frame_index == 0U) {
        db_cpu_fill_surface_seed_background(surface);
    }

    g_state.damage_block_count = 0U;
    if (g_state.runtime_flags.is_bands != 0) {
        db_frame_delta_plan_t delta = {0};
        db_grid_block_t logical_damage[1] = {{0U, 0U, 0U, 0U}};
        (void)db_frame_delta_produce_bands(
            &(const db_frame_delta_bands_producer_t){
                .pattern = g_state.runtime.pattern,
                .rows = render_surface->pixel_height,
                .cols = render_surface->pixel_width,
                .pixel_width = render_surface->pixel_width,
                .pixel_height = render_surface->pixel_height,
                .damage_blocks = logical_damage,
                .damage_capacity = 1U,
                .repair_blocks = NULL,
                .repair_capacity = 0U,
            },
            &delta);
        db_render_bands(render_surface, frame_index);
        db_cpu_publish_grid_blocks(delta.logical_damage_blocks,
                                   delta.logical_damage_block_count);
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
        db_render_snake_step(render_surface, &plan, &target.region,
                             shape_cache_ptr, &scratch, target.target_rgb,
                             target.force_full_fill_on_phase_complete);
        if (use_replace_surface != 0) {
            db_cpu_set_full_damage(render_surface);
        } else {
            db_frame_delta_plan_t delta = {0};
            if (db_frame_delta_produce_snake(
                    &(const db_frame_delta_snake_producer_t){
                        .pattern = g_state.runtime.pattern,
                        .region = &target.region,
                        .plan = &plan,
                        .shape_cache = shape_cache_ptr,
                        .cols = render_surface->pixel_width,
                        .rows = render_surface->pixel_height,
                        .pixel_width = render_surface->pixel_width,
                        .pixel_height = render_surface->pixel_height,
                        .force_full_recovery =
                            ((target.force_full_fill_on_phase_complete != 0) &&
                             (plan.phase_completed != 0))
                                ? 1
                                : 0,
                        .get_color_bits = NULL,
                        .color_user_data = NULL,
                        .damage_blocks = g_state.snake_scratch.damage.blocks,
                        .damage_capacity =
                            g_state.snake_scratch.damage.capacity,
                        .compact_blocks = NULL,
                        .compact_capacity = 0U,
                        .repair_blocks = NULL,
                        .repair_capacity = 0U,
                    },
                    &delta) == 0 ||
                (delta.mode == DB_FRAME_DELTA_MODE_FULL_REBUILD)) {
                db_cpu_set_full_damage(render_surface);
            } else {
                db_cpu_publish_grid_blocks(delta.logical_damage_blocks,
                                           delta.logical_damage_block_count);
            }
        }
        db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
    } else if (g_state.runtime_flags.is_gradient != 0) {
        db_grid_block_t gradient_dirty_blocks[2U] = {
            {0U, 0U, 0U, 0U},
            {0U, 0U, 0U, 0U},
        };
        db_frame_delta_compact_block_t gradient_compact_blocks[2U] = {
            {0U, 0U, 0U, 0U, {0U, 0U, 0U}},
            {0U, 0U, 0U, 0U, {0U, 0U, 0U}},
        };
        db_frame_delta_plan_t delta = {0};
        (void)db_frame_delta_produce_gradient(
            &(const db_frame_delta_gradient_producer_t){
                .pattern = g_state.runtime.pattern,
                .head_row = g_state.runtime.gradient.head_row,
                .direction_down = g_state.runtime.gradient.direction_down,
                .cycle_index = g_state.runtime.gradient.cycle_index,
                .head_step = db_u32_max(g_state.runtime.bench_speed_step, 1U),
                .rows = render_surface->pixel_height,
                .cols = render_surface->pixel_width,
                .pixel_width = render_surface->pixel_width,
                .pixel_height = render_surface->pixel_height,
                .damage_blocks = gradient_dirty_blocks,
                .damage_capacity = sizeof(gradient_dirty_blocks) /
                                   sizeof(gradient_dirty_blocks[0]),
                .compact_blocks = gradient_compact_blocks,
                .compact_capacity = sizeof(gradient_compact_blocks) /
                                    sizeof(gradient_compact_blocks[0]),
                .repair_blocks = NULL,
                .repair_capacity = 0U,
            },
            &delta);
        const db_gradient_damage_plan_t plan = delta.gradient_plan;
        const int force_full_gradient_frame = (g_state.frame.frame_index == 0U);
        if ((force_full_gradient_frame != 0) ||
            (delta.mode == DB_FRAME_DELTA_MODE_FULL_REBUILD)) {
            const db_grid_block_t full_block = db_grid_block_full(
                render_surface->pixel_height, render_surface->pixel_width);
            db_cpu_apply_gradient_damage_block(render_surface, &full_block,
                                               &plan.render_state);
            db_cpu_set_full_damage(render_surface);
        } else {
            if (delta.logical_damage_block_count == 0U) {
                const db_grid_block_t full_block = db_grid_block_full(
                    render_surface->pixel_height, render_surface->pixel_width);
                db_cpu_apply_gradient_damage_block(render_surface, &full_block,
                                                   &plan.render_state);
                db_cpu_set_full_damage(render_surface);
            } else {
                db_cpu_apply_gradient_damage_blocks(
                    render_surface, &plan, delta.logical_damage_blocks,
                    delta.logical_damage_block_count);
                db_cpu_publish_grid_blocks(delta.logical_damage_blocks,
                                           delta.logical_damage_block_count);
            }
        }
        db_history_apply_gradient_step_to_runtime(&g_state.runtime, &plan);
    }

    if (use_replace_surface != 0) {
        db_cpu_copy_surface_or_fail(surface, render_surface);
        db_cpu_set_full_damage(surface);
    }

    db_history_finalize_frame(&g_state.frame, &g_state.runtime,
                              surface->pixel_width, surface->pixel_height);
    if (out_damage_count != NULL) {
        *out_damage_count = g_state.damage_block_count;
    }
    return g_state.damage_blocks;
}

const db_damage_block_t *db_renderer_cpu_renderer_render_frame_to_surface(
    uint32_t frame_index, const db_benchmark_pixel_surface_t *surface,
    size_t *out_damage_count) {
    return db_renderer_cpu_renderer_render_frame_to_surface_mode(
        frame_index, surface, DB_CPU_RENDER_TARGET_PRESERVED_SURFACE,
        out_damage_count);
}

uint32_t db_renderer_cpu_renderer_work_unit_count(void) {
    return db_runtime_work_unit_count(&g_state.runtime, g_state.initialized);
}

const char *db_renderer_cpu_renderer_capability_mode(void) {
    if (g_state.target_uses_rgba16f != 0) {
        return DB_CAP_MODE_CPU_SURFACE_HDR;
    }
    return DB_CAP_MODE_CPU_SURFACE;
}

uint64_t db_renderer_cpu_renderer_state_hash(void) {
    return g_state.frame.state_hash;
}

void db_renderer_cpu_renderer_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.damage_blocks);
    free(g_state.replace_surface_rgba8);
    free(g_state.replace_surface_rgba16f);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    g_state = (db_cpu_renderer_state_t){0};
}
