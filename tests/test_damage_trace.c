#include "support/test_harness.h"

#include <stddef.h>

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

unsigned db_damage_trace_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"damage_trace_union_is_order_and_partition_independent",
         db_test_damage_trace_union_is_order_and_partition_independent},
        {"damage_trace_reports_overlap_and_rejected_blocks",
         db_test_damage_trace_reports_overlap_and_rejected_blocks},
        {"damage_trace_detail_limits", db_test_damage_trace_detail_limits},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
