#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_history_common.h"

static void db_test_snake_grid_initial_bootstrap_uses_seed_background(
    db_test_state_t *state) {
    db_history_snake_backbuffer_state_t backbuffer_state = {0};
    db_history_snake_backbuffer_state_reset(&backbuffer_state, 2U);
    const db_history_backbuffer_recovery_action_t action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_GRID, 1, 2U,
                                                &backbuffer_state);

    DB_TEST_EXPECT_EQ_INT(state, action.should_seed_background_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_rebuild_current_frame_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_force_full_upload_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.initial_seed_done, 0);
    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.backbuffer_valid, 1);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.seed_frames_remaining, 1U);
}

static void db_test_snake_grid_resize_invalidation_requests_rebuild(
    db_test_state_t *state) {
    db_history_snake_backbuffer_state_t backbuffer_state =
        db_history_snake_backbuffer_state_load(0U, 0U, 1, 1, 0);
    db_history_invalidate_snake_backbuffer_on_resize(
        DB_PATTERN_SNAKE_GRID, 2U, NULL, NULL, &backbuffer_state);
    const db_history_backbuffer_recovery_action_t action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_GRID, 1, 2U,
                                                &backbuffer_state);

    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.initial_seed_done, 1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_seed_background_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_rebuild_current_frame_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_force_full_upload_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.backbuffer_valid, 1);
    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.authoritative_rebuild_pending,
                          0);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.seed_frames_remaining, 0U);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.resync_frames_remaining, 1U);
}

static void db_test_snake_grid_resize_rebuilds_entire_preserved_chain(
    db_test_state_t *state) {
    db_history_snake_backbuffer_state_t backbuffer_state =
        db_history_snake_backbuffer_state_load(0U, 0U, 1, 1, 0);
    db_history_invalidate_snake_backbuffer_on_resize(
        DB_PATTERN_SNAKE_GRID, 2U, NULL, NULL, &backbuffer_state);

    const db_history_backbuffer_recovery_action_t first_action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_GRID, 1, 2U,
                                                &backbuffer_state);
    const db_history_backbuffer_recovery_action_t second_action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_GRID, 1, 2U,
                                                &backbuffer_state);
    const db_history_backbuffer_recovery_action_t third_action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_GRID, 1, 2U,
                                                &backbuffer_state);

    DB_TEST_EXPECT_EQ_INT(state, first_action.should_rebuild_current_frame_now,
                          1);
    DB_TEST_EXPECT_EQ_INT(state, first_action.should_force_full_upload_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, second_action.should_rebuild_current_frame_now,
                          1);
    DB_TEST_EXPECT_EQ_INT(state, second_action.should_force_full_upload_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, third_action.should_rebuild_current_frame_now,
                          0);
    DB_TEST_EXPECT_EQ_INT(state, third_action.should_force_full_upload_now, 0);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.resync_frames_remaining, 0U);
}

static void db_test_non_grid_resize_invalidation_keeps_bootstrap_seed_behavior(
    db_test_state_t *state) {
    db_history_snake_backbuffer_state_t backbuffer_state =
        db_history_snake_backbuffer_state_load(0U, 0U, 1, 1, 0);
    db_history_invalidate_snake_backbuffer_on_resize(
        DB_PATTERN_SNAKE_RECT, 2U, NULL, NULL, &backbuffer_state);
    const db_history_backbuffer_recovery_action_t action =
        db_history_eval_snake_backbuffer_action(DB_PATTERN_SNAKE_RECT, 1, 2U,
                                                &backbuffer_state);

    DB_TEST_EXPECT_EQ_INT(state, backbuffer_state.initial_seed_done, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_seed_background_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_rebuild_current_frame_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_force_full_upload_now, 1);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.seed_frames_remaining, 1U);
    DB_TEST_EXPECT_EQ_U32(state, backbuffer_state.resync_frames_remaining, 1U);
}

static void db_test_invalid_frame_rebuild_rule_matches_gradient_style(
    db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_INT(
        state, db_history_should_rebuild_current_frame_on_invalid(0), 1);
    DB_TEST_EXPECT_EQ_INT(
        state, db_history_should_rebuild_current_frame_on_invalid(1), 0);
}

static void
db_test_gradient_invalid_backbuffer_requests_rebuild(db_test_state_t *state) {
    const db_history_backbuffer_recovery_action_t action =
        db_history_eval_history_backbuffer_recovery_action(0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_seed_background_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_rebuild_current_frame_now, 1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_force_full_upload_now, 0);
}

static void
db_test_gradient_valid_backbuffer_is_incremental(db_test_state_t *state) {
    const db_history_backbuffer_recovery_action_t action =
        db_history_eval_history_backbuffer_recovery_action(1);
    DB_TEST_EXPECT_EQ_INT(state, action.should_seed_background_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_rebuild_current_frame_now, 0);
    DB_TEST_EXPECT_EQ_INT(state, action.should_force_full_upload_now, 0);
}

unsigned db_snake_history_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"snake_grid_initial_bootstrap_uses_seed_background",
         db_test_snake_grid_initial_bootstrap_uses_seed_background},
        {"snake_grid_resize_invalidation_requests_rebuild",
         db_test_snake_grid_resize_invalidation_requests_rebuild},
        {"snake_grid_resize_rebuilds_entire_preserved_chain",
         db_test_snake_grid_resize_rebuilds_entire_preserved_chain},
        {"non_grid_resize_invalidation_keeps_bootstrap_seed_behavior",
         db_test_non_grid_resize_invalidation_keeps_bootstrap_seed_behavior},
        {"invalid_frame_rebuild_rule_matches_gradient_style",
         db_test_invalid_frame_rebuild_rule_matches_gradient_style},
        {"gradient_invalid_backbuffer_requests_rebuild",
         db_test_gradient_invalid_backbuffer_requests_rebuild},
        {"gradient_valid_backbuffer_is_incremental",
         db_test_gradient_valid_backbuffer_is_incremental},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
