#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../config/benchmark_config.h"
#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_hash.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_common.h"
#include "../renderer_snake_shape_common.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define DB_ALPHA_U8 UINT8_MAX
#define DB_ALPHA_F16 0x3C00U
#define DB_CAP_MODE_CPU_OFFSCREEN_BO "cpu_offscreen_bo"
#define DB_CAP_MODE_CPU_OFFSCREEN_BO_HDR "cpu_offscreen_bo_hdr_rgba16f"
#define DB_FLOAT_CHANNELS_PER_PIXEL 4U

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
    uint32_t *pixels_rgba8_staging;
    uint64_t staging_converted_frame_index;
    int staging_initialized;
    db_history_snake_scratch_t snake_scratch;
    db_renderer_frame_stats_t frame;
    int initialized;
    db_benchmark_runtime_init_t runtime;
    db_history_pattern_mode_flags_t runtime_flags;
} db_cpu_renderer_state_t;

typedef struct {
    db_cpu_bo_t *bo;
    uint32_t cols;
} db_cpu_gradient_block_apply_ctx_t;

static db_cpu_renderer_state_t g_state = {0};

static int db_cpu_hdr_enabled_from_runtime(void) {
    const char *value = db_runtime_option_get(DB_RUNTIME_OPT_CPU_HDR);
    if ((value == NULL) || (value[0] == '\0')) {
        return 1;
    }
    int parsed = 0;
    if (db_parse_bool_text(value, &parsed) != 0) {
        return (parsed != 0) ? 1 : 0;
    }
    db_failf(BACKEND_NAME,
             "invalid value for %s: %s (expected bool 0/1/true/false)",
             DB_RUNTIME_OPT_CPU_HDR, value);
    return 1;
}

static void db_cpu_bo_write_rgb_index(db_cpu_bo_t *bo, size_t idx,
                                      const double rgb[3]) {
    if ((bo == NULL) || (rgb == NULL)) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const size_t base = idx * DB_FLOAT_CHANNELS_PER_PIXEL;
        bo->pixels_rgba16f[base + 0U] = db_double_to_f16(rgb[0]);
        bo->pixels_rgba16f[base + 1U] = db_double_to_f16(rgb[1]);
        bo->pixels_rgba16f[base + 2U] = db_double_to_f16(rgb[2]);
        bo->pixels_rgba16f[base + 3U] = DB_ALPHA_F16;
        return;
    }
    bo->pixels_rgba8[idx] =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], DB_ALPHA_U8);
}

static void db_cpu_bo_read_rgb_index(const db_cpu_bo_t *bo, size_t idx,
                                     double out_rgb[3]) {
    if (out_rgb == NULL) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const size_t base = idx * DB_FLOAT_CHANNELS_PER_PIXEL;
        db_rgb_f16_to_f64_rgb3(&bo->pixels_rgba16f[base], out_rgb);
        return;
    }
    db_unpack_rgba8888_rgb01(bo->pixels_rgba8[idx], out_rgb);
}

static void db_cpu_bo_fill_solid_rgb(db_cpu_bo_t *bo, const double rgb[3]) {
    if ((bo == NULL) || (rgb == NULL) || (bo->width == 0U) ||
        (bo->height == 0U)) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const uint16_t rgba_f16[4] = {db_double_to_f16(rgb[0]),
                                      db_double_to_f16(rgb[1]),
                                      db_double_to_f16(rgb[2]), DB_ALPHA_F16};
        const size_t row_stride =
            (size_t)bo->width * DB_FLOAT_CHANNELS_PER_PIXEL;
        uint16_t *dst = bo->pixels_rgba16f;
        for (uint32_t row = 0U; row < bo->height; row++) {
            db_fill_rgba16f_buffer(dst, bo->width, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], DB_ALPHA_U8);
    uint32_t *dst = bo->pixels_rgba8;
    for (uint32_t row = 0U; row < bo->height; row++) {
        db_fill_u32_buffer(dst, bo->width, packed_color);
        dst += bo->width;
    }
}

static void db_cpu_bo_fill_damage_block_rgb(db_cpu_bo_t *bo, uint32_t row_start,
                                            uint32_t row_count,
                                            uint32_t col_start,
                                            uint32_t col_count,
                                            const double rgb[3]) {
    if ((bo == NULL) || (rgb == NULL) || (row_start >= bo->height) ||
        (row_count == 0U) || (col_count == 0U)) {
        return;
    }
    if (col_start >= bo->width) {
        return;
    }
    const uint32_t max_rows = bo->height - row_start;
    const uint32_t span_rows = db_u32_min(row_count, max_rows);
    const uint32_t max_cols = bo->width - col_start;
    const uint32_t span_cols = db_u32_min(col_count, max_cols);
    if ((span_rows == 0U) || (span_cols == 0U)) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const uint16_t rgba_f16[4] = {db_double_to_f16(rgb[0]),
                                      db_double_to_f16(rgb[1]),
                                      db_double_to_f16(rgb[2]), DB_ALPHA_F16};
        const size_t row_stride =
            (size_t)bo->width * DB_FLOAT_CHANNELS_PER_PIXEL;
        uint16_t *dst = bo->pixels_rgba16f +
                        ((((size_t)row_start * bo->width) + col_start) *
                         DB_FLOAT_CHANNELS_PER_PIXEL);
        for (uint32_t row_offset = 0U; row_offset < span_rows; row_offset++) {
            db_fill_rgba16f_buffer(dst, span_cols, rgba_f16);
            dst += row_stride;
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(rgb[0], rgb[1], rgb[2], DB_ALPHA_U8);
    uint32_t *dst =
        bo->pixels_rgba8 + (((size_t)row_start * bo->width) + col_start);
    for (uint32_t row_offset = 0U; row_offset < span_rows; row_offset++) {
        db_fill_u32_buffer(dst, span_cols, packed_color);
        dst += bo->width;
    }
}

static inline void
db_cpu_snapshot_prior_if_valid(const db_cpu_bo_t *bo, uint32_t update_index,
                               const uint32_t *active_tile_indices,
                               const uint8_t *active_tile_valid,
                               double *prior_rgb) {
    if (active_tile_valid[update_index] == 0U) {
        return;
    }
    const size_t idx = (size_t)active_tile_indices[update_index];
    const size_t base = (size_t)update_index * 3U;
    db_cpu_bo_read_rgb_index(bo, idx, &prior_rgb[base]);
}

static inline void db_cpu_blend_write_if_valid(
    db_cpu_bo_t *bo, uint32_t update_index, uint32_t batch_size,
    const uint32_t *active_tile_indices, const uint8_t *active_tile_valid,
    const double *prior_rgb, const double target_rgb[3]) {
    if (active_tile_valid[update_index] == 0U) {
        return;
    }
    const size_t idx = (size_t)active_tile_indices[update_index];
    const size_t prior_base = (size_t)update_index * 3U;
    const double blend = db_window_blend_factor(update_index, batch_size);
    double out_rgb[3] = {0.0, 0.0, 0.0};
    db_blend_rgb3(&prior_rgb[prior_base], target_rgb, blend, out_rgb);
    db_cpu_bo_write_rgb_index(bo, idx, out_rgb);
}

static inline void
db_cpu_convert_hdr_staging_full(db_cpu_renderer_state_t *state) {
    db_convert_rgba16f_to_rgba8888_block(
        state->pixels_rgba8_staging, (size_t)state->bo.width,
        state->bo.pixels_rgba16f, (size_t)state->bo.width, 0U, state->bo.height,
        0U, state->bo.width, DB_ALPHA_U8);
}

static void
db_cpu_update_rgba8_staging_from_hdr(db_cpu_renderer_state_t *state) {
    if ((state == NULL) || (state->pixels_rgba8_staging == NULL) ||
        (state->bo.pixels_rgba16f == NULL)) {
        return;
    }
    if (state->damage_block_count == 0U) {
        return;
    }
    if ((state->damage_block_count == 1U) &&
        (state->damage_blocks[0].row_start == 0U) &&
        (state->damage_blocks[0].row_count >= state->bo.height) &&
        (state->damage_blocks[0].col_start == 0U) &&
        (state->damage_blocks[0].col_count >= state->bo.width)) {
        db_cpu_convert_hdr_staging_full(state);
        return;
    }
    for (size_t block_index = 0U; block_index < state->damage_block_count;
         block_index++) {
        const db_damage_block_t block = state->damage_blocks[block_index];
        const uint32_t row_end =
            db_u32_min(state->bo.height,
                       db_checked_add_u32(BACKEND_NAME, "cpu_hdr_block_row_end",
                                          block.row_start, block.row_count));
        const uint32_t col_end =
            db_u32_min(state->bo.width,
                       db_checked_add_u32(BACKEND_NAME, "cpu_hdr_block_col_end",
                                          block.col_start, block.col_count));
        if ((row_end <= block.row_start) || (col_end <= block.col_start)) {
            continue;
        }
        db_convert_rgba16f_to_rgba8888_block(
            state->pixels_rgba8_staging, (size_t)state->bo.width,
            state->bo.pixels_rgba16f, (size_t)state->bo.width, block.row_start,
            row_end - block.row_start, block.col_start,
            col_end - block.col_start, DB_ALPHA_U8);
    }
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

static void db_cpu_publish_damage_blocks(const db_damage_block_t *blocks,
                                         size_t block_count) {
    if ((g_state.damage_blocks == NULL) ||
        (g_state.damage_block_capacity == 0U)) {
        g_state.damage_block_count = 0U;
        return;
    }
    const size_t copy_limit = (g_state.damage_block_capacity < block_count)
                                  ? g_state.damage_block_capacity
                                  : block_count;
    for (size_t index = 0U; index < copy_limit; index++) {
        g_state.damage_blocks[index] = blocks[index];
    }
    g_state.damage_block_count = copy_limit;
}

static void db_cpu_apply_gradient_color_block(uint32_t row_start,
                                              uint32_t row_count,
                                              const double row_rgb[3],
                                              void *user_data) {
    db_cpu_gradient_block_apply_ctx_t *ctx =
        (db_cpu_gradient_block_apply_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->bo == NULL) || (row_rgb == NULL)) {
        return;
    }
    db_cpu_bo_fill_damage_block_rgb(ctx->bo, row_start, row_count, 0U,
                                    ctx->cols, row_rgb);
}

static void
db_cpu_apply_gradient_damage_block(db_cpu_bo_t *bo,
                                   const db_damage_block_t *block,
                                   const db_gradient_state_t *state) {
    if ((bo == NULL) || (block == NULL) || (state == NULL) ||
        (block->row_count == 0U)) {
        return;
    }
    db_cpu_gradient_block_apply_ctx_t apply_ctx = {
        .bo = bo,
        .cols = block->col_count,
    };
    db_for_each_gradient_row_block_color(
        block->row_start, block->row_count, state->head_row,
        state->direction_down, state->cycle_index,
        db_cpu_apply_gradient_color_block, &apply_ctx);
}

static void db_cpu_apply_gradient_damage_blocks(
    db_cpu_bo_t *bo, const db_gradient_damage_plan_t *plan,
    const db_damage_block_t *damage_blocks, size_t damage_block_count) {
    if ((bo == NULL) || (plan == NULL) || (damage_blocks == NULL) ||
        (damage_block_count == 0U)) {
        return;
    }
    for (size_t block_index = 0U; block_index < damage_block_count;
         block_index++) {
        const db_damage_block_t *const block = &damage_blocks[block_index];
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
                                 const db_snake_shape_cache_t *shape_cache_ptr,
                                 const double target_rgb[3],
                                 int force_full_fill_on_phase_complete) {
    if ((plan == NULL) || (region == NULL) || (target_rgb == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }

    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    // Snapshot prior colors for the active blend window BEFORE we mutate the
    // buffer. This mirrors the GL1.5 path and is required for determinism.
    const uint32_t batch_limit =
        db_snake_plan_active_batch_limit(plan, BENCH_SNAKE_PHASE_WINDOW_TILES);
    uint32_t active_tile_indices_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    uint8_t active_tile_valid_local[BENCH_SNAKE_PHASE_WINDOW_TILES] = {0U};
    double prior_rgb_local[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0};
    uint32_t *active_tile_indices = active_tile_indices_local;
    uint8_t *active_tile_valid = active_tile_valid_local;
    double *prior_rgb = prior_rgb_local;
    if ((g_state.snake_scratch.shape.active_tile_indices != NULL) &&
        (g_state.snake_scratch.shape.active_tile_valid != NULL) &&
        (g_state.snake_scratch.shape.active_prior_rgb != NULL) &&
        (g_state.snake_scratch.shape.active_tile_capacity >= batch_limit)) {
        active_tile_indices = g_state.snake_scratch.shape.active_tile_indices;
        active_tile_valid = g_state.snake_scratch.shape.active_tile_valid;
        prior_rgb = g_state.snake_scratch.shape.active_prior_rgb;
    }
    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        active_tile_valid[update_index] = 0U;
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_active_tile(plan, region, shape_cache_ptr,
                                              update_index, cols, rows,
                                              &tile) == 0) {
            continue;
        }
        active_tile_indices[update_index] = tile.tile_index;
        active_tile_valid[update_index] = 1U;
        db_cpu_snapshot_prior_if_valid(bo, update_index, active_tile_indices,
                                       active_tile_valid, prior_rgb);
    }

    if ((force_full_fill_on_phase_complete != 0) &&
        (plan->phase_completed != 0)) {
        db_cpu_bo_fill_solid_rgb(bo, target_rgb);
        return;
    }

    for (uint32_t prev_offset = 0U; prev_offset < plan->prev_count;
         prev_offset++) {
        db_snake_step_tile_t tile = {0};
        if (db_snake_plan_resolve_prev_tile(plan, region, shape_cache_ptr,
                                            prev_offset, cols, rows,
                                            &tile) == 0) {
            continue;
        }
        db_cpu_bo_write_rgb_index(bo, (size_t)tile.tile_index, target_rgb);
    }

    // Blend the active window using the snapshotted prior colors.
    uint32_t update_index = 0U;
    for (; (update_index + 3U) < batch_limit; update_index += 4U) {
        db_cpu_blend_write_if_valid(bo, update_index, plan->batch_size,
                                    active_tile_indices, active_tile_valid,
                                    prior_rgb, target_rgb);
        db_cpu_blend_write_if_valid(bo, update_index + 1U, plan->batch_size,
                                    active_tile_indices, active_tile_valid,
                                    prior_rgb, target_rgb);
        db_cpu_blend_write_if_valid(bo, update_index + 2U, plan->batch_size,
                                    active_tile_indices, active_tile_valid,
                                    prior_rgb, target_rgb);
        db_cpu_blend_write_if_valid(bo, update_index + 3U, plan->batch_size,
                                    active_tile_indices, active_tile_valid,
                                    prior_rgb, target_rgb);
    }
    for (; update_index < batch_limit; update_index++) {
        db_cpu_blend_write_if_valid(bo, update_index, plan->batch_size,
                                    active_tile_indices, active_tile_valid,
                                    prior_rgb, target_rgb);
    }
}

void db_renderer_cpu_renderer_init(void) {
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
    if ((pixel_count == 0U) ||
        (pixel_count > ((uint64_t)SIZE_MAX / sizeof(uint32_t)))) {
        db_failf(BACKEND_NAME, "invalid offscreen BO size: %ux%u", grid_cols,
                 grid_rows);
    }

    db_cpu_bo_t bo = {
        .width = grid_cols,
        .height = grid_rows,
        .pixels_rgba8 = NULL,
        .pixels_rgba16f = NULL,
        .is_hdr_float_bo = db_cpu_hdr_enabled_from_runtime(),
    };
    if (bo.is_hdr_float_bo != 0) {
        bo.pixels_rgba16f = (uint16_t *)db_alloc_aligned_array_or_fail(
            BACKEND_NAME, "pixels_rgba16f",
            (size_t)pixel_count * DB_FLOAT_CHANNELS_PER_PIXEL, sizeof(uint16_t),
            DB_CACHELINE_ALIGNMENT_BYTES);
    } else {
        bo.pixels_rgba8 = (uint32_t *)db_alloc_aligned_array_or_fail(
            BACKEND_NAME, "pixels_rgba8", (size_t)pixel_count, sizeof(uint32_t),
            DB_CACHELINE_ALIGNMENT_BYTES);
    }
    double seed_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(&init_state, seed_rgb);
    db_cpu_bo_fill_solid_rgb(&bo, seed_rgb);
    db_snake_shape_row_bounds_t *snake_row_bounds = NULL;
    size_t snake_row_bounds_capacity = 0U;
    db_damage_block_t *snake_damage_blocks = NULL;
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
        snake_damage_blocks = (db_damage_block_t *)db_alloc_array_or_fail(
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
        db_render_snake_step(write_bo, &plan, &target.region, shape_cache_ptr,
                             target.target_rgb,
                             target.force_full_fill_on_phase_complete);
        if ((target.force_full_fill_on_phase_complete != 0) &&
            (plan.phase_completed != 0)) {
            db_cpu_set_full_damage(write_bo);
        } else {
            const db_damage_block_t *blocks = NULL;
            size_t block_count = 0U;
            if ((g_state.snake_scratch.damage.blocks != NULL) &&
                (db_snake_collect_damage_blocks_for_plan(
                     &target.region, &plan, shape_cache_ptr,
                     g_state.snake_scratch.damage.blocks,
                     g_state.snake_scratch.damage.capacity,
                     &block_count) != 0)) {
                blocks = g_state.snake_scratch.damage.blocks;
                db_cpu_publish_damage_blocks(blocks, block_count);
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
            const db_damage_block_t full_block = {
                .row_start = 0U,
                .row_count = write_bo->height,
                .col_start = 0U,
                .col_count = write_bo->width,
            };
            db_cpu_apply_gradient_damage_block(write_bo, &full_block,
                                               &plan.render_state);
            db_cpu_set_full_damage(write_bo);
        } else {
            db_damage_block_t gradient_dirty_blocks[2U] = {
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
                const db_damage_block_t full_block = {
                    .row_start = 0U,
                    .row_count = write_bo->height,
                    .col_start = 0U,
                    .col_count = write_bo->width,
                };
                db_cpu_apply_gradient_damage_block(write_bo, &full_block,
                                                   &plan.render_state);
            } else {
                db_cpu_apply_gradient_damage_blocks(write_bo, &plan,
                                                    gradient_dirty_blocks,
                                                    gradient_dirty_count);
                db_cpu_publish_damage_blocks(gradient_dirty_blocks,
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
    const uint64_t pixel_count =
        (uint64_t)g_state.bo.width * (uint64_t)g_state.bo.height;
    if (g_state.pixels_rgba8_staging == NULL) {
        g_state.pixels_rgba8_staging =
            (uint32_t *)db_alloc_aligned_array_or_fail(
                BACKEND_NAME, "pixels_rgba8_staging", (size_t)pixel_count,
                sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    }
    const uint64_t frame_index = g_state.frame.frame_index;
    if (g_state.staging_initialized == 0) {
        db_cpu_convert_hdr_staging_full(&g_state);
        g_state.staging_initialized = 1;
        g_state.staging_converted_frame_index = frame_index;
        return g_state.pixels_rgba8_staging;
    }
    if (g_state.staging_converted_frame_index == frame_index) {
        return g_state.pixels_rgba8_staging;
    }
    if (g_state.damage_block_count == 0U) {
        g_state.staging_converted_frame_index = frame_index;
        return g_state.pixels_rgba8_staging;
    }
    db_cpu_update_rgba8_staging_from_hdr(&g_state);
    g_state.staging_converted_frame_index = frame_index;
    return g_state.pixels_rgba8_staging;
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

int db_renderer_cpu_renderer_is_hdr_float_bo(void) {
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
    free(g_state.pixels_rgba8_staging);
    db_history_snake_active_cache_free(&g_state.snake_scratch);
    free(g_state.damage_blocks);
    free(g_state.snake_scratch.damage.blocks);
    free(g_state.snake_scratch.shape.row_bounds);
    free(g_state.bo.pixels_rgba8);
    free(g_state.bo.pixels_rgba16f);
    g_state = (db_cpu_renderer_state_t){0};
}
