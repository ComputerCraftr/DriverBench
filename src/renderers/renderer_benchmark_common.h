#ifndef DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H
#define DRIVERBENCH_RENDERER_BENCHMARK_COMMON_H

#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "../displays/bench_config.h"

#define DB_BAND_TRI_VERTS_PER_BAND 6U
#define DB_BAND_POS_FLOATS 2U
#define DB_BAND_COLOR_FLOATS 3U
#define DB_BAND_VERT_FLOATS (DB_BAND_POS_FLOATS + DB_BAND_COLOR_FLOATS)
#define DB_BENCHMARK_MODE_ENV "DRIVERBENCH_BENCHMARK_MODE"
#define DB_BENCHMARK_MODE_BANDS "bands"
#define DB_BENCHMARK_MODE_SNAKE_GRID "snake_grid"

typedef enum {
    DB_PATTERN_BANDS = 0,
    DB_PATTERN_SNAKE_GRID = 1,
} db_pattern_t;

typedef struct {
    uint32_t active_cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int clearing_phase;
    int phase_completed;
    uint32_t next_cursor;
    uint32_t next_prev_start;
    uint32_t next_prev_count;
    int next_clearing_phase;
} db_snake_damage_plan_t;

typedef struct {
    float *vertices;
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    uint32_t snake_cursor;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t snake_batch_size;
    int snake_phase_completed;
    int snake_clearing_phase;
} db_pattern_vertex_init_t;

static inline uint32_t db_snake_grid_rows_effective(void) {
    return (uint32_t)BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_snake_grid_cols_effective(void) {
    return (uint32_t)BENCH_WINDOW_WIDTH_PX;
}

static inline int
db_parse_benchmark_pattern_from_env(db_pattern_t *out_pattern) {
    const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
    if ((mode == NULL) || (strcmp(mode, DB_BENCHMARK_MODE_BANDS) == 0)) {
        *out_pattern = DB_PATTERN_BANDS;
        return 1;
    }
    if (strcmp(mode, DB_BENCHMARK_MODE_SNAKE_GRID) == 0) {
        *out_pattern = DB_PATTERN_SNAKE_GRID;
        return 1;
    }
    *out_pattern = DB_PATTERN_BANDS;
    return 0;
}

static inline uint32_t db_pattern_work_unit_count(db_pattern_t pattern) {
    if (pattern == DB_PATTERN_SNAKE_GRID) {
        const uint32_t rows = db_snake_grid_rows_effective();
        const uint32_t cols = db_snake_grid_cols_effective();
        const uint64_t count = (uint64_t)rows * cols;
        if ((count == 0U) || (count > UINT32_MAX)) {
            return 0U;
        }
        return (uint32_t)count;
    }
    return BENCH_BANDS;
}

static inline uint32_t db_snake_grid_tile_index_from_step(uint32_t step) {
    const uint32_t cols = db_snake_grid_cols_effective();
    const uint32_t row = step / cols;
    const uint32_t col_step = step % cols;
    const uint32_t col = ((row & 1U) == 0U) ? col_step : (cols - 1U - col_step);
    return (row * cols) + col;
}

static inline void db_snake_grid_tile_bounds_ndc(uint32_t tile_index, float *x0,
                                                 float *y0, float *x1,
                                                 float *y1) {
    const uint32_t cols = db_snake_grid_cols_effective();
    const uint32_t rows = db_snake_grid_rows_effective();
    const uint32_t row = tile_index / cols;
    const uint32_t col = tile_index % cols;
    const float inv_cols = 1.0F / (float)cols;
    const float inv_rows = 1.0F / (float)rows;

    *x0 = (2.0F * (float)col * inv_cols) - 1.0F;
    *x1 = (2.0F * (float)(col + 1U) * inv_cols) - 1.0F;
    *y1 = 1.0F - (2.0F * (float)row * inv_rows);
    *y0 = 1.0F - (2.0F * (float)(row + 1U) * inv_rows);
}

static inline void db_fill_rect_unit_pos(float *unit_base, float x0, float y0,
                                         float x1, float y1,
                                         size_t stride_floats) {
    // Triangle 1
    unit_base[0] = x0;
    unit_base[1] = y0;
    unit_base[stride_floats] = x1;
    unit_base[stride_floats + 1U] = y0;
    unit_base[2U * stride_floats] = x1;
    unit_base[(2U * stride_floats) + 1U] = y1;

    // Triangle 2
    unit_base[3U * stride_floats] = x0;
    unit_base[(3U * stride_floats) + 1U] = y0;
    unit_base[4U * stride_floats] = x1;
    unit_base[(4U * stride_floats) + 1U] = y1;
    unit_base[5U * stride_floats] = x0;
    unit_base[(5U * stride_floats) + 1U] = y1;
}

static inline void db_set_rect_unit_rgb(float *unit_base, size_t stride_floats,
                                        size_t color_offset_floats,
                                        float color_r, float color_g,
                                        float color_b) {
    for (uint32_t v = 0; v < DB_BAND_TRI_VERTS_PER_BAND; v++) {
        float *color =
            &unit_base[((size_t)v * stride_floats) + color_offset_floats];
        color[0] = color_r;
        color[1] = color_g;
        color[2] = color_b;
    }
}

static inline void db_band_color_rgb(uint32_t band_index, uint32_t band_count,
                                     double time_s, float *out_r, float *out_g,
                                     float *out_b) {
    const float band_f = (float)band_index;
    const float pulse =
        BENCH_PULSE_BASE_F +
        (BENCH_PULSE_AMP_F * sinf((float)((time_s * BENCH_PULSE_FREQ_F) +
                                          (band_f * BENCH_PULSE_PHASE_F))));
    const float color_r =
        pulse * (BENCH_COLOR_R_BASE_F +
                 (BENCH_COLOR_R_SCALE_F * band_f / (float)band_count));
    *out_r = color_r;
    *out_g = pulse * BENCH_COLOR_G_SCALE_F;
    *out_b = 1.0F - color_r;
}

static inline uint32_t db_snake_grid_tiles_per_step(uint32_t work_unit_count) {
    if (work_unit_count == 0U) {
        return 1U;
    }
    uint32_t tiles_per_step = BENCH_SNAKE_PHASE_WINDOW_TILES;
    if (tiles_per_step == 0U) {
        tiles_per_step = 1U;
    }
    if (tiles_per_step > work_unit_count) {
        tiles_per_step = work_unit_count;
    }
    return tiles_per_step;
}

static inline uint32_t db_snake_grid_step_batch_size(uint32_t cursor,
                                                     uint32_t work_unit_count,
                                                     uint32_t tiles_per_step) {
    if ((work_unit_count == 0U) || (cursor >= work_unit_count)) {
        return 0U;
    }
    const uint32_t remaining = work_unit_count - cursor;
    return (tiles_per_step < remaining) ? tiles_per_step : remaining;
}

static inline db_snake_damage_plan_t
db_snake_grid_plan_next_step(uint32_t snake_cursor, uint32_t snake_prev_start,
                             uint32_t snake_prev_count, int clearing_phase,
                             uint32_t work_unit_count) {
    db_snake_damage_plan_t plan = {0};
    plan.active_cursor = snake_cursor;
    plan.prev_start = snake_prev_start;
    plan.prev_count = snake_prev_count;
    plan.clearing_phase = clearing_phase;

    const uint32_t tiles_per_step =
        db_snake_grid_tiles_per_step(work_unit_count);
    plan.batch_size = db_snake_grid_step_batch_size(
        snake_cursor, work_unit_count, tiles_per_step);
    plan.phase_completed =
        ((snake_cursor + plan.batch_size) >= work_unit_count) ? 1 : 0;

    plan.next_prev_start = snake_cursor;
    plan.next_prev_count = plan.phase_completed ? 0U : plan.batch_size;
    plan.next_cursor = snake_cursor + plan.batch_size;
    plan.next_clearing_phase = clearing_phase;
    if (plan.next_cursor >= work_unit_count) {
        plan.next_cursor = 0U;
        plan.next_clearing_phase = !clearing_phase;
    }
    return plan;
}

static inline void db_snake_grid_target_color_rgb(int clearing_phase,
                                                  float *out_r, float *out_g,
                                                  float *out_b) {
    if (clearing_phase) {
        *out_r = BENCH_GRID_PHASE0_R;
        *out_g = BENCH_GRID_PHASE0_G;
        *out_b = BENCH_GRID_PHASE0_B;
        return;
    }
    *out_r = BENCH_GRID_PHASE1_R;
    *out_g = BENCH_GRID_PHASE1_G;
    *out_b = BENCH_GRID_PHASE1_B;
}

static inline void db_snake_grid_window_color_rgb(uint32_t window_index,
                                                  uint32_t window_size,
                                                  int clearing_phase,
                                                  float *out_r, float *out_g,
                                                  float *out_b) {
    const uint32_t span = (window_size > 0U) ? window_size : 1U;
    const float blend_factor = (float)(span - window_index) / (float)span;
    const float from_r =
        clearing_phase ? BENCH_GRID_PHASE1_R : BENCH_GRID_PHASE0_R;
    const float from_g =
        clearing_phase ? BENCH_GRID_PHASE1_G : BENCH_GRID_PHASE0_G;
    const float from_b =
        clearing_phase ? BENCH_GRID_PHASE1_B : BENCH_GRID_PHASE0_B;
    float to_r = 0.0F;
    float to_g = 0.0F;
    float to_b = 0.0F;
    db_snake_grid_target_color_rgb(clearing_phase, &to_r, &to_g, &to_b);

    *out_r = from_r + ((to_r - from_r) * blend_factor);
    *out_g = from_g + ((to_g - from_g) * blend_factor);
    *out_b = from_b + ((to_b - from_b) * blend_factor);
}

static inline int
db_init_band_vertices_common(db_pattern_vertex_init_t *out_state) {
    const size_t vertex_count =
        (size_t)BENCH_BANDS * DB_BAND_TRI_VERTS_PER_BAND;
    const size_t float_count = vertex_count * DB_BAND_VERT_FLOATS;

    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    *out_state = (db_pattern_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->pattern = DB_PATTERN_BANDS;
    out_state->work_unit_count = BENCH_BANDS;
    out_state->draw_vertex_count = (uint32_t)vertex_count;
    return 1;
}

static inline int
db_init_snake_grid_vertices_common(db_pattern_vertex_init_t *out_state) {
    const uint64_t tile_count_u64 =
        (uint64_t)db_pattern_work_unit_count(DB_PATTERN_SNAKE_GRID);
    if ((tile_count_u64 == 0U) || (tile_count_u64 > UINT32_MAX)) {
        return 0;
    }

    const uint64_t vertex_count_u64 =
        tile_count_u64 * DB_BAND_TRI_VERTS_PER_BAND;
    if (vertex_count_u64 > (uint64_t)INT32_MAX) {
        return 0;
    }

    const uint64_t float_count_u64 = vertex_count_u64 * DB_BAND_VERT_FLOATS;
    if (float_count_u64 > ((uint64_t)SIZE_MAX / sizeof(float))) {
        return 0;
    }

    const size_t float_count = (size_t)float_count_u64;
    const uint32_t tile_count = (uint32_t)tile_count_u64;
    float *vertices = (float *)calloc(float_count, sizeof(float));
    if (vertices == NULL) {
        return 0;
    }

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        db_snake_grid_tile_bounds_ndc(tile_index, &x0, &y0, &x1, &y1);
        const size_t base = (size_t)tile_index * DB_BAND_TRI_VERTS_PER_BAND *
                            DB_BAND_VERT_FLOATS;
        float *unit = &vertices[base];
        db_fill_rect_unit_pos(unit, x0, y0, x1, y1, DB_BAND_VERT_FLOATS);
        db_set_rect_unit_rgb(unit, DB_BAND_VERT_FLOATS, DB_BAND_POS_FLOATS,
                             BENCH_GRID_PHASE0_R, BENCH_GRID_PHASE0_G,
                             BENCH_GRID_PHASE0_B);
    }

    *out_state = (db_pattern_vertex_init_t){0};
    out_state->vertices = vertices;
    out_state->pattern = DB_PATTERN_SNAKE_GRID;
    out_state->work_unit_count = tile_count;
    out_state->draw_vertex_count = (uint32_t)vertex_count_u64;
    return 1;
}

static inline int
db_init_vertices_for_mode_common(const char *backend_name,
                                 db_pattern_vertex_init_t *out_state) {
    db_pattern_t requested = DB_PATTERN_BANDS;
    if (!db_parse_benchmark_pattern_from_env(&requested)) {
        const char *mode = getenv(DB_BENCHMARK_MODE_ENV);
        db_infof(backend_name, "Invalid %s='%s'; using '%s'",
                 DB_BENCHMARK_MODE_ENV, (mode != NULL) ? mode : "",
                 DB_BENCHMARK_MODE_BANDS);
    }
    if ((requested == DB_PATTERN_SNAKE_GRID) &&
        db_init_snake_grid_vertices_common(out_state)) {
        db_infof(backend_name,
                 "benchmark mode: %s (%ux%u tiles, deterministic snake sweep)",
                 DB_BENCHMARK_MODE_SNAKE_GRID, db_snake_grid_rows_effective(),
                 db_snake_grid_cols_effective());
        return 1;
    }

    if (requested == DB_PATTERN_SNAKE_GRID) {
        db_infof(
            backend_name,
            "snake_grid initialization failed; falling back to bands mode");
    }

    if (!db_init_band_vertices_common(out_state)) {
        return 0;
    }

    db_infof(backend_name, "benchmark mode: %s (%u vertical bands)",
             DB_BENCHMARK_MODE_BANDS, BENCH_BANDS);
    return 1;
}

static inline void db_fill_band_vertices_pos_rgb(float *out_vertices,
                                                 uint32_t band_count,
                                                 double time_s) {
    const float inv_band_count = 1.0F / (float)band_count;
    for (uint32_t band_index = 0; band_index < band_count; band_index++) {
        const float band_f = (float)band_index;
        const float x0 = ((2.0F * band_f) * inv_band_count) - 1.0F;
        const float x1 = ((2.0F * (band_f + 1.0F)) * inv_band_count) - 1.0F;
        float color_r = 0.0F;
        float color_g = 0.0F;
        float color_b = 0.0F;
        db_band_color_rgb(band_index, band_count, time_s, &color_r, &color_g,
                          &color_b);

        const size_t base_offset = (size_t)band_index *
                                   DB_BAND_TRI_VERTS_PER_BAND *
                                   DB_BAND_VERT_FLOATS;
        float *out = &out_vertices[base_offset];

        // Triangle 1
        *out++ = x0;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        // Triangle 2
        *out++ = x0;
        *out++ = -1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x1;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;

        *out++ = x0;
        *out++ = 1.0F;
        *out++ = color_r;
        *out++ = color_g;
        *out++ = color_b;
    }
}

#endif
