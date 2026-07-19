#include "core/db_frame_coordinator.h"

#include "core/db_benchmark_model.h"
#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_frame_contracts.h"
#include "core/db_frame_plan.h"
#include "core/db_numeric.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_renderer_diagnostics.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t probe_count;
    uint32_t generate_count;
    uint32_t commit_count;
    uint32_t abort_count;
    uint32_t acquire_count;
    uint32_t execute_count;
    uint32_t present_count;
    uint32_t finalize_commit_count;
    uint32_t finalize_abort_count;
    uint32_t presenter_stale_count;
    uint32_t present_retry_count;
    uint32_t present_target_lost_count;
    db_frame_phase_t fail_phase;
    int failure_enabled;
} db_frame_coordinator_fixture_t;

static db_frame_plan_status_t
fake_probe(const db_benchmark_model_t *model, uint32_t frame_index,
           db_frame_requirements_t *requirements) {
    (void)frame_index;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)model->context;
    fixture->probe_count++;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_PROBE)) {
        return DB_FRAME_PLAN_INVALID;
    }
    *requirements = (db_frame_requirements_t){0};
    return DB_FRAME_PLAN_OK;
}

static db_frame_plan_status_t
fake_provision(db_benchmark_model_t *model,
               const db_frame_requirements_t *requirements,
               db_frame_checkpoint_binding_t *binding) {
    (void)model;
    (void)requirements;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)model->context;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_PROVISION)) {
        return DB_FRAME_PLAN_CHECKPOINT_UNAVAILABLE;
    }
    *binding = (db_frame_checkpoint_binding_t){0};
    return DB_FRAME_PLAN_OK;
}

static db_frame_plan_status_t
fake_generate(db_benchmark_model_t *model, uint32_t frame_index,
              const db_frame_plan_request_t *request, db_frame_plan_t *plan) {
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)model->context;
    fixture->generate_count++;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_GENERATE)) {
        return DB_FRAME_PLAN_INVALID;
    }
    *plan = (db_frame_plan_t){
        .frame_index = frame_index,
        .preparation_token = request->preparation_token,
        .expected_state_hash = UINT64_C(0x1234),
    };
    return DB_FRAME_PLAN_OK;
}

static void fake_commit(db_benchmark_model_t *model,
                        const db_frame_plan_t *plan,
                        const db_render_result_t *result) {
    (void)plan;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)model->context;
    if ((result != NULL) && (result->success != 0)) {
        fixture->commit_count++;
    }
}

static void fake_abort(db_benchmark_model_t *model) {
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)model->context;
    fixture->abort_count++;
}

static int fake_acquire(void *user_data, uint32_t frame_index,
                        db_presenter_facts_t *facts) {
    (void)frame_index;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    fixture->acquire_count++;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_ACQUIRE)) {
        return 0;
    }
    *facts = (db_presenter_facts_t){
        .destination_width = 64U,
        .destination_height = 32U,
        .generation = fixture->acquire_count,
        .valid = 1,
    };
    return 1;
}

static int fake_presenter_validate(void *user_data,
                                   const db_presenter_facts_t *facts) {
    (void)facts;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    if (fixture->presenter_stale_count != 0U) {
        fixture->presenter_stale_count--;
        return 0;
    }
    return 1;
}

static db_present_result_t
fake_present(void *user_data, const db_frame_plan_t *plan,
             const db_renderer_frame_output_t *output) {
    (void)plan;
    (void)output;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    fixture->present_count++;
    if (fixture->present_retry_count != 0U) {
        fixture->present_retry_count--;
        return DB_PRESENT_RETRYABLE;
    }
    if (fixture->present_target_lost_count != 0U) {
        fixture->present_target_lost_count--;
        return DB_PRESENT_TARGET_LOST;
    }
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_PRESENT)) {
        return DB_PRESENT_FATAL;
    }
    return DB_PRESENT_ACCEPTED;
}

static int fake_preflight(void *user_data,
                          const db_presenter_facts_t *presenter,
                          const db_frame_requirements_t *requirements,
                          const db_qualification_snapshot_t *qualification,
                          db_renderer_preflight_t *preflight) {
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    (void)requirements;
    (void)qualification;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_PREFLIGHT)) {
        return 0;
    }
    *preflight = (db_renderer_preflight_t){
        .plan_request =
            {
                .pixel_width = presenter->destination_width,
                .pixel_height = presenter->destination_height,
            },
        .target_strategy = DB_RENDER_TARGET_CPU_SURFACE,
        .strategy_generation = 1U,
    };
    return 1;
}

static int fake_renderer_provision(void *user_data,
                                   const db_renderer_preflight_t *preflight,
                                   db_renderer_target_t *target) {
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_PROVISION)) {
        return 0;
    }
    *target = (db_renderer_target_t){
        .identity = 1U,
        .generation = preflight->strategy_generation,
        .strategy = preflight->target_strategy,
        .valid = 1,
    };
    return 1;
}

static int fake_renderer_validate(void *user_data,
                                  const db_renderer_target_t *target) {
    (void)user_data;
    return DB_BOOL((target != NULL) && (target->valid != 0));
}

static db_renderer_execute_status_t
fake_execute(void *user_data, const db_frame_plan_t *plan,
             const db_renderer_target_t *target,
             db_renderer_frame_output_t *output) {
    (void)plan;
    (void)target;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    fixture->execute_count++;
    if ((fixture->failure_enabled != 0) &&
        (fixture->fail_phase == DB_FRAME_EXECUTE)) {
        output->target_content = DB_TARGET_CONTENT_PARTIALLY_MODIFIED;
        return DB_RENDER_FATAL;
    }
    output->result = db_render_result_success();
    output->target_content = DB_TARGET_CONTENT_VALID_UNCOMMITTED;
    return DB_RENDER_EXECUTED;
}

static void fake_finalize(void *user_data, const db_frame_plan_t *plan,
                          const db_renderer_frame_output_t *output,
                          int commit) {
    (void)plan;
    (void)output;
    db_frame_coordinator_fixture_t *const fixture =
        (db_frame_coordinator_fixture_t *)user_data;
    if (commit != 0) {
        fixture->finalize_commit_count++;
    } else {
        fixture->finalize_abort_count++;
    }
}

static db_frame_coordinator_t
make_coordinator(db_frame_coordinator_fixture_t *fixture,
                 db_benchmark_model_t *model) {
    static const db_benchmark_model_ops_t benchmark_ops = {
        .probe = fake_probe,
        .provision = fake_provision,
        .generate = fake_generate,
        .commit = fake_commit,
        .abort = fake_abort,
    };
    *model = (db_benchmark_model_t){.context = fixture};
    db_frame_coordinator_t coordinator = {0};
    (void)db_frame_coordinator_init(
        &coordinator, &(const db_frame_coordinator_config_t){
                          .model = model,
                          .benchmark_ops = &benchmark_ops,
                          .presenter_ops =
                              {
                                  .acquire = fake_acquire,
                                  .validate = fake_presenter_validate,
                                  .present = fake_present,
                              },
                          .renderer_ops =
                              {
                                  .preflight = fake_preflight,
                                  .provision = fake_renderer_provision,
                                  .validate = fake_renderer_validate,
                                  .execute = fake_execute,
                                  .finalize = fake_finalize,
                              },
                          .presenter_context = fixture,
                          .renderer_context = fixture,
                      });
    return coordinator;
}

static void test_successful_transaction(db_test_state_t *state) {
    db_frame_coordinator_fixture_t fixture = {0};
    db_benchmark_model_t model = {0};
    db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
    DB_TEST_EXPECT_TRUE(state, db_frame_coordinator_begin(&coordinator, 7U));
    const db_frame_step_result_t result =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, result.outcome, DB_FRAME_STEP_FRAME_COMMITTED);
    DB_TEST_EXPECT_EQ_U64(state, fixture.generate_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.commit_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.abort_count, 0U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.present_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.finalize_commit_count, 1U);
}

static void test_retryable_present_keeps_plan(db_test_state_t *state) {
    db_frame_coordinator_fixture_t fixture = {.present_retry_count = 1U};
    db_benchmark_model_t model = {0};
    db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
    (void)db_frame_coordinator_begin(&coordinator, 2U);
    const db_frame_step_result_t first =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, first.outcome, DB_FRAME_STEP_WAIT);
    const db_frame_step_result_t second =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, second.outcome, DB_FRAME_STEP_FRAME_COMMITTED);
    DB_TEST_EXPECT_EQ_U64(state, fixture.generate_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.execute_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.present_count, 2U);
}

static void test_stale_presenter_restarts_transaction(db_test_state_t *state) {
    db_frame_coordinator_fixture_t fixture = {.presenter_stale_count = 1U};
    db_benchmark_model_t model = {0};
    db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
    (void)db_frame_coordinator_begin(&coordinator, 3U);
    const db_frame_step_result_t first =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, first.outcome, DB_FRAME_STEP_WAIT);
    const db_frame_step_result_t second =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, second.outcome, DB_FRAME_STEP_FRAME_COMMITTED);
    DB_TEST_EXPECT_EQ_U64(state, fixture.generate_count, 2U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.abort_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.finalize_abort_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.commit_count, 1U);
}

static void test_target_loss_restarts_transaction(db_test_state_t *state) {
    db_frame_coordinator_fixture_t fixture = {
        .present_target_lost_count = 1U,
    };
    db_benchmark_model_t model = {0};
    db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
    (void)db_frame_coordinator_begin(&coordinator, 4U);
    const db_frame_step_result_t first =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, first.outcome, DB_FRAME_STEP_WAIT);
    const db_frame_step_result_t second =
        db_frame_coordinator_drive(&coordinator);
    DB_TEST_EXPECT_EQ_INT(state, second.outcome, DB_FRAME_STEP_FRAME_COMMITTED);
    DB_TEST_EXPECT_EQ_U64(state, fixture.generate_count, 2U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.execute_count, 2U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.abort_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.finalize_abort_count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, fixture.finalize_commit_count, 1U);
}

static void test_fallible_phases_rollback_once(db_test_state_t *state) {
    static const db_frame_phase_t phases[] = {
        DB_FRAME_ACQUIRE,   DB_FRAME_PROBE,    DB_FRAME_PREFLIGHT,
        DB_FRAME_PROVISION, DB_FRAME_GENERATE, DB_FRAME_EXECUTE,
        DB_FRAME_PRESENT,
    };
    for (size_t index = 0U; index < sizeof(phases) / sizeof(phases[0]);
         index++) {
        db_frame_coordinator_fixture_t fixture = {
            .fail_phase = phases[index],
            .failure_enabled = 1,
        };
        db_benchmark_model_t model = {0};
        db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
        (void)db_frame_coordinator_begin(
            &coordinator, db_checked_size_to_u32("test", "phase_index", index));
        const db_frame_step_result_t result =
            db_frame_coordinator_drive(&coordinator);
        DB_TEST_EXPECT_EQ_INT(state, result.outcome, DB_FRAME_STEP_FAILED);
        DB_TEST_EXPECT_EQ_U64(state, fixture.commit_count, 0U);
        DB_TEST_EXPECT_EQ_U64(state, fixture.abort_count, 1U);
        DB_TEST_EXPECT_EQ_U64(state, fixture.finalize_abort_count, 1U);
        DB_TEST_EXPECT_TRUE(state, fixture.generate_count <= 1U);
    }
}

static void test_shared_preflight_policy(db_test_state_t *state) {
    const db_qualification_snapshot_t qualification = {
        .generation = 9U,
        .implementation = DB_GRADIENT_IMPLEMENTATION_SEMANTIC,
        .strategy = DB_RENDER_TARGET_GL1_PERSISTENT_FBO,
        .production_qualified = 1,
    };
    db_renderer_preflight_t preflight = {0};
    DB_TEST_EXPECT_TRUE(state,
                        db_renderer_preflight_policy_resolve(
                            &(const db_renderer_preflight_policy_input_t){
                                .profile = DB_RENDERER_PREFLIGHT_GL1_WINDOW,
                                .gl1_target_request = DB_GL1_TARGET_AUTO,
                            },
                            &(const db_presenter_facts_t){
                                .destination_width = 1000U,
                                .destination_height = 600U,
                                .generation = 3U,
                                .buffer_age_valid = 1,
                                .valid = 1,
                            },
                            &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(state, preflight.target_strategy,
                          DB_RENDER_TARGET_GL1_DIRECT_WINDOW);
    DB_TEST_EXPECT_EQ_INT(state, preflight.gradient_path,
                          DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT);
    DB_TEST_EXPECT_EQ_U64(state, preflight.strategy_generation, 9U);

    DB_TEST_EXPECT_TRUE(state,
                        db_renderer_preflight_policy_resolve(
                            &(const db_renderer_preflight_policy_input_t){
                                .profile = DB_RENDERER_PREFLIGHT_GL1_WINDOW,
                                .gl1_target_request = DB_GL1_TARGET_AUTO,
                            },
                            &(const db_presenter_facts_t){
                                .destination_width = 1000U,
                                .destination_height = 600U,
                                .generation = 4U,
                                .valid = 1,
                            },
                            &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(state, preflight.target_strategy,
                          DB_RENDER_TARGET_GL1_PERSISTENT_FBO);
}

unsigned db_frame_coordinator_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"successful_transaction", test_successful_transaction},
        {"retryable_present_keeps_plan", test_retryable_present_keeps_plan},
        {"stale_presenter_restarts_transaction",
         test_stale_presenter_restarts_transaction},
        {"target_loss_restarts_transaction",
         test_target_loss_restarts_transaction},
        {"fallible_phases_rollback_once", test_fallible_phases_rollback_once},
        {"shared_preflight_policy", test_shared_preflight_policy},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
