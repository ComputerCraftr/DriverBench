#include <stdint.h>
#include <stdlib.h>

#include "../../core/db_core.h"
#include "../../renderers/renderer_benchmark_common.h"
#include "../bench_config.h"

#define BACKEND_NAME "display_offscreen_sanitizer"
#define RENDERER_NAME "offscreen_sanitizer"
#define API_NAME "Offscreen"
#define DEFAULT_OFFSCREEN_FRAMES 600U
#define DB_OFFSCREEN_FRAMES_ENV "DRIVERBENCH_OFFSCREEN_FRAMES"
#define DB_OFFSCREEN_CAPABILITY_MODE "offscreen_cpu"

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

int main(void) {
    db_install_signal_handlers();

    db_pattern_vertex_init_t state = {0};
    if (!db_init_vertices_for_mode_common(BACKEND_NAME, &state)) {
        db_failf(BACKEND_NAME, "offscreen init failed");
    }

    const uint32_t frame_limit = db_offscreen_frames_from_env();
    const uint32_t grid_cols = db_grid_cols_effective();
    const uint32_t grid_rows = db_grid_rows_effective();
    uint64_t frames = 0U;
    double next_progress_log_due_ms = 0.0;
    volatile uint64_t checksum = 0U;

    uint32_t snake_cursor = state.snake_cursor;
    uint32_t snake_prev_start = state.snake_prev_start;
    uint32_t snake_prev_count = state.snake_prev_count;
    int snake_clearing_phase = state.snake_clearing_phase;
    uint32_t gradient_head_row = state.gradient_head_row;
    uint32_t rect_snake_index = 0U;
    uint32_t rect_snake_cursor = 0U;
    const uint32_t rect_seed = state.rect_snake_seed;

    for (uint32_t frame = 0U; (frame < frame_limit) && !db_should_stop();
         frame++) {
        const double time_s = (double)frame / BENCH_TARGET_FPS_D;
        if (state.pattern == DB_PATTERN_BANDS) {
            db_fill_band_vertices_pos_rgb(state.vertices, state.work_unit_count,
                                          time_s);
            checksum ^= (uint64_t)state.vertices[0];
        } else if (state.pattern == DB_PATTERN_SNAKE_GRID) {
            const db_snake_damage_plan_t plan = db_snake_grid_plan_next_step(
                snake_cursor, snake_prev_start, snake_prev_count,
                snake_clearing_phase, state.work_unit_count);
            snake_cursor = plan.next_cursor;
            snake_prev_start = plan.next_prev_start;
            snake_prev_count = plan.next_prev_count;
            snake_clearing_phase = plan.next_clearing_phase;
            checksum +=
                (uint64_t)plan.active_cursor + (uint64_t)plan.batch_size;
        } else if (state.pattern == DB_PATTERN_GRADIENT_SWEEP) {
            const db_gradient_sweep_damage_plan_t plan =
                db_gradient_sweep_plan_next_frame(gradient_head_row);
            gradient_head_row = plan.next_head_row;
            checksum +=
                (uint64_t)plan.render_head_row + (uint64_t)plan.dirty_row_count;
        } else if (state.pattern == DB_PATTERN_GRADIENT_FILL) {
            const db_gradient_fill_damage_plan_t plan =
                db_gradient_fill_plan_next_frame(gradient_head_row,
                                                 snake_clearing_phase);
            gradient_head_row = plan.next_head_row;
            snake_clearing_phase = plan.next_clearing_phase;
            checksum +=
                (uint64_t)plan.render_head_row + (uint64_t)plan.dirty_row_count;
        } else if (state.pattern == DB_PATTERN_RECT_SNAKE) {
            const db_rect_snake_plan_t plan = db_rect_snake_plan_next_step(
                rect_seed, rect_snake_index, rect_snake_cursor);
            const db_rect_snake_rect_t rect = db_rect_snake_rect_from_index(
                rect_seed, plan.active_rect_index);
            db_snake_col_span_t
                spans[(size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U];
            size_t span_count = 0U;
            db_snake_append_step_spans_for_rect(
                spans, (size_t)BENCH_SNAKE_PHASE_WINDOW_TILES * (size_t)2U,
                &span_count, rect.x, rect.y, rect.width, rect.height,
                plan.active_cursor, plan.batch_size);
            rect_snake_index = plan.next_rect_index;
            rect_snake_cursor = plan.next_cursor;
            checksum += (uint64_t)plan.active_rect_index +
                        (uint64_t)plan.batch_size + (uint64_t)span_count;
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
    db_infof(BACKEND_NAME, "checksum=%llu grid=%ux%u",
             (unsigned long long)checksum, grid_rows, grid_cols);

    free(state.vertices);
    return EXIT_SUCCESS;
}
