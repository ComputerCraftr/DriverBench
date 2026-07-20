#include "benchmarks/db_benchmark_emitters.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_geometry.h"
#include "core/db_render_ir.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stdint.h>

static const double test_rgb[3] = {0.25, 0.5, 0.75};

static void emitters_bands_use_ir_fills(db_test_state_t *state) {
    db_grid_block_t logical[1] = {};
    db_render_ir_fill_t fills[4] = {};
    db_benchmark_ir_emitter_t emitter = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .fills = fills,
        .fill_capacity = 4U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_emit_bands(16U, 8U, 4U, 0U, &emitter),
                          DB_BENCHMARK_IR_EMITTER_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.logical_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.fill_count, 4U);
    DB_TEST_EXPECT_EQ_INT(state, fills[0].rect.width, 4);
}

static void emitters_gradient_uses_ir_fills(db_test_state_t *state) {
    db_grid_block_t logical[2] = {};
    db_render_ir_fill_t fills[64] = {};
    db_benchmark_ir_emitter_t emitter = {
        .logical_blocks = logical,
        .logical_capacity = 2U,
        .fills = fills,
        .fill_capacity = 64U,
    };
    const db_gradient_damage_plan_t plan = {
        .render_state = {.head_row = 4U,
                         .cycle_index = 0U,
                         .direction_down = 1},
        .dirty_row_start = 0U,
        .dirty_row_count = 8U,
    };
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_emit_gradient(16U, 8U, &plan, 1, &emitter),
        DB_BENCHMARK_IR_EMITTER_OK);
    DB_TEST_EXPECT_TRUE(state, emitter.fill_count > 0U);
    DB_TEST_EXPECT_EQ_INT(state, fills[0].rect.width, 16);
}

static void grid_state_merges_adjacent_equal_spans(db_test_state_t *state) {
    static const double rgb[] = {
        0.25, 0.5, 0.75, 0.25, 0.5, 0.75, 0.25, 0.5, 0.75, 0.25, 0.5, 0.75,
    };
    const db_grid_block_t full = db_grid_block_full(2U, 2U);
    db_grid_block_t logical[1] = {};
    db_render_ir_fill_t fills[2] = {};
    db_benchmark_ir_emitter_t emitter = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .fills = fills,
        .fill_capacity = 2U,
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_benchmark_emit_grid_state_damage(
            2U, 2U, (db_grid_block_view_t){.blocks = &full, .count = 1U}, rgb,
            4U, &emitter),
        DB_BENCHMARK_IR_EMITTER_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.logical_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.fill_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, fills[0].rect.height, 2);
    DB_TEST_EXPECT_EQ_INT(state, fills[0].rect.width, 2);
}

static void emitters_report_capacity(db_test_state_t *state) {
    db_grid_block_t logical[1] = {};
    db_render_ir_fill_t fills[1] = {};
    db_benchmark_ir_emitter_t emitter = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .fills = fills,
        .fill_capacity = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_emit_bands(16U, 8U, 4U, 0U, &emitter),
                          DB_BENCHMARK_IR_EMITTER_CAPACITY);
}

static void
malformed_arenas_are_rejected_without_writes(db_test_state_t *state) {
    db_benchmark_ir_emitter_t damage = {.logical_capacity = 1U};
    const db_grid_block_t block = db_grid_block_full(1U, 1U);
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_ir_emitter_add_damage(&damage, &block), 0);
    DB_TEST_EXPECT_EQ_SIZE(state, damage.logical_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, damage.status,
                          DB_BENCHMARK_IR_EMITTER_INVALID);

    db_benchmark_ir_emitter_t fills = {.fill_capacity = 1U};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_benchmark_ir_emitter_add_rect(&fills, 0U, 1U, 0U, 1U, test_rgb), 0);
    DB_TEST_EXPECT_EQ_SIZE(state, fills.fill_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, fills.status, DB_BENCHMARK_IR_EMITTER_INVALID);

    db_benchmark_ir_emitter_t stale_damage = {.logical_count = 1U};
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_ir_emitter_add_damage(&stale_damage, &block), 0);
    DB_TEST_EXPECT_EQ_SIZE(state, stale_damage.logical_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, stale_damage.status,
                          DB_BENCHMARK_IR_EMITTER_INVALID);

    db_benchmark_ir_emitter_t stale_fills = {.fill_count = 1U};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_ir_emitter_add_rect(&stale_fills, 0U, 1U,
                                                           0U, 1U, test_rgb),
                          0);
    DB_TEST_EXPECT_EQ_SIZE(state, stale_fills.fill_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, stale_fills.status,
                          DB_BENCHMARK_IR_EMITTER_INVALID);
}

static void
rectangle_endpoint_overflow_is_transactional(db_test_state_t *state) {
    db_render_ir_fill_t storage[1] = {0};
    db_benchmark_ir_emitter_t emitter = {
        .fills = storage,
        .fill_capacity = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_ir_emitter_add_rect(
                              &emitter, INT32_MAX, 1U, 0U, 1U, test_rgb),
                          0);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.fill_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, emitter.status,
                          DB_BENCHMARK_IR_EMITTER_INVALID);

    db_benchmark_ir_emitter_reset(&emitter);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_ir_emitter_add_rect(
                              &emitter, 0U, 1U, INT32_MAX, 1U, test_rgb),
                          0);
    DB_TEST_EXPECT_EQ_SIZE(state, emitter.fill_count, 0U);
}

unsigned db_benchmark_emitters_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"emitters_bands_use_ir_fills", emitters_bands_use_ir_fills},
        {"emitters_gradient_uses_ir_fills", emitters_gradient_uses_ir_fills},
        {"grid_state_merges_adjacent_equal_spans",
         grid_state_merges_adjacent_equal_spans},
        {"emitters_report_capacity", emitters_report_capacity},
        {"malformed_arenas_are_rejected_without_writes",
         malformed_arenas_are_rejected_without_writes},
        {"rectangle_endpoint_overflow_is_transactional",
         rectangle_endpoint_overflow_is_transactional},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
