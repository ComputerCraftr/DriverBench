#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/db_core.h"
#include "../../displays/bench_config.h"
#include "../renderer_benchmark_common.h"

#define BACKEND_NAME "renderer_cpu_renderer"
#define RENDERER_NAME "cpu_renderer"
#define API_NAME "CPU"
#define DEFAULT_OFFSCREEN_FRAMES 600U
#define DB_OFFSCREEN_FRAMES_ENV "DRIVERBENCH_OFFSCREEN_FRAMES"
#define DB_OFFSCREEN_CAPABILITY_MODE "cpu_offscreen_bo"
#define DB_HASH_EVERY_FRAME_ENV "DRIVERBENCH_HASH_EVERY_FRAME"
#define DB_ALPHA_U8 255U
#define DB_U8_MAX_F 255.0F
#define DB_FNV1A64_OFFSET 1469598103934665603ULL
#define DB_FNV1A64_PRIME 1099511628211ULL

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels_rgba8;
} db_cpu_bo_t;

static uint32_t db_offscreen_frames_from_env(void) {
    const char *value = getenv(DB_OFFSCREEN_FRAMES_ENV);
    if (value == NULL) {
        return DEFAULT_OFFSCREEN_FRAMES;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    if ((end == value) || (end == NULL) || (*end != '\0') || (parsed == 0UL) ||
        (parsed > UINT32_MAX)) {
        db_failf(BACKEND_NAME, "Invalid %s='%s'", DB_OFFSCREEN_FRAMES_ENV,
                 value);
    }
    return (uint32_t)parsed;
}

static uint32_t db_channel_to_u8(float value) {
    float clamped = value;
    if (clamped < 0.0F) {
        clamped = 0.0F;
    } else if (clamped > 1.0F) {
        clamped = 1.0F;
    }
    return (uint32_t)(clamped * DB_U8_MAX_F + 0.5F);
}

static uint32_t db_pack_rgb(float r, float g, float b) {
    const uint32_t ru = db_channel_to_u8(r);
    const uint32_t gu = db_channel_to_u8(g);
    const uint32_t bu = db_channel_to_u8(b);
    return (DB_ALPHA_U8 << 24U) | (bu << 16U) | (gu << 8U) | ru;
}

static void db_unpack_rgb(uint32_t rgba, float *r, float *g, float *b) {
    *r = (float)(rgba & 255U) / DB_U8_MAX_F;
    *g = (float)((rgba >> 8U) & 255U) / DB_U8_MAX_F;
    *b = (float)((rgba >> 16U) & 255U) / DB_U8_MAX_F;
}

static void db_bo_fill_solid(db_cpu_bo_t *bo, uint32_t rgba) {
    const uint64_t pixel_count = (uint64_t)bo->width * (uint64_t)bo->height;
    for (uint64_t i = 0U; i < pixel_count; i++) {
        bo->pixels_rgba8[i] = rgba;
    }
}

static void db_bo_copy(db_cpu_bo_t *dst, const db_cpu_bo_t *src) {
    const uint64_t pixel_count = (uint64_t)dst->width * (uint64_t)dst->height;
    memcpy(dst->pixels_rgba8, src->pixels_rgba8,
           (size_t)(pixel_count * sizeof(uint32_t)));
}

static uint64_t db_bo_hash(const db_cpu_bo_t *bo) {
    const uint64_t pixel_count = (uint64_t)bo->width * (uint64_t)bo->height;
    const uint8_t *bytes = (const uint8_t *)bo->pixels_rgba8;
    uint64_t hash = DB_FNV1A64_OFFSET;
    const uint64_t byte_count = pixel_count * sizeof(uint32_t);
    for (uint64_t i = 0U; i < byte_count; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= DB_FNV1A64_PRIME;
    }
    return hash;
}

static size_t db_grid_index(uint32_t row, uint32_t col, uint32_t cols) {
    return (size_t)((row * cols) + col);
}

static void db_render_bands(db_cpu_bo_t *bo, double time_s) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    if ((cols == 0U) || (rows == 0U)) {
        return;
    }

    for (uint32_t band = 0U; band < BENCH_BANDS; band++) {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        db_band_color_rgb(band, BENCH_BANDS, time_s, &r, &g, &b);
        const uint32_t color = db_pack_rgb(r, g, b);
        const uint32_t x0 = (band * cols) / BENCH_BANDS;
        const uint32_t x1 = ((band + 1U) * cols) / BENCH_BANDS;
        for (uint32_t y = 0U; y < rows; y++) {
            const size_t row_base = (size_t)y * cols;
            for (uint32_t x = x0; x < x1; x++) {
                bo->pixels_rgba8[row_base + x] = color;
            }
        }
    }
}

static void db_render_snake_grid(db_cpu_bo_t *write_bo,
                                 const db_cpu_bo_t *read_bo,
                                 const db_snake_damage_plan_t *plan,
                                 uint32_t work_unit_count) {
    float target_r = 0.0F;
    float target_g = 0.0F;
    float target_b = 0.0F;
    db_snake_grid_target_color_rgb(plan->clearing_phase, &target_r, &target_g,
                                   &target_b);
    const uint32_t target_rgba = db_pack_rgb(target_r, target_g, target_b);

    if (plan->phase_completed != 0) {
        db_bo_fill_solid(write_bo, target_rgba);
        return;
    }

    const uint32_t cols = write_bo->width;
    const uint32_t rows = write_bo->height;
    for (uint32_t step = 0U; step < plan->active_cursor; step++) {
        if (step >= work_unit_count) {
            break;
        }
        const uint32_t tile = db_grid_tile_index_from_step(step);
        const uint32_t row = tile / cols;
        const uint32_t col = tile % cols;
        if (row >= rows) {
            break;
        }
        write_bo->pixels_rgba8[db_grid_index(row, col, cols)] = target_rgba;
    }

    for (uint32_t i = 0U; i < plan->batch_size; i++) {
        const uint32_t step = plan->active_cursor + i;
        if (step >= work_unit_count) {
            break;
        }
        const uint32_t tile = db_grid_tile_index_from_step(step);
        const uint32_t row = tile / cols;
        const uint32_t col = tile % cols;
        if (row >= rows) {
            break;
        }
        const size_t idx = db_grid_index(row, col, cols);
        float prior_r = 0.0F;
        float prior_g = 0.0F;
        float prior_b = 0.0F;
        db_unpack_rgb(read_bo->pixels_rgba8[idx], &prior_r, &prior_g, &prior_b);
        const float blend = db_window_blend_factor(i, plan->batch_size);
        float out_r = 0.0F;
        float out_g = 0.0F;
        float out_b = 0.0F;
        db_blend_rgb(prior_r, prior_g, prior_b, target_r, target_g, target_b,
                     blend, &out_r, &out_g, &out_b);
        write_bo->pixels_rgba8[idx] = db_pack_rgb(out_r, out_g, out_b);
    }
}

static void db_render_rect_snake(db_cpu_bo_t *write_bo,
                                 const db_cpu_bo_t *read_bo,
                                 const db_rect_snake_plan_t *plan,
                                 uint32_t rect_seed) {
    const db_rect_snake_rect_t rect =
        db_rect_snake_rect_from_index(rect_seed, plan->active_rect_index);
    if ((rect.width == 0U) || (rect.height == 0U)) {
        return;
    }

    const uint32_t cols = write_bo->width;
    const uint32_t rows = write_bo->height;
    const uint32_t target_rgba =
        db_pack_rgb(rect.color_r, rect.color_g, rect.color_b);
    const uint32_t batch_end = plan->active_cursor + plan->batch_size;

    for (uint32_t local_row = 0U; local_row < rect.height; local_row++) {
        const uint32_t row = rect.y + local_row;
        if (row >= rows) {
            break;
        }
        for (uint32_t local_col = 0U; local_col < rect.width; local_col++) {
            const uint32_t col = rect.x + local_col;
            if (col >= cols) {
                break;
            }

            const uint32_t snake_col = ((local_row & 1U) == 0U)
                                           ? local_col
                                           : ((rect.width - 1U) - local_col);
            const uint32_t step = (local_row * rect.width) + snake_col;
            const size_t idx = db_grid_index(row, col, cols);

            if (step < plan->active_cursor) {
                write_bo->pixels_rgba8[idx] = target_rgba;
                continue;
            }
            if (step >= batch_end) {
                continue;
            }

            float prior_r = 0.0F;
            float prior_g = 0.0F;
            float prior_b = 0.0F;
            db_unpack_rgb(read_bo->pixels_rgba8[idx], &prior_r, &prior_g,
                          &prior_b);
            const uint32_t window_index = step - plan->active_cursor;
            const float blend =
                db_window_blend_factor(window_index, plan->batch_size);
            float out_r = 0.0F;
            float out_g = 0.0F;
            float out_b = 0.0F;
            db_blend_rgb(prior_r, prior_g, prior_b, rect.color_r, rect.color_g,
                         rect.color_b, blend, &out_r, &out_g, &out_b);
            write_bo->pixels_rgba8[idx] = db_pack_rgb(out_r, out_g, out_b);
        }
    }
}

static void
db_render_gradient_sweep(db_cpu_bo_t *bo,
                         const db_gradient_sweep_damage_plan_t *plan) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    for (uint32_t row = 0U; row < rows; row++) {
        float row_r = 0.0F;
        float row_g = 0.0F;
        float row_b = 0.0F;
        db_gradient_sweep_row_color_rgb(
            row, plan->render_head_row, plan->render_direction_down,
            plan->render_cycle_index, &row_r, &row_g, &row_b);
        const uint32_t rgba = db_pack_rgb(row_r, row_g, row_b);
        const size_t row_base = (size_t)row * cols;
        for (uint32_t col = 0U; col < cols; col++) {
            bo->pixels_rgba8[row_base + col] = rgba;
        }
    }
}

static void
db_render_gradient_fill(db_cpu_bo_t *bo,
                        const db_gradient_fill_damage_plan_t *plan) {
    const uint32_t cols = bo->width;
    const uint32_t rows = bo->height;
    for (uint32_t row = 0U; row < rows; row++) {
        float row_r = 0.0F;
        float row_g = 0.0F;
        float row_b = 0.0F;
        db_gradient_fill_row_color_rgb(row, plan->render_head_row,
                                       plan->render_cycle_index, &row_r, &row_g,
                                       &row_b);
        const uint32_t rgba = db_pack_rgb(row_r, row_g, row_b);
        const size_t row_base = (size_t)row * cols;
        for (uint32_t col = 0U; col < cols; col++) {
            bo->pixels_rgba8[row_base + col] = rgba;
        }
    }
}

int main(void) {
    db_install_signal_handlers();

    db_pattern_vertex_init_t state = {0};
    if (!db_init_vertices_for_mode_common(BACKEND_NAME, &state)) {
        db_failf(BACKEND_NAME, "cpu renderer init failed");
    }

    const uint32_t frame_limit = db_offscreen_frames_from_env();
    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    const uint64_t pixel_count = (uint64_t)grid_cols * (uint64_t)grid_rows;
    if ((pixel_count == 0U) || (pixel_count > SIZE_MAX / sizeof(uint32_t))) {
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
        free(state.vertices);
        db_failf(BACKEND_NAME, "failed to allocate offscreen BOs");
    }

    const uint32_t phase0 = db_pack_rgb(
        BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G, BENCH_GRID_PHASE0_B);
    db_bo_fill_solid(&bos[0], phase0);
    db_bo_fill_solid(&bos[1], phase0);

    const int history_mode = db_pattern_uses_history_texture(state.pattern);
    const int hash_every_frame = db_env_is_truthy(DB_HASH_EVERY_FRAME_ENV);

    uint64_t frames = 0U;
    double next_progress_log_due_ms = 0.0;
    uint64_t aggregate_hash = DB_FNV1A64_OFFSET;

    uint32_t snake_cursor = state.snake_cursor;
    uint32_t snake_prev_start = state.snake_prev_start;
    uint32_t snake_prev_count = state.snake_prev_count;
    int snake_clearing_phase = state.snake_clearing_phase;
    uint32_t gradient_head_row = state.gradient_head_row;
    int gradient_sweep_direction_down = state.gradient_sweep_direction_down;
    uint32_t gradient_sweep_cycle = state.gradient_sweep_cycle;
    uint32_t gradient_fill_cycle = state.gradient_fill_cycle;
    uint32_t rect_snake_index = 0U;
    uint32_t rect_snake_cursor = 0U;
    const uint32_t rect_seed = state.rect_snake_seed;
    int history_read_index = 0;

    for (uint32_t frame = 0U; (frame < frame_limit) && !db_should_stop();
         frame++) {
        const double time_s = (double)frame / BENCH_TARGET_FPS_D;

        int write_index = 0;
        if (history_mode) {
            write_index = (history_read_index == 0) ? 1 : 0;
            db_bo_copy(&bos[write_index], &bos[history_read_index]);
        }

        db_cpu_bo_t *write_bo = &bos[write_index];
        const db_cpu_bo_t *read_bo = &bos[history_read_index];

        if (state.pattern == DB_PATTERN_BANDS) {
            db_render_bands(write_bo, time_s);
        } else if (state.pattern == DB_PATTERN_SNAKE_GRID) {
            const db_snake_damage_plan_t plan = db_snake_grid_plan_next_step(
                snake_cursor, snake_prev_start, snake_prev_count,
                snake_clearing_phase, state.work_unit_count);
            db_render_snake_grid(write_bo, read_bo, &plan,
                                 state.work_unit_count);
            snake_cursor = plan.next_cursor;
            snake_prev_start = plan.next_prev_start;
            snake_prev_count = plan.next_prev_count;
            snake_clearing_phase = plan.next_clearing_phase;
        } else if (state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
            const db_gradient_sweep_damage_plan_t plan =
                db_gradient_sweep_plan_next_frame(gradient_head_row,
                                                  gradient_sweep_direction_down,
                                                  gradient_sweep_cycle);
            db_render_gradient_sweep(write_bo, &plan);
            gradient_head_row = plan.next_head_row;
            gradient_sweep_direction_down = plan.next_direction_down;
            gradient_sweep_cycle = plan.next_cycle_index;
        } else if (state.pattern == DB_PATTERN_GRADIENT_FILL) {
            const db_gradient_fill_damage_plan_t plan =
                db_gradient_fill_plan_next_frame(gradient_head_row,
                                                 gradient_fill_cycle);
            db_render_gradient_fill(write_bo, &plan);
            gradient_head_row = plan.next_head_row;
            gradient_fill_cycle = plan.next_cycle_index;
        } else if (state.pattern == DB_PATTERN_RECT_SNAKE) {
            const db_rect_snake_plan_t plan = db_rect_snake_plan_next_step(
                rect_seed, rect_snake_index, rect_snake_cursor);
            db_render_rect_snake(write_bo, read_bo, &plan, rect_seed);
            rect_snake_index = plan.next_rect_index;
            rect_snake_cursor = plan.next_cursor;
        }

        if (history_mode) {
            history_read_index = write_index;
        }

        const uint64_t frame_hash = db_bo_hash(&bos[history_read_index]);
        aggregate_hash ^= frame_hash;
        aggregate_hash *= DB_FNV1A64_PRIME;

        if (hash_every_frame != 0) {
            db_infof(BACKEND_NAME, "frame=%u hash=0x%016llx", frame,
                     (unsigned long long)frame_hash);
        }

        frames++;
        const double elapsed_ms =
            ((double)frames * BENCH_MS_PER_SEC_D) / BENCH_TARGET_FPS_D;
        db_benchmark_log_periodic(
            API_NAME, RENDERER_NAME, BACKEND_NAME, frames,
            state.work_unit_count, elapsed_ms, DB_OFFSCREEN_CAPABILITY_MODE,
            &next_progress_log_due_ms, BENCH_LOG_INTERVAL_MS_D);
    }

    const double total_ms =
        ((double)frames * BENCH_MS_PER_SEC_D) / BENCH_TARGET_FPS_D;
    db_benchmark_log_final(API_NAME, RENDERER_NAME, BACKEND_NAME, frames,
                           state.work_unit_count, total_ms,
                           DB_OFFSCREEN_CAPABILITY_MODE);

    const uint64_t final_hash = db_bo_hash(&bos[history_read_index]);
    db_infof(BACKEND_NAME,
             "bo_hash_final=0x%016llx bo_hash_aggregate=0x%016llx grid=%ux%u",
             (unsigned long long)final_hash, (unsigned long long)aggregate_hash,
             grid_rows, grid_cols);

    free(bos[0].pixels_rgba8);
    free(bos[1].pixels_rgba8);
    free(state.vertices);
    return EXIT_SUCCESS;
}
