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
#define DB_ALPHA_U8 255U
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
    db_dirty_row_range_t damage_rows[2];
    size_t damage_row_count;
    uint32_t *pixels_rgba8_staging;
    db_history_snake_scratch_t snake_scratch;
    db_renderer_frame_stats_t frame;
    int initialized;
    db_benchmark_runtime_init_t runtime;
} db_cpu_renderer_state_t;

typedef struct {
    db_cpu_bo_t *bo;
    uint32_t cols;
} db_cpu_gradient_row_apply_ctx_t;

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

static void db_cpu_bo_write_rgb_index(db_cpu_bo_t *bo, size_t idx, double red,
                                      double green, double blue) {
    if (bo->is_hdr_float_bo != 0) {
        const size_t base = idx * DB_FLOAT_CHANNELS_PER_PIXEL;
        bo->pixels_rgba16f[base + 0U] = db_double_to_f16(red);
        bo->pixels_rgba16f[base + 1U] = db_double_to_f16(green);
        bo->pixels_rgba16f[base + 2U] = db_double_to_f16(blue);
        bo->pixels_rgba16f[base + 3U] = DB_ALPHA_F16;
        return;
    }
    bo->pixels_rgba8[idx] =
        db_pack_rgba8888_from_rgb01(red, green, blue, DB_ALPHA_U8);
}

static void db_cpu_bo_read_rgb_index(const db_cpu_bo_t *bo, size_t idx,
                                     double *out_red, double *out_green,
                                     double *out_blue) {
    if (bo->is_hdr_float_bo != 0) {
        const size_t base = idx * DB_FLOAT_CHANNELS_PER_PIXEL;
        *out_red = db_f16_to_double(bo->pixels_rgba16f[base + 0U]);
        *out_green = db_f16_to_double(bo->pixels_rgba16f[base + 1U]);
        *out_blue = db_f16_to_double(bo->pixels_rgba16f[base + 2U]);
        return;
    }
    const uint32_t rgba = bo->pixels_rgba8[idx];
    db_unpack_rgba8888_rgb01(rgba, out_red, out_green, out_blue);
}

static void db_cpu_bo_fill_solid_rgb(db_cpu_bo_t *bo, double red, double green,
                                     double blue) {
    if ((bo == NULL) || (bo->width == 0U) || (bo->height == 0U)) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const uint16_t red_f16 = db_double_to_f16(red);
        const uint16_t green_f16 = db_double_to_f16(green);
        const uint16_t blue_f16 = db_double_to_f16(blue);
        for (uint32_t row = 0U; row < bo->height; row++) {
            uint16_t *dst =
                bo->pixels_rgba16f +
                ((size_t)row * (size_t)bo->width * DB_FLOAT_CHANNELS_PER_PIXEL);
            db_fill_rgba16f_buffer(dst, bo->width, red_f16, green_f16, blue_f16,
                                   DB_ALPHA_F16);
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(red, green, blue, DB_ALPHA_U8);
    for (uint32_t row = 0U; row < bo->height; row++) {
        uint32_t *dst = bo->pixels_rgba8 + ((size_t)row * bo->width);
        db_fill_u32_buffer(dst, bo->width, packed_color);
    }
}

static void db_cpu_bo_fill_row_span_rgb(db_cpu_bo_t *bo, uint32_t row,
                                        uint32_t col_start, uint32_t col_count,
                                        double red, double green, double blue) {
    if ((bo == NULL) || (row >= bo->height) || (col_start >= bo->width) ||
        (col_count == 0U)) {
        return;
    }
    const uint32_t max_cols = bo->width - col_start;
    const uint32_t span_cols = db_u32_min(col_count, max_cols);
    if (span_cols == 0U) {
        return;
    }
    if (bo->is_hdr_float_bo != 0) {
        const uint16_t red_f16 = db_double_to_f16(red);
        const uint16_t green_f16 = db_double_to_f16(green);
        const uint16_t blue_f16 = db_double_to_f16(blue);
        uint16_t *dst =
            bo->pixels_rgba16f + ((((size_t)row * bo->width) + col_start) *
                                  DB_FLOAT_CHANNELS_PER_PIXEL);
        db_fill_rgba16f_buffer(dst, span_cols, red_f16, green_f16, blue_f16,
                               DB_ALPHA_F16);
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(red, green, blue, DB_ALPHA_U8);
    uint32_t *dst = bo->pixels_rgba8 + (((size_t)row * bo->width) + col_start);
    db_fill_u32_buffer(dst, span_cols, packed_color);
}

static void db_cpu_bo_fill_row_range_span_rgb(
    db_cpu_bo_t *bo, uint32_t row_start, uint32_t row_count, uint32_t col_start,
    uint32_t col_count, double red, double green, double blue) {
    if ((bo == NULL) || (row_start >= bo->height) || (row_count == 0U) ||
        (col_count == 0U)) {
        return;
    }
    const uint32_t max_rows = bo->height - row_start;
    const uint32_t span_rows = db_u32_min(row_count, max_rows);
    if (bo->is_hdr_float_bo != 0) {
        const uint16_t red_f16 = db_double_to_f16(red);
        const uint16_t green_f16 = db_double_to_f16(green);
        const uint16_t blue_f16 = db_double_to_f16(blue);
        for (uint32_t row_offset = 0U; row_offset < span_rows; row_offset++) {
            const uint32_t row = row_start + row_offset;
            if ((row >= bo->height) || (col_start >= bo->width)) {
                continue;
            }
            const uint32_t max_cols = bo->width - col_start;
            const uint32_t span_cols = db_u32_min(col_count, max_cols);
            if (span_cols == 0U) {
                continue;
            }
            uint16_t *dst =
                bo->pixels_rgba16f + ((((size_t)row * bo->width) + col_start) *
                                      DB_FLOAT_CHANNELS_PER_PIXEL);
            db_fill_rgba16f_buffer(dst, span_cols, red_f16, green_f16, blue_f16,
                                   DB_ALPHA_F16);
        }
        return;
    }
    const uint32_t packed_color =
        db_pack_rgba8888_from_rgb01(red, green, blue, DB_ALPHA_U8);
    for (uint32_t row_offset = 0U; row_offset < span_rows; row_offset++) {
        const uint32_t row = row_start + row_offset;
        if ((row >= bo->height) || (col_start >= bo->width)) {
            continue;
        }
        const uint32_t max_cols = bo->width - col_start;
        const uint32_t span_cols = db_u32_min(col_count, max_cols);
        if (span_cols == 0U) {
            continue;
        }
        uint32_t *dst =
            bo->pixels_rgba8 + (((size_t)row * bo->width) + col_start);
        db_fill_u32_buffer(dst, span_cols, packed_color);
    }
}

static void db_cpu_set_full_damage(const db_cpu_bo_t *bo) {
    if ((bo == NULL) || (bo->height == 0U)) {
        g_state.damage_row_count = 0U;
        return;
    }
    g_state.damage_rows[0] = (db_dirty_row_range_t){
        .row_start = 0U,
        .row_count = bo->height,
    };
    g_state.damage_rows[1] = (db_dirty_row_range_t){0U, 0U};
    g_state.damage_row_count = 1U;
}

static void
db_cpu_set_damage_from_gradient_plan(const db_gradient_damage_plan_t *plan,
                                     uint32_t rows) {
    g_state.damage_row_count = db_gradient_collect_dirty_ranges_clamped(
        plan, rows, g_state.damage_rows, 2U);
    if (g_state.damage_row_count == 0U) {
        db_cpu_set_full_damage(&g_state.bo);
    }
}

static void db_cpu_set_damage_from_spans(const db_snake_col_span_t *spans,
                                         size_t span_count, uint32_t rows) {
    uint32_t row_start = 0U;
    uint32_t row_count = 0U;
    if (db_snake_span_row_bounds(spans, span_count, rows, &row_start,
                                 &row_count) == 0) {
        g_state.damage_row_count = 0U;
        return;
    }
    g_state.damage_rows[0] = (db_dirty_row_range_t){
        .row_start = row_start,
        .row_count = row_count,
    };
    g_state.damage_rows[1] = (db_dirty_row_range_t){0U, 0U};
    g_state.damage_row_count = 1U;
}

static void db_cpu_apply_gradient_row_color(uint32_t row, double row_red,
                                            double row_green, double row_blue,
                                            void *user_data) {
    db_cpu_gradient_row_apply_ctx_t *ctx =
        (db_cpu_gradient_row_apply_ctx_t *)user_data;
    if ((ctx == NULL) || (ctx->bo == NULL)) {
        return;
    }
    db_cpu_bo_fill_row_span_rgb(ctx->bo, row, 0U, ctx->cols, row_red, row_green,
                                row_blue);
}

static void db_render_bands(db_cpu_bo_t *bo, uint32_t frame_index) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        double band_red = 0.0;
        double band_green = 0.0;
        double band_blue = 0.0;
        db_band_color_rgb(band, BENCH_BANDS, frame_index, &band_red,
                          &band_green, &band_blue);
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
        db_cpu_bo_fill_row_range_span_rgb(bo, 0U, rows, x0, span_cols, band_red,
                                          band_green, band_blue);
    }
}

static void db_render_snake_step(db_cpu_bo_t *bo, const db_snake_plan_t *plan,
                                 const db_snake_region_t *region,
                                 const db_snake_shape_cache_t *shape_cache_ptr,
                                 double target_red, double target_green,
                                 double target_blue,
                                 int full_fill_on_phase_completed) {
    if ((plan == NULL) || (region == NULL)) {
        return;
    }
    if ((region->width == 0U) || (region->height == 0U)) {
        return;
    }

    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    // Snapshot prior colors for the active blend window BEFORE we mutate the
    // buffer. This mirrors the GL1.5 path and is required for determinism.
    double prior_rgb[BENCH_SNAKE_PHASE_WINDOW_TILES * 3U] = {0.0};

    const uint32_t batch_limit =
        (plan->batch_size <= BENCH_SNAKE_PHASE_WINDOW_TILES)
            ? plan->batch_size
            : BENCH_SNAKE_PHASE_WINDOW_TILES;

    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t idx = ((size_t)tile.row * (size_t)cols) + (size_t)tile.col;
        double pr = 0.0;
        double pg = 0.0;
        double pb = 0.0;
        db_cpu_bo_read_rgb_index(bo, idx, &pr, &pg, &pb);
        const size_t base = (size_t)update_index * 3U;
        prior_rgb[base + 0U] = pr;
        prior_rgb[base + 1U] = pg;
        prior_rgb[base + 2U] = pb;
    }

    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_cpu_bo_fill_solid_rgb(bo, target_red, target_green, target_blue);
        return;
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t idx = ((size_t)tile.row * (size_t)cols) + (size_t)tile.col;
        db_cpu_bo_write_rgb_index(bo, idx, target_red, target_green,
                                  target_blue);
    }

    // Blend the active window using the snapshotted prior colors.
    for (uint32_t update_index = 0U; update_index < batch_limit;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->target_tile_count) {
            break;
        }
        db_snake_step_tile_t tile = {0};
        if (db_snake_step_resolve_tile(region, shape_cache_ptr, step, cols,
                                       rows, &tile) == 0) {
            continue;
        }
        const size_t idx = ((size_t)tile.row * (size_t)cols) + (size_t)tile.col;

        const size_t prior_base = (size_t)update_index * 3U;
        const double prior_red = prior_rgb[prior_base + 0U];
        const double prior_green = prior_rgb[prior_base + 1U];
        const double prior_blue = prior_rgb[prior_base + 2U];

        const double blend =
            db_window_blend_factor(update_index, plan->batch_size);
        double out_red = 0.0;
        double out_green = 0.0;
        double out_blue = 0.0;
        db_blend_rgb(prior_red, prior_green, prior_blue, target_red,
                     target_green, target_blue, blend, &out_red, &out_green,
                     &out_blue);

        db_cpu_bo_write_rgb_index(bo, idx, out_red, out_green, out_blue);
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
    double seed_r = 0.0;
    double seed_g = 0.0;
    double seed_b = 0.0;
    db_benchmark_seed_background_color_rgb(&init_state, &seed_r, &seed_g,
                                           &seed_b);
    db_cpu_bo_fill_solid_rgb(&bo, seed_r, seed_g, seed_b);
    db_snake_shape_row_bounds_t *snake_row_bounds = NULL;
    size_t snake_row_bounds_capacity = 0U;
    const db_history_pattern_mode_flags_t pattern_flags =
        db_history_pattern_mode_flags(init_state.pattern);
    if (pattern_flags.is_snake_shapes != 0) {
        snake_row_bounds =
            (db_snake_shape_row_bounds_t *)db_alloc_array_or_fail(
                BACKEND_NAME, "snake_row_bounds", (size_t)grid_rows,
                sizeof(*snake_row_bounds));
        snake_row_bounds_capacity = (size_t)grid_rows;
    }

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.runtime = init_state;
    g_state.bo = bo;
    g_state.runtime.snake.shape_index = 0U;
    g_state.snake_scratch.row_bounds = snake_row_bounds;
    g_state.snake_scratch.row_bounds_capacity = snake_row_bounds_capacity;
}

void db_renderer_cpu_renderer_render_frame(uint32_t frame_index) {
    if (g_state.initialized == 0) {
        return;
    }

    db_cpu_bo_t *write_bo = &g_state.bo;
    const db_history_runtime_mode_flags_t mode_flags =
        db_history_runtime_mode_flags(&g_state.runtime);

    g_state.damage_row_count = 0U;
    if (mode_flags.is_bands != 0) {
        db_render_bands(write_bo, frame_index);
        db_cpu_set_full_damage(write_bo);
    } else if (mode_flags.is_snake_history_texture != 0) {
        const db_history_snake_step_eval_t eval =
            db_history_eval_snake_step_from_runtime(&g_state.runtime);
        const db_snake_plan_t plan = eval.plan;
        const db_snake_step_target_t target = eval.target;
        const db_snake_shape_kind_t shape_kind = eval.shape_kind;
        db_snake_shape_cache_t shape_cache = {0};
        const db_snake_shape_cache_t *shape_cache_ptr = NULL;
        if (eval.is_shapes_mode != 0) {
            if ((g_state.snake_scratch.row_bounds != NULL) &&
                (g_state.snake_scratch.row_bounds_capacity > 0U) &&
                (db_snake_shape_cache_init_from_index(
                     &shape_cache, g_state.snake_scratch.row_bounds,
                     g_state.snake_scratch.row_bounds_capacity,
                     g_state.runtime.pattern_seed, plan.active_shape_index,
                     DB_U32_SALT_PALETTE, &target.region, shape_kind) != 0)) {
                shape_cache_ptr = &shape_cache;
            }
        }
        db_render_snake_step(write_bo, &plan, &target.region, shape_cache_ptr,
                             target.target_r, target.target_g, target.target_b,
                             target.full_fill_on_phase_completed);
        if ((target.full_fill_on_phase_completed != 0) &&
            (plan.phase_completed != 0)) {
            db_cpu_set_full_damage(write_bo);
        } else {
            const size_t max_spans = db_snake_plan_span_capacity_needed(&plan);
            db_snake_col_span_t spans[BENCH_SNAKE_PHASE_WINDOW_TILES * 2U];
            if (max_spans <= ((size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * 2U)) {
                const size_t span_count =
                    db_snake_collect_damage_spans_for_plan(
                        spans, max_spans, &target.region, &plan,
                        shape_cache_ptr);
                db_cpu_set_damage_from_spans(spans, span_count,
                                             write_bo->height);
            } else {
                DB_LOG_CAPACITY_EXCEEDED_ONCE(
                    BACKEND_NAME, "cpu_snake_damage_spans", max_spans,
                    (size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * 2U);
                db_cpu_set_full_damage(write_bo);
            }
        }
        db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
    } else if (mode_flags.is_gradient != 0) {
        const db_gradient_damage_plan_t plan = db_gradient_step_from_runtime(
            g_state.runtime.pattern, g_state.runtime.gradient.head_row,
            g_state.runtime.gradient.direction_down,
            g_state.runtime.gradient.cycle_index,
            g_state.runtime.bench_speed_step);
        db_cpu_gradient_row_apply_ctx_t apply_ctx = {
            .bo = write_bo,
            .cols = write_bo->width,
        };
        db_for_each_gradient_row_color(
            0U, write_bo->height, plan.render_state.head_row,
            plan.render_state.direction_down, plan.render_state.cycle_index,
            db_cpu_apply_gradient_row_color, &apply_ctx);
        db_cpu_set_damage_from_gradient_plan(&plan, write_bo->height);
        db_gradient_apply_step_to_runtime(&g_state.runtime, &plan);
    }

    g_state.frame.state_hash = db_benchmark_runtime_state_hash_cross_renderer(
        &g_state.runtime, g_state.frame.frame_index, write_bo->width,
        write_bo->height);
    g_state.frame.frame_index++;
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
    db_convert_rgba16f_to_rgba8888_rows(
        g_state.pixels_rgba8_staging, (size_t)g_state.bo.width,
        g_state.bo.pixels_rgba16f,
        (size_t)g_state.bo.width * DB_FLOAT_CHANNELS_PER_PIXEL,
        g_state.bo.width, g_state.bo.height, DB_ALPHA_U8);
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

const db_dirty_row_range_t *
db_renderer_cpu_renderer_damage_rows(size_t *out_count) {
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (g_state.initialized == 0) {
        return NULL;
    }
    const size_t count = db_u32_min((uint32_t)g_state.damage_row_count, 2U);
    if (out_count != NULL) {
        *out_count = count;
    }
    return g_state.damage_rows;
}

void db_renderer_cpu_renderer_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    free(g_state.pixels_rgba8_staging);
    free(g_state.snake_scratch.row_bounds);
    free(g_state.bo.pixels_rgba8);
    free(g_state.bo.pixels_rgba16f);
    g_state = (db_cpu_renderer_state_t){0};
}
