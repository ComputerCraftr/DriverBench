#include "core/db_frame_coordinator.h"

#include "core/db_benchmark_model.h"
#include "core/db_conformance.h"
#include "core/db_core.h"
#include "core/db_format_contract.h"
#include "core/db_frame_contracts.h"
#include "core/db_frame_plan.h"
#include "core/db_numeric.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_diagnostics.h"
#include "support/test_harness.h"

#include <stddef.h>
#include <stdint.h>

enum { DB_TEST_MISMATCHED_CHANNEL_BITS = 10U };

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

static void
test_transaction_identity_exhaustion_does_not_wrap(db_test_state_t *state) {
    db_frame_coordinator_fixture_t fixture = {0};
    db_benchmark_model_t model = {0};
    db_frame_coordinator_t coordinator = make_coordinator(&fixture, &model);
    coordinator.next_transaction_id = UINT64_MAX;
    DB_TEST_EXPECT_EQ_INT(state, db_frame_coordinator_begin(&coordinator, 0U),
                          0);
    DB_TEST_EXPECT_EQ_INT(state, coordinator.active, 0);
    DB_TEST_EXPECT_EQ_U64(state, coordinator.next_transaction_id, UINT64_MAX);
    DB_TEST_EXPECT_EQ_U64(state, coordinator.identity.transaction_id, 0U);
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
    const db_gl1_direct_window_capabilities_t direct_capabilities = {
        .can_control_dither = 1,
        .can_control_srgb = 1,
        .can_select_required_buffers = 1,
        .pre_swap_readback_qualified = 1,
        .fixed_function_raster_qualified = 1,
    };
    const db_presenter_facts_t direct_presenter = {
        .destination_width = 1000U,
        .destination_height = 600U,
        .gl =
            {
                .native_width = 1000U,
                .native_height = 600U,
                .native_format = DB_NATIVE_OUTPUT_XRGB8888,
                .channel_bits = {8U, 8U, 8U, 0U},
                .generation = 3U,
                .valid = 1,
            },
        .generation = 3U,
        .buffer_age_valid = 1,
        .valid = 1,
    };
    const db_renderer_preflight_policy_input_t direct_input = {
        .profile = DB_RENDERER_PREFLIGHT_GL1_WINDOW,
        .gl1_target_request = DB_GL1_TARGET_AUTO,
        .working_format = DB_PIXEL_FORMAT_RGBA8,
        .gl1_direct_window = direct_capabilities,
        .previous_strategy = DB_RENDER_TARGET_GL1_DIRECT_WINDOW,
        .previous_target_generation = 3U,
        .direct_window_lineage_valid = 1,
    };
    db_renderer_preflight_t preflight = {0};
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &direct_input, &direct_presenter,
                                   &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(state, preflight.target_strategy,
                          DB_RENDER_TARGET_GL1_DIRECT_WINDOW);
    DB_TEST_EXPECT_EQ_INT(state, preflight.gradient_path,
                          DB_RENDER_OPERATION_GL1_INTERPOLATED_GRADIENT);
    DB_TEST_EXPECT_EQ_U64(state, preflight.strategy_generation, 9U);
    DB_TEST_EXPECT_EQ_INT(state, preflight.strategy_reason,
                          DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_ELIGIBLE);
    const db_renderer_target_t target =
        db_renderer_target_from_preflight(&preflight, 17U, 23U);
    DB_TEST_EXPECT_EQ_U64(state, target.identity, 17U);
    DB_TEST_EXPECT_EQ_U64(state, target.generation, 23U);
    DB_TEST_EXPECT_EQ_INT(state, target.strategy, preflight.target_strategy);
    DB_TEST_EXPECT_EQ_INT(state, target.gradient_path, preflight.gradient_path);
    DB_TEST_EXPECT_EQ_U64(state, target.strategy_generation,
                          preflight.strategy_generation);
    DB_TEST_EXPECT_EQ_U64(state, target.qualification_generation,
                          preflight.qualification_generation);
    DB_TEST_EXPECT_EQ_U64(state, target.target_generation,
                          preflight.target_generation);
    DB_TEST_EXPECT_EQ_INT(state, target.strategy_reason,
                          preflight.strategy_reason);

    db_presenter_facts_t pending_age = direct_presenter;
    pending_age.buffer_age_valid = 0;
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &direct_input, &pending_age, &qualification,
                                   &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(state, preflight.target_strategy,
                          DB_RENDER_TARGET_GL1_PERSISTENT_FBO);
    DB_TEST_EXPECT_EQ_INT(state, preflight.strategy_reason,
                          DB_RENDERER_STRATEGY_REASON_BUFFER_AGE_PENDING);

    db_presenter_facts_t format_mismatch = direct_presenter;
    format_mismatch.gl.channel_bits[0] = DB_TEST_MISMATCHED_CHANNEL_BITS;
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &direct_input, &format_mismatch,
                                   &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(
        state, preflight.strategy_reason,
        DB_RENDERER_STRATEGY_REASON_PRESENTER_FORMAT_MISMATCH);

    db_presenter_facts_t conversion = direct_presenter;
    conversion.gl.platform_conversion_required = 1;
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &direct_input, &conversion, &qualification,
                                   &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(
        state, preflight.strategy_reason,
        DB_RENDERER_STRATEGY_REASON_PRESENTATION_CONVERSION_REQUIRED);

    db_renderer_preflight_policy_input_t missing_capability = direct_input;
    missing_capability.gl1_direct_window.can_control_dither = 0;
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &missing_capability, &direct_presenter,
                                   &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(
        state, preflight.strategy_reason,
        DB_RENDERER_STRATEGY_REASON_DIRECT_WINDOW_CAPABILITY_MISSING);

    db_renderer_preflight_policy_input_t transition = direct_input;
    transition.previous_strategy = DB_RENDER_TARGET_GL1_PERSISTENT_FBO;
    transition.direct_window_lineage_valid = 0;
    DB_TEST_EXPECT_TRUE(state, db_renderer_preflight_policy_resolve(
                                   &transition, &direct_presenter,
                                   &qualification, &preflight) != 0);
    DB_TEST_EXPECT_EQ_INT(
        state, preflight.strategy_reason,
        DB_RENDERER_STRATEGY_REASON_STRATEGY_TRANSITION_REBUILD);
    DB_TEST_EXPECT_EQ_INT(state, preflight.rebuild_required, 1);
}

unsigned db_frame_coordinator_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"successful_transaction", test_successful_transaction},
        {"transaction_identity_exhaustion_does_not_wrap",
         test_transaction_identity_exhaustion_does_not_wrap},
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
