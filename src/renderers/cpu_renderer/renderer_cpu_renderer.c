#include "renderer_cpu_renderer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define DB_CAP_MODE_CPU_OFFSCREEN_BO "cpu_offscreen_bo"
#define DB_ALPHA_U8 255U
#define DB_U8_MAX_F 255.0F
#define DB_ROUND_HALF_UP_F 0.5F
#define DB_COLOR_SHIFT_R 0U
#define DB_COLOR_SHIFT_G 8U
#define DB_COLOR_SHIFT_B 16U
#define DB_COLOR_SHIFT_A 24U

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels_rgba8;
} db_cpu_bo_t;

typedef struct {
    int initialized;
    db_pattern_vertex_init_t state;
    db_cpu_bo_t bos[2];
    int history_mode;
    int history_read_index;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    int mode_phase_flag;
    uint32_t gradient_head_row;
    uint32_t gradient_cycle;
    uint32_t snake_rect_index;
    uint32_t rect_seed;
} db_cpu_renderer_state_t;

static db_cpu_renderer_state_t g_state = {0};

static uint32_t db_channel_to_u8(float value) {
    float clamped = value;
    if (clamped < 0.0F) {
        clamped = 0.0F;
    } else if (clamped > 1.0F) {
        clamped = 1.0F;
    }
    return (uint32_t)((clamped * DB_U8_MAX_F) + DB_ROUND_HALF_UP_F);
}

static uint32_t db_pack_rgb(float red, float green, float blue) {
    const uint32_t red_u8 = db_channel_to_u8(red);
    const uint32_t green_u8 = db_channel_to_u8(green);
    const uint32_t blue_u8 = db_channel_to_u8(blue);
    return (DB_ALPHA_U8 << DB_COLOR_SHIFT_A) | (blue_u8 << DB_COLOR_SHIFT_B) |
           (green_u8 << DB_COLOR_SHIFT_G) | (red_u8 << DB_COLOR_SHIFT_R);
}

static void db_unpack_rgb(uint32_t rgba, float *out_red, float *out_green,
                          float *out_blue) {
    *out_red = (float)((rgba >> DB_COLOR_SHIFT_R) & 255U) / DB_U8_MAX_F;
    *out_green = (float)((rgba >> DB_COLOR_SHIFT_G) & 255U) / DB_U8_MAX_F;
    *out_blue = (float)((rgba >> DB_COLOR_SHIFT_B) & 255U) / DB_U8_MAX_F;
}

static void db_bo_fill_solid(db_cpu_bo_t *bo, uint32_t rgba) {
    const uint64_t pixel_count = (uint64_t)bo->width * (uint64_t)bo->height;
    for (uint64_t idx = 0U; idx < pixel_count; idx++) {
        bo->pixels_rgba8[idx] = rgba;
    }
}

static void db_bo_copy(db_cpu_bo_t *dst, const db_cpu_bo_t *src) {
    const uint64_t pixel_count = (uint64_t)dst->width * (uint64_t)dst->height;
    for (uint64_t idx = 0U; idx < pixel_count; idx++) {
        dst->pixels_rgba8[idx] = src->pixels_rgba8[idx];
    }
}

static size_t db_grid_index(uint32_t row, uint32_t col, uint32_t cols) {
    return ((size_t)row * (size_t)cols) + (size_t)col;
}

static void db_render_bands(db_cpu_bo_t *bo, double time_s) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        float band_red = 0.0F;
        float band_green = 0.0F;
        float band_blue = 0.0F;
        db_band_color_rgb(band, BENCH_BANDS, time_s, &band_red, &band_green,
                          &band_blue);
        const uint32_t color = db_pack_rgb(band_red, band_green, band_blue);
        const uint32_t x0 = (band * cols) / BENCH_BANDS;
        const uint32_t x1 = ((band + 1U) * cols) / BENCH_BANDS;
        for (uint32_t row = 0U; row < rows; row++) {
            const size_t row_base = (size_t)row * cols;
            for (uint32_t col = x0; col < x1; col++) {
                bo->pixels_rgba8[row_base + col] = color;
            }
        }
    }
}

static void db_render_snake_step(db_cpu_bo_t *write_bo,
                                 const db_cpu_bo_t *read_bo,
                                 const db_snake_plan_t *plan,
                                 const db_rect_snake_rect_t *rect,
                                 float target_red, float target_green,
                                 float target_blue,
                                 int full_fill_on_phase_completed) {
    if ((plan == NULL) || (rect == NULL)) {
        return;
    }
    if ((rect->width == 0U) || (rect->height == 0U)) {
        return;
    }

    const uint32_t cols = write_bo->width;
    const uint32_t rows = write_bo->height;
    const uint32_t target_rgba =
        db_pack_rgb(target_red, target_green, target_blue);
    if ((full_fill_on_phase_completed != 0) && (plan->phase_completed != 0)) {
        db_bo_fill_solid(write_bo, target_rgba);
        return;
    }

    for (uint32_t update_index = 0U; update_index < plan->prev_count;
         update_index++) {
        const uint32_t step = plan->prev_start + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(rect, step);
        const uint32_t row = tile_index / cols;
        const uint32_t col = tile_index % cols;
        if ((row >= rows) || (col >= cols)) {
            continue;
        }
        write_bo->pixels_rgba8[db_grid_index(row, col, cols)] = target_rgba;
    }

    for (uint32_t update_index = 0U; update_index < plan->batch_size;
         update_index++) {
        const uint32_t step = plan->active_cursor + update_index;
        if (step >= plan->rect_tile_count) {
            break;
        }
        const uint32_t tile_index =
            db_rect_snake_tile_index_from_step(rect, step);
        const uint32_t row = tile_index / cols;
        const uint32_t col = tile_index % cols;
        if ((row >= rows) || (col >= cols)) {
            continue;
        }
        const size_t idx = db_grid_index(row, col, cols);
        float prior_red = 0.0F;
        float prior_green = 0.0F;
        float prior_blue = 0.0F;
        db_unpack_rgb(read_bo->pixels_rgba8[idx], &prior_red, &prior_green,
                      &prior_blue);
        const float blend =
            db_window_blend_factor(update_index, plan->batch_size);
        float out_red = 0.0F;
        float out_green = 0.0F;
        float out_blue = 0.0F;
        db_blend_rgb(prior_red, prior_green, prior_blue, target_red,
                     target_green, target_blue, blend, &out_red, &out_green,
                     &out_blue);
        write_bo->pixels_rgba8[idx] = db_pack_rgb(out_red, out_green, out_blue);
    }
}

static void db_render_gradient(db_cpu_bo_t *bo, uint32_t head_row,
                               int direction_down, uint32_t cycle_index) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    for (uint32_t row = 0U; row < rows; row++) {
        float row_red = 0.0F;
        float row_green = 0.0F;
        float row_blue = 0.0F;
        db_gradient_row_color_rgb(row, head_row, direction_down, cycle_index,
                                  &row_red, &row_green, &row_blue);
        const uint32_t rgba = db_pack_rgb(row_red, row_green, row_blue);
        const size_t row_base = (size_t)row * cols;
        for (uint32_t col = 0U; col < cols; col++) {
            bo->pixels_rgba8[row_base + col] = rgba;
        }
    }
}

void db_renderer_cpu_renderer_init(void) {
    if (g_state.initialized != 0) {
        return;
    }

    db_pattern_vertex_init_t init_state = {0};
    if (!db_init_vertices_for_mode_common(BACKEND_NAME, &init_state)) {
        db_failf(BACKEND_NAME, "cpu renderer init failed");
    }

    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint64_t pixel_count = (uint64_t)grid_cols * (uint64_t)grid_rows;
    if ((pixel_count == 0U) ||
        (pixel_count > ((uint64_t)SIZE_MAX / sizeof(uint32_t)))) {
        free(init_state.vertices);
        db_failf(BACKEND_NAME, "invalid offscreen BO size: %ux%u", grid_cols,
                 grid_rows);
    }

    db_cpu_bo_t bos[2] = {
        {.width = grid_cols,
         .height = grid_rows,
         .pixels_rgba8 =
             (uint32_t *)calloc((size_t)pixel_count, sizeof(uint32_t))},
        {.width = grid_cols,
         .height = grid_rows,
         .pixels_rgba8 =
             (uint32_t *)calloc((size_t)pixel_count, sizeof(uint32_t))},
    };
    if ((bos[0].pixels_rgba8 == NULL) || (bos[1].pixels_rgba8 == NULL)) {
        free(bos[0].pixels_rgba8);
        free(bos[1].pixels_rgba8);
        free(init_state.vertices);
        db_failf(BACKEND_NAME, "failed to allocate offscreen BOs");
    }

    const uint32_t phase0 = db_pack_rgb(
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    db_bo_fill_solid(&bos[0], phase0);
    db_bo_fill_solid(&bos[1], phase0);

    g_state = (db_cpu_renderer_state_t){0};
    g_state.initialized = 1;
    g_state.state = init_state;
    g_state.bos[0] = bos[0];
    g_state.bos[1] = bos[1];
    g_state.history_mode = db_pattern_uses_history_texture(init_state.pattern);
    g_state.history_read_index = 0;
    g_state.snake_cursor = init_state.snake_cursor;
    g_state.snake_prev_start = init_state.snake_prev_start;
    g_state.snake_prev_count = init_state.snake_prev_count;
    g_state.mode_phase_flag = init_state.mode_phase_flag;
    g_state.gradient_head_row = init_state.gradient_head_row;
    g_state.gradient_cycle = init_state.gradient_cycle;
    g_state.snake_rect_index = 0U;
    g_state.rect_seed = init_state.pattern_seed;
}

void db_renderer_cpu_renderer_render_frame(double time_s) {
    if (g_state.initialized == 0) {
        return;
    }

    int write_index = 0;
    if (g_state.history_mode != 0) {
        write_index = (g_state.history_read_index == 0) ? 1 : 0;
        db_bo_copy(&g_state.bos[write_index],
                   &g_state.bos[g_state.history_read_index]);
    }

    db_cpu_bo_t *write_bo = &g_state.bos[write_index];
    const db_cpu_bo_t *read_bo = &g_state.bos[g_state.history_read_index];

    if (g_state.state.pattern == DB_PATTERN_BANDS) {
        db_render_bands(write_bo, time_s);
    } else if ((g_state.state.pattern == DB_PATTERN_SNAKE_GRID) ||
               (g_state.state.pattern == DB_PATTERN_RECT_SNAKE)) {
        const int is_grid = (g_state.state.pattern == DB_PATTERN_SNAKE_GRID);
        const db_snake_plan_request_t request = db_snake_plan_request_make(
            is_grid, g_state.rect_seed, g_state.snake_rect_index,
            g_state.snake_cursor, g_state.snake_prev_start,
            g_state.snake_prev_count, g_state.mode_phase_flag);
        const db_snake_plan_t plan = db_snake_plan_next_step(&request);
        db_rect_snake_rect_t rect = {0};
        float target_red = 0.0F;
        float target_green = 0.0F;
        float target_blue = 0.0F;
        int full_fill_on_phase_completed = 0;
        if (is_grid != 0) {
            rect = (db_rect_snake_rect_t){
                .x = 0U,
                .y = 0U,
                .width = db_grid_cols_effective(),
                .height = db_grid_rows_effective(),
                .color_r = 0.0F,
                .color_g = 0.0F,
                .color_b = 0.0F,
            };
            db_grid_target_color_rgb(plan.clearing_phase, &target_red,
                                     &target_green, &target_blue);
            full_fill_on_phase_completed = 1;
            g_state.mode_phase_flag = plan.next_clearing_phase;
        } else {
            rect = db_rect_snake_rect_from_index(g_state.rect_seed,
                                                 plan.active_rect_index);
            target_red = rect.color_r;
            target_green = rect.color_g;
            target_blue = rect.color_b;
            g_state.snake_rect_index = plan.next_rect_index;
        }
        db_render_snake_step(write_bo, read_bo, &plan, &rect, target_red,
                             target_green, target_blue,
                             full_fill_on_phase_completed);
        g_state.snake_cursor = plan.next_cursor;
        g_state.snake_prev_start = plan.next_prev_start;
        g_state.snake_prev_count = plan.next_prev_count;
    } else if ((g_state.state.pattern == DB_PATTERN_GRADIENT_SWEEP) ||
               (g_state.state.pattern == DB_PATTERN_GRADIENT_FILL)) {
        const int is_sweep =
            (g_state.state.pattern == DB_PATTERN_GRADIENT_SWEEP);
        const db_gradient_damage_plan_t plan = db_gradient_plan_next_frame(
            g_state.gradient_head_row, is_sweep ? g_state.mode_phase_flag : 1,
            g_state.gradient_cycle, is_sweep ? 0 : 1);
        db_render_gradient(write_bo, plan.render_head_row,
                           is_sweep ? plan.render_direction_down : 1,
                           plan.render_cycle_index);
        g_state.gradient_head_row = plan.next_head_row;
        g_state.mode_phase_flag = plan.next_direction_down;
        g_state.gradient_cycle = plan.next_cycle_index;
    }

    if (g_state.history_mode != 0) {
        g_state.history_read_index = write_index;
    }
}

const uint32_t *db_renderer_cpu_renderer_pixels_rgba8(uint32_t *out_width,
                                                      uint32_t *out_height) {
    if (g_state.initialized == 0) {
        return NULL;
    }
    if (out_width != NULL) {
        *out_width = g_state.bos[g_state.history_read_index].width;
    }
    if (out_height != NULL) {
        *out_height = g_state.bos[g_state.history_read_index].height;
    }
    return g_state.bos[g_state.history_read_index].pixels_rgba8;
}

uint32_t db_renderer_cpu_renderer_work_unit_count(void) {
    if (g_state.initialized == 0) {
        return 0U;
    }
    return g_state.state.work_unit_count;
}

const char *db_renderer_cpu_renderer_capability_mode(void) {
    return DB_CAP_MODE_CPU_OFFSCREEN_BO;
}

void db_renderer_cpu_renderer_shutdown(void) {
    if (g_state.initialized == 0) {
        return;
    }
    free(g_state.bos[0].pixels_rgba8);
    free(g_state.bos[1].pixels_rgba8);
    free(g_state.state.vertices);
    g_state = (db_cpu_renderer_state_t){0};
}
