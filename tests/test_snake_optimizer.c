#include "support/test_harness.h"

#include <stdint.h>
#include <string.h>

#include "renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_damage.h"
#include "renderers/renderer_benchmark_common_types_internal.h"
#include "renderers/renderer_benchmark_types.h"
#include "renderers/renderer_history_common.h"
#include "renderers/renderer_snake_collect.h"
#include "renderers/renderer_snake_common_types_internal.h"
#include "renderers/renderer_snake_shape_common.h"
#include "renderers/snake/renderer_snake_optimizer.h"

enum {
    DB_TEST_SNAKE_COUNT_SENTINEL = 99U,
    DB_TEST_SNAKE_NOOP_COLOR = 7U,
    DB_TEST_SNAKE_DISJOINT_COLOR = 9U,
    DB_TEST_SNAKE_GRID_COLOR = 11U,
    DB_TEST_SNAKE_SHAPE_COLOR = 13U,
    DB_TEST_SNAKE_ROW_BOUNDS_CAPACITY = 128,
    DB_TEST_SNAKE_BLOCK_CAPACITY = 256,
};

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

static void db_test_expect_damage_block(db_test_state_t *state,
                                        const db_grid_block_t *block,
                                        uint32_t row_start, uint32_t row_count,
                                        uint32_t col_start,
                                        uint32_t col_count) {
    DB_TEST_EXPECT_EQ_U32(state, block->row_start, row_start);
    DB_TEST_EXPECT_EQ_U32(state, block->row_count, row_count);
    DB_TEST_EXPECT_EQ_U32(state, block->col_start, col_start);
    DB_TEST_EXPECT_EQ_U32(state, block->col_count, col_count);
}

static void db_test_noop_collect(db_test_state_t *state) {
    const db_snake_region_t region = {
        .x = 0U, .y = 0U, .width = 4U, .height = 4U};
    const db_snake_plan_t plan = {.active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 0U};
    db_grid_block_t damage[4] = {0};
    db_snake_compact_block_t compact[4] = {0};
    size_t damage_count = DB_TEST_SNAKE_COUNT_SENTINEL;
    size_t compact_count = DB_TEST_SNAKE_COUNT_SENTINEL;
    uint32_t color = DB_TEST_SNAKE_NOOP_COLOR;
    DB_TEST_EXPECT_TRUE(state,
                        db_snake_collect_blocks_for_plan(
                            &region, &plan, NULL, 4U, 4U,
                            db_test_constant_color_bits, &color, damage, 4U,
                            &damage_count, compact, 4U, &compact_count) != 0);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_count, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, compact_count, 0U);
}

static void db_test_contiguous_collect(db_test_state_t *state) {
    const db_snake_region_t region = {
        .x = 0U, .y = 0U, .width = 4U, .height = 4U};
    const db_snake_plan_t plan = {.active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 4U};
    db_grid_block_t damage[4] = {0};
    db_snake_compact_block_t compact[4] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    uint32_t color = 3U;
    DB_TEST_EXPECT_TRUE(state,
                        db_snake_collect_blocks_for_plan(
                            &region, &plan, NULL, 4U, 4U,
                            db_test_constant_color_bits, &color, damage, 4U,
                            &damage_count, compact, 4U, &compact_count) != 0);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, compact_count, 1U);
    db_test_expect_damage_block(state, &damage[0], 0U, 1U, 0U, 4U);
    DB_TEST_EXPECT_EQ_U32(state, compact[0].row_start, 0U);
    DB_TEST_EXPECT_EQ_U32(state, compact[0].row_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, compact[0].col_start, 0U);
    DB_TEST_EXPECT_EQ_U32(state, compact[0].col_count, 4U);
}

static void db_test_disjoint_collect_order(db_test_state_t *state) {
    const db_snake_region_t region = {
        .x = 0U, .y = 0U, .width = 4U, .height = 4U};
    const db_snake_plan_t plan = {.active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 5U};
    db_grid_block_t damage[8] = {0};
    db_snake_compact_block_t compact[8] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    uint32_t color = DB_TEST_SNAKE_DISJOINT_COLOR;
    DB_TEST_EXPECT_TRUE(state,
                        db_snake_collect_blocks_for_plan(
                            &region, &plan, NULL, 4U, 4U,
                            db_test_constant_color_bits, &color, damage, 8U,
                            &damage_count, compact, 8U, &compact_count) != 0);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_count, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, compact_count, 2U);
    DB_TEST_EXPECT_TRUE(state, damage[0].row_start <= damage[1].row_start);
    DB_TEST_EXPECT_TRUE(state, compact[0].row_start <= compact[1].row_start);
}

static void
db_test_gl1_compact_unavailable_damage_available(db_test_state_t *state) {
    const db_snake_plan_t plan = {.active_shape_index = 0U,
                                  .active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 4U};
    db_grid_block_t damage[8] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    db_gl1_snake_frame_mode_t frame_mode =
        DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED;
    const db_gl1_damage_collect_ctx_t ctx = {
        .pattern = DB_PATTERN_SNAKE_GRID,
        .cols = 4U,
        .rows = 4U,
        .force_full_upload = 0,
        .snake_plan = &plan,
        .pattern_seed = 123456U,
        .snake_scratch = &(db_history_snake_scratch_t){0},
        .get_color_bits = db_test_constant_color_bits,
        .color_user_data = &(uint32_t){5U},
    };
    DB_TEST_EXPECT_TRUE(state, db_gl1_collect_current_snake_frame_blocks(
                                   &ctx, damage, 8U, &damage_count, NULL, 0U,
                                   &compact_count, &frame_mode) != 0);
    DB_TEST_EXPECT_TRUE(state, damage_count > 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, compact_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, frame_mode, DB_GL1_SNAKE_FRAME_MODE_COMPACT);
}

static void db_test_gl1_force_full_recovery(db_test_state_t *state) {
    const db_snake_plan_t plan = {.active_shape_index = 0U,
                                  .active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 4U};
    db_grid_block_t damage[1] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    db_gl1_snake_frame_mode_t frame_mode = DB_GL1_SNAKE_FRAME_MODE_COMPACT;
    const db_gl1_damage_collect_ctx_t ctx = {
        .pattern = DB_PATTERN_SNAKE_GRID,
        .cols = 4U,
        .rows = 4U,
        .force_full_upload = 1,
        .snake_plan = &plan,
        .pattern_seed = 123456U,
        .snake_scratch = &(db_history_snake_scratch_t){0},
        .get_color_bits = db_test_constant_color_bits,
        .color_user_data = &(uint32_t){5U},
    };
    DB_TEST_EXPECT_TRUE(state, db_gl1_collect_current_snake_frame_blocks(
                                   &ctx, damage, 1U, &damage_count, NULL, 0U,
                                   &compact_count, &frame_mode) != 0);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, frame_mode,
                          DB_GL1_SNAKE_FRAME_MODE_FULL_RECOVERY_REQUIRED);
    db_test_expect_damage_block(state, &damage[0], 0U, 4U, 0U, 4U);
}

static void db_test_pixel_block_equivalence_grid(db_test_state_t *state) {
    const db_snake_region_t region = {
        .x = 0U, .y = 0U, .width = 4U, .height = 4U};
    const db_snake_plan_t plan = {.active_cursor = 0U,
                                  .prev_start = 0U,
                                  .prev_count = 0U,
                                  .batch_size = 5U};
    db_grid_block_t damage[8] = {0};
    db_snake_compact_block_t compact[8] = {0};
    db_damage_block_t damage_pixels[8] = {0};
    db_damage_block_t compact_pixels[8] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    uint32_t color = DB_TEST_SNAKE_GRID_COLOR;
    DB_TEST_EXPECT_TRUE(state,
                        db_snake_collect_blocks_for_plan(
                            &region, &plan, NULL, 4U, 4U,
                            db_test_constant_color_bits, &color, damage, 8U,
                            &damage_count, compact, 8U, &compact_count) != 0);
    const size_t damage_pixel_count =
        db_snake_optimizer_build_pixel_blocks_from_damage_blocks(
            4U, 4U, 400U, 400U, damage, damage_count, damage_pixels, 8U);
    const size_t compact_pixel_count =
        db_snake_optimizer_build_pixel_blocks_from_compact_blocks(
            4U, 4U, 400U, 400U, compact, compact_count, compact_pixels, 8U);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_pixel_count, compact_pixel_count);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(damage_pixels, compact_pixels,
                      damage_pixel_count * sizeof(damage_pixels[0])) == 0);
}

static void db_test_shape_fixture_equivalence(db_test_state_t *state) {
    db_history_snake_scratch_t scratch = {0};
    db_snake_shape_row_bounds_t row_bounds[DB_TEST_SNAKE_ROW_BOUNDS_CAPACITY] =
        {0};
    scratch.shape.row_bounds = row_bounds;
    scratch.shape.row_bounds_capacity = DB_TEST_SNAKE_ROW_BOUNDS_CAPACITY;
    const db_snake_region_t region = db_snake_region_from_index(123456U, 0U);
    const db_snake_plan_t plan = {
        .active_shape_index = 0U,
        .active_cursor = 0U,
        .prev_start = 0U,
        .prev_count = 0U,
        .batch_size =
            db_checked_mul_u32("test_snake_optimizer", "shape_batch_size",
                               region.width, region.height),
    };
    const db_snake_shape_kind_t kind =
        db_snake_shapes_kind_from_index(123456U, 0U, DB_U32_SALT_PALETTE);
    db_snake_shape_cache_t shape_cache = {0};
    DB_TEST_EXPECT_TRUE(
        state, db_snake_shape_cache_init_from_index(
                   &shape_cache, row_bounds, DB_TEST_SNAKE_ROW_BOUNDS_CAPACITY,
                   123456U, 0U, DB_U32_SALT_PALETTE, &region, kind) != 0);
    db_grid_block_t damage[DB_TEST_SNAKE_BLOCK_CAPACITY] = {0};
    db_snake_compact_block_t compact[DB_TEST_SNAKE_BLOCK_CAPACITY] = {0};
    db_damage_block_t damage_pixels[DB_TEST_SNAKE_BLOCK_CAPACITY] = {0};
    db_damage_block_t compact_pixels[DB_TEST_SNAKE_BLOCK_CAPACITY] = {0};
    size_t damage_count = 0U;
    size_t compact_count = 0U;
    uint32_t color = DB_TEST_SNAKE_SHAPE_COLOR;
    DB_TEST_EXPECT_TRUE(
        state, db_snake_collect_blocks_for_plan(
                   &region, &plan, &shape_cache, db_snake_grid_cols_effective(),
                   db_snake_grid_rows_effective(), db_test_constant_color_bits,
                   &color, damage, DB_TEST_SNAKE_BLOCK_CAPACITY, &damage_count,
                   compact, DB_TEST_SNAKE_BLOCK_CAPACITY, &compact_count) != 0);
    DB_TEST_EXPECT_TRUE(state, damage_count > 0U);
    DB_TEST_EXPECT_TRUE(state, compact_count > 0U);
    const size_t damage_pixel_count = db_snake_optimizer_build_repair_blocks(
        db_snake_grid_cols_effective(), db_snake_grid_rows_effective(), 1000U,
        600U, damage, damage_count, NULL, 0U, damage_pixels,
        DB_TEST_SNAKE_BLOCK_CAPACITY);
    const size_t compact_pixel_count = db_snake_optimizer_build_repair_blocks(
        db_snake_grid_cols_effective(), db_snake_grid_rows_effective(), 1000U,
        600U, NULL, 0U, compact, compact_count, compact_pixels,
        DB_TEST_SNAKE_BLOCK_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, damage_pixel_count, compact_pixel_count);
    DB_TEST_EXPECT_TRUE(
        state, memcmp(damage_pixels, compact_pixels,
                      damage_pixel_count * sizeof(damage_pixels[0])) == 0);
}

unsigned db_snake_optimizer_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"noop_collect", db_test_noop_collect},
        {"contiguous_collect", db_test_contiguous_collect},
        {"disjoint_collect_order", db_test_disjoint_collect_order},
        {"gl1_compact_unavailable_damage_available",
         db_test_gl1_compact_unavailable_damage_available},
        {"gl1_force_full_recovery", db_test_gl1_force_full_recovery},
        {"pixel_block_equivalence_grid", db_test_pixel_block_equivalence_grid},
        {"shape_fixture_equivalence", db_test_shape_fixture_equivalence},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
