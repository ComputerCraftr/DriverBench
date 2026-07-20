#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "benchmarks/db_benchmark_checkpoint_internal.h"
#include "benchmarks/db_benchmark_core.h"
#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_frame_plan.h"
#include "core/db_geometry.h"
#include "core/db_numeric.h"
#include "core/db_render_ir.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

enum {
    TEST_CHECKPOINT_LAST_GREEN_INDEX = 6U,
    TEST_CHECKPOINT_SEARCH_ROWS = 64U,
    TEST_CHECKPOINT_SEARCH_LEVELS = 6U,
    TEST_CHECKPOINT_INVALID_FRAME = 9U,
};

static db_frame_plan_status_t
db_test_benchmark_generate_plan(db_benchmark_core_t *core, uint32_t frame_index,
                                const db_frame_plan_request_t *request,
                                db_frame_plan_t *plan) {
    db_frame_requirements_t requirements = {0};
    db_frame_plan_status_t status =
        db_benchmark_core_probe_frame(core, frame_index, &requirements);
    if ((status == DB_FRAME_PLAN_CHECKPOINT_REQUIRED) ||
        ((status == DB_FRAME_PLAN_OK) &&
         requirements.checkpoint_required != 0)) {
        db_frame_checkpoint_binding_t binding = {0};
        status = db_benchmark_core_provision_requirements(core, &requirements,
                                                          &binding);
    }
    return (status == DB_FRAME_PLAN_OK) ? db_benchmark_core_generate_plan(
                                              core, frame_index, request, plan)
                                        : status;
}

static db_benchmark_runtime_init_t
db_test_overlapping_runtime(db_test_state_t *state, const char *mode) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(state, db_init_benchmark_runtime_from_options(
                                   "test",
                                   &(const db_benchmark_runtime_options_t){
                                       .benchmark_mode_text = mode,
                                       .bench_speed_text = "1024",
                                       .random_seed_text = "123456",
                                       .backbuffer_draw_full = 0},
                                   &runtime) != 0);
    return runtime;
}

static void db_test_overlapping_checkpoint_is_fixed_and_transactional(
    db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime =
        db_test_overlapping_runtime(state, DB_BENCHMARK_MODE_SNAKE_RECT);
    db_benchmark_core_t core = {0};
    db_benchmark_core_init(&core, &runtime, DB_PIXEL_FORMAT_RGBA8);
    const size_t pixel_count =
        (size_t)db_grid_cols_effective() * db_grid_rows_effective();
    const size_t dirty_span_extent =
        pixel_count + (size_t)db_grid_rows_effective();
    const size_t dirty_span_capacity =
        (dirty_span_extent / 2U) + (dirty_span_extent % 2U);
    const size_t expected_bytes =
        (2U * pixel_count * DB_RGBA8_BYTES_PER_PIXEL) +
        (pixel_count * sizeof(uint32_t)) +
        (dirty_span_capacity * sizeof(db_benchmark_checkpoint_span_t));
    DB_TEST_EXPECT_EQ_INT(state, core.checkpoint.enabled, 0);

    db_frame_plan_t plan = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_core_generate_plan(&core, 0U, NULL, &plan),
        DB_FRAME_PLAN_CHECKPOINT_REQUIRED);
    DB_TEST_EXPECT_EQ_INT(state, core.checkpoint.enabled, 0);
    db_frame_requirements_t requirements = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_core_probe_frame(&core, 0U, &requirements),
        DB_FRAME_PLAN_CHECKPOINT_REQUIRED);
    DB_TEST_EXPECT_TRUE(state, requirements.checkpoint_required != 0);
    const uint64_t requirements_token = requirements.requirements_token;
    DB_TEST_EXPECT_EQ_SIZE(state, requirements.checkpoint_allocation_bytes,
                           expected_bytes);
    db_frame_checkpoint_binding_t binding = {0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_core_provision_requirements(
                              &core, &requirements, &binding),
                          DB_FRAME_PLAN_OK);
    DB_TEST_EXPECT_EQ_U64(state, requirements.requirements_token,
                          requirements_token);
    DB_TEST_EXPECT_TRUE(state, binding.valid != 0);
    DB_TEST_EXPECT_TRUE(state, binding.binding_token != requirements_token);
    core.checkpoint.generation++;
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_core_generate_plan(&core, 0U, NULL, &plan),
        DB_FRAME_PLAN_INVALID);
    core.checkpoint.generation--;
    const uint32_t cursor_before_stale_generation = core.runtime.snake.cursor;
    const uint64_t revision_before_stale_generation =
        core.checkpoint.content_revision;
    DB_TEST_EXPECT_EQ_INT(
        state, db_benchmark_core_generate_plan(&core, 1U, NULL, &plan),
        DB_FRAME_PLAN_INVALID);
    DB_TEST_EXPECT_EQ_U32(state, core.runtime.snake.cursor,
                          cursor_before_stale_generation);
    DB_TEST_EXPECT_TRUE(state, core.checkpoint.content_revision ==
                                   revision_before_stale_generation);
    const db_frame_plan_request_t prepared_request = {
        .force_rebuild = 1,
        .rebuild_reason = DB_FRAME_REBUILD_EXPLICIT,
        .preparation_token = UINT64_C(0x1234),
    };
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_benchmark_core_generate_plan(&core, 0U, &prepared_request, &plan),
        DB_FRAME_PLAN_OK);
    DB_TEST_EXPECT_TRUE(state, plan.preparation_token == UINT64_C(0x1234));
    DB_TEST_EXPECT_TRUE(state, core.checkpoint.enabled != 0);
    DB_TEST_EXPECT_EQ_SIZE(state, core.checkpoint.allocation_size_bytes,
                           expected_bytes);
    DB_TEST_EXPECT_EQ_SIZE(state, plan.external_bindings.count, 1U);
    DB_TEST_EXPECT_TRUE(state, plan.external_bindings.bindings[0].pixels ==
                                   core.checkpoint.surface.pixels);
    const uint64_t revision_before = core.checkpoint.content_revision;
    const uint32_t cursor_before = core.runtime.snake.cursor;
    db_benchmark_core_apply_plan(&core, &plan,
                                 &(const db_render_result_t){.success = 0});
    DB_TEST_EXPECT_TRUE(state,
                        core.checkpoint.content_revision == revision_before);
    DB_TEST_EXPECT_EQ_U32(state, core.runtime.snake.cursor, cursor_before);

    db_benchmark_core_apply_plan(&core, &plan,
                                 &(const db_render_result_t){.success = 1});
    DB_TEST_EXPECT_TRUE(state, core.checkpoint.content_revision ==
                                   revision_before + 1U);
    DB_TEST_EXPECT_EQ_U32(state, core.checkpoint.committed_frame_index, 0U);

    core.runtime.snake.shape_index = UINT32_MAX / 2U;
    core.pending_runtime = core.runtime;
    db_frame_plan_t rebuild = {0};
    db_test_benchmark_generate_plan(
        &core, 1U,
        &(const db_frame_plan_request_t){
            .force_rebuild = 1,
            .rebuild_reason = DB_FRAME_REBUILD_EXPLICIT,
        },
        &rebuild);
    DB_TEST_EXPECT_EQ_SIZE(state, rebuild.external_bindings.count, 1U);
    DB_TEST_EXPECT_TRUE(state, rebuild.update_metadata.instance_count <=
                                   DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY);
    DB_TEST_EXPECT_EQ_SIZE(state, core.checkpoint.allocation_size_bytes,
                           expected_bytes);
    db_benchmark_core_shutdown(&core);
}

static void
checkpoint_spans_preserve_last_writer_and_rollback(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double red[3] = {1.0, 0.0, 0.0};
    const double green[3] = {0.0, 1.0, 0.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 8U, 2U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 1U, 1U, 5U, red);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 1U, 3U, 4U, green);
    const uint64_t revision = checkpoint.content_revision;
    const db_frame_plan_t plan = {.frame_index = 7U};
    db_benchmark_checkpoint_commit(&checkpoint, &plan,
                                   &(const db_render_result_t){.success = 0});
    DB_TEST_EXPECT_EQ_U64(state, checkpoint.content_revision, revision);
    double observed[3] = {0};
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format, 3U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, black) != 0);

    db_benchmark_checkpoint_commit(&checkpoint, &plan,
                                   &(const db_render_result_t){.success = 1});
    DB_TEST_EXPECT_EQ_U64(state, checkpoint.content_revision, revision + 1U);
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format, 1U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, red) != 0);
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format, 3U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, green) != 0);
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format,
                                 TEST_CHECKPOINT_LAST_GREEN_INDEX, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, green) != 0);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void
invalid_checkpoint_span_cannot_commit_identity(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double red[3] = {1.0, 0.0, 0.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 4U, 2U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 1U, 0U, 1U, red);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 1U, 1U, 0U, 1U, red);
    DB_TEST_EXPECT_EQ_SIZE(state, checkpoint.overlay_dirty_span_count, 2U);
    checkpoint.overlay_dirty_spans[1].row = checkpoint.surface.pixel_height;
    const uint64_t revision = checkpoint.content_revision;
    db_benchmark_checkpoint_commit(
        &checkpoint,
        &(const db_frame_plan_t){.frame_index = TEST_CHECKPOINT_INVALID_FRAME},
        &(const db_render_result_t){.success = 1});
    DB_TEST_EXPECT_EQ_U64(state, checkpoint.content_revision, revision);
    DB_TEST_EXPECT_EQ_INT(state, checkpoint.committed_frame_valid, 0);
    DB_TEST_EXPECT_EQ_INT(state, checkpoint.overlay_valid, 0);
    double observed[3] = {0};
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format, 0U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, black) != 0);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void checkpoint_span_search_is_logarithmic(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double white[3] = {1.0, 1.0, 1.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(
                              &checkpoint, 8U, TEST_CHECKPOINT_SEARCH_ROWS,
                              DB_PIXEL_FORMAT_RGBA8, black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    db_benchmark_checkpoint_overlay_write(
        &checkpoint, 0U, TEST_CHECKPOINT_SEARCH_ROWS, 2U, 3U, white);
    DB_TEST_EXPECT_EQ_SIZE(state, checkpoint.overlay_dirty_span_count,
                           TEST_CHECKPOINT_SEARCH_ROWS);
    DB_TEST_EXPECT_TRUE(state, checkpoint.dirty_span_search_comparisons <=
                                   (uint64_t)TEST_CHECKPOINT_SEARCH_ROWS *
                                       TEST_CHECKPOINT_SEARCH_LEVELS);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void checkpoint_revision_cannot_wrap(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double white[3] = {1.0, 1.0, 1.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 1U, 1U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 1U, 0U, 1U, white);
    checkpoint.content_revision = UINT64_MAX;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_commit(
                              &checkpoint,
                              &(const db_frame_plan_t){.frame_index = 1U},
                              &(const db_render_result_t){.success = 1}),
                          0);
    double observed[3] = {0};
    db_rgb_pixels_read_index_f64(checkpoint.surface.pixels,
                                 checkpoint.surface.format, 0U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, black) != 0);
    DB_TEST_EXPECT_EQ_U64(state, checkpoint.content_revision, UINT64_MAX);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void malformed_checkpoint_index_is_rejected(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double white[3] = {1.0, 1.0, 1.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 2U, 1U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       white),
                          DB_BENCHMARK_CHECKPOINT_OK);
    checkpoint.pixel_count = 1U;
    double observed[3] = {1.0, 1.0, 1.0};
    db_benchmark_checkpoint_read_with_overlay(&checkpoint, 0U, 1U, observed);
    DB_TEST_EXPECT_TRUE(state, db_equal_f64_rgb3(observed, black) != 0);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    const size_t dirty_count = checkpoint.overlay_dirty_span_count;
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 1U, 1U, 1U, white);
    DB_TEST_EXPECT_EQ_INT(state, checkpoint.overlay_valid, 0);
    DB_TEST_EXPECT_EQ_SIZE(state, checkpoint.overlay_dirty_span_count,
                           dirty_count);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void
malformed_late_span_cannot_partially_commit(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double white[3] = {1.0, 1.0, 1.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 2U, 2U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 2U, 0U, 1U, white);
    uint8_t committed_before[16U] = {0};
    DB_TEST_EXPECT_EQ_SIZE(state, checkpoint.surface_size_bytes,
                           sizeof(committed_before));
    memcpy(committed_before, checkpoint.surface.pixels,
           checkpoint.surface_size_bytes);
    checkpoint.pixel_count = 2U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_commit(
                              &checkpoint,
                              &(const db_frame_plan_t){.frame_index = 1U},
                              &(const db_render_result_t){.success = 1}),
                          0);
    DB_TEST_EXPECT_TRUE(state,
                        memcmp(committed_before, checkpoint.surface.pixels,
                               checkpoint.surface_size_bytes) == 0);
    DB_TEST_EXPECT_EQ_U64(state, checkpoint.content_revision, 1U);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

static void
checkpoint_capacity_failure_is_transactional(db_test_state_t *state) {
    db_benchmark_checkpoint_t checkpoint = {0};
    const double black[3] = {0.0, 0.0, 0.0};
    const double white[3] = {1.0, 1.0, 1.0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_benchmark_checkpoint_init(&checkpoint, 2U, 2U,
                                                       DB_PIXEL_FORMAT_RGBA8,
                                                       black),
                          DB_BENCHMARK_CHECKPOINT_OK);
    db_benchmark_checkpoint_overlay_begin(&checkpoint);
    checkpoint.overlay_dirty_span_capacity = 1U;
    const db_benchmark_checkpoint_span_t sentinel =
        checkpoint.overlay_dirty_spans[0];
    uint8_t overlay_before[32] = {0};
    DB_TEST_EXPECT_TRUE(state, checkpoint.surface_size_bytes <=
                                   sizeof(overlay_before));
    memcpy(overlay_before, checkpoint.overlay_pixels,
           checkpoint.surface_size_bytes);
    db_benchmark_checkpoint_overlay_write(&checkpoint, 0U, 2U, 0U, 1U, white);
    DB_TEST_EXPECT_EQ_INT(state, checkpoint.overlay_valid, 0);
    DB_TEST_EXPECT_EQ_SIZE(state, checkpoint.overlay_dirty_span_count, 0U);
    DB_TEST_EXPECT_EQ_U32(state, checkpoint.overlay_dirty_spans[0].row,
                          sentinel.row);
    DB_TEST_EXPECT_EQ_U32(state, checkpoint.overlay_dirty_spans[0].col_start,
                          sentinel.col_start);
    DB_TEST_EXPECT_EQ_U32(state, checkpoint.overlay_dirty_spans[0].col_end,
                          sentinel.col_end);
    DB_TEST_EXPECT_TRUE(state, memcmp(overlay_before, checkpoint.overlay_pixels,
                                      checkpoint.surface_size_bytes) == 0);
    db_benchmark_checkpoint_shutdown(&checkpoint);
}

unsigned db_benchmark_checkpoint_transaction_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"overlapping_checkpoint_is_fixed_and_transactional",
         db_test_overlapping_checkpoint_is_fixed_and_transactional},
        {"checkpoint_spans_preserve_last_writer_and_rollback",
         checkpoint_spans_preserve_last_writer_and_rollback},
        {"invalid_checkpoint_span_cannot_commit_identity",
         invalid_checkpoint_span_cannot_commit_identity},
        {"checkpoint_span_search_is_logarithmic",
         checkpoint_span_search_is_logarithmic},
        {"checkpoint_revision_cannot_wrap", checkpoint_revision_cannot_wrap},
        {"malformed_checkpoint_index_is_rejected",
         malformed_checkpoint_index_is_rejected},
        {"malformed_late_span_cannot_partially_commit",
         malformed_late_span_cannot_partially_commit},
        {"checkpoint_capacity_failure_is_transactional",
         checkpoint_capacity_failure_is_transactional},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
