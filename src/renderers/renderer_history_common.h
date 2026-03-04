#ifndef DRIVERBENCH_RENDERER_HISTORY_COMMON_H
#define DRIVERBENCH_RENDERER_HISTORY_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../core/db_buffer_convert.h"
#include "../core/db_numeric.h"
#include "renderer_benchmark_common.h"
#include "renderer_snake_common.h"
#include "renderer_snake_shape_common.h"

typedef struct {
    uint64_t state_hash;
    uint64_t full_draw_frames;
    uint64_t dirty_draw_frames;
    uint32_t frame_index;
} db_renderer_frame_stats_t;

typedef struct {
    int is_valid;
    int read_index;
} db_history_pair_state_t;

static inline db_history_pair_state_t
db_history_pair_state_make(int is_valid, int read_index) {
    return (db_history_pair_state_t){
        .is_valid = is_valid,
        .read_index = read_index,
    };
}

static inline void db_history_pair_state_reset(db_history_pair_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = db_history_pair_state_make(0, -1);
}

static inline void
db_history_pair_state_seeded(db_history_pair_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = db_history_pair_state_make(1, 0);
}

typedef struct {
    db_snake_col_span_t *spans;
    size_t span_capacity;
    db_snake_shape_row_bounds_t *row_bounds;
    size_t row_bounds_capacity;
} db_history_snake_scratch_t;

static inline int db_runtime_backbuffer_replay_enabled(
    const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return 0;
    }
    const int is_gradient = (runtime->pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                            (runtime->pattern == DB_PATTERN_GRADIENT_FILL);
    const int is_snake_history_texture =
        (runtime->pattern == DB_PATTERN_SNAKE_GRID) ||
        (runtime->pattern == DB_PATTERN_SNAKE_RECT) ||
        (runtime->pattern == DB_PATTERN_SNAKE_SHAPES);
    return (runtime->backbuffer_draw_full == 0) &&
           ((is_gradient != 0) || (is_snake_history_texture != 0));
}

static inline int db_runtime_uses_dirty_backbuffer_mode(
    const db_benchmark_runtime_init_t *runtime) {
    return (runtime != NULL) && (runtime->backbuffer_draw_full == 0);
}

static inline uint32_t
db_history_seed_frame_count_for_swapchain(int is_double_buffered) {
    return (is_double_buffered != 0) ? 2U : 1U;
}

static inline uint32_t
db_history_resync_frame_count_for_swapchain(int is_double_buffered) {
    return db_history_seed_frame_count_for_swapchain(is_double_buffered);
}

static inline int db_history_should_seed_backbuffer_now(
    int uses_dirty_backbuffer_mode, int initial_seed_done, int backbuffer_valid,
    uint32_t seed_frames_remaining) {
    return (uses_dirty_backbuffer_mode != 0) && (initial_seed_done == 0) &&
           (backbuffer_valid == 0) && (seed_frames_remaining > 0U);
}

static inline int db_history_should_queue_seed_backbuffer(
    int uses_dirty_backbuffer_mode, int initial_seed_done, int backbuffer_valid,
    uint32_t seed_frames_remaining) {
    return (uses_dirty_backbuffer_mode != 0) && (initial_seed_done == 0) &&
           (backbuffer_valid == 0) && (seed_frames_remaining == 0U);
}

static inline int db_history_should_force_full_upload_invalid_backbuffer(
    int uses_dirty_backbuffer_mode, int backbuffer_valid) {
    return (uses_dirty_backbuffer_mode != 0) && (backbuffer_valid == 0);
}

static inline int
db_history_should_force_full_upload_resync(int uses_dirty_backbuffer_mode,
                                           uint32_t resync_frames_remaining) {
    return (uses_dirty_backbuffer_mode != 0) && (resync_frames_remaining > 0U);
}

static inline int db_history_can_replay_previous_damage(
    int is_double_buffered, int uses_dirty_backbuffer_mode,
    int backbuffer_valid, size_t previous_upload_count) {
    return (is_double_buffered != 0) && (uses_dirty_backbuffer_mode != 0) &&
           (backbuffer_valid != 0) && (previous_upload_count > 0U);
}

static inline int
db_history_should_use_snake_history_scissor_pass(int uses_dirty_backbuffer_mode,
                                                 int snake_plan_valid) {
    return (uses_dirty_backbuffer_mode != 0) && (snake_plan_valid != 0);
}

static inline int db_history_should_use_snake_settled_scissor_base(
    int has_vbo, int is_grid_or_rect, int snake_transition_frame,
    int force_full_upload, int uses_dirty_backbuffer_mode,
    int has_span_scratch) {
    return (has_vbo != 0) && (is_grid_or_rect != 0) &&
           (snake_transition_frame == 0) && (force_full_upload == 0) &&
           (uses_dirty_backbuffer_mode != 0) && (has_span_scratch != 0);
}

static inline int db_history_should_use_snake_settled_scissor(
    int has_vbo, int is_grid_or_rect, int snake_transition_frame,
    int force_full_upload, int uses_dirty_backbuffer_mode, int has_span_scratch,
    int heuristic_ok) {
    return db_history_should_use_snake_settled_scissor_base(
               has_vbo, is_grid_or_rect, snake_transition_frame,
               force_full_upload, uses_dirty_backbuffer_mode,
               has_span_scratch) &&
           (heuristic_ok != 0);
}

static inline int db_history_should_reset_gradient_replay(db_pattern_t pattern,
                                                          int preserved) {
    const int is_gradient = (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                            (pattern == DB_PATTERN_GRADIENT_FILL);
    return (is_gradient != 0) && (preserved == 0);
}

typedef struct {
    int is_bands;
    int is_gradient;
    int is_gradient_fill;
    int is_gradient_sweep;
    int is_shape_snake;
    int is_snake_grid;
    int is_snake_rect;
    int is_snake_shapes;
    int is_snake_history_texture;
    int uses_dirty_backbuffer_mode;
    int uses_ff_rect_draw_mode;
    int uses_history_pipeline;
} db_history_pattern_mode_flags_t;

typedef db_history_pattern_mode_flags_t db_history_runtime_mode_flags_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    db_snake_shape_kind_t shape_kind;
    int is_grid_mode;
    int is_shapes_mode;
} db_history_snake_step_eval_t;

static inline db_history_pattern_mode_flags_t
db_history_pattern_mode_flags(db_pattern_t pattern) {
    db_history_pattern_mode_flags_t flags = {0};
    flags.is_bands = (pattern == DB_PATTERN_BANDS);
    flags.is_gradient = (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                        (pattern == DB_PATTERN_GRADIENT_FILL);
    flags.is_gradient_fill = (pattern == DB_PATTERN_GRADIENT_FILL);
    flags.is_gradient_sweep = (pattern == DB_PATTERN_GRADIENT_SWEEP);
    flags.is_snake_grid = (pattern == DB_PATTERN_SNAKE_GRID);
    flags.is_snake_rect = (pattern == DB_PATTERN_SNAKE_RECT);
    flags.is_snake_shapes = (pattern == DB_PATTERN_SNAKE_SHAPES);
    flags.is_shape_snake =
        (flags.is_snake_rect != 0) || (flags.is_snake_shapes != 0);
    flags.is_snake_history_texture = (flags.is_snake_grid != 0) ||
                                     (flags.is_snake_rect != 0) ||
                                     (flags.is_snake_shapes != 0);
    flags.uses_dirty_backbuffer_mode = 0;
    flags.uses_ff_rect_draw_mode =
        (flags.is_gradient != 0) || (flags.is_bands != 0);
    flags.uses_history_pipeline =
        (flags.is_snake_history_texture != 0) || (flags.is_gradient != 0);
    return flags;
}

static inline db_history_snake_step_eval_t
db_history_eval_snake_step_from_runtime(
    const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_history_snake_step_eval_t){0};
    }
    const db_snake_step_eval_t eval = db_snake_step_eval_from_runtime(
        runtime->pattern, runtime->pattern_seed, runtime->snake.shape_index,
        runtime->snake.cursor, runtime->snake.prev_start,
        runtime->snake.prev_count, runtime->gradient.direction_down,
        runtime->bench_speed_step);
    return (db_history_snake_step_eval_t){
        .plan = eval.plan,
        .target = eval.target,
        .shape_kind = eval.shape_kind,
        .is_grid_mode = eval.is_grid_mode,
        .is_shapes_mode = eval.is_shapes_mode,
    };
}

static inline void db_history_apply_snake_step_to_runtime(
    db_benchmark_runtime_init_t *runtime,
    const db_history_snake_step_eval_t *eval) {
    if ((runtime == NULL) || (eval == NULL)) {
        return;
    }
    const db_snake_plan_t *plan = &eval->plan;
    const db_snake_step_target_t *target = &eval->target;
    if (target->has_next_direction_flag != 0) {
        runtime->gradient.direction_down = target->next_direction_flag;
    }
    if (eval->is_grid_mode == 0) {
        if (target->has_next_shape_index != 0) {
            runtime->snake.shape_index = target->next_shape_index;
        }
        if (plan->wrapped != 0) {
            runtime->snake.prev_count = 0U;
        }
    }
    runtime->snake.cursor = plan->next_cursor;
    runtime->snake.prev_start = plan->next_prev_start;
    runtime->snake.prev_count = plan->next_prev_count;
    runtime->snake.batch_size = plan->batch_size;
    runtime->snake.phase_completed = plan->phase_completed;
}

static inline void
db_history_seed_background_rgb_d(const db_benchmark_runtime_init_t *runtime,
                                 double *out_r, double *out_g, double *out_b) {
    double clear_r = 0.0;
    double clear_g = 0.0;
    double clear_b = 0.0;
    db_benchmark_seed_background_color_rgb(runtime, &clear_r, &clear_g,
                                           &clear_b);
    if (out_r != NULL) {
        *out_r = clear_r;
    }
    if (out_g != NULL) {
        *out_g = clear_g;
    }
    if (out_b != NULL) {
        *out_b = clear_b;
    }
}

static inline void
db_history_seed_background_rgb_f32(const db_benchmark_runtime_init_t *runtime,
                                   float *out_r, float *out_g, float *out_b) {
    double clear_r = 0.0;
    double clear_g = 0.0;
    double clear_b = 0.0;
    db_history_seed_background_rgb_d(runtime, &clear_r, &clear_g, &clear_b);
    if (out_r != NULL) {
        *out_r = db_double_to_f32(clear_r);
    }
    if (out_g != NULL) {
        *out_g = db_double_to_f32(clear_g);
    }
    if (out_b != NULL) {
        *out_b = db_double_to_f32(clear_b);
    }
}

static inline void
db_history_seed_background_rgba_f32(const db_benchmark_runtime_init_t *runtime,
                                    float out_rgba[4]) {
    if (out_rgba == NULL) {
        return;
    }
    db_history_seed_background_rgb_f32(runtime, &out_rgba[0], &out_rgba[1],
                                       &out_rgba[2]);
    out_rgba[3] = db_double_to_f32(1.0);
}

typedef void (*db_history_seed_clear_f32_cb_t)(const float rgba[4],
                                               void *user_data);

static inline int db_history_run_seed_clear_if_needed(
    int should_seed, const db_benchmark_runtime_init_t *runtime,
    db_history_seed_clear_f32_cb_t clear_cb, void *clear_user_data) {
    if ((should_seed == 0) || (runtime == NULL) || (clear_cb == NULL)) {
        return 0;
    }
    float seed_rgba[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    db_history_seed_background_rgba_f32(runtime, seed_rgba);
    clear_cb(seed_rgba, clear_user_data);
    return 1;
}

static inline void
db_history_seed_background_rgba8(const db_benchmark_runtime_init_t *runtime,
                                 uint8_t out_rgba[4]) {
    if (out_rgba == NULL) {
        return;
    }
    double clear_r = 0.0;
    double clear_g = 0.0;
    double clear_b = 0.0;
    db_history_seed_background_rgb_d(runtime, &clear_r, &clear_g, &clear_b);
    out_rgba[0] = db_double01_to_u8_clamped(clear_r);
    out_rgba[1] = db_double01_to_u8_clamped(clear_g);
    out_rgba[2] = db_double01_to_u8_clamped(clear_b);
    out_rgba[3] = 255U;
}

static inline void
db_history_dirty_row_ranges_clear(db_dirty_row_range_t *ranges,
                                  size_t range_capacity) {
    if ((ranges == NULL) || (range_capacity == 0U)) {
        return;
    }
    for (size_t i = 0U; i < range_capacity; i++) {
        ranges[i] = (db_dirty_row_range_t){0U, 0U};
    }
}

static inline size_t db_history_dirty_row_ranges_copy_trunc(
    db_dirty_row_range_t *dst_ranges, size_t dst_capacity,
    const db_dirty_row_range_t *src_ranges, size_t src_count) {
    if ((dst_ranges == NULL) || (dst_capacity == 0U)) {
        return 0U;
    }
    const size_t available = (src_ranges != NULL) ? src_count : 0U;
    const size_t copied = (available < dst_capacity) ? available : dst_capacity;
    if (copied > 0U) {
        db_copy_bytes(dst_ranges, src_ranges,
                      copied * sizeof(db_dirty_row_range_t));
    }
    return copied;
}

static inline void db_history_gradient_replay_state_reset(
    db_gradient_backbuffer_replay_state_t *state) {
    if (state == NULL) {
        return;
    }
    const size_t draw_rows_capacity =
        sizeof(state->draw_rows) / sizeof(state->draw_rows[0]);
    db_history_dirty_row_ranges_clear(state->draw_rows, draw_rows_capacity);
    state->draw_count = 0U;
    state->state.head_row = 0U;
    state->state.direction_down = 1;
    state->state.cycle_index = 0U;
}

static inline void db_history_gradient_replay_state_store(
    db_gradient_backbuffer_replay_state_t *state,
    const db_dirty_row_range_t *draw_ranges, size_t draw_count,
    const db_gradient_state_t *render_state) {
    if (state == NULL) {
        return;
    }
    const size_t draw_rows_capacity =
        sizeof(state->draw_rows) / sizeof(state->draw_rows[0]);
    db_history_dirty_row_ranges_clear(state->draw_rows, draw_rows_capacity);
    state->draw_count = db_history_dirty_row_ranges_copy_trunc(
        state->draw_rows, draw_rows_capacity, draw_ranges, draw_count);
    if (render_state != NULL) {
        state->state = *render_state;
    }
}

static inline void db_history_record_draw_stats(uint64_t *full_draw_frames,
                                                uint64_t *dirty_draw_frames,
                                                int frame_full_draw,
                                                int frame_dirty_draw) {
    if ((full_draw_frames == NULL) || (dirty_draw_frames == NULL)) {
        return;
    }
    if (frame_full_draw != 0) {
        (*full_draw_frames)++;
    } else if (frame_dirty_draw != 0) {
        (*dirty_draw_frames)++;
    }
}

typedef struct {
    int counted_full_draw;
    int counted_dirty_draw;
} db_history_draw_stats_counted_t;

static inline db_history_draw_stats_counted_t
db_history_classify_counted_draw(int frame_full_draw, int frame_dirty_draw,
                                 uint32_t work_units_drawn) {
    const int has_work = (work_units_drawn > 0U) ? 1 : 0;
    return (db_history_draw_stats_counted_t){
        .counted_full_draw =
            ((frame_full_draw != 0) && (has_work != 0)) ? 1 : 0,
        .counted_dirty_draw =
            ((frame_dirty_draw != 0) && (has_work != 0)) ? 1 : 0,
    };
}

static inline db_history_runtime_mode_flags_t
db_history_runtime_mode_flags(const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_history_runtime_mode_flags_t){0};
    }
    db_history_runtime_mode_flags_t flags =
        db_history_pattern_mode_flags(runtime->pattern);
    flags.uses_dirty_backbuffer_mode = (runtime->backbuffer_draw_full == 0);
    return flags;
}

typedef struct {
    int reset_gradient_replay;
    int reset_shape_snake_prev_count;
} db_history_preserve_reset_flags_t;

typedef struct {
    db_history_preserve_reset_flags_t reset_flags;
    int should_seed_targets;
    int history_valid_after_resize;
} db_history_resize_preserve_policy_t;

static inline db_history_preserve_reset_flags_t
db_history_preserve_reset_flags_for_pattern(db_pattern_t pattern,
                                            int preserved) {
    db_history_preserve_reset_flags_t flags = {0};
    flags.reset_gradient_replay =
        db_history_should_reset_gradient_replay(pattern, preserved);
    flags.reset_shape_snake_prev_count =
        (((pattern == DB_PATTERN_SNAKE_RECT) ||
          (pattern == DB_PATTERN_SNAKE_SHAPES)) &&
         (preserved == 0));
    return flags;
}

static inline db_history_resize_preserve_policy_t
db_history_resize_preserve_policy_for_pattern(db_pattern_t pattern,
                                              int uses_history_pipeline,
                                              int preserved,
                                              int seed_targets_immediately) {
    db_history_resize_preserve_policy_t policy = {
        .reset_flags =
            db_history_preserve_reset_flags_for_pattern(pattern, preserved),
        .should_seed_targets = 0,
        .history_valid_after_resize = 0,
    };
    const int should_seed_targets =
        (uses_history_pipeline != 0) && (preserved == 0);
    policy.should_seed_targets =
        (should_seed_targets != 0) && (seed_targets_immediately != 0);
    policy.history_valid_after_resize =
        (preserved != 0) || (policy.should_seed_targets != 0);
    return policy;
}

static inline void db_history_apply_preserve_reset_flags(
    const db_history_preserve_reset_flags_t *flags,
    db_benchmark_runtime_init_t *runtime, int *gradient_history_valid,
    db_gradient_backbuffer_replay_state_t *gradient_prev_frame) {
    if ((flags == NULL) || (runtime == NULL)) {
        return;
    }
    if (flags->reset_shape_snake_prev_count != 0) {
        runtime->snake.prev_count = 0U;
    }
    if (flags->reset_gradient_replay != 0) {
        if (gradient_history_valid != NULL) {
            *gradient_history_valid = 0;
        }
        if (gradient_prev_frame != NULL) {
            db_history_gradient_replay_state_reset(gradient_prev_frame);
        }
    }
}

static inline void db_history_apply_resize_preserve_policy(
    const db_history_resize_preserve_policy_t *policy,
    db_benchmark_runtime_init_t *runtime, db_history_pair_state_t *history_pair,
    db_gradient_backbuffer_replay_state_t *gradient_prev_frame) {
    if ((policy == NULL) || (runtime == NULL) || (history_pair == NULL)) {
        return;
    }
    db_history_apply_preserve_reset_flags(&policy->reset_flags, runtime,
                                          &history_pair->is_valid,
                                          gradient_prev_frame);
    history_pair->is_valid = policy->history_valid_after_resize;
}

static inline int
db_history_should_seed_targets_after_resize(int uses_history_pipeline,
                                            int preserved) {
    return (uses_history_pipeline != 0) && (preserved == 0);
}

static inline int db_history_should_seed_full_on_invalid(int history_valid) {
    return history_valid == 0;
}

static inline int db_history_apply_full_seed_rows_if_needed(
    int *io_history_valid, uint32_t total_rows, db_dirty_row_range_t *io_ranges,
    size_t range_capacity, size_t *io_range_count) {
    if ((io_history_valid == NULL) || (io_ranges == NULL) ||
        (io_range_count == NULL) || (range_capacity == 0U) ||
        (total_rows == 0U)) {
        return 0;
    }
    if (db_history_should_seed_full_on_invalid(*io_history_valid) == 0) {
        return 0;
    }
    db_history_dirty_row_ranges_clear(io_ranges, range_capacity);
    io_ranges[0] = (db_dirty_row_range_t){0U, total_rows};
    *io_range_count = 1U;
    *io_history_valid = 1;
    return 1;
}

static inline size_t
db_history_set_full_row_ranges(uint32_t total_rows,
                               db_dirty_row_range_t *out_ranges,
                               size_t range_capacity) {
    if ((out_ranges == NULL) || (range_capacity == 0U) || (total_rows == 0U)) {
        return 0U;
    }
    db_history_dirty_row_ranges_clear(out_ranges, range_capacity);
    out_ranges[0] = (db_dirty_row_range_t){0U, total_rows};
    return 1U;
}

static inline int db_history_pair_other_index(int index) {
    return (index == 0) ? 1 : 0;
}

static inline int db_history_index_is_pair_member(int index) {
    return (index == 0) || (index == 1);
}

static inline int
db_history_pair_read_index(const db_history_pair_state_t *state) {
    if (state == NULL) {
        return -1;
    }
    return state->read_index;
}

static inline int
db_history_pair_write_index(const db_history_pair_state_t *state) {
    return db_history_pair_other_index(db_history_pair_read_index(state));
}

static inline void
db_history_pair_flip_to_write(db_history_pair_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->read_index = db_history_pair_other_index(state->read_index);
}

static inline int db_history_should_update_descriptor_binding(
    int uses_history_pipeline, int descriptor_index, int read_index) {
    return (uses_history_pipeline != 0) && (descriptor_index != read_index) &&
           (db_history_index_is_pair_member(read_index) != 0);
}

static inline int db_history_pair_sync_descriptor_index_if_needed(
    int uses_history_pipeline, int *io_descriptor_index,
    const db_history_pair_state_t *state) {
    if ((io_descriptor_index == NULL) || (state == NULL)) {
        return 0;
    }
    if (db_history_should_update_descriptor_binding(uses_history_pipeline,
                                                    *io_descriptor_index,
                                                    state->read_index) == 0) {
        return 0;
    }
    *io_descriptor_index = state->read_index;
    return 1;
}

typedef struct {
    uint32_t seed_frames_remaining;
    uint32_t resync_frames_remaining;
    int initial_seed_done;
    int backbuffer_valid;
} db_history_snake_backbuffer_state_t;

typedef struct {
    int should_seed_now;
    int should_force_full_upload;
} db_history_snake_backbuffer_action_t;

static inline db_history_snake_backbuffer_state_t
db_history_snake_backbuffer_state_load(uint32_t seed_frames_remaining,
                                       uint32_t resync_frames_remaining,
                                       int initial_seed_done,
                                       int backbuffer_valid) {
    return (db_history_snake_backbuffer_state_t){
        .seed_frames_remaining = seed_frames_remaining,
        .resync_frames_remaining = resync_frames_remaining,
        .initial_seed_done = initial_seed_done,
        .backbuffer_valid = backbuffer_valid,
    };
}

static inline void db_history_snake_backbuffer_state_reset(
    db_history_snake_backbuffer_state_t *state, int is_double_buffered) {
    if (state == NULL) {
        return;
    }
    *state = db_history_snake_backbuffer_state_load(
        0U, db_history_resync_frame_count_for_swapchain(is_double_buffered), 0,
        0);
}

static inline void db_history_snake_backbuffer_state_store(
    const db_history_snake_backbuffer_state_t *state,
    uint32_t *out_seed_frames_remaining, uint32_t *out_resync_frames_remaining,
    int *out_initial_seed_done, int *out_backbuffer_valid) {
    if (state == NULL) {
        return;
    }
    if (out_seed_frames_remaining != NULL) {
        *out_seed_frames_remaining = state->seed_frames_remaining;
    }
    if (out_resync_frames_remaining != NULL) {
        *out_resync_frames_remaining = state->resync_frames_remaining;
    }
    if (out_initial_seed_done != NULL) {
        *out_initial_seed_done = state->initial_seed_done;
    }
    if (out_backbuffer_valid != NULL) {
        *out_backbuffer_valid = state->backbuffer_valid;
    }
}

static inline db_history_snake_backbuffer_action_t
db_history_eval_snake_backbuffer_action(
    int uses_dirty_backbuffer_mode, int is_double_buffered,
    db_history_snake_backbuffer_state_t *state) {
    db_history_snake_backbuffer_action_t action = {0};
    if (state == NULL) {
        return action;
    }
    if (db_history_should_queue_seed_backbuffer(
            uses_dirty_backbuffer_mode, state->initial_seed_done,
            state->backbuffer_valid, state->seed_frames_remaining) != 0) {
        state->seed_frames_remaining =
            db_history_seed_frame_count_for_swapchain(is_double_buffered);
    }
    if (db_history_should_seed_backbuffer_now(
            uses_dirty_backbuffer_mode, state->initial_seed_done,
            state->backbuffer_valid, state->seed_frames_remaining) != 0) {
        action.should_seed_now = 1;
        state->backbuffer_valid = 1;
        if (state->seed_frames_remaining > 0U) {
            state->seed_frames_remaining--;
        }
        if (state->seed_frames_remaining == 0U) {
            state->initial_seed_done = 1;
        }
    } else if (db_history_should_force_full_upload_invalid_backbuffer(
                   uses_dirty_backbuffer_mode, state->backbuffer_valid) != 0) {
        action.should_force_full_upload = 1;
    }
    if (db_history_should_force_full_upload_resync(
            uses_dirty_backbuffer_mode, state->resync_frames_remaining) != 0) {
        action.should_force_full_upload = 1;
        if (state->resync_frames_remaining > 0U) {
            state->resync_frames_remaining--;
        }
    }
    return action;
}

static inline db_history_snake_backbuffer_action_t
db_history_eval_snake_backbuffer_action_io(int uses_dirty_backbuffer_mode,
                                           int is_double_buffered,
                                           uint32_t *io_seed_frames_remaining,
                                           uint32_t *io_resync_frames_remaining,
                                           int *io_initial_seed_done,
                                           int *io_backbuffer_valid) {
    db_history_snake_backbuffer_state_t state =
        db_history_snake_backbuffer_state_load(
            (io_seed_frames_remaining != NULL) ? *io_seed_frames_remaining : 0U,
            (io_resync_frames_remaining != NULL) ? *io_resync_frames_remaining
                                                 : 0U,
            (io_initial_seed_done != NULL) ? *io_initial_seed_done : 0,
            (io_backbuffer_valid != NULL) ? *io_backbuffer_valid : 0);
    const db_history_snake_backbuffer_action_t action =
        db_history_eval_snake_backbuffer_action(uses_dirty_backbuffer_mode,
                                                is_double_buffered, &state);
    db_history_snake_backbuffer_state_store(
        &state, io_seed_frames_remaining, io_resync_frames_remaining,
        io_initial_seed_done, io_backbuffer_valid);
    return action;
}

static inline void db_history_invalidate_snake_backbuffer_on_resize(
    int is_double_buffered, int *io_backbuffer_valid,
    size_t *io_previous_upload_count,
    db_history_snake_backbuffer_state_t *io_snake_backbuffer_state) {
    if (io_backbuffer_valid != NULL) {
        *io_backbuffer_valid = 0;
    }
    if (io_previous_upload_count != NULL) {
        *io_previous_upload_count = 0U;
    }
    if (io_snake_backbuffer_state != NULL) {
        io_snake_backbuffer_state->backbuffer_valid = 0;
        io_snake_backbuffer_state->resync_frames_remaining =
            db_history_resync_frame_count_for_swapchain(is_double_buffered);
    }
}

#endif
