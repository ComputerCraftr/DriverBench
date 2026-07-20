#include "db_frame_coordinator.h"

#include "db_benchmark_model.h"
#include "db_core.h"
#include "db_frame_contracts.h"
#include "db_frame_plan.h"
#include "db_hash.h"
#include "db_progress_policy.h"

#include <stdint.h>

static db_frame_step_result_t step_result(db_frame_step_outcome_t outcome,
                                          db_frame_phase_t completed,
                                          db_frame_phase_t next,
                                          db_frame_retry_reason_t retry,
                                          db_deadline_t deadline) {
    return (db_frame_step_result_t){
        .outcome = outcome,
        .completed_phase = completed,
        .next_phase = next,
        .retry_reason = retry,
        .next_deadline = deadline,
    };
}

static db_frame_step_result_t
fail_transaction(db_frame_coordinator_t *coordinator,
                 db_frame_phase_t completed) {
    coordinator->phase = DB_FRAME_ROLLBACK;
    coordinator->failed = 1;
    return step_result(DB_FRAME_STEP_PROGRESS, completed, DB_FRAME_ROLLBACK,
                       DB_FRAME_RETRY_NONE, (db_deadline_t){0});
}

static db_frame_step_result_t
wait_for_retry(db_frame_coordinator_t *coordinator, db_frame_phase_t phase,
               db_frame_retry_reason_t reason) {
    const uint64_t now_ns = db_now_ns_monotonic();
    if (coordinator->retry.progress.active == 0) {
        if (db_progress_session_begin(&coordinator->retry.progress,
                                      DB_PROGRESS_FRAME_RETRY, now_ns) == 0) {
            return fail_transaction(coordinator, phase);
        }
    }
    coordinator->retry.reason = reason;
    if (db_progress_session_record_retry(&coordinator->retry.progress, now_ns,
                                         0U, "frame_transaction_retry") == 0) {
        return fail_transaction(coordinator, phase);
    }
    return step_result(DB_FRAME_STEP_WAIT, phase, phase, reason,
                       coordinator->retry.progress.deadline);
}

static void reset_transaction(db_frame_coordinator_t *coordinator) {
    coordinator->presenter = (db_presenter_frame_session_t){0};
    coordinator->benchmark = (db_benchmark_frame_transaction_t){
        .transaction_id = coordinator->identity.transaction_id,
    };
    coordinator->renderer = (db_renderer_frame_transaction_t){0};
    coordinator->plan = (db_frame_plan_storage_t){0};
    db_progress_session_reset(&coordinator->retry.progress);
    coordinator->retry.reason = DB_FRAME_RETRY_NONE;
}

static int take_transaction_id(db_frame_coordinator_t *coordinator,
                               uint64_t *transaction_id) {
    if ((coordinator == NULL) || (transaction_id == NULL) ||
        (coordinator->next_transaction_id == 0U) ||
        (coordinator->next_transaction_id == UINT64_MAX)) {
        return 0;
    }
    *transaction_id = coordinator->next_transaction_id;
    coordinator->next_transaction_id++;
    return 1;
}

static db_frame_step_result_t
restart_transaction(db_frame_coordinator_t *coordinator,
                    db_frame_phase_t completed,
                    db_frame_retry_reason_t reason) {
    const db_frame_step_result_t retry =
        wait_for_retry(coordinator, completed, reason);
    if (retry.outcome != DB_FRAME_STEP_WAIT) {
        return retry;
    }
    uint64_t next_transaction_id = 0U;
    if (take_transaction_id(coordinator, &next_transaction_id) == 0) {
        return fail_transaction(coordinator, completed);
    }
    coordinator->benchmark_ops.abort(coordinator->model);
    if (coordinator->renderer_ops.finalize != NULL) {
        coordinator->renderer_ops.finalize(
            coordinator->renderer_context,
            coordinator->plan.valid != 0 ? &coordinator->plan.published : NULL,
            &coordinator->renderer.output, 0);
    }
    const db_progress_session_t retry_progress = coordinator->retry.progress;
    coordinator->identity.transaction_id = next_transaction_id;
    reset_transaction(coordinator);
    coordinator->retry.progress = retry_progress;
    coordinator->retry.reason = reason;
    coordinator->phase = DB_FRAME_ACQUIRE;
    return step_result(DB_FRAME_STEP_WAIT, completed, DB_FRAME_ACQUIRE, reason,
                       retry_progress.deadline);
}

int db_frame_coordinator_init(
    db_frame_coordinator_t *coordinator,
    const db_frame_coordinator_config_t *configuration) {
    if ((coordinator == NULL) || (configuration == NULL) ||
        (configuration->model == NULL) ||
        (configuration->presenter_ops.acquire == NULL) ||
        (configuration->presenter_ops.present == NULL) ||
        (configuration->renderer_ops.preflight == NULL) ||
        (configuration->renderer_ops.provision == NULL) ||
        (configuration->renderer_ops.execute == NULL)) {
        return 0;
    }
    const db_benchmark_model_ops_t *const benchmark_ops =
        (configuration->benchmark_ops != NULL) ? configuration->benchmark_ops
                                               : db_benchmark_model_ops();
    if ((benchmark_ops == NULL) || (benchmark_ops->probe == NULL) ||
        (benchmark_ops->provision == NULL) ||
        (benchmark_ops->generate == NULL) || (benchmark_ops->commit == NULL) ||
        (benchmark_ops->abort == NULL)) {
        return 0;
    }
    *coordinator = (db_frame_coordinator_t){
        .phase = DB_FRAME_ACQUIRE,
        .model = configuration->model,
        .benchmark_ops = *benchmark_ops,
        .qualification = configuration->qualification,
        .presenter_ops = configuration->presenter_ops,
        .renderer_ops = configuration->renderer_ops,
        .presenter_context = configuration->presenter_context,
        .renderer_context = configuration->renderer_context,
        .next_transaction_id = 1U,
    };
    return 1;
}

int db_frame_coordinator_begin(db_frame_coordinator_t *coordinator,
                               uint32_t frame_index) {
    if ((coordinator == NULL) || (coordinator->model == NULL) ||
        (coordinator->active != 0)) {
        return 0;
    }
    uint64_t transaction_id = 0U;
    if (take_transaction_id(coordinator, &transaction_id) == 0) {
        return 0;
    }
    coordinator->identity = (db_frame_identity_t){
        .transaction_id = transaction_id,
        .frame_index = frame_index,
    };
    coordinator->phase = DB_FRAME_ACQUIRE;
    coordinator->active = 1;
    coordinator->failed = 0;
    reset_transaction(coordinator);
    return 1;
}

static int presenter_is_current(const db_frame_coordinator_t *coordinator) {
    return (coordinator->presenter_ops.validate == NULL) ||
           (coordinator->presenter_ops.validate(
                coordinator->presenter_context,
                &coordinator->presenter.facts) != 0);
}

static int renderer_is_current(const db_frame_coordinator_t *coordinator) {
    return (coordinator->renderer_ops.validate == NULL) ||
           (coordinator->renderer_ops.validate(coordinator->renderer_context,
                                               &coordinator->renderer.target) !=
            0);
}

static uint64_t preparation_token(const db_frame_coordinator_t *coordinator) {
    uint64_t token = DB_FNV1A64_OFFSET;
    token = db_fnv1a64_mix_u64(token, coordinator->identity.transaction_id);
    token = db_fnv1a64_mix_u64(token, coordinator->presenter.facts.generation);
    token = db_fnv1a64_mix_u64(
        token, coordinator->benchmark.requirements.requirements_token);
    token =
        db_fnv1a64_mix_u64(token, coordinator->benchmark.binding.binding_token);
    token = db_fnv1a64_mix_u64(token, coordinator->renderer.target.identity);
    token = db_fnv1a64_mix_u64(token, coordinator->renderer.target.generation);
    token = db_fnv1a64_mix_u64(
        token, coordinator->renderer.preflight.strategy_generation);
    return token;
}

db_frame_step_result_t
db_frame_coordinator_step(db_frame_coordinator_t *coordinator) {
    if ((coordinator == NULL) || (coordinator->active == 0)) {
        return step_result(DB_FRAME_STEP_FAILED, DB_FRAME_ROLLBACK,
                           DB_FRAME_ROLLBACK, DB_FRAME_RETRY_NONE,
                           (db_deadline_t){0});
    }
    const db_frame_phase_t completed = coordinator->phase;
    switch (coordinator->phase) {
    case DB_FRAME_ACQUIRE:
        if (coordinator->presenter_ops.acquire(
                coordinator->presenter_context,
                coordinator->identity.frame_index,
                &coordinator->presenter.facts) == 0 ||
            coordinator->presenter.facts.valid == 0) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->presenter.acquired_generation =
            coordinator->presenter.facts.generation;
        coordinator->phase = DB_FRAME_PROBE;
        break;
    case DB_FRAME_PROBE: {
        const db_frame_plan_status_t status = coordinator->benchmark_ops.probe(
            coordinator->model, coordinator->identity.frame_index,
            &coordinator->benchmark.requirements);
        if ((status != DB_FRAME_PLAN_OK) &&
            (status != DB_FRAME_PLAN_CHECKPOINT_REQUIRED)) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->phase = DB_FRAME_PREFLIGHT;
        break;
    }
    case DB_FRAME_PREFLIGHT:
        if (coordinator->renderer_ops.preflight(
                coordinator->renderer_context, &coordinator->presenter.facts,
                &coordinator->benchmark.requirements,
                coordinator->qualification,
                &coordinator->renderer.preflight) == 0) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->phase = DB_FRAME_PROVISION;
        break;
    case DB_FRAME_PROVISION:
        if (coordinator->benchmark.requirements.checkpoint_required != 0) {
            if (coordinator->benchmark_ops.provision(
                    coordinator->model, &coordinator->benchmark.requirements,
                    &coordinator->benchmark.binding) != DB_FRAME_PLAN_OK) {
                return fail_transaction(coordinator, completed);
            }
        }
        if (coordinator->renderer_ops.provision(
                coordinator->renderer_context, &coordinator->renderer.preflight,
                &coordinator->renderer.target) == 0 ||
            coordinator->renderer.target.valid == 0) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->phase = DB_FRAME_GENERATE;
        break;
    case DB_FRAME_GENERATE: {
        db_frame_plan_request_t *const request =
            &coordinator->renderer.preflight.plan_request;
        request->force_rebuild =
            coordinator->renderer.preflight.rebuild_required;
        request->rebuild_reason =
            coordinator->renderer.preflight.rebuild_reason;
        request->preparation_token = preparation_token(coordinator);
        if (coordinator->benchmark.published_plan_count != 0U) {
            return fail_transaction(coordinator, completed);
        }
        const db_frame_plan_status_t status =
            coordinator->benchmark_ops.generate(
                coordinator->model, coordinator->identity.frame_index, request,
                &coordinator->plan.published);
        if (status != DB_FRAME_PLAN_OK) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->benchmark.published_plan_count++;
        coordinator->benchmark.pending = 1;
        coordinator->plan.valid = 1;
        coordinator->phase = DB_FRAME_EXECUTE;
        break;
    }
    case DB_FRAME_EXECUTE: {
        if (presenter_is_current(coordinator) == 0) {
            return restart_transaction(coordinator, completed,
                                       DB_FRAME_RETRY_STALE_PRESENTER);
        }
        if (renderer_is_current(coordinator) == 0) {
            return restart_transaction(coordinator, completed,
                                       DB_FRAME_RETRY_STALE_TARGET);
        }
        const db_renderer_execute_status_t status =
            coordinator->renderer_ops.execute(
                coordinator->renderer_context, &coordinator->plan.published,
                &coordinator->renderer.target, &coordinator->renderer.output);
        if (status == DB_RENDER_RETRYABLE) {
            return restart_transaction(coordinator, completed,
                                       DB_FRAME_RETRY_RENDERER);
        }
        if (status != DB_RENDER_EXECUTED) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->phase = DB_FRAME_PRESENT;
        break;
    }
    case DB_FRAME_PRESENT: {
        if (presenter_is_current(coordinator) == 0) {
            return restart_transaction(coordinator, completed,
                                       DB_FRAME_RETRY_STALE_PRESENTER);
        }
        const db_present_result_t status = coordinator->presenter_ops.present(
            coordinator->presenter_context, &coordinator->plan.published,
            &coordinator->renderer.output);
        if (status == DB_PRESENT_RETRYABLE) {
            return wait_for_retry(coordinator, completed,
                                  DB_FRAME_RETRY_PRESENTATION);
        }
        if (status == DB_PRESENT_TARGET_LOST) {
            coordinator->renderer.output.target_content =
                DB_TARGET_CONTENT_VALID_UNCOMMITTED;
            return restart_transaction(coordinator, completed,
                                       DB_FRAME_RETRY_STALE_PRESENTER);
        }
        if (status != DB_PRESENT_ACCEPTED) {
            return fail_transaction(coordinator, completed);
        }
        coordinator->phase = DB_FRAME_COMMIT;
        break;
    }
    case DB_FRAME_COMMIT:
        coordinator->renderer.output.result.success = 1;
        if (coordinator->renderer_ops.finalize != NULL) {
            coordinator->renderer_ops.finalize(
                coordinator->renderer_context, &coordinator->plan.published,
                &coordinator->renderer.output, 1);
        }
        coordinator->benchmark_ops.commit(coordinator->model,
                                          &coordinator->plan.published,
                                          &coordinator->renderer.output.result);
        coordinator->benchmark.pending = 0;
        coordinator->active = 0;
        db_progress_session_reset(&coordinator->retry.progress);
        return step_result(DB_FRAME_STEP_FRAME_COMMITTED, completed,
                           DB_FRAME_ACQUIRE, DB_FRAME_RETRY_NONE,
                           (db_deadline_t){0});
    case DB_FRAME_ROLLBACK:
        coordinator->benchmark_ops.abort(coordinator->model);
        if (coordinator->renderer_ops.finalize != NULL) {
            coordinator->renderer_ops.finalize(
                coordinator->renderer_context,
                coordinator->plan.valid != 0 ? &coordinator->plan.published
                                             : NULL,
                &coordinator->renderer.output, 0);
        }
        coordinator->benchmark.pending = 0;
        coordinator->active = 0;
        db_progress_session_reset(&coordinator->retry.progress);
        return step_result(DB_FRAME_STEP_FAILED, completed, DB_FRAME_ACQUIRE,
                           coordinator->retry.reason, (db_deadline_t){0});
    }
    return step_result(DB_FRAME_STEP_PROGRESS, completed, coordinator->phase,
                       DB_FRAME_RETRY_NONE, (db_deadline_t){0});
}

db_frame_step_result_t
db_frame_coordinator_drive(db_frame_coordinator_t *coordinator) {
    db_frame_step_result_t result = {0};
    do {
        result = db_frame_coordinator_step(coordinator);
    } while (result.outcome == DB_FRAME_STEP_PROGRESS);
    return result;
}

db_frame_step_result_t
db_frame_coordinator_run_frame(db_frame_coordinator_t *coordinator,
                               uint32_t frame_index) {
    if (coordinator == NULL) {
        return step_result(DB_FRAME_STEP_FAILED, DB_FRAME_ROLLBACK,
                           DB_FRAME_ROLLBACK, DB_FRAME_RETRY_NONE,
                           (db_deadline_t){0});
    }
    if ((coordinator->active == 0) &&
        (db_frame_coordinator_begin(coordinator, frame_index) == 0)) {
        return step_result(DB_FRAME_STEP_FAILED, DB_FRAME_ROLLBACK,
                           DB_FRAME_ROLLBACK, DB_FRAME_RETRY_NONE,
                           (db_deadline_t){0});
    }
    return db_frame_coordinator_drive(coordinator);
}

void db_frame_coordinator_abort(db_frame_coordinator_t *coordinator) {
    if ((coordinator == NULL) || (coordinator->active == 0)) {
        return;
    }
    coordinator->phase = DB_FRAME_ROLLBACK;
    (void)db_frame_coordinator_step(coordinator);
}
