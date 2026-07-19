#include "core/db_run_session.h"

#include "benchmarks/db_benchmark_runtime.h"
#include "benchmarks/db_benchmark_runtime_internal.h"
#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_frame_contracts.h"
#include "core/db_frame_plan.h"
#include "core/db_probe_protocol.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "support/test_harness.h"

#include <stdint.h>

enum {
    DB_RUN_SESSION_TEST_WIDTH = 1000U,
    DB_RUN_SESSION_TEST_HEIGHT = 600U,
    DB_RUN_SESSION_TEST_FRAME_LIMIT = 1000U,
    DB_RUN_SESSION_TEST_STEADY_FRAMES = 256U,
};

typedef struct {
    db_qualification_identity_generation_t generation;
    db_renderer_applied_selection_t active;
    uint32_t describe_count;
    uint32_t prepare_count;
    uint32_t commit_apply_count;
    uint32_t abort_apply_count;
    uint32_t acquire_count;
    uint32_t execute_count;
    uint32_t present_count;
    int reject_prepare;
} db_run_session_fixture_t;

static int
describe_qualification(void *context,
                       db_renderer_qualification_descriptor_store_t *store) {
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->describe_count++;
    *store = (db_renderer_qualification_descriptor_store_t){
        .generation = fixture->generation,
    };
    return db_qualification_descriptor_store_append(
        store, &(const db_renderer_probe_descriptor_t){
                   .backend = DB_PROBE_BACKEND_GL3,
                   .strategy = DB_RENDER_TARGET_GL3_PERSISTENT_FBO,
                   .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
                   .lane_index = 0U,
                   .is_primary = 1,
                   .working_format = DB_PIXEL_FORMAT_RGBA8,
                   .logical_width = DB_RUN_SESSION_TEST_WIDTH,
                   .logical_height = DB_RUN_SESSION_TEST_HEIGHT,
                   .compatibility_validated = 1,
               });
}

static db_renderer_prepare_status_t
prepare_qualification(void *context,
                      const db_qualification_snapshot_t *snapshot,
                      db_renderer_selection_candidate_t *candidate) {
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->prepare_count++;
    if (fixture->reject_prepare != 0) {
        return DB_RENDERER_PREPARE_UNAVAILABLE;
    }
    *candidate = (db_renderer_selection_candidate_t){
        .snapshot = *snapshot,
        .renderer_generation = fixture->generation.implementation_generation,
        .prepared = 1,
    };
    return DB_RENDERER_PREPARE_OK;
}

static db_renderer_commit_status_t
commit_qualification(void *context,
                     db_renderer_selection_candidate_t *candidate,
                     db_renderer_applied_selection_t *applied) {
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    if ((candidate == NULL) || (candidate->prepared == 0) ||
        (candidate->renderer_generation !=
         fixture->generation.implementation_generation)) {
        return DB_RENDERER_COMMIT_STALE;
    }
    *applied = (db_renderer_applied_selection_t){
        .generation = candidate->snapshot.generation,
        .implementation = candidate->snapshot.implementation,
        .retained_lanes = candidate->snapshot.retained_lanes,
        .lane_count = candidate->snapshot.lane_count,
        .strategy = candidate->snapshot.strategy,
        .source = candidate->snapshot.source,
        .cache_status = candidate->snapshot.cache_status,
        .production_qualified = candidate->snapshot.production_qualified,
        .diagnostic_forced = candidate->snapshot.diagnostic_forced,
    };
    (void)db_snprintf(applied->reason, sizeof(applied->reason), "%s",
                      candidate->snapshot.reason);
    fixture->active = *applied;
    fixture->commit_apply_count++;
    candidate->prepared = 0;
    return DB_RENDERER_COMMIT_OK;
}

static void abort_qualification(void *context,
                                db_renderer_selection_candidate_t *candidate) {
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->abort_apply_count++;
    if (candidate != NULL) {
        *candidate = (db_renderer_selection_candidate_t){0};
    }
}

static int acquire_presenter(void *context, uint32_t frame_index,
                             db_presenter_facts_t *facts) {
    (void)frame_index;
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->acquire_count++;
    *facts = (db_presenter_facts_t){
        .destination_width = DB_RUN_SESSION_TEST_WIDTH,
        .destination_height = DB_RUN_SESSION_TEST_HEIGHT,
        .generation = 1U,
        .valid = 1,
    };
    return 1;
}

static db_present_result_t
present_frame(void *context, const db_frame_plan_t *plan,
              const db_renderer_frame_output_t *output) {
    (void)plan;
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->present_count++;
    return ((output != NULL) && (output->result.success != 0))
               ? DB_PRESENT_ACCEPTED
               : DB_PRESENT_FATAL;
}

static int preflight_renderer(void *context,
                              const db_presenter_facts_t *presenter,
                              const db_frame_requirements_t *requirements,
                              const db_qualification_snapshot_t *qualification,
                              db_renderer_preflight_t *preflight) {
    (void)context;
    (void)requirements;
    *preflight = (db_renderer_preflight_t){
        .plan_request =
            {
                .pixel_width = presenter->destination_width,
                .pixel_height = presenter->destination_height,
            },
        .target_strategy = qualification->strategy,
        .gradient_path = db_qualification_gradient_path(
            qualification, qualification->strategy),
        .strategy_generation = qualification->generation,
    };
    return 1;
}

static int provision_renderer(void *context,
                              const db_renderer_preflight_t *preflight,
                              db_renderer_target_t *target) {
    (void)context;
    *target = (db_renderer_target_t){
        .identity = 1U,
        .generation = preflight->strategy_generation,
        .strategy = preflight->target_strategy,
        .valid = 1,
    };
    return 1;
}

static db_renderer_execute_status_t
execute_renderer(void *context, const db_frame_plan_t *plan,
                 const db_renderer_target_t *target,
                 db_renderer_frame_output_t *output) {
    (void)plan;
    (void)target;
    db_run_session_fixture_t *const fixture =
        (db_run_session_fixture_t *)context;
    fixture->execute_count++;
    output->result = db_render_result_success();
    output->target_content = DB_TARGET_CONTENT_VALID_UNCOMMITTED;
    return DB_RENDER_EXECUTED;
}

static db_benchmark_runtime_init_t benchmark_runtime(db_test_state_t *state) {
    db_benchmark_runtime_init_t runtime = {0};
    DB_TEST_EXPECT_TRUE(state,
                        db_init_benchmark_runtime_from_options(
                            "run_session_test",
                            &(const db_benchmark_runtime_options_t){
                                .benchmark_mode_text = DB_BENCHMARK_MODE_BANDS,
                                .bench_speed_text = "1",
                                .random_seed_text = "123456",
                            },
                            &runtime) != 0);
    return runtime;
}

static db_run_session_t *
create_session(db_test_state_t *state, db_run_session_fixture_t *fixture,
               const db_benchmark_runtime_init_t *runtime) {
    db_run_session_t *session = NULL;
    const db_run_session_status_t status = db_run_session_create(
        &(const db_run_session_config_t){
            .benchmark =
                {
                    .benchmark_configuration = runtime,
                    .working_format = DB_PIXEL_FORMAT_RGBA8,
                },
            .presenter_ops =
                {
                    .acquire = acquire_presenter,
                    .present = present_frame,
                },
            .renderer_ops =
                {
                    .preflight = preflight_renderer,
                    .provision = provision_renderer,
                    .execute = execute_renderer,
                },
            .qualification_ops =
                {
                    .describe = describe_qualification,
                    .prepare_apply = prepare_qualification,
                    .commit_apply = commit_qualification,
                    .abort_apply = abort_qualification,
                },
            .presenter_context = fixture,
            .renderer_context = fixture,
            .frame_limit = DB_RUN_SESSION_TEST_FRAME_LIMIT,
        },
        &session);
    DB_TEST_EXPECT_EQ_INT(state, status, DB_RUN_SESSION_OK);
    return session;
}

static void steady_state_does_not_requalify(db_test_state_t *state) {
    db_run_session_fixture_t fixture = {
        .generation =
            {
                .device_generation = 1U,
                .implementation_generation = 1U,
                .target_contract_generation = 1U,
            },
    };
    const db_benchmark_runtime_init_t runtime = benchmark_runtime(state);
    db_run_session_t *const session = create_session(state, &fixture, &runtime);
    for (uint32_t frame = 0U; frame < DB_RUN_SESSION_TEST_STEADY_FRAMES;
         frame++) {
        const db_run_step_result_t result = db_run_session_step(session);
        DB_TEST_EXPECT_EQ_INT(state, result.outcome, DB_RUN_FRAME_COMMITTED);
    }
    DB_TEST_EXPECT_EQ_U32(state, fixture.describe_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, fixture.prepare_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, fixture.commit_apply_count, 1U);
    DB_TEST_EXPECT_EQ_U32(state, fixture.execute_count,
                          DB_RUN_SESSION_TEST_STEADY_FRAMES);
    db_run_session_destroy(session);
}

static void
failed_requalification_preserves_active_selection(db_test_state_t *state) {
    db_run_session_fixture_t fixture = {
        .generation =
            {
                .device_generation = 1U,
                .implementation_generation = 1U,
                .target_contract_generation = 1U,
            },
    };
    const db_benchmark_runtime_init_t runtime = benchmark_runtime(state);
    db_run_session_t *const session = create_session(state, &fixture, &runtime);
    const db_renderer_applied_selection_t active = fixture.active;
    fixture.generation.implementation_generation++;
    fixture.reject_prepare = 1;
    db_run_session_notify_qualification_change(session, fixture.generation);
    const db_run_step_result_t result = db_run_session_step(session);
    DB_TEST_EXPECT_EQ_INT(state, result.outcome, DB_RUN_FAILED);
    DB_TEST_EXPECT_EQ_INT(state, result.stop_reason,
                          DB_RUN_STOP_QUALIFICATION_FAILED);
    DB_TEST_EXPECT_EQ_U64(state, fixture.active.generation, active.generation);
    DB_TEST_EXPECT_EQ_U32(state, fixture.commit_apply_count, 1U);
    DB_TEST_EXPECT_TRUE(state, fixture.abort_apply_count > 0U);
    db_run_session_destroy(session);
}

unsigned db_run_session_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"steady_state_does_not_requalify", steady_state_does_not_requalify},
        {"failed_requalification_preserves_active_selection",
         failed_requalification_preserves_active_selection},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
