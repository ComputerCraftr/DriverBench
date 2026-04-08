#include "../../core/db_buffer_convert.h"
#include "../../core/db_core.h"
#include "../../core/db_numeric.h"
#include "../renderer_benchmark_gradient.h"
#include "../renderer_benchmark_runtime.h"
#include "../renderer_benchmark_types.h"
#include "../renderer_gl_common.h"
#include "../renderer_history_common.h"
#include "../renderer_snake_emit.h"
#include "../renderer_snake_shape_common.h"
#include "../renderer_snake_types.h"
#include "renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderer_opengl_gl1_5_gles1_1_internal.h"
#include <stddef.h>
#include <stdint.h>

void db_gl1_render_snake_draw_pass(
    const db_gl1_snake_frame_state_t *snake_frame, int dirty_backbuffer_mode,
    int viewport_w, int viewport_h) {
    if (snake_frame == NULL) {
        return;
    }
    const int allow_empty_dirty_draw = dirty_backbuffer_mode;
    const int must_force_full_draw =
        (g_state.runtime.backbuffer_draw_full != 0);
    const db_grid_block_t *draw_damage_blocks = NULL;
    size_t draw_damage_block_count = 0U;
    const db_snake_compact_block_t *draw_compact_blocks = NULL;
    size_t draw_compact_block_count = 0U;
    db_gl1_snake_frame_mode_t frame_mode = DB_GL1_SNAKE_FRAME_MODE_COMPACT;
    db_gl1_shadow_upload_intent_t shadow_upload_intent =
        DB_GL1_SHADOW_UPLOAD_INTENT_NONE;
    const db_damage_block_t *shadow_upload_blocks = NULL;
    size_t shadow_upload_block_count = 0U;
    int shadow_used_recovery_source = 0;
    const int has_viewport = (viewport_w > 0) && (viewport_h > 0);
    const db_gl1_damage_collect_ctx_t collect_ctx = {
        .pattern = g_state.runtime.pattern,
        .cols = db_grid_cols_effective(),
        .rows = db_grid_rows_effective(),
        .force_full_upload =
            (must_force_full_draw != 0) ? 1 : snake_frame->force_full_upload,
        .snake_plan = &snake_frame->plan,
        .pattern_seed = g_state.runtime.pattern_seed,
        .snake_scratch = &g_state.snake_scratch,
        .get_color_bits = db_gl1_get_snake_color_bits,
        .color_user_data = NULL,
    };
    if (db_gl1_collect_current_snake_frame_blocks(
            &collect_ctx, g_state.snake_scratch.damage.blocks,
            g_state.snake_scratch.damage.capacity, &draw_damage_block_count,
            g_state.snake_scratch.compact.blocks,
            g_state.snake_scratch.compact.capacity, &draw_compact_block_count,
            &frame_mode) != 0) {
        if (draw_damage_block_count > 0U) {
            draw_damage_blocks = g_state.snake_scratch.damage.blocks;
        }
        if ((frame_mode == DB_GL1_SNAKE_FRAME_MODE_COMPACT) &&
            (draw_compact_block_count > 0U)) {
            draw_compact_blocks = g_state.snake_scratch.compact.blocks;
        }
    } else if ((g_state.snake_scratch.damage.blocks != NULL) &&
               (g_state.snake_scratch.damage.capacity > 0U)) {
        g_state.snake_scratch.damage.blocks[0] = db_grid_block_full(
            db_grid_rows_effective(), db_grid_cols_effective());
        draw_damage_blocks = g_state.snake_scratch.damage.blocks;
        draw_damage_block_count = 1U;
        frame_mode = DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED;
    }
    const int has_draw_work =
        ((draw_compact_blocks != NULL) && (draw_compact_block_count > 0U)) ||
        (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
        ((draw_damage_blocks != NULL) && (draw_damage_block_count > 0U) &&
         (allow_empty_dirty_draw == 0));

    if (has_viewport != 0) {
        const uint32_t viewport_w_u32 =
            db_checked_int_to_u32(BACKEND_NAME, "viewport_w", viewport_w);
        const uint32_t viewport_h_u32 =
            db_checked_int_to_u32(BACKEND_NAME, "viewport_h", viewport_h);
        db_gl1_ensure_shadow_framebuffer_capacity(viewport_w_u32,
                                                  viewport_h_u32);
        if ((g_state.snake_shadow_present.backing_valid == 0) ||
            (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
            ((draw_damage_blocks == NULL) && (has_draw_work != 0))) {
            db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                   viewport_h_u32);
            g_state.snake_shadow_stats.backing_rebuild_frames++;
        } else if ((draw_damage_blocks != NULL) &&
                   (draw_damage_block_count > 0U)) {
            db_gl1_update_shadow_framebuffer_from_snake_step(
                snake_frame, viewport_w_u32, viewport_h_u32);
        }
    }

    if (has_draw_work == 0) {
        // No damage this frame; preserve backbuffer contents.
    } else {
        int drew_frame = 0;
        int used_compact_draw = 0;
        int used_fallback_draw = 0;
        const char *fallback_reason = NULL;
        if ((draw_compact_blocks != NULL) && (draw_compact_block_count > 0U)) {
            drew_frame = db_gl1_draw_compact_blocks_from_snake_colors_once(
                draw_compact_blocks, draw_compact_block_count);
            if (drew_frame != 0) {
                used_compact_draw = 1;
            }
        }
        if (drew_frame == 0) {
            if ((snake_frame->force_full_upload != 0) &&
                (dirty_backbuffer_mode != 0)) {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_INVALID_RECOVERY,
                    "invalid_backbuffer_recovery");
                fallback_reason = "invalid_backbuffer_recovery";
            } else if (g_state.runtime_flags.is_snake_shapes != 0) {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_SHAPE_FALLBACK, "shape_fallback");
                fallback_reason = "shape_fallback";
            } else {
                db_gl1_log_shadow_fallback_once(
                    DB_GL1_SNAKE_SHADOW_LOG_COMPACT_CAPACITY,
                    "compact_capacity");
                fallback_reason = "compact_capacity";
            }
            if (has_viewport != 0) {
                const uint32_t viewport_w_u32 = db_checked_int_to_u32(
                    BACKEND_NAME, "viewport_w", viewport_w);
                const uint32_t viewport_h_u32 = db_checked_int_to_u32(
                    BACKEND_NAME, "viewport_h", viewport_h);
                if ((frame_mode ==
                     DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) ||
                    (draw_damage_blocks == NULL) ||
                    (draw_damage_block_count == 0U)) {
                    db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                           viewport_h_u32);
                    g_state.snake_shadow_stats.backing_rebuild_frames++;
                    shadow_upload_intent =
                        DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD;
                } else {
                    shadow_upload_block_count =
                        db_gl1_build_shadow_upload_blocks_from_damage_blocks(
                            draw_damage_blocks, draw_damage_block_count,
                            viewport_w_u32, viewport_h_u32);
                    if (shadow_upload_block_count == 0U) {
                        db_gl1_rebuild_shadow_framebuffer_full(viewport_w_u32,
                                                               viewport_h_u32);
                        g_state.snake_shadow_stats.backing_rebuild_frames++;
                        shadow_upload_intent =
                            DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD;
                    } else {
                        shadow_upload_blocks =
                            g_state.snake_shadow_upload_blocks;
                        shadow_upload_intent =
                            DB_GL1_SHADOW_UPLOAD_INTENT_PARTIAL_BLOCKS;
                    }
                }
                if (shadow_upload_intent ==
                    DB_GL1_SHADOW_UPLOAD_INTENT_FULL_UPLOAD) {
                    g_state.snake_shadow_present.texture_valid = 0;
                    g_state.snake_shadow_present.texture_needs_full_upload = 1;
                    g_state.snake_shadow_stats.texture_full_upload_frames++;
                } else if (shadow_upload_intent ==
                           DB_GL1_SHADOW_UPLOAD_INTENT_PARTIAL_BLOCKS) {
                    g_state.snake_shadow_stats.texture_partial_upload_frames++;
                }
                if (fallback_reason != NULL) {
                    shadow_used_recovery_source = 1;
                }
                drew_frame = db_gl1_draw_shadow_framebuffer_once(
                    shadow_upload_blocks, shadow_upload_block_count,
                    viewport_w_u32, viewport_h_u32);
                if (drew_frame != 0) {
                    used_fallback_draw = 1;
                }
            }
        }
        db_gl1_record_compact_health(dirty_backbuffer_mode, used_compact_draw,
                                     used_fallback_draw, frame_mode,
                                     draw_compact_block_count,
                                     draw_damage_block_count, fallback_reason);
        if ((used_fallback_draw != 0) && (shadow_used_recovery_source != 0)) {
            g_state.snake_shadow_stats.recovery_from_shadow_frames++;
        }
        if (drew_frame != 0) {
            db_history_record_mesh_draw_stats(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames,
                (dirty_backbuffer_mode == 0) ? 1 : 0, 1U);
        }
    }
    if (frame_mode == DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED) {
        g_state.snake_replay.replay_mode =
            DB_GL1_SNAKE_REPLAY_FULL_RECOVERY_REQUIRED;
        g_state.snake_replay.prev_draw_block_count = 0U;
    } else if ((draw_compact_blocks != NULL) &&
               (draw_compact_block_count > 0U) &&
               (g_state.snake_replay.prev_draw_blocks != NULL) &&
               (g_state.snake_replay.draw_block_capacity > 0U)) {
        const size_t copy_limit =
            (draw_compact_block_count <
             g_state.snake_replay.draw_block_capacity)
                ? draw_compact_block_count
                : g_state.snake_replay.draw_block_capacity;
        db_copy_bytes(g_state.snake_replay.prev_draw_blocks,
                      draw_compact_blocks,
                      copy_limit * sizeof(*draw_compact_blocks));
        g_state.snake_replay.prev_draw_block_count = copy_limit;
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_COMPACT;
    } else {
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
    }
    g_state.snake_backbuffer_state.backbuffer_valid = 1;
}

void db_gl1_prepare_snake_frame_state(db_gl1_snake_frame_state_t *state,
                                      uint32_t preserved_framebuffer_count,
                                      int dirty_backbuffer_mode,
                                      int has_viewport) {
    if (state == NULL) {
        return;
    }

    const db_history_snake_step_eval_t eval =
        db_history_eval_snake_step_from_runtime(&g_state.runtime);
    state->plan = eval.plan;
    state->target = eval.target;
    const db_snake_shape_kind_t shape_kind = eval.shape_kind;

    const db_history_snake_backbuffer_action_t history_action =
        db_history_eval_snake_backbuffer_action_io(
            dirty_backbuffer_mode, preserved_framebuffer_count,
            &g_state.snake_backbuffer_state.seed_frames_remaining,
            &g_state.snake_backbuffer_state.resync_frames_remaining,
            &g_state.snake_backbuffer_state.initial_seed_done,
            &g_state.snake_backbuffer_state.backbuffer_valid);

    if (db_history_run_seed_clear_if_needed(
            history_action.should_seed_now, &g_state.runtime,
            db_gl1_seed_backbuffer_clear_cb, NULL) != 0) {
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, 1U);
        g_state.snake_replay.replay_mode = DB_GL1_SNAKE_REPLAY_NONE;
        g_state.snake_replay.prev_draw_block_count = 0U;
    }
    if (history_action.should_force_full_upload != 0) {
        state->force_full_upload = 1;
    }
    int collect_force_full_blocks = state->force_full_upload;
    if ((g_state.runtime_flags.is_snake_grid != 0) &&
        (state->force_full_upload != 0) && dirty_backbuffer_mode &&
        has_viewport) {
        // Grid fast-path on invalid backbuffer: clear to the pre-step base
        // phase, then redraw the full non-base set (settled + active).
        const int base_phase = (state->plan.phase_flag == 0) ? 1 : 0;
        double base_rgb[3] = {0.0, 0.0, 0.0};
        db_grid_target_color_rgb3(base_phase, base_rgb);
        float base_rgb_f32[3] = {0.0F, 0.0F, 0.0F};
        db_rgb_f64_to_f32_rgb3(base_rgb, base_rgb_f32);
        db_gl_clear_color_rgba(base_rgb_f32[0], base_rgb_f32[1],
                               base_rgb_f32[2], 1.0F);
        db_gl_clear_color_buffer();
        db_history_record_draw_stats_for_work(&g_state.frame.full_draw_frames,
                                              &g_state.frame.dirty_draw_frames,
                                              1, 0, 1U);
        collect_force_full_blocks = 0;
    }
    state->force_full_upload = collect_force_full_blocks;
    const int can_replay_snake = db_history_can_replay_previous_damage(
        preserved_framebuffer_count, dirty_backbuffer_mode,
        g_state.snake_backbuffer_state.backbuffer_valid,
        (g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT) ? 1U
                                                                          : 0U);
    if (can_replay_snake != 0) {
        if ((g_state.snake_replay.replay_mode == DB_GL1_SNAKE_REPLAY_COMPACT) &&
            (g_state.snake_replay.prev_draw_blocks != NULL) &&
            (g_state.snake_replay.prev_draw_block_count > 0U)) {
            (void)db_gl1_draw_compact_blocks_from_snake_colors_once(
                g_state.snake_replay.prev_draw_blocks,
                g_state.snake_replay.prev_draw_block_count);
        }
    }

    db_render_snake_step(&state->plan, &state->target.region, shape_kind,
                         g_state.runtime.pattern_seed,
                         state->plan.active_shape_index,
                         state->target.target_rgb,
                         state->target.force_full_fill_on_phase_complete);
    db_history_apply_snake_step_to_runtime(&g_state.runtime, &eval);
}

void db_gl1_render_gradient_frame(int viewport_w, int viewport_h,
                                  uint32_t preserved_framebuffer_count) {
    const db_gradient_damage_plan_t gradient_plan =
        db_history_eval_gradient_step_from_runtime(&g_state.runtime);

    // Render MUST use the plan's render_* state. The plan's next_* state is
    // only applied to the runtime AFTER we draw, matching CPU renderer
    // semantics.
    const uint32_t gradient_render_head_row =
        gradient_plan.render_state.head_row;
    const int gradient_render_direction_down =
        gradient_plan.render_state.direction_down;
    const uint32_t gradient_render_cycle_index =
        gradient_plan.render_state.cycle_index;
    const uint32_t rows = db_grid_rows_effective();
    const uint32_t full_width_cols = db_grid_cols_effective();

    db_grid_block_t gradient_dirty_blocks[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
        {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}};
    const size_t gradient_dirty_count = db_gradient_collect_dirty_blocks(
        &gradient_plan, rows, full_width_cols, gradient_dirty_blocks,
        DB_GL1_GRADIENT_REPLAY_ROW_CAP);

    // When bench_speed_step > 1, the head can advance by multiple rows. Plan
    // dirty ranges cover the new sweep window, but traversed rows can be
    // skipped unless we repaint them as solid per-row colors.
    db_grid_block_t skipped_blocks[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
        {0U, 0U, 0U, 0U},
        {0U, 0U, 0U, 0U},
    };
    size_t skipped_count = 0U;

    const uint32_t render_head = gradient_plan.render_state.head_row;
    const uint32_t next_head = gradient_plan.next_state.head_row;
    if (rows > 0U) {
        uint32_t delta = 0U;
        if (gradient_render_direction_down != 0) {
            // Moving down => head increases modulo rows.
            delta = (next_head + rows - render_head) % rows;
        } else {
            // Moving up => head decreases modulo rows.
            delta = (render_head + rows - next_head) % rows;
        }

        // If head changed but delta==0, we wrapped a full cycle.
        // Conservatively repaint everything.
        if ((next_head != render_head) && (delta == 0U)) {
            delta = rows;
        }

        if (delta > 1U) {
            const uint32_t skipped_rows = delta - 1U;
            if (gradient_render_direction_down != 0) {
                // Downwards: skipped rows are render+1 .. render+skipped.
                const uint32_t start_row = (render_head + 1U) % rows;
                skipped_blocks[0].row_start = start_row;
                skipped_blocks[0].row_count =
                    db_u32_min(rows - start_row, skipped_rows);
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_blocks[1].row_start = 0U;
                skipped_blocks[1].row_count =
                    skipped_rows - skipped_blocks[0].row_count;
                skipped_blocks[1].col_start = 0U;
                skipped_blocks[1].col_count = full_width_cols;
                skipped_count = (skipped_blocks[1].row_count > 0U) ? 2U : 1U;
            } else if (render_head >= skipped_rows) {
                // No wrap past 0: [render-skipped .. render-1]
                skipped_blocks[0].row_start = render_head - skipped_rows;
                skipped_blocks[0].row_count = skipped_rows;
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_count = (skipped_rows > 0U) ? 1U : 0U;
            } else {
                // Wraps past 0: [0 .. render-1] and [rows-underflow .. rows-1]
                const uint32_t underflow = skipped_rows - render_head;
                skipped_blocks[0].row_start = 0U;
                skipped_blocks[0].row_count = render_head;
                skipped_blocks[0].col_start = 0U;
                skipped_blocks[0].col_count = full_width_cols;
                skipped_blocks[1].row_start = rows - underflow;
                skipped_blocks[1].row_count = underflow;
                skipped_blocks[1].col_start = 0U;
                skipped_blocks[1].col_count = full_width_cols;
                skipped_count = 0U;
                if (skipped_blocks[0].row_count > 0U) {
                    skipped_count++;
                }
                if (skipped_blocks[1].row_count > 0U) {
                    skipped_count++;
                }
            }
        }
    }

    // Gradient patterns: damage-only updates directly on the default
    // framebuffer/backbuffer.
    if ((viewport_w > 0) && (viewport_h > 0) && (rows > 0U)) {
        if (g_state.runtime.backbuffer_draw_full != 0) {
            db_grid_block_t full_blocks[DB_GL1_GRADIENT_DIRTY_RANGE_CAP] = {
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
            };
            full_blocks[0] = (db_grid_block_t){
                .row_start = 0U,
                .row_count = rows,
                .col_start = 0U,
                .col_count = full_width_cols,
            };
            const size_t full_count = 1U;
            db_gl1_draw_gradient_dirty_blocks_mesh(
                full_blocks, full_count, gradient_render_head_row,
                gradient_render_direction_down, gradient_render_cycle_index,
                viewport_w, viewport_h);
            g_state.backbuffer_valid = 1;
            const db_gradient_state_t render_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };
            db_history_gradient_replay_state_store(&g_state.gradient_prev_frame,
                                                   full_blocks, full_count,
                                                   &render_state);
            db_history_record_draw_stats_for_work(
                &g_state.frame.full_draw_frames,
                &g_state.frame.dirty_draw_frames, 1, 0, 1U);
        } else {
            db_grid_block_t curr_draw_blocks[DB_GL1_GRADIENT_REPLAY_ROW_CAP] = {
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U},
                {0U, 0U, 0U, 0U}};
            size_t curr_draw_count = db_gradient_build_curr_draw_blocks(
                skipped_blocks, skipped_count, gradient_dirty_blocks,
                gradient_dirty_count, full_width_cols, curr_draw_blocks,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            const int needs_full_seed = db_history_should_seed_full_on_invalid(
                g_state.backbuffer_valid);
            const db_grid_block_t *persist_blocks = curr_draw_blocks;
            size_t persist_count = curr_draw_count;
            db_gradient_state_t persist_state = {
                .head_row = gradient_render_head_row,
                .cycle_index = gradient_render_cycle_index,
                .direction_down = gradient_render_direction_down,
            };

            if (needs_full_seed != 0) {
                g_state.backbuffer_valid = 1;
                curr_draw_count = 1U;
                curr_draw_blocks[0] = (db_grid_block_t){
                    .row_start = 0U,
                    .row_count = rows,
                    .col_start = 0U,
                    .col_count = full_width_cols,
                };
                db_gl1_draw_gradient_dirty_blocks_mesh(
                    curr_draw_blocks, curr_draw_count, gradient_render_head_row,
                    gradient_render_direction_down, gradient_render_cycle_index,
                    viewport_w, viewport_h);
                persist_count = curr_draw_count;
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 1, 0, 1U);
            } else {
                const int has_replay =
                    (preserved_framebuffer_count >= 2) &&
                    (g_state.gradient_prev_frame.draw_count > 0U);
                const int has_current = (curr_draw_count > 0U);
                db_grid_block_t replay_draw_blocks[4] = {{0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U},
                                                         {0U, 0U, 0U, 0U}};
                const db_grid_block_t *replay_draw_ptr =
                    g_state.gradient_prev_frame.draw_blocks;
                size_t replay_draw_count =
                    g_state.gradient_prev_frame.draw_count;
                if ((has_replay != 0) && (has_current != 0)) {
                    replay_draw_count = db_gradient_subtract_replay_blocks(
                        g_state.gradient_prev_frame.draw_blocks,
                        g_state.gradient_prev_frame.draw_count,
                        curr_draw_blocks, curr_draw_count, replay_draw_blocks,
                        DB_GRADIENT_DRAW_RANGE_WORK_CAP);
                    replay_draw_ptr = replay_draw_blocks;
                }
                const int has_replay_draw = (replay_draw_count > 0U);
                if ((g_state.runtime_flags.is_gradient_sweep != 0) &&
                    (has_replay != 0) && (has_current == 0)) {
                    // At bottom bounce, sweep can be replay-only for one frame.
                    persist_blocks = g_state.gradient_prev_frame.draw_blocks;
                    persist_count = g_state.gradient_prev_frame.draw_count;
                    persist_state = g_state.gradient_prev_frame.state;
                }

                if (has_replay_draw != 0) {
                    db_gl1_draw_gradient_dirty_blocks_mesh(
                        replay_draw_ptr, replay_draw_count,
                        g_state.gradient_prev_frame.state.head_row,
                        g_state.gradient_prev_frame.state.direction_down,
                        g_state.gradient_prev_frame.state.cycle_index,
                        viewport_w, viewport_h);
                }
                if (has_current != 0) {
                    db_gl1_draw_gradient_dirty_blocks_mesh(
                        curr_draw_blocks, curr_draw_count,
                        gradient_render_head_row,
                        gradient_render_direction_down,
                        gradient_render_cycle_index, viewport_w, viewport_h);
                }
                db_history_record_draw_stats_for_work(
                    &g_state.frame.full_draw_frames,
                    &g_state.frame.dirty_draw_frames, 0, 1,
                    ((has_replay_draw != 0) || (has_current != 0)) ? 1U : 0U);
            }

            // Persist the damage we drew this frame for next double-buffer
            // replay. `curr_draw_ranges` and replay-diff ranges are already
            // generated in normalized order, so avoid a second normalize pass.
            size_t persist_count_limited = persist_count;
            DB_LOG_CAPACITY_EXCEEDED_ONCE(
                BACKEND_NAME, "gradient_replay_persist_blocks", persist_count,
                DB_GL1_GRADIENT_REPLAY_ROW_CAP);
            if (persist_count_limited > DB_GL1_GRADIENT_REPLAY_ROW_CAP) {
                persist_count_limited = DB_GL1_GRADIENT_REPLAY_ROW_CAP;
            }
            db_history_gradient_replay_state_store(
                &g_state.gradient_prev_frame, persist_blocks,
                persist_count_limited, &persist_state);
        }
    }

    // Apply step AFTER rendering (CPU renderer ordering).
    db_history_apply_gradient_step_to_runtime(&g_state.runtime, &gradient_plan);
}
