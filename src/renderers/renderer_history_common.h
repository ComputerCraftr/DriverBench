#ifndef DRIVERBENCH_RENDERER_HISTORY_COMMON_H
#define DRIVERBENCH_RENDERER_HISTORY_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/db_buffer_convert.h"
#include "../core/db_core.h"
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

typedef struct {
    db_grid_block_t *blocks;
    size_t capacity;
} db_history_snake_damage_block_scratch_t;

typedef struct {
    db_snake_compact_block_t *blocks;
    size_t capacity;
} db_history_snake_compact_block_scratch_t;

typedef struct {
    db_snake_shape_row_bounds_t *row_bounds;
    size_t row_bounds_capacity;
    uint32_t *active_tile_indices;
    uint8_t *active_tile_valid;
    double *active_prior_rgb;
    uint32_t active_tile_capacity;
} db_history_snake_shape_scratch_t;

typedef struct {
    db_history_snake_damage_block_scratch_t damage;
    db_history_snake_compact_block_scratch_t compact;
    db_history_snake_shape_scratch_t shape;
} db_history_snake_scratch_t;

typedef struct {
    int is_bands;
    int is_gradient;
    int is_gradient_sweep;
    int is_snake_region_mode;
    int is_snake_grid;
    int is_snake_rect;
    int is_snake_shapes;
    int is_snake_history_texture;
    int uses_dirty_backbuffer_mode;
    int uses_ff_rect_draw_mode;
    int uses_history_pipeline;
} db_history_pattern_mode_flags_t;

typedef struct {
    db_snake_plan_t plan;
    db_snake_step_target_t target;
    db_snake_shape_kind_t shape_kind;
    int is_grid_mode;
    int is_shapes_mode;
} db_history_snake_step_eval_t;

typedef void (*db_history_seed_clear_f32_cb_t)(const float *rgba,
                                               void *user_data);

typedef struct {
    int counted_full_draw;
    int counted_dirty_draw;
} db_history_draw_stats_counted_t;

typedef struct {
    int reset_gradient_replay;
    int reset_shape_snake_prev_count;
} db_history_preserve_reset_flags_t;

typedef struct {
    db_history_preserve_reset_flags_t reset_flags;
    int should_seed_targets;
    int history_valid_after_resize;
} db_history_resize_preserve_policy_t;

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
db_history_snake_active_cache_init(db_history_snake_scratch_t *scratch,
                                   const char *backend, uint32_t tile_capacity,
                                   uint32_t prior_rgb_components) {
    if ((scratch == NULL) || (backend == NULL) || (tile_capacity == 0U) ||
        (prior_rgb_components == 0U)) {
        return;
    }
    scratch->shape.active_tile_indices =
        (uint32_t *)db_alloc_aligned_array_or_fail(
            backend, "snake_active_tile_indices", (size_t)tile_capacity,
            sizeof(uint32_t), DB_CACHELINE_ALIGNMENT_BYTES);
    scratch->shape.active_tile_valid =
        (uint8_t *)db_alloc_aligned_array_or_fail(
            backend, "snake_active_tile_valid", (size_t)tile_capacity,
            sizeof(uint8_t), DB_CACHELINE_ALIGNMENT_BYTES);
    scratch->shape.active_prior_rgb = (double *)db_alloc_aligned_array_or_fail(
        backend, "snake_active_prior_rgb",
        (size_t)tile_capacity * (size_t)prior_rgb_components, sizeof(double),
        DB_CACHELINE_ALIGNMENT_BYTES);
    scratch->shape.active_tile_capacity = tile_capacity;
}

static inline void
db_history_snake_active_cache_free(db_history_snake_scratch_t *scratch) {
    if (scratch == NULL) {
        return;
    }
    free(scratch->shape.active_tile_indices);
    free(scratch->shape.active_tile_valid);
    free(scratch->shape.active_prior_rgb);
    scratch->shape.active_tile_indices = NULL;
    scratch->shape.active_tile_valid = NULL;
    scratch->shape.active_prior_rgb = NULL;
    scratch->shape.active_tile_capacity = 0U;
}

static inline void
db_history_pair_state_seeded(db_history_pair_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = db_history_pair_state_make(1, 0);
}

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

static inline uint32_t db_history_seed_frame_count_for_swapchain(
    uint32_t preserved_framebuffer_count) {
    return preserved_framebuffer_count;
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
    uint32_t preserved_framebuffer_count, int uses_dirty_backbuffer_mode,
    int backbuffer_valid, size_t previous_upload_count) {
    return (preserved_framebuffer_count >= 2) &&
           (uses_dirty_backbuffer_mode != 0) && (backbuffer_valid != 0) &&
           (previous_upload_count > 0U);
}

static inline int
db_history_should_use_snake_dirty_history_pass(int uses_dirty_backbuffer_mode,
                                               int snake_plan_valid) {
    return (uses_dirty_backbuffer_mode != 0) && (snake_plan_valid != 0);
}

static inline int db_history_should_reset_gradient_replay(db_pattern_t pattern,
                                                          int preserved) {
    const int is_gradient = (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                            (pattern == DB_PATTERN_GRADIENT_FILL);
    return (is_gradient != 0) && (preserved == 0);
}

static inline db_history_pattern_mode_flags_t
db_history_pattern_mode_flags(db_pattern_t pattern) {
    db_history_pattern_mode_flags_t flags = {0};
    flags.is_bands = (pattern == DB_PATTERN_BANDS);
    flags.is_gradient = (pattern == DB_PATTERN_GRADIENT_SWEEP) ||
                        (pattern == DB_PATTERN_GRADIENT_FILL);
    flags.is_gradient_sweep = (pattern == DB_PATTERN_GRADIENT_SWEEP);
    flags.is_snake_grid = (pattern == DB_PATTERN_SNAKE_GRID);
    flags.is_snake_rect = (pattern == DB_PATTERN_SNAKE_RECT);
    flags.is_snake_shapes = (pattern == DB_PATTERN_SNAKE_SHAPES);
    flags.is_snake_region_mode =
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
        runtime->snake.prev_count, runtime->snake.grid_phase_flag,
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
    if (target->has_next_phase_flag != 0) {
        runtime->snake.grid_phase_flag = target->next_phase_flag;
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
db_history_seed_background_rgba_f32(const db_benchmark_runtime_init_t *runtime,
                                    float *out_rgba) {
    if (out_rgba == NULL) {
        return;
    }
    double clear_rgb[3] = {0.0, 0.0, 0.0};
    db_benchmark_seed_background_color_rgb3(runtime, clear_rgb);
    db_rgb_f64_to_f32_rgb3(clear_rgb, out_rgba);
    out_rgba[3] = db_double_to_f32(1.0);
}

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
                                 uint8_t *out_rgba) {
    if (out_rgba == NULL) {
        return;
    }
    double clear_rgba[4] = {0.0, 0.0, 0.0, 1.0};
    db_benchmark_seed_background_color_rgb3(runtime, clear_rgba);
    db_rgba01_to_u8_rgba4(clear_rgba, out_rgba);
}

static inline size_t db_history_grid_blocks_copy_trunc(
    db_grid_block_t *dst_blocks, size_t dst_capacity,
    const db_grid_block_t *src_blocks, size_t src_count) {
    if ((dst_blocks == NULL) || (dst_capacity == 0U)) {
        return 0U;
    }
    const size_t available = (src_blocks != NULL) ? src_count : 0U;
    const size_t copied = (available < dst_capacity) ? available : dst_capacity;
    if (copied > 0U) {
        db_copy_bytes(dst_blocks, src_blocks,
                      copied * sizeof(db_grid_block_t));
    }
    return copied;
}

static inline void db_history_gradient_replay_state_reset(
    db_gradient_backbuffer_replay_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->draw_count = 0U;
    state->draw_blocks[0] = (db_grid_block_t){0U, 0U, 0U, 0U};
    state->state.head_row = 0U;
    state->state.direction_down = 1;
    state->state.cycle_index = 0U;
}

static inline void db_history_gradient_replay_state_store(
    db_gradient_backbuffer_replay_state_t *state,
    const db_grid_block_t *draw_blocks, size_t draw_count,
    const db_gradient_state_t *render_state) {
    if (state == NULL) {
        return;
    }
    state->draw_count = db_history_grid_blocks_copy_trunc(
        state->draw_blocks,
        sizeof(state->draw_blocks) / sizeof(state->draw_blocks[0]), draw_blocks,
        draw_count);
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

static inline void db_history_record_draw_stats_for_work(
    uint64_t *full_draw_frames, uint64_t *dirty_draw_frames,
    int frame_full_draw, int frame_dirty_draw, uint32_t work_units_drawn) {
    const db_history_draw_stats_counted_t counted =
        db_history_classify_counted_draw(frame_full_draw, frame_dirty_draw,
                                         work_units_drawn);
    db_history_record_draw_stats(full_draw_frames, dirty_draw_frames,
                                 counted.counted_full_draw,
                                 counted.counted_dirty_draw);
}

static inline void db_history_record_mesh_draw_stats(
    uint64_t *full_draw_frames, uint64_t *dirty_draw_frames,
    int draw_is_full_mesh, uint32_t work_units_drawn) {
    const int frame_full_draw = (draw_is_full_mesh != 0) ? 1 : 0;
    const int frame_dirty_draw = (draw_is_full_mesh != 0) ? 0 : 1;
    db_history_record_draw_stats_for_work(full_draw_frames, dirty_draw_frames,
                                          frame_full_draw, frame_dirty_draw,
                                          work_units_drawn);
}

static inline void db_history_record_history_pass_draw_stats(
    uint64_t *full_draw_frames, uint64_t *dirty_draw_frames,
    int used_dirty_history_path, uint32_t work_units_drawn) {
    const int frame_full_draw = (used_dirty_history_path != 0) ? 0 : 1;
    const int frame_dirty_draw = (used_dirty_history_path != 0) ? 1 : 0;
    db_history_record_draw_stats_for_work(full_draw_frames, dirty_draw_frames,
                                          frame_full_draw, frame_dirty_draw,
                                          work_units_drawn);
}

static inline void
db_history_copy_draw_stats(const db_renderer_frame_stats_t *frame,
                           uint64_t *full_draw_frames,
                           uint64_t *dirty_draw_frames) {
    if (frame == NULL) {
        return;
    }
    if (full_draw_frames != NULL) {
        *full_draw_frames = frame->full_draw_frames;
    }
    if (dirty_draw_frames != NULL) {
        *dirty_draw_frames = frame->dirty_draw_frames;
    }
}

static inline db_gradient_damage_plan_t
db_history_eval_gradient_step_from_runtime(
    const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_gradient_damage_plan_t){0};
    }
    return db_gradient_step_from_runtime(
        runtime->pattern, runtime->gradient.head_row,
        runtime->gradient.direction_down, runtime->gradient.cycle_index,
        runtime->bench_speed_step);
}

static inline void db_history_apply_gradient_step_to_runtime(
    db_benchmark_runtime_init_t *runtime,
    const db_gradient_damage_plan_t *plan) {
    if ((runtime == NULL) || (plan == NULL)) {
        return;
    }
    db_gradient_apply_step_to_runtime(runtime, plan);
}

static inline void
db_history_finalize_frame(db_renderer_frame_stats_t *frame,
                          const db_benchmark_runtime_init_t *runtime,
                          uint32_t cols, uint32_t rows) {
    if ((frame == NULL) || (runtime == NULL)) {
        return;
    }
    frame->state_hash = db_benchmark_runtime_state_hash_cross_renderer(
        runtime, frame->frame_index, cols, rows);
    frame->frame_index++;
}

static inline db_history_pattern_mode_flags_t
db_history_runtime_mode_flags(const db_benchmark_runtime_init_t *runtime) {
    if (runtime == NULL) {
        return (db_history_pattern_mode_flags_t){0};
    }
    db_history_pattern_mode_flags_t flags =
        db_history_pattern_mode_flags(runtime->pattern);
    flags.uses_dirty_backbuffer_mode = (runtime->backbuffer_draw_full == 0);
    return flags;
}

static inline db_history_preserve_reset_flags_t
db_history_preserve_reset_flags_for_pattern(db_pattern_t pattern,
                                            int preserved) {
    const db_history_pattern_mode_flags_t mode_flags =
        db_history_pattern_mode_flags(pattern);
    db_history_preserve_reset_flags_t flags = {0};
    flags.reset_gradient_replay =
        db_history_should_reset_gradient_replay(pattern, preserved);
    flags.reset_shape_snake_prev_count =
        (mode_flags.is_snake_region_mode != 0) && (preserved == 0);
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

static inline int db_history_should_seed_full_on_invalid(int history_valid) {
    return history_valid == 0;
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
    db_history_snake_backbuffer_state_t *state,
    uint32_t preserved_framebuffer_count) {
    if (state == NULL) {
        return;
    }
    *state = db_history_snake_backbuffer_state_load(
        0U,
        db_history_seed_frame_count_for_swapchain(preserved_framebuffer_count),
        0, 0);
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
    int uses_dirty_backbuffer_mode, uint32_t preserved_framebuffer_count,
    db_history_snake_backbuffer_state_t *state) {
    db_history_snake_backbuffer_action_t action = {0};
    if (state == NULL) {
        return action;
    }
    if (db_history_should_queue_seed_backbuffer(
            uses_dirty_backbuffer_mode, state->initial_seed_done,
            state->backbuffer_valid, state->seed_frames_remaining) != 0) {
        state->seed_frames_remaining =
            db_history_seed_frame_count_for_swapchain(
                preserved_framebuffer_count);
    }
    if (db_history_should_seed_backbuffer_now(
            uses_dirty_backbuffer_mode, state->initial_seed_done,
            state->backbuffer_valid, state->seed_frames_remaining) != 0) {
        action.should_seed_now = 1;
        action.should_force_full_upload = 1;
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
                                           uint32_t preserved_framebuffer_count,
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
        db_history_eval_snake_backbuffer_action(
            uses_dirty_backbuffer_mode, preserved_framebuffer_count, &state);
    db_history_snake_backbuffer_state_store(
        &state, io_seed_frames_remaining, io_resync_frames_remaining,
        io_initial_seed_done, io_backbuffer_valid);
    return action;
}

static inline void db_history_invalidate_snake_backbuffer_on_resize(
    uint32_t preserved_framebuffer_count, int *io_backbuffer_valid,
    size_t *io_previous_upload_count,
    db_history_snake_backbuffer_state_t *io_snake_backbuffer_state) {
    if (io_backbuffer_valid != NULL) {
        *io_backbuffer_valid = 0;
    }
    if (io_previous_upload_count != NULL) {
        *io_previous_upload_count = 0U;
    }
    if (io_snake_backbuffer_state != NULL) {
        io_snake_backbuffer_state->seed_frames_remaining =
            db_history_seed_frame_count_for_swapchain(
                preserved_framebuffer_count);
        io_snake_backbuffer_state->backbuffer_valid = 0;
        io_snake_backbuffer_state->initial_seed_done = 0;
        io_snake_backbuffer_state->resync_frames_remaining =
            db_history_seed_frame_count_for_swapchain(
                preserved_framebuffer_count);
    }
}

#endif
