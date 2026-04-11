#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

#include "benchmarks/db_benchmark_emitters.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_geometry.h"
#include "core/db_geometry_builder.h"
#include "core/db_render_types.h"

static void db_test_geometry_builder_preserves_order_and_merges_only_previous(
    db_test_state_t *state) {
    db_grid_block_t logical[2] = {{0}};
    db_colored_f64_block_t colored[4] = {{0}};
    db_geometry_builder_t builder = {
        .logical_blocks = logical,
        .logical_capacity = 2U,
        .colored_blocks = colored,
        .colored_capacity = 4U,
    };
    static const double red[3] = {1.0, 0.0, 0.0};
    static const double blue[3] = {0.0, 0.0, 1.0};
    db_geometry_builder_reset(&builder);
    DB_TEST_EXPECT_TRUE(
        state, db_geometry_builder_add_span(&builder, 0U, 0U, 2U, red));
    DB_TEST_EXPECT_TRUE(
        state, db_geometry_builder_add_span(&builder, 1U, 0U, 2U, red));
    DB_TEST_EXPECT_TRUE(
        state, db_geometry_builder_add_span(&builder, 2U, 0U, 2U, blue));
    DB_TEST_EXPECT_TRUE(
        state, db_geometry_builder_add_span(&builder, 3U, 0U, 2U, red));
    DB_TEST_EXPECT_EQ_SIZE(state, builder.colored_count, 3U);
    DB_TEST_EXPECT_EQ_U32(state, colored[0].row_count, 2U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, colored[1].rgb[2], 1.0);
    DB_TEST_EXPECT_EQ_U32(state, colored[2].row_start, 3U);
}

static void db_test_geometry_builder_normalizes_damage_independently(
    db_test_state_t *state) {
    db_grid_block_t logical[2] = {{0}};
    db_geometry_builder_t builder = {
        .logical_blocks = logical,
        .logical_capacity = 2U,
    };
    db_geometry_builder_reset(&builder);
    const db_grid_block_t first = {
        .row_start = 4U, .row_count = 1U, .col_start = 1U, .col_count = 2U};
    const db_grid_block_t second = {
        .row_start = 4U, .row_count = 1U, .col_start = 3U, .col_count = 1U};
    DB_TEST_EXPECT_TRUE(state,
                        db_geometry_builder_add_damage(&builder, &first));
    DB_TEST_EXPECT_TRUE(state,
                        db_geometry_builder_add_damage(&builder, &second));
    DB_TEST_EXPECT_EQ_SIZE(state, builder.logical_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, logical[0].col_start, 1U);
    DB_TEST_EXPECT_EQ_U32(state, logical[0].col_count, 3U);
}

static void
db_test_geometry_builder_reports_invalid_and_overflow(db_test_state_t *state) {
    db_colored_f64_block_t colored[1] = {{0}};
    db_geometry_builder_t builder = {
        .colored_blocks = colored,
        .colored_capacity = 1U,
    };
    static const double rgb[3] = {0.25, 0.5, 0.75};
    db_geometry_builder_reset(&builder);
    DB_TEST_EXPECT_TRUE(
        state, db_geometry_builder_add_span(&builder, 0U, 0U, 1U, rgb));
    DB_TEST_EXPECT_EQ_INT(
        state, db_geometry_builder_add_span(&builder, 0U, 1U, 2U, rgb), 0);
    DB_TEST_EXPECT_EQ_INT(state, builder.status, DB_GEOMETRY_BUILDER_OVERFLOW);
    db_geometry_builder_reset(&builder);
    DB_TEST_EXPECT_EQ_INT(
        state, db_geometry_builder_add_span(&builder, 0U, 2U, 2U, rgb), 0);
    DB_TEST_EXPECT_EQ_INT(state, builder.status, DB_GEOMETRY_BUILDER_INVALID);
}

static void db_test_emitters_bands_use_double_blocks(db_test_state_t *state) {
    db_grid_block_t logical[1] = {{0}};
    db_colored_f64_block_t colored[4] = {{0}};
    db_block_emitter_sink_t sink = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .colored_blocks = colored,
        .colored_capacity = 4U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_emit_bands(16U, 8U, 4U, 0U, &sink),
                          DB_BLOCK_EMITTER_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, sink.logical_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, sink.colored_count, 4U);
    DB_TEST_EXPECT_EQ_U32(state, colored[0].col_count, 4U);
}

static void db_test_emitters_gradient_uses_common_sink(db_test_state_t *state) {
    db_grid_block_t logical[2] = {{0}, {0}};
    db_colored_f64_block_t colored[64] = {{0}};
    db_block_emitter_sink_t sink = {
        .logical_blocks = logical,
        .logical_capacity = 2U,
        .colored_blocks = colored,
        .colored_capacity = 64U,
    };
    const db_gradient_damage_plan_t plan = {
        .render_state = {.head_row = 4U,
                         .cycle_index = 0U,
                         .direction_down = 1},
        .dirty_row_start = 0U,
        .dirty_row_count = 8U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_emit_gradient(16U, 8U, &plan, 1, &sink),
                          DB_BLOCK_EMITTER_STATUS_OK);
    DB_TEST_EXPECT_TRUE(state, sink.colored_count > 0U);
    DB_TEST_EXPECT_EQ_U32(state, colored[0].col_count, 16U);
}

static void
db_test_emitters_grid_state_uses_canonical_compactor(db_test_state_t *state) {
    static const double rgb[] = {
        0.25, 0.5, 0.75, 0.25, 0.5, 0.75, 0.25, 0.5, 0.75, 0.25, 0.5, 0.75,
    };
    const db_grid_block_t full = db_grid_block_full(2U, 2U);
    db_grid_block_t logical[1] = {{0}};
    db_colored_f64_block_t colored[2] = {{0}};
    db_colored_f64_block_t scratch[2] = {{0}};
    db_colored_f64_block_t compacted[2] = {{0}};
    db_block_emitter_sink_t sink = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .colored_blocks = colored,
        .colored_capacity = 2U,
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_benchmark_emit_grid_state_damage(
            2U, 2U, (db_grid_block_view_t){.blocks = &full, .count = 1U}, rgb,
            4U, &sink),
        DB_BLOCK_EMITTER_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, sink.logical_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, sink.colored_count, 1U);
    size_t compacted_count = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_colored_f64_blocks_compact(
            (db_colored_f64_block_view_t){.blocks = colored,
                                          .count = sink.colored_count},
            scratch, 2U, compacted, 2U, &compacted_count),
        DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, compacted_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, compacted[0].row_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, compacted[0].col_count, 2U);
}

static void db_test_compactor_preserves_overdraw_order(db_test_state_t *state) {
    static const db_colored_f64_block_t input[] = {
        {.row_start = 0U,
         .row_count = 4U,
         .col_start = 0U,
         .col_count = 4U,
         .rgb = {0.22, 0.22, 0.22}},
        {.row_start = 0U,
         .row_count = 1U,
         .col_start = 0U,
         .col_count = 2U,
         .rgb = {0.12, 0.85, 0.20}},
        {.row_start = 1U,
         .row_count = 1U,
         .col_start = 0U,
         .col_count = 2U,
         .rgb = {0.22, 0.22, 0.22}},
    };
    db_colored_f64_block_t scratch[3] = {{0}};
    db_colored_f64_block_t output[3] = {{0}};
    size_t output_count = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_colored_f64_blocks_compact(
            (db_colored_f64_block_view_t){
                .blocks = input, .count = sizeof(input) / sizeof(input[0])},
            scratch, sizeof(scratch) / sizeof(scratch[0]), output,
            sizeof(output) / sizeof(output[0]), &output_count),
        DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, output_count, 3U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, output[0].rgb[0], 0.22);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, output[1].rgb[1], 0.85);
    DB_TEST_EXPECT_EQ_U32(state, output[2].row_start, 1U);
}

static void db_test_emitters_report_capacity_overflow(db_test_state_t *state) {
    db_grid_block_t logical[1] = {{0}};
    db_colored_f64_block_t colored[1] = {{0}};
    db_block_emitter_sink_t sink = {
        .logical_blocks = logical,
        .logical_capacity = 1U,
        .colored_blocks = colored,
        .colored_capacity = 1U,
    };
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_emit_bands(16U, 8U, 4U, 0U, &sink),
                          DB_BLOCK_EMITTER_STATUS_OVERFLOW);
}

static db_colored_f64_block_t history_block(uint32_t row, double red) {
    const double green = 0.25;
    const double blue = 0.5;
    return (db_colored_f64_block_t){
        .row_start = row,
        .row_count = 1U,
        .col_start = 0U,
        .col_count = 2U,
        .rgb = {red, green, blue},
    };
}

static void
db_test_geometry_history_is_bounded_and_chronological(db_test_state_t *state) {
    db_colored_f64_block_t storage[4] = {{0}};
    size_t frame_counts[2] = {0U, 0U};
    db_colored_f64_block_t scratch[2] = {{0}};
    db_geometry_history_t history = {0};
    db_geometry_history_init(&history, storage, frame_counts, 2U, 2U);
    const db_colored_f64_block_t first = history_block(1U, 0.1);
    const db_colored_f64_block_t second = history_block(2U, 0.2);
    const db_colored_f64_block_t third = history_block(3U, 0.3);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_geometry_history_append(
            &history,
            (db_colored_f64_block_view_t){.blocks = &first, .count = 1U},
            scratch, 2U),
        DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_geometry_history_append(
            &history,
            (db_colored_f64_block_view_t){.blocks = &second, .count = 1U},
            scratch, 2U),
        DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_geometry_history_append(
            &history,
            (db_colored_f64_block_view_t){.blocks = &third, .count = 1U},
            scratch, 2U),
        DB_GEOMETRY_F64_STATUS_OK);

    db_colored_f64_block_t assembled[3] = {{0}};
    size_t historical_count = 0U;
    size_t assembled_count = 0U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_geometry_history_assemble(
                              &history, 2U, (db_colored_f64_block_view_t){0},
                              assembled, 3U, &historical_count,
                              &assembled_count),
                          DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, historical_count, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, assembled_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, assembled[0].row_start, 2U);
    DB_TEST_EXPECT_EQ_U32(state, assembled[1].row_start, 3U);
}

static void db_test_pixel_union_ignores_color_and_preserves_coverage(
    db_test_state_t *state) {
    const db_colored_f64_block_t input[] = {
        {.row_count = 4U, .col_count = 2U, .rgb = {1.0, 0.0, 0.0}},
        {.row_count = 4U,
         .col_start = 2U,
         .col_count = 2U,
         .rgb = {0.0, 1.0, 0.0}},
        {.row_start = 4U,
         .row_count = 1U,
         .col_start = 6U,
         .col_count = 2U,
         .rgb = {0.0, 0.0, 1.0}},
    };
    db_damage_block_t output[3] = {{0}};
    size_t output_count = 0U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_colored_f64_blocks_pixel_union(
                              (db_colored_f64_block_view_t){
                                  .blocks = input,
                                  .count = sizeof(input) / sizeof(input[0]),
                              },
                              8U, 8U, 16U, 16U, output,
                              sizeof(output) / sizeof(output[0]),
                              &output_count),
                          DB_GEOMETRY_F64_STATUS_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, output_count, 2U);
    DB_TEST_EXPECT_EQ_U32(state, output[0].row_count, 8U);
    DB_TEST_EXPECT_EQ_U32(state, output[0].col_count, 8U);
    DB_TEST_EXPECT_EQ_U32(state, output[1].row_start, 8U);
    DB_TEST_EXPECT_EQ_U32(state, output[1].col_start, 12U);
}

unsigned db_benchmark_emitters_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"geometry_builder_preserves_order_and_merges_only_previous",
         db_test_geometry_builder_preserves_order_and_merges_only_previous},
        {"geometry_builder_normalizes_damage_independently",
         db_test_geometry_builder_normalizes_damage_independently},
        {"geometry_builder_reports_invalid_and_overflow",
         db_test_geometry_builder_reports_invalid_and_overflow},
        {"emitters_bands_use_double_blocks",
         db_test_emitters_bands_use_double_blocks},
        {"emitters_gradient_uses_common_sink",
         db_test_emitters_gradient_uses_common_sink},
        {"emitters_grid_state_uses_canonical_compactor",
         db_test_emitters_grid_state_uses_canonical_compactor},
        {"compactor_preserves_overdraw_order",
         db_test_compactor_preserves_overdraw_order},
        {"emitters_report_capacity_overflow",
         db_test_emitters_report_capacity_overflow},
        {"geometry_history_is_bounded_and_chronological",
         db_test_geometry_history_is_bounded_and_chronological},
        {"pixel_union_ignores_color_and_preserves_coverage",
         db_test_pixel_union_ignores_color_and_preserves_coverage},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
