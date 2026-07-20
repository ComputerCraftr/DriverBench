#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "core/db_geometry.h"
#include "core/db_render_types.h"

#include "renderers/damage_trace.h"

static db_damage_trace_event_t
db_test_damage_event(const db_damage_block_t *blocks, size_t block_count) {
    return (db_damage_trace_event_t){
        .frame_index = 3U,
        .backend = DB_DAMAGE_TRACE_BACKEND_CPU,
        .stage = DB_DAMAGE_TRACE_STAGE_NORMALIZED,
        .operation = DB_DAMAGE_TRACE_OP_COPY,
        .source = DB_DAMAGE_TRACE_BUFFER_LOGICAL_PLAN,
        .destination = DB_DAMAGE_TRACE_BUFFER_CPU_SURFACE,
        .space = DB_DAMAGE_TRACE_SPACE_PIXEL,
        .width = 8U,
        .height = 4U,
        .pixel_format = DB_PIXEL_FORMAT_RGBA8,
        .blocks = blocks,
        .block_count = block_count,
        .result = DB_DAMAGE_TRACE_RESULT_EXECUTED,
    };
}

static void db_test_damage_trace_union_is_order_and_partition_independent(
    db_test_state_t *state) {
    const db_damage_block_t partitioned[] = {
        {.row_start = 0U, .row_count = 2U, .col_start = 0U, .col_count = 2U},
        {.row_start = 0U, .row_count = 2U, .col_start = 2U, .col_count = 2U},
    };
    const db_damage_block_t combined[] = {
        {.row_start = 0U, .row_count = 2U, .col_start = 0U, .col_count = 4U},
    };
    const db_damage_trace_event_t event_a =
        db_test_damage_event(partitioned, 2U);
    const db_damage_trace_event_t event_b = db_test_damage_event(combined, 1U);
    const db_damage_trace_summary_t summary_a =
        db_damage_trace_summarize(&event_a);
    const db_damage_trace_summary_t summary_b =
        db_damage_trace_summarize(&event_b);
    DB_TEST_EXPECT_EQ_SIZE(state, summary_a.covered_units, 8U);
    DB_TEST_EXPECT_EQ_SIZE(state, summary_b.covered_units, 8U);
    DB_TEST_EXPECT_TRUE(state, summary_a.union_hash == summary_b.union_hash);
}

static void db_test_damage_trace_reports_overlap_and_rejected_blocks(
    db_test_state_t *state) {
    const db_damage_block_t blocks[] = {
        {.row_start = 0U, .row_count = 2U, .col_start = 0U, .col_count = 4U},
        {.row_start = 1U, .row_count = 2U, .col_start = 2U, .col_count = 4U},
        {.row_start = 4U, .row_count = 1U, .col_start = 0U, .col_count = 1U},
    };
    const db_damage_trace_event_t event = db_test_damage_event(blocks, 3U);
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.valid_block_count, 2U);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.rejected_block_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.covered_units, 14U);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.duplicate_units, 2U);
}

static void db_test_damage_trace_detail_limits(db_test_state_t *state) {
    DB_TEST_EXPECT_EQ_SIZE(state, db_damage_trace_detail_count(200U, 0), 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_damage_trace_detail_count(200U, 1), 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_damage_trace_detail_count(200U, 2), 128U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_damage_trace_detail_count(200U, 3), 200U);
}

static void db_test_damage_trace_work_is_independent_of_surface_area(
    db_test_state_t *state) {
    const db_damage_block_t block = {
        .row_start = UINT32_MAX - 1U,
        .row_count = 1U,
        .col_start = UINT32_MAX - 1U,
        .col_count = 1U,
    };
    db_damage_trace_event_t event = db_test_damage_event(&block, 1U);
    event.width = UINT32_MAX;
    event.height = UINT32_MAX;
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.valid_block_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, summary.covered_units, 1U);
    DB_TEST_EXPECT_EQ_U64(state, summary.duplicate_units, 0U);

    event.width = 2U;
    event.height = 2U;
    const db_damage_block_t small_block = {
        .row_start = 1U, .row_count = 1U, .col_start = 1U, .col_count = 1U};
    event.blocks = &small_block;
    const db_damage_trace_summary_t small_summary =
        db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_U64(state, summary.band_block_comparisons,
                          small_summary.band_block_comparisons);
    DB_TEST_EXPECT_EQ_U64(state, summary.interval_sort_comparisons,
                          small_summary.interval_sort_comparisons);
    DB_TEST_EXPECT_EQ_U64(state, summary.interval_merge_comparisons,
                          small_summary.interval_merge_comparisons);
}

static void
db_test_damage_trace_region_work_is_bounded(db_test_state_t *state) {
    enum { BLOCK_COUNT = 1025U };
    db_damage_block_t *const blocks =
        (db_damage_block_t *)calloc(BLOCK_COUNT, sizeof(*blocks));
    DB_TEST_EXPECT_TRUE(state, blocks != NULL);
    if (blocks == NULL) {
        return;
    }
    for (uint32_t index = 0U; index < BLOCK_COUNT; index++) {
        blocks[index] = (db_damage_block_t){
            .row_start = index,
            .row_count = 1U,
            .col_count = 1U,
        };
    }
    db_damage_trace_event_t event = db_test_damage_event(blocks, BLOCK_COUNT);
    event.width = 1U;
    event.height = BLOCK_COUNT;
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_INT(state, summary.truncated, 1);
    DB_TEST_EXPECT_TRUE(state, summary.band_block_comparisons <=
                                   DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET);
    DB_TEST_EXPECT_TRUE(state, summary.interval_sort_comparisons <=
                                   DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET);
    DB_TEST_EXPECT_TRUE(state, summary.interval_merge_comparisons <=
                                   DB_DAMAGE_TRACE_REGION_COMPARISON_BUDGET);
    DB_TEST_EXPECT_EQ_U64(state, summary.band_block_comparisons, 0U);
    DB_TEST_EXPECT_EQ_U64(state, summary.interval_sort_comparisons, 0U);
    DB_TEST_EXPECT_EQ_U64(state, summary.interval_merge_comparisons, 0U);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.rejected_block_count, BLOCK_COUNT);
    free(blocks);
}

static void
db_test_damage_trace_area_widens_before_multiply(db_test_state_t *state) {
    const db_damage_block_t block = {
        .row_count = UINT32_MAX,
        .col_count = UINT32_MAX,
    };
    db_damage_trace_event_t event = db_test_damage_event(&block, 1U);
    event.width = UINT32_MAX;
    event.height = UINT32_MAX;
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    const uint64_t expected = (uint64_t)UINT32_MAX * (uint64_t)UINT32_MAX;
    DB_TEST_EXPECT_EQ_U64(state, summary.covered_units, expected);
    DB_TEST_EXPECT_EQ_U64(state, summary.duplicate_units, 0U);
}

static void db_test_damage_trace_duplicate_area_overflow_is_explicit(
    db_test_state_t *state) {
    const db_damage_block_t blocks[] = {
        {.row_count = UINT32_MAX, .col_count = UINT32_MAX},
        {.row_count = UINT32_MAX, .col_count = UINT32_MAX},
    };
    db_damage_trace_event_t event = db_test_damage_event(blocks, 2U);
    event.width = UINT32_MAX;
    event.height = UINT32_MAX;
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_INT(state, summary.truncated, 1);
    DB_TEST_EXPECT_EQ_U64(state, summary.covered_units,
                          (uint64_t)UINT32_MAX * (uint64_t)UINT32_MAX);
}

static void
db_test_damage_trace_rejects_workspace_size_overflow(db_test_state_t *state) {
    const db_damage_block_t sentinel = {0};
    db_damage_trace_event_t event = db_test_damage_event(&sentinel, SIZE_MAX);
    const db_damage_trace_summary_t summary = db_damage_trace_summarize(&event);
    DB_TEST_EXPECT_EQ_INT(state, summary.truncated, 1);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.rejected_block_count, SIZE_MAX);
    DB_TEST_EXPECT_EQ_SIZE(state, summary.valid_block_count, 0U);
}

unsigned db_damage_trace_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"damage_trace_union_is_order_and_partition_independent",
         db_test_damage_trace_union_is_order_and_partition_independent},
        {"damage_trace_reports_overlap_and_rejected_blocks",
         db_test_damage_trace_reports_overlap_and_rejected_blocks},
        {"damage_trace_detail_limits", db_test_damage_trace_detail_limits},
        {"damage_trace_work_is_independent_of_surface_area",
         db_test_damage_trace_work_is_independent_of_surface_area},
        {"damage_trace_region_work_is_bounded",
         db_test_damage_trace_region_work_is_bounded},
        {"damage_trace_area_widens_before_multiply",
         db_test_damage_trace_area_widens_before_multiply},
        {"damage_trace_duplicate_area_overflow_is_explicit",
         db_test_damage_trace_duplicate_area_overflow_is_explicit},
        {"damage_trace_rejects_workspace_size_overflow",
         db_test_damage_trace_rejects_workspace_size_overflow},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
