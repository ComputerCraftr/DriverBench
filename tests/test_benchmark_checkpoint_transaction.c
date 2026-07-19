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
#include "core/db_render_ir.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

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
    const size_t expected_bytes =
        (2U * pixel_count * DB_RGBA8_BYTES_PER_PIXEL) +
        (2U * pixel_count * sizeof(uint32_t));
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

unsigned db_benchmark_checkpoint_transaction_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"overlapping_checkpoint_is_fixed_and_transactional",
         db_test_overlapping_checkpoint_is_fixed_and_transactional},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
