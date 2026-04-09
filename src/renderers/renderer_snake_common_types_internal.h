#ifndef DRIVERBENCH_RENDERER_SNAKE_COMMON_TYPES_INTERNAL_H
#define DRIVERBENCH_RENDERER_SNAKE_COMMON_TYPES_INTERNAL_H

#include "renderer_benchmark_types.h"
#include "renderer_snake_shape_common.h"

#define DB_SNAKE_COMMON_BACKEND "renderer_snake_common"
#define DB_SNAKE_COMMON_COLOR_BIAS 0.20
#define DB_SNAKE_COMMON_COLOR_SCALE 0.75
#define DB_SNAKE_CURSOR_PRE_ENTRY UINT32_MAX
#define DB_SNAKE_REGION_MAX_DIM_DIVISOR 3U
#define DB_SNAKE_REGION_MIN_DIM_LARGE 8U
#define DB_SNAKE_REGION_MIN_DIM_SMALL 1U
#define DB_SNAKE_REGION_MIN_DIM_THRESHOLD 16U
#define DB_SNAKE_REGION_SALT_HEIGHT 0x63D83595U
#define DB_SNAKE_REGION_SALT_ORIGIN_X DB_U32_GOLDEN_RATIO
#define DB_SNAKE_REGION_SALT_ORIGIN_Y DB_U32_SALT_ORIGIN_Y

typedef struct {
    uint32_t active_shape_index;
    uint32_t active_cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    uint32_t batch_size;
    int phase_flag;
    int phase_completed;
    uint32_t next_prev_start;
    uint32_t next_prev_count;
    int next_phase_flag;
    uint32_t target_tile_count;
    uint32_t next_shape_index;
    uint32_t next_cursor;
    int wrapped;
} db_snake_plan_t;

typedef struct {
    int is_grid_mode;
    uint32_t seed;
    uint32_t shape_index;
    uint32_t cursor;
    uint32_t prev_start;
    uint32_t prev_count;
    int phase_flag;
    uint32_t speed_step;
} db_snake_plan_request_t;

typedef struct {
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    double *active_prior_rgb;
    uint32_t batch_limit;
} db_snake_active_batch_t;

typedef enum {
    DB_SNAKE_RGB_SINK_PIXEL_SURFACE_DIRECT = 0,
    DB_SNAKE_RGB_SINK_PIXEL_SURFACE_PROJECTED = 1,
    DB_SNAKE_RGB_SINK_TILE_RGB_F32 = 2,
} db_snake_rgb_sink_kind_t;

typedef struct {
    db_snake_rgb_sink_kind_t kind;
    uint32_t logical_cols;
    uint32_t logical_rows;
    db_benchmark_pixel_surface_t pixel_surface;
    db_benchmark_pixel_surface_t mirror_pixel_surface;
    int mirror_pixel_surface_enabled;
    float *tile_rgb_f32;
    uint32_t tile_count;
} db_snake_rgb_sink_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
    uint32_t color_bits[3];
} db_snake_compact_block_t;

typedef struct {
    db_grid_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_grid_block_t open_block;
    int open_block_valid;
} db_snake_damage_block_collect_ctx_t;

typedef int (*db_snake_emit_row_segment_cb_t)(uint32_t row, uint32_t col_start,
                                              uint32_t col_end,
                                              void *user_data);

typedef void (*db_snake_get_color_bits_cb_t)(uint32_t row, uint32_t col,
                                             void *user_data,
                                             uint32_t *color_bits);

typedef int (*db_snake_emit_color_run_cb_t)(uint32_t row, uint32_t col_start,
                                            uint32_t col_count,
                                            const uint32_t *color_bits,
                                            void *user_data);

typedef struct {
    db_snake_compact_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_snake_compact_block_t *open_block;
    int *open_block_valid;
} db_snake_compact_block_collect_ctx_t;

typedef struct {
    uint32_t cols;
    uint32_t rows;
    db_snake_get_color_bits_cb_t get_color_bits;
    void *color_user_data;
    db_snake_compact_block_t *out_blocks;
    size_t out_capacity;
    size_t *out_count;
    db_snake_compact_block_t open_block;
    int open_block_valid;
} db_snake_compact_block_row_segment_collect_ctx_t;

typedef struct {
    db_snake_damage_block_collect_ctx_t *damage_ctx;
    db_snake_compact_block_row_segment_collect_ctx_t *compact_ctx;
} db_snake_dual_block_collect_ctx_t;

typedef struct {
    db_snake_emit_row_segment_cb_t emit;
    void *user_data;
    uint32_t open_row;
    uint32_t open_col_start;
    uint32_t open_col_end;
    int open_valid;
} db_snake_row_segment_emit_state_t;

static inline int db_snake_collect_damage_blocks_row_segment_cb(
    uint32_t row, uint32_t col_start, uint32_t col_end, void *user_data);

typedef struct {
    db_snake_region_t region;
    double target_rgb[3];
    int force_full_fill_on_phase_complete;
    int has_next_phase_flag;
    int next_phase_flag;
    int has_next_shape_index;
    uint32_t next_shape_index;
    db_snake_shape_kind_t shape_kind;
} db_snake_step_target_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    db_snake_shape_kind_t shape_kind;
    int is_grid_mode;
    int is_shapes_mode;
} db_snake_step_eval_t;

typedef struct {
    uint32_t tile_index;
    uint32_t row;
    uint32_t col;
} db_snake_step_tile_t;

typedef struct {
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    double *active_prior_rgb;
    size_t active_tile_capacity;
} db_snake_active_tile_scratch_t;

typedef int (*db_snake_tile_rgb_read_fn_t)(uint32_t tile_index, void *user_data,
                                           double *out_rgb);

typedef void (*db_snake_tile_rgb_write_fn_t)(uint32_t tile_index,
                                             const double *rgb,
                                             void *user_data);

enum db_snake_shape_profile_value_index_t {
    DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_X = 0U,
    DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_Y = 1U,
    DB_SNAKE_PROFILE_VAL_DIAMOND_RADIUS = 2U,
    DB_SNAKE_PROFILE_VAL_TRIANGLE_BOTTOM_WIDTH = 3U,
    DB_SNAKE_PROFILE_VAL_TRAPEZOID_TOP_WIDTH = 4U,
    DB_SNAKE_PROFILE_VAL_TRAPEZOID_BOTTOM_WIDTH = 5U,
    DB_SNAKE_PROFILE_VAL_RECT_HALF_WIDTH = 6U,
    DB_SNAKE_PROFILE_VAL_RECT_HALF_HEIGHT = 7U,
    DB_SNAKE_PROFILE_VAL_EXTENT_X = 8U,
    DB_SNAKE_PROFILE_VAL_EXTENT_Y = 9U,
    DB_SNAKE_PROFILE_VAL_ROTATE_COS = 10U,
    DB_SNAKE_PROFILE_VAL_ROTATE_SIN = 11U,
    DB_SNAKE_PROFILE_VAL_COUNT = 12U,
};

typedef struct {
    float values[DB_SNAKE_PROFILE_VAL_COUNT];
    uint32_t triangle_variant;
} db_snake_shape_profile_f32_t;

static inline db_snake_plan_request_t
db_snake_plan_request_make(int is_grid_mode, uint32_t seed,
                           uint32_t shape_index, uint32_t cursor,
                           uint32_t prev_start, uint32_t prev_count,
                           int phase_flag, uint32_t speed_step) {
    const db_snake_plan_request_t request = {
        .is_grid_mode = is_grid_mode,
        .seed = seed,
        .shape_index = shape_index,
        .cursor = cursor,
        .prev_start = prev_start,
        .prev_count = prev_count,
        .phase_flag = phase_flag,
        .speed_step = speed_step,
    };
    return request;
}

static inline size_t
db_snake_upload_range_capacity_needed(uint32_t settled_count,
                                      uint32_t active_count) {
    return (size_t)settled_count + (size_t)active_count;
}

static inline void
db_snake_shape_profile_to_f32(const db_snake_shape_profile_t *profile,
                              db_snake_shape_profile_f32_t *out_profile_f32) {
    if ((profile == NULL) || (out_profile_f32 == NULL)) {
        return;
    }
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_X] =
        db_double_to_f32(profile->circle_radius_x);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_CIRCLE_RADIUS_Y] =
        db_double_to_f32(profile->circle_radius_y);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_DIAMOND_RADIUS] =
        db_double_to_f32(profile->diamond_radius);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRIANGLE_BOTTOM_WIDTH] =
        db_double_to_f32(profile->triangle_bottom_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_TOP_WIDTH] =
        db_double_to_f32(profile->trapezoid_top_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_TRAPEZOID_BOTTOM_WIDTH] =
        db_double_to_f32(profile->trapezoid_bottom_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_RECT_HALF_WIDTH] =
        db_double_to_f32(profile->rect_half_width);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_RECT_HALF_HEIGHT] =
        db_double_to_f32(profile->rect_half_height);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_EXTENT_X] =
        db_double_to_f32(profile->extent_x);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_EXTENT_Y] =
        db_double_to_f32(profile->extent_y);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_ROTATE_COS] =
        db_double_to_f32(profile->rotate_cos);
    out_profile_f32->values[DB_SNAKE_PROFILE_VAL_ROTATE_SIN] =
        db_double_to_f32(profile->rotate_sin);
    out_profile_f32->triangle_variant = profile->triangle_variant;
}

static inline size_t
db_snake_plan_upload_range_capacity_needed(const db_snake_plan_t *plan) {
    if (plan == NULL) {
        return 0U;
    }
    return db_snake_upload_range_capacity_needed(plan->prev_count,
                                                 plan->batch_size);
}

static inline uint32_t db_snake_grid_rows_effective(void) {
    return BENCH_WINDOW_HEIGHT_PX;
}

static inline uint32_t db_snake_grid_cols_effective(void) {
    return BENCH_WINDOW_WIDTH_PX;
}

#endif
