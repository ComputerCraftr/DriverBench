#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "renderers/delta/renderer_frame_delta.h"
#include "renderers/delta/renderer_frame_delta_consumers.h"
#include "renderers/delta/renderer_frame_delta_producers.h"
#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_snake_types.h"

enum { DB_TEST_FRAME_DELTA_NOOP_COLOR = 7U };

static void db_test_constant_color_bits(uint32_t row, uint32_t col,
                                        void *user_data, uint32_t *color_bits) {
    (void)row;
    (void)col;
    const uint32_t base =
        (user_data != NULL) ? *(const uint32_t *)user_data : 1U;
    color_bits[0] = base;
    color_bits[1] = base;
    color_bits[2] = base;
}

static void db_test_frame_delta_bands_full_rebuild(db_test_state_t *state) {
    db_grid_block_t logical_damage[1] = {{0U, 0U, 0U, 0U}};
    db_damage_block_t repair[1] = {{0U, 0U, 0U, 0U}};
    db_frame_delta_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(state, db_frame_delta_produce_bands(
                                   &(const db_frame_delta_bands_producer_t){
                                       .pattern = DB_PATTERN_BANDS,
                                       .rows = 8U,
                                       .cols = 16U,
                                       .pixel_width = 160U,
                                       .pixel_height = 80U,
                                       .damage_blocks = logical_damage,
                                       .damage_capacity = 1U,
                                       .repair_blocks = repair,
                                       .repair_capacity = 1U,
                                   },
                                   &plan) != 0);
    DB_TEST_EXPECT_EQ_U32(state, plan.mode, DB_FRAME_DELTA_MODE_FULL_REBUILD);
    DB_TEST_EXPECT_EQ_SIZE(state, plan.logical_damage_block_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, plan.logical_damage_blocks[0].row_count, 8U);
    DB_TEST_EXPECT_EQ_U32(state, plan.logical_damage_blocks[0].col_count, 16U);
    DB_TEST_EXPECT_EQ_SIZE(state, plan.repair_block_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, plan.repair_blocks[0].row_count, 80U);
    DB_TEST_EXPECT_EQ_U32(state, plan.repair_blocks[0].col_count, 160U);
}

static void
db_test_frame_delta_gradient_dirty_and_compact(db_test_state_t *state) {
    db_grid_block_t logical_damage[4] = {0};
    db_frame_delta_compact_block_t compact[2] = {0};
    db_damage_block_t repair[4] = {0};
    db_frame_delta_plan_t plan = {0};
    DB_TEST_EXPECT_TRUE(state, db_frame_delta_produce_gradient(
                                   &(const db_frame_delta_gradient_producer_t){
                                       .pattern = DB_PATTERN_GRADIENT_FILL,
                                       .head_row = 0U,
                                       .direction_down = 1,
                                       .cycle_index = 0U,
                                       .head_step = 1U,
                                       .rows = 64U,
                                       .cols = 64U,
                                       .pixel_width = 64U,
                                       .pixel_height = 64U,
                                       .damage_blocks = logical_damage,
                                       .damage_capacity = 4U,
                                       .compact_blocks = compact,
                                       .compact_capacity = 2U,
                                       .repair_blocks = repair,
                                       .repair_capacity = 4U,
                                   },
                                   &plan) != 0);
    DB_TEST_EXPECT_TRUE(state, plan.logical_damage_block_count > 0U);
    DB_TEST_EXPECT_TRUE(state, plan.compact_block_count > 0U);
    DB_TEST_EXPECT_TRUE(state, plan.repair_block_count > 0U);
    DB_TEST_EXPECT_TRUE(state, plan.replay_safe != 0);
    DB_TEST_EXPECT_TRUE(state, plan.ring_repair_safe != 0);
}

static void
db_test_frame_delta_normalize_adjacent_blocks(db_test_state_t *state) {
    const db_grid_block_t input[2] = {
        {.row_start = 4U, .row_count = 2U, .col_start = 0U, .col_count = 32U},
        {.row_start = 6U, .row_count = 3U, .col_start = 0U, .col_count = 32U},
    };
    db_grid_block_t output[2] = {0};
    const size_t out_count =
        db_frame_delta_normalize_grid_blocks(input, 2U, 32U, 32U, output, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, out_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_start, 4U);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_count, 5U);
}

static void db_test_frame_delta_repair_prefers_damage(db_test_state_t *state) {
    const db_grid_block_t damage[1] = {
        {.row_start = 1U, .row_count = 2U, .col_start = 3U, .col_count = 4U},
    };
    const db_frame_delta_compact_block_t compact[1] = {
        {.row_start = 0U,
         .row_count = 8U,
         .col_start = 0U,
         .col_count = 8U,
         .color_bits = {1U, 1U, 1U}},
    };
    db_damage_block_t repair[2] = {0};
    const db_frame_delta_plan_t plan = {
        .benchmark_kind = DB_PATTERN_SNAKE_GRID,
        .mode = DB_FRAME_DELTA_MODE_COMPACT_GEOMETRY,
        .logical_damage_blocks = damage,
        .logical_damage_block_count = 1U,
        .compact_blocks = compact,
        .compact_block_count = 1U,
    };
    const size_t repair_count = db_frame_delta_build_repair_blocks_from_plan(
        &plan, 8U, 8U, 80U, 80U, repair, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, repair_count, 1U);
    DB_TEST_EXPECT_TRUE(state, repair[0].row_count < 80U);
    DB_TEST_EXPECT_TRUE(state, repair[0].col_count < 80U);
}

static void db_test_frame_delta_snake_noop(db_test_state_t *state) {
    const db_snake_region_t region = {
        .x = 0U, .y = 0U, .width = 4U, .height = 4U};
    const db_snake_plan_t plan = {.active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 0U};
    db_grid_block_t damage[4] = {0};
    db_frame_delta_compact_block_t compact[4] = {0};
    db_damage_block_t repair[4] = {0};
    db_frame_delta_plan_t delta = {0};
    uint32_t color = DB_TEST_FRAME_DELTA_NOOP_COLOR;
    DB_TEST_EXPECT_TRUE(state,
                        db_frame_delta_produce_snake(
                            &(const db_frame_delta_snake_producer_t){
                                .pattern = DB_PATTERN_SNAKE_GRID,
                                .region = &region,
                                .plan = &plan,
                                .shape_cache = NULL,
                                .cols = 4U,
                                .rows = 4U,
                                .pixel_width = 40U,
                                .pixel_height = 40U,
                                .force_full_recovery = 0,
                                .get_color_bits = db_test_constant_color_bits,
                                .color_user_data = &color,
                                .damage_blocks = damage,
                                .damage_capacity = 4U,
                                .compact_blocks = compact,
                                .compact_capacity = 4U,
                                .repair_blocks = repair,
                                .repair_capacity = 4U,
                            },
                            &delta) != 0);
    DB_TEST_EXPECT_EQ_U32(state, delta.mode, DB_FRAME_DELTA_MODE_NO_OP);
    DB_TEST_EXPECT_EQ_SIZE(state, delta.logical_damage_block_count, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, delta.compact_block_count, 0U);
}

unsigned db_frame_delta_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"bands_full_rebuild", db_test_frame_delta_bands_full_rebuild},
        {"gradient_dirty_and_compact",
         db_test_frame_delta_gradient_dirty_and_compact},
        {"normalize_adjacent_blocks",
         db_test_frame_delta_normalize_adjacent_blocks},
        {"repair_prefers_damage", db_test_frame_delta_repair_prefers_damage},
        {"snake_noop", db_test_frame_delta_snake_noop},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
