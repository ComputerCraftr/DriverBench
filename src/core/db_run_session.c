#include "db_run_session.h"

#include "db_benchmark_model.h"
#include "db_conformance.h"
#include "db_conformance_service.h"
#include "db_core.h"
#include "db_frame_contracts.h"
#include "db_frame_coordinator.h"
#include "db_frame_plan.h"
#include "db_metrics_policy.h"
#include "db_numeric.h"
#include "db_qualification_contracts.h"
#include "db_render_result.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    DB_RUN_QUALIFICATION_MAX_ATTEMPTS = 24U,
};

#define DB_RUN_DEFAULT_QUALIFICATION_TIMEOUT_NS UINT64_C(60000000000)

struct db_run_session {
    db_run_session_config_t config;
    db_run_metrics_t metrics;
    db_qualification_snapshot_t qualification;
    db_renderer_applied_selection_t applied;
    db_renderer_qualification_descriptor_store_t descriptors;
    db_qualification_service_workspace_t qualification_workspace;
    db_benchmark_model_t benchmark;
    db_frame_coordinator_t coordinator;
    db_qualification_identity_generation_t pending_generation;
    uint64_t unavailable_candidate_mask;
    uint64_t qualification_resolution_count;
    uint64_t committed_frames;
    uint64_t retries;
    uint64_t frame_start_ns;
    uint64_t pacing_deadline_ns;
    uint64_t next_frame_index;
    int qualification_dirty;
    int frame_started;
};

static db_run_step_result_t run_result(db_run_outcome_t outcome,
                                       db_run_stop_reason_t reason) {
    return (db_run_step_result_t){
        .outcome = outcome,
        .stop_reason = reason,
    };
}

static int
qualification_ops_valid(const db_renderer_qualification_ops_t *operations) {
    return (operations != NULL) && (operations->describe != NULL) &&
           (operations->prepare_apply != NULL) &&
           (operations->commit_apply != NULL) &&
           (operations->abort_apply != NULL);
}

static int resolve_qualification(db_run_session_t *session) {
    if (qualification_ops_valid(&session->config.qualification_ops) == 0) {
        session->qualification = (db_qualification_snapshot_t){
            .generation = 1U,
            .outcome = DB_QUALIFICATION_OUTCOME_CONFORMING,
            .source = DB_QUALIFICATION_SOURCE_BASELINE,
            .implementation = DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES,
            .retained_lanes = 1U,
            .lane_count = 1U,
            .strategy = DB_RENDER_TARGET_CPU_SURFACE,
            .production_qualified = 1,
        };
        (void)db_snprintf(session->qualification.reason,
                          sizeof(session->qualification.reason), "%s",
                          "baseline_renderer");
        return 1;
    }

    session->descriptors = (db_renderer_qualification_descriptor_store_t){0};
    if (session->config.qualification_ops.describe(
            session->config.renderer_context, &session->descriptors) == 0) {
        return 0;
    }
    session->qualification_resolution_count++;
    const uint64_t timeout = (session->config.qualification_timeout_ns != 0U)
                                 ? session->config.qualification_timeout_ns
                                 : DB_RUN_DEFAULT_QUALIFICATION_TIMEOUT_NS;
    for (uint32_t attempt = 0U; attempt < DB_RUN_QUALIFICATION_MAX_ATTEMPTS;
         attempt++) {
        db_qualification_snapshot_t snapshot = {0};
        if (db_qualification_service_resolve_descriptors(
                &session->descriptors, &session->config.qualification_query,
                timeout, session->unavailable_candidate_mask,
                &session->qualification_workspace, &snapshot) == 0) {
            return 0;
        }
        if ((snapshot.production_qualified == 0) &&
            (snapshot.diagnostic_forced == 0)) {
            return 0;
        }
        db_renderer_selection_candidate_t candidate = {0};
        const db_renderer_prepare_status_t prepare =
            session->config.qualification_ops.prepare_apply(
                session->config.renderer_context, &snapshot, &candidate);
        if (prepare == DB_RENDERER_PREPARE_UNAVAILABLE) {
            if (snapshot.candidate_id >= 64U) {
                return 0;
            }
            session->unavailable_candidate_mask |= UINT64_C(1)
                                                   << snapshot.candidate_id;
            session->config.qualification_ops.abort_apply(
                session->config.renderer_context, &candidate);
            continue;
        }
        if (prepare != DB_RENDERER_PREPARE_OK) {
            session->config.qualification_ops.abort_apply(
                session->config.renderer_context, &candidate);
            return 0;
        }
        if (session->coordinator.active != 0) {
            db_frame_coordinator_abort(&session->coordinator);
        }
        const db_renderer_commit_status_t commit =
            session->config.qualification_ops.commit_apply(
                session->config.renderer_context, &candidate,
                &session->applied);
        if (commit != DB_RENDERER_COMMIT_OK) {
            session->config.qualification_ops.abort_apply(
                session->config.renderer_context, &candidate);
            return 0;
        }
        session->qualification = snapshot;
        session->pending_generation = session->descriptors.generation;
        session->qualification_dirty = 0;
        return 1;
    }
    return 0;
}

static int session_config_valid(const db_run_session_config_t *config) {
    return (config != NULL) &&
           (config->benchmark.benchmark_configuration != NULL) &&
           (config->presenter_ops.acquire != NULL) &&
           (config->presenter_ops.present != NULL) &&
           (config->renderer_ops.preflight != NULL) &&
           (config->renderer_ops.provision != NULL) &&
           (config->renderer_ops.execute != NULL);
}

db_run_session_status_t
db_run_session_create(const db_run_session_config_t *config,
                      db_run_session_t **session) {
    if ((session == NULL) || (session_config_valid(config) == 0)) {
        return DB_RUN_SESSION_INVALID;
    }
    *session = NULL;
    db_run_session_t *const created = calloc(1U, sizeof(*created));
    if (created == NULL) {
        return DB_RUN_SESSION_ALLOCATION_FAILED;
    }
    created->config = *config;
    created->next_frame_index = config->initial_frame_index;
    created->pending_generation = config->qualification_generation;
    if (db_run_metrics_init(&created->metrics,
                            config->recent_metrics_enabled) == 0) {
        free(created);
        return DB_RUN_SESSION_ALLOCATION_FAILED;
    }
    if (resolve_qualification(created) == 0) {
        db_run_metrics_shutdown(&created->metrics);
        free(created);
        return DB_RUN_SESSION_QUALIFICATION_FAILED;
    }
    if (db_benchmark_model_init(&created->benchmark, &config->benchmark) == 0) {
        db_run_metrics_shutdown(&created->metrics);
        free(created);
        return DB_RUN_SESSION_MODEL_FAILED;
    }
    if (db_frame_coordinator_init(
            &created->coordinator,
            &(const db_frame_coordinator_config_t){
                .model = &created->benchmark,
                .qualification = &created->qualification,
                .presenter_ops = config->presenter_ops,
                .renderer_ops = config->renderer_ops,
                .presenter_context = config->presenter_context,
                .renderer_context = config->renderer_context,
            }) == 0) {
        db_benchmark_model_shutdown(&created->benchmark);
        db_run_metrics_shutdown(&created->metrics);
        free(created);
        return DB_RUN_SESSION_COORDINATOR_FAILED;
    }
    *session = created;
    return DB_RUN_SESSION_OK;
}

static uint64_t pacing_interval_ns(double fps_cap) {
    if (!isfinite(fps_cap) || (fps_cap <= 0.0)) {
        return 0U;
    }
    const double interval = DB_NS_PER_SECOND / fps_cap;
    if ((interval <= 0.0) || (interval >= DB_TO_F64(UINT64_MAX))) {
        return 0U;
    }
    return db_checked_double_to_u64("run_session", "pacing_interval_ns",
                                    interval);
}

static db_committed_frame_summary_t committed_summary(db_run_session_t *session,
                                                      uint64_t frame_end_ns) {
    const db_frame_plan_t *const plan =
        (session->coordinator.plan.valid != 0)
            ? &session->coordinator.plan.published
            : NULL;
    const db_renderer_frame_output_t *const output =
        &session->coordinator.renderer.output;
    db_committed_frame_summary_t summary = {
        .frame_index = db_checked_u64_to_u32(
            "run_session", "committed_frame_index", session->next_frame_index),
        .committed_frame_count = session->committed_frames + 1U,
        .frame_total_ms =
            DB_TO_F64(frame_end_ns - session->frame_start_ns) / DB_NS_PER_MS,
        .qualification = session->qualification,
    };
    if (plan != NULL) {
        summary.expected_state_hash = plan->expected_state_hash;
    }
    if (output != NULL) {
        summary.working_hash = output->result.working_hash;
        summary.working_hash_valid = output->result.working_hash_valid;
        summary.renderer_critical_ms = output->critical_path_ms;
        summary.execution = output->result.execution;
        summary.execution.qualification_source = session->qualification.source;
        summary.execution.cache_status = session->qualification.cache_status;
        summary.execution.qualification_lane_count =
            session->qualification.lane_count;
        summary.execution.qualification_reason = session->qualification.reason;
        summary.execution.qualified =
            session->qualification.production_qualified;
        summary.execution.diagnostic_forced =
            session->qualification.diagnostic_forced;
    }
    return summary;
}

static void update_metrics_snapshot(db_run_session_t *session,
                                    db_committed_frame_summary_t *summary) {
    summary->metrics = (db_run_metrics_snapshot_t){
        .frame_ema_ms = session->metrics.frame_ema_ms,
        .jitter_ema_ms = session->metrics.jitter_ema_ms,
        .metric_total_samples = session->metrics.frame_total.total_samples,
        .renderer_metric_total_samples =
            session->metrics.renderer_critical.total_samples,
        .retries = session->retries,
        .metric_window_sample_count =
            db_checked_size_to_u32("run_session", "frame_metric_count",
                                   session->metrics.frame_total.count),
        .renderer_metric_window_sample_count =
            db_checked_size_to_u32("run_session", "renderer_metric_count",
                                   session->metrics.renderer_critical.count),
        .metric_window_capacity = (session->metrics.storage != NULL)
                                      ? DB_METRIC_RECENT_SAMPLE_CAPACITY
                                      : 0U,
    };
    const int final_frame =
        (session->config.frame_limit != 0U) &&
        (session->committed_frames + 1U >= session->config.frame_limit);
    if ((session->metrics.storage == NULL) || (final_frame == 0)) {
        return;
    }
    db_f64_sample_summary_t frame = {0};
    db_f64_sample_summary_t renderer = {0};
    if ((db_run_metrics_summarize_frame(&session->metrics, &frame) ==
         DB_F64_SAMPLE_RING_OK) &&
        (db_run_metrics_summarize_renderer(&session->metrics, &renderer) ==
         DB_F64_SAMPLE_RING_OK)) {
        summary->metrics.frame_window_p50_ms = frame.p50;
        summary->metrics.frame_window_p95_ms = frame.p95;
        summary->metrics.frame_window_p99_ms = frame.p99;
        summary->metrics.renderer_window_p50_ms = renderer.p50;
        summary->metrics.renderer_window_p95_ms = renderer.p95;
        summary->metrics.renderer_window_p99_ms = renderer.p99;
    }
}

db_run_step_result_t db_run_session_step(db_run_session_t *session) {
    if (session == NULL) {
        return run_result(DB_RUN_FAILED, DB_RUN_STOP_FRAME_FAILED);
    }
    const uint64_t now_ns = db_now_ns_monotonic();
    if ((session->pacing_deadline_ns != 0U) &&
        (now_ns < session->pacing_deadline_ns)) {
        db_run_step_result_t result = run_result(DB_RUN_WAIT, DB_RUN_STOP_NONE);
        result.wait_until.nanoseconds = session->pacing_deadline_ns;
        return result;
    }
    session->pacing_deadline_ns = 0U;
    if ((session->config.frame_limit > 0U) &&
        (session->committed_frames >= session->config.frame_limit)) {
        return run_result(DB_RUN_COMPLETE, DB_RUN_STOP_FRAME_LIMIT);
    }
    if (session->next_frame_index > UINT32_MAX) {
        return run_result(DB_RUN_COMPLETE, DB_RUN_STOP_FRAME_INDEX_EXHAUSTED);
    }
    if (session->qualification_dirty != 0) {
        if (session->coordinator.active != 0) {
            db_frame_coordinator_abort(&session->coordinator);
        }
        session->unavailable_candidate_mask = 0U;
        if (resolve_qualification(session) == 0) {
            return run_result(DB_RUN_FAILED, DB_RUN_STOP_QUALIFICATION_FAILED);
        }
    }
    if (session->frame_started == 0) {
        session->frame_start_ns = now_ns;
        session->frame_started = 1;
    }
    const db_frame_step_result_t frame = db_frame_coordinator_run_frame(
        &session->coordinator,
        db_checked_u64_to_u32("run_session", "next_frame_index",
                              session->next_frame_index));
    if (frame.outcome == DB_FRAME_STEP_WAIT) {
        session->retries = db_u64_saturating_add(session->retries, 1U);
        db_run_step_result_t result = run_result(DB_RUN_WAIT, DB_RUN_STOP_NONE);
        result.wait_until.nanoseconds = frame.next_deadline.expires_ns;
        return result;
    }
    if (frame.outcome != DB_FRAME_STEP_FRAME_COMMITTED) {
        return run_result(DB_RUN_FAILED, DB_RUN_STOP_FRAME_FAILED);
    }

    const uint64_t frame_end_ns = db_now_ns_monotonic();
    db_run_step_result_t result =
        run_result(DB_RUN_FRAME_COMMITTED, DB_RUN_STOP_NONE);
    result.committed_frame = committed_summary(session, frame_end_ns);
    if (db_run_metrics_accept_frame(
            &session->metrics,
            &(const db_completed_frame_metrics_t){
                .frame_total_ms = result.committed_frame.frame_total_ms,
                .renderer_critical_ms =
                    result.committed_frame.renderer_critical_ms,
                .classification = DB_RUN_METRIC_PRODUCTION,
                .renderer_critical_valid =
                    result.committed_frame.renderer_critical_ms > 0.0,
            }) != DB_F64_SAMPLE_RING_OK) {
        return run_result(DB_RUN_FAILED, DB_RUN_STOP_FRAME_FAILED);
    }
    update_metrics_snapshot(session, &result.committed_frame);
    session->committed_frames++;
    session->next_frame_index++;
    session->frame_started = 0;
    const uint64_t interval = pacing_interval_ns(session->config.fps_cap);
    if (interval != 0U) {
        session->pacing_deadline_ns =
            db_u64_saturating_add(session->frame_start_ns, interval);
        result.wait_until.nanoseconds = session->pacing_deadline_ns;
    }
    return result;
}

void db_run_session_notify_qualification_change(
    db_run_session_t *session,
    db_qualification_identity_generation_t generation) {
    if ((session == NULL) ||
        (db_qualification_generation_equal(session->pending_generation,
                                           generation) != 0)) {
        return;
    }
    session->pending_generation = generation;
    session->qualification_dirty = 1;
}

void db_run_session_destroy(db_run_session_t *session) {
    if (session == NULL) {
        return;
    }
    if (session->coordinator.active != 0) {
        db_frame_coordinator_abort(&session->coordinator);
    }
    db_benchmark_model_shutdown(&session->benchmark);
    db_run_metrics_shutdown(&session->metrics);
    free(session);
}
