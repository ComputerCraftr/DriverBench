#include "support/test_harness.h"

#include "core/db_conformance.h"
#include "core/db_conformance_cache.h"
#include "core/db_gradient_divergence.h"
#include "core/db_render_ir.h"
#include "core/db_replay_policy.h"

#include <stdint.h>
#include <stdio.h>

enum { DB_TEST_DIVERGENCE_LINE_CAPACITY = 1024 };

static void compare_equal(db_test_state_t *state) {
    const uint8_t pixels[] = {1U, 2U, 3U, 255U, 4U, 5U, 6U, 255U};
    const db_gradient_divergence_t result = db_gradient_compare_rgba8(
        pixels, pixels, 2U, 1U, &(const db_gradient_compare_context_t){0});
    DB_TEST_EXPECT_EQ_INT(state, result.divergent, 0);
    DB_TEST_EXPECT_EQ_INT(state, result.stage, DB_GRADIENT_DIVERGENCE_NONE);
}

static void compare_first_conversion(db_test_state_t *state) {
    const uint8_t expected[] = {1U, 2U, 3U, 255U, 4U, 5U, 6U, 255U};
    const uint8_t observed[] = {1U, 2U, 3U, 255U, 4U, 9U, 6U, 255U};
    const db_gradient_divergence_t result = db_gradient_compare_rgba8(
        expected, observed, 2U, 1U,
        &(const db_gradient_compare_context_t){.command_index = 7U});
    DB_TEST_EXPECT_EQ_INT(state, result.divergent, 1);
    DB_TEST_EXPECT_EQ_INT(state, result.stage,
                          DB_GRADIENT_DIVERGENCE_CONVERSION);
    DB_TEST_EXPECT_EQ_U32(state, result.command_index, 7U);
    DB_TEST_EXPECT_EQ_U32(state, result.pixel_x, 1U);
    DB_TEST_EXPECT_EQ_U32(state, result.component, 1U);
    DB_TEST_EXPECT_EQ_U32(state, result.expected_rgba8[1], 5U);
    DB_TEST_EXPECT_EQ_U32(state, result.observed_rgba8[1], 9U);
}

static void compare_sentinel_coverage(db_test_state_t *state) {
    const uint8_t expected[] = {1U, 2U, 3U, 255U};
    const uint8_t observed[] = {9U, 2U, 3U, 255U};
    const db_gradient_divergence_t result = db_gradient_compare_rgba8(
        expected, observed, 1U, 1U,
        &(const db_gradient_compare_context_t){.expected_sentinel = 1,
                                               .observed_sentinel = 0});
    DB_TEST_EXPECT_EQ_INT(state, result.stage, DB_GRADIENT_DIVERGENCE_COVERAGE);
}

static void compare_readback_failure(db_test_state_t *state) {
    const uint8_t expected[] = {1U, 2U, 3U, 255U};
    const db_gradient_divergence_t result =
        db_gradient_compare_rgba8(expected, NULL, 1U, 1U, NULL);
    DB_TEST_EXPECT_EQ_INT(state, result.stage, DB_GRADIENT_DIVERGENCE_READBACK);
}

static void replay_policy_age_semantics(db_test_state_t *state) {
    db_replay_policy_t policy;
    db_replay_policy_init(&policy, 3U);
    db_replay_resolution_t resolved =
        db_replay_policy_resolve(&policy, 1U, 1, 1);
    DB_TEST_EXPECT_EQ_INT(state, resolved.use_rebuild, 0);
    DB_TEST_EXPECT_EQ_U32(state, resolved.history_stream_count, 0U);
    db_replay_policy_commit(&policy);
    resolved = db_replay_policy_resolve(&policy, 2U, 1, 1);
    DB_TEST_EXPECT_EQ_INT(state, resolved.use_rebuild, 0);
    DB_TEST_EXPECT_EQ_U32(state, resolved.history_stream_count, 1U);
    resolved = db_replay_policy_resolve(&policy, 3U, 1, 1);
    DB_TEST_EXPECT_EQ_INT(state, resolved.use_rebuild, 1);
    DB_TEST_EXPECT_STR_EQ(state, resolved.reason, "history_insufficient");
    resolved = db_replay_policy_resolve(&policy, 0U, 0, 1);
    DB_TEST_EXPECT_EQ_INT(state, resolved.use_rebuild, 1);
    DB_TEST_EXPECT_STR_EQ(state, resolved.reason, "buffer_age_unavailable");
}

static void divergence_file_uses_schema_two(db_test_state_t *state) {
    const char *const path = "/tmp/driverbench-gradient-divergence-test.log";
    (void)remove(path);
    const db_gradient_divergence_t divergence = {
        .stage = DB_GRADIENT_DIVERGENCE_COORDINATE,
        .command_index = 2U,
        .pixel_x = 3U,
        .pixel_y = 4U,
        .divergent = 1,
    };
    DB_TEST_EXPECT_TRUE(state, db_gradient_divergence_write(path, &divergence));
    FILE *const input = fopen(path, "rb");
    DB_TEST_EXPECT_TRUE(state, input != NULL);
    if (input != NULL) {
        char line[DB_TEST_DIVERGENCE_LINE_CAPACITY] = {0};
        DB_TEST_EXPECT_TRUE(state, fgets(line, sizeof(line), input) != NULL);
        DB_TEST_EXPECT_STR_CONTAINS(state, line,
                                    "event=gradient_divergence schema=2");
        DB_TEST_EXPECT_STR_CONTAINS(state, line, "stage=coordinate");
        (void)fclose(input);
    }
    (void)remove(path);
}

static void
bounded_vectors_preserve_original_axis_and_clips(db_test_state_t *state) {
    const db_render_ir_linear_gradient_command_t gradient = {
        .bounds = {.x = 0, .y = 10, .width = 8, .height = 4},
        .axis_start = 10,
        .axis_end = 13,
        .start_color = {.rgba = {0.0, 0.25, 0.5, 1.0}},
        .end_color = {.rgba = {1.0, 0.75, 0.0, 1.0}},
    };
    const db_render_ir_rect_t clips[] = {
        {.x = 0, .y = 11, .width = 4, .height = 2},
        {.x = 4, .y = 13, .width = 4, .height = 1},
    };
    db_gradient_vector_t vectors[3] = {};
    size_t count = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_gradient_vectors_generate(&gradient, clips, 2U, vectors, 3U, &count),
        DB_GRADIENT_VECTOR_OK);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 3U);
    DB_TEST_EXPECT_EQ_INT(state, vectors[0].logical_row, 11);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, vectors[0].interpolation_parameter,
                                1.0 / 3.0);
    DB_TEST_EXPECT_EQ_INT(state, vectors[2].logical_row, 13);
    DB_TEST_EXPECT_EQ_INT(state, vectors[2].rgba8[0], 255);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_gradient_vectors_generate(&gradient, clips, 2U, vectors, 2U, &count),
        DB_GRADIENT_VECTOR_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, count, 0U);
}

static void
topology_selects_one_consistent_implementation(db_test_state_t *state) {
    db_lane_qualification_t lanes[2] = {
        {.is_primary = 1,
         .semantic = DB_CONFORMANCE_CONFORMING,
         .exact_lookup = DB_CONFORMANCE_CONFORMING,
         .row_instances = DB_CONFORMANCE_CONFORMING},
        {.semantic = DB_CONFORMANCE_CONFORMING,
         .exact_lookup = DB_CONFORMANCE_CONFORMING,
         .row_instances = DB_CONFORMANCE_CONFORMING},
    };
    db_topology_qualification_t selected =
        db_topology_qualification_reduce(lanes, 2U);
    DB_TEST_EXPECT_EQ_INT(state, selected.implementation,
                          DB_GRADIENT_IMPLEMENTATION_SEMANTIC);
    DB_TEST_EXPECT_EQ_INT(state, selected.qualified, 1);

    lanes[1].semantic = DB_CONFORMANCE_NONCONFORMING;
    selected = db_topology_qualification_reduce(lanes, 2U);
    DB_TEST_EXPECT_EQ_INT(state, selected.implementation,
                          DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP);
    DB_TEST_EXPECT_EQ_SIZE(state, selected.conforming_lane_count, 2U);

    lanes[1].exact_lookup = DB_CONFORMANCE_NONCONFORMING;
    selected = db_topology_qualification_reduce(lanes, 2U);
    DB_TEST_EXPECT_EQ_INT(state, selected.implementation,
                          DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES);
    DB_TEST_EXPECT_EQ_INT(state, selected.qualified, 1);

    lanes[1].row_instances = DB_CONFORMANCE_UNTESTED;
    selected = db_topology_qualification_reduce(lanes, 2U);
    DB_TEST_EXPECT_EQ_INT(state, selected.qualified, 1);
    DB_TEST_EXPECT_EQ_SIZE(state, selected.lane_count, 1U);
    DB_TEST_EXPECT_EQ_SIZE(state, selected.removed_lane_count, 1U);
    DB_TEST_EXPECT_EQ_INT(state, selected.retained_lane_mask, 1);
    DB_TEST_EXPECT_STR_EQ(state, selected.reason,
                          "nonconforming_lanes_removed_semantic");

    lanes[0].row_instances = DB_CONFORMANCE_NONCONFORMING;
    selected = db_topology_qualification_reduce(lanes, 2U);
    DB_TEST_EXPECT_EQ_INT(state, selected.qualified, 0);
    DB_TEST_EXPECT_STR_EQ(state, selected.reason,
                          "primary_qualification_unavailable");
}

unsigned db_gradient_divergence_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"gradient_divergence_equal", compare_equal},
        {"gradient_divergence_first_conversion", compare_first_conversion},
        {"gradient_divergence_sentinel_coverage", compare_sentinel_coverage},
        {"gradient_divergence_readback_failure", compare_readback_failure},
        {"replay_policy_age_semantics", replay_policy_age_semantics},
        {"gradient_divergence_schema_two", divergence_file_uses_schema_two},
        {"bounded_vectors_preserve_original_axis_and_clips",
         bounded_vectors_preserve_original_axis_and_clips},
        {"topology_selects_one_consistent_implementation",
         topology_selects_one_consistent_implementation},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
