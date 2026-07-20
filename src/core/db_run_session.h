#ifndef DRIVERBENCH_CORE_DB_RUN_SESSION_H
#define DRIVERBENCH_CORE_DB_RUN_SESSION_H

#include "core/db_render_result.h"
#include "db_benchmark_model.h"
#include "db_conformance_service.h"
#include "db_frame_contracts.h"
#include "db_qualification_contracts.h"

#include <stdint.h>

typedef struct db_run_session db_run_session_t;

typedef enum {
    DB_RUN_SESSION_OK = 0,
    DB_RUN_SESSION_INVALID,
    DB_RUN_SESSION_ALLOCATION_FAILED,
    DB_RUN_SESSION_QUALIFICATION_FAILED,
    DB_RUN_SESSION_MODEL_FAILED,
    DB_RUN_SESSION_COORDINATOR_FAILED,
} db_run_session_status_t;

typedef enum {
    DB_RUN_PROGRESS = 0,
    DB_RUN_WAIT,
    DB_RUN_FRAME_COMMITTED,
    DB_RUN_COMPLETE,
    DB_RUN_FAILED,
} db_run_outcome_t;

typedef enum {
    DB_RUN_STOP_NONE = 0,
    DB_RUN_STOP_FRAME_LIMIT,
    DB_RUN_STOP_FRAME_FAILED,
    DB_RUN_STOP_QUALIFICATION_FAILED,
    DB_RUN_STOP_FRAME_INDEX_EXHAUSTED,
    DB_RUN_STOP_EXTERNAL,
} db_run_stop_reason_t;

typedef struct {
    uint64_t nanoseconds;
} db_time_point_t;

typedef struct {
    double frame_ema_ms;
    double jitter_ema_ms;
    double frame_window_p50_ms;
    double frame_window_p95_ms;
    double frame_window_p99_ms;
    double renderer_window_p50_ms;
    double renderer_window_p95_ms;
    double renderer_window_p99_ms;
    uint64_t metric_total_samples;
    uint64_t renderer_metric_total_samples;
    uint64_t retries;
    uint32_t metric_window_sample_count;
    uint32_t renderer_metric_window_sample_count;
    uint32_t metric_window_capacity;
} db_run_metrics_snapshot_t;

typedef struct {
    uint32_t frame_index;
    uint64_t committed_frame_count;
    uint64_t expected_state_hash;
    uint64_t working_hash;
    double frame_total_ms;
    double renderer_critical_ms;
    db_render_execution_report_t execution;
    db_qualification_snapshot_t qualification;
    db_run_metrics_snapshot_t metrics;
    int working_hash_valid;
} db_committed_frame_summary_t;

typedef struct {
    db_run_outcome_t outcome;
    db_time_point_t wait_until;
    db_run_stop_reason_t stop_reason;
    db_committed_frame_summary_t committed_frame;
} db_run_step_result_t;

typedef struct {
    db_benchmark_model_config_t benchmark;
    db_presenter_frame_ops_t presenter_ops;
    db_renderer_frame_ops_t renderer_ops;
    db_renderer_qualification_ops_t qualification_ops;
    db_conformance_query_t qualification_query;
    db_qualification_identity_generation_t qualification_generation;
    void *presenter_context;
    void *renderer_context;
    uint64_t qualification_timeout_ns;
    double fps_cap;
    uint32_t frame_limit;
    uint32_t initial_frame_index;
    int recent_metrics_enabled;
} db_run_session_config_t;

db_run_session_status_t
db_run_session_create(const db_run_session_config_t *config,
                      db_run_session_t **session);
db_run_step_result_t db_run_session_step(db_run_session_t *session);
void db_run_session_notify_qualification_change(
    db_run_session_t *session,
    db_qualification_identity_generation_t generation);
void db_run_session_destroy(db_run_session_t *session);

#endif
