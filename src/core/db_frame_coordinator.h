#ifndef DRIVERBENCH_CORE_FRAME_COORDINATOR_H
#define DRIVERBENCH_CORE_FRAME_COORDINATOR_H

#include "db_benchmark_model.h"
#include "db_frame_contracts.h"

#include <stdint.h>

typedef enum {
    DB_FRAME_ACQUIRE = 0,
    DB_FRAME_PROBE,
    DB_FRAME_PREFLIGHT,
    DB_FRAME_PROVISION,
    DB_FRAME_GENERATE,
    DB_FRAME_EXECUTE,
    DB_FRAME_PRESENT,
    DB_FRAME_COMMIT,
    DB_FRAME_ROLLBACK,
} db_frame_phase_t;

typedef enum {
    DB_FRAME_STEP_PROGRESS = 0,
    DB_FRAME_STEP_WAIT,
    DB_FRAME_STEP_FRAME_COMMITTED,
    DB_FRAME_STEP_RUN_COMPLETE,
    DB_FRAME_STEP_FAILED,
} db_frame_step_outcome_t;

typedef enum {
    DB_FRAME_RETRY_NONE = 0,
    DB_FRAME_RETRY_RENDERER,
    DB_FRAME_RETRY_PRESENTATION,
    DB_FRAME_RETRY_STALE_PRESENTER,
    DB_FRAME_RETRY_STALE_TARGET,
} db_frame_retry_reason_t;

typedef struct {
    db_frame_step_outcome_t outcome;
    db_frame_phase_t completed_phase;
    db_frame_phase_t next_phase;
    db_frame_retry_reason_t retry_reason;
    db_deadline_t next_deadline;
} db_frame_step_result_t;

typedef struct {
    db_presenter_facts_t facts;
    uint64_t acquired_generation;
} db_presenter_frame_session_t;

typedef struct {
    db_frame_requirements_t requirements;
    db_frame_checkpoint_binding_t binding;
    uint64_t transaction_id;
    uint32_t published_plan_count;
    int pending;
} db_benchmark_frame_transaction_t;

typedef struct {
    db_renderer_preflight_t preflight;
    db_renderer_target_t target;
    db_renderer_frame_output_t output;
} db_renderer_frame_transaction_t;

typedef struct {
    db_frame_plan_t published;
    int valid;
} db_frame_plan_storage_t;

typedef struct {
    db_progress_session_t progress;
    db_frame_retry_reason_t reason;
} db_frame_retry_session_t;

typedef struct {
    uint64_t transaction_id;
    uint32_t frame_index;
} db_frame_identity_t;

typedef struct {
    db_frame_phase_t phase;
    db_presenter_frame_session_t presenter;
    db_benchmark_frame_transaction_t benchmark;
    db_renderer_frame_transaction_t renderer;
    db_frame_plan_storage_t plan;
    db_frame_retry_session_t retry;
    db_frame_identity_t identity;
    db_benchmark_model_t *model;
    db_benchmark_model_ops_t benchmark_ops;
    const db_qualification_snapshot_t *qualification;
    db_presenter_frame_ops_t presenter_ops;
    db_renderer_frame_ops_t renderer_ops;
    void *presenter_context;
    void *renderer_context;
    uint64_t next_transaction_id;
    int active;
    int failed;
} db_frame_coordinator_t;

typedef struct {
    db_benchmark_model_t *model;
    const db_benchmark_model_ops_t *benchmark_ops;
    const db_qualification_snapshot_t *qualification;
    db_presenter_frame_ops_t presenter_ops;
    db_renderer_frame_ops_t renderer_ops;
    void *presenter_context;
    void *renderer_context;
} db_frame_coordinator_config_t;

int db_frame_coordinator_init(
    db_frame_coordinator_t *coordinator,
    const db_frame_coordinator_config_t *configuration);
int db_frame_coordinator_begin(db_frame_coordinator_t *coordinator,
                               uint32_t frame_index);
db_frame_step_result_t
db_frame_coordinator_step(db_frame_coordinator_t *coordinator);
db_frame_step_result_t
db_frame_coordinator_drive(db_frame_coordinator_t *coordinator);
db_frame_step_result_t
db_frame_coordinator_run_frame(db_frame_coordinator_t *coordinator,
                               uint32_t frame_index);
void db_frame_coordinator_abort(db_frame_coordinator_t *coordinator);

#endif
