#include "db_progress_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "db_log.h"
#include "db_numeric.h"

#define DB_NS_PER_MS_U64 1000000ULL
#define DB_NS_PER_SECOND_U64 1000000000ULL
#define DB_PROGRESS_POLICY(id_, name_, kind_, max_, attempt_, total_, action_) \
    {.name = (name_),                                                          \
     .attempt_timeout_ns = (attempt_),                                         \
     .total_timeout_ns = (total_),                                             \
     .id = (id_),                                                              \
     .kind = (kind_),                                                          \
     .max_attempts = (max_),                                                   \
     .action = (action_)}

static const db_progress_policy_t g_progress_policies[] = {
    DB_PROGRESS_POLICY(DB_PROGRESS_VK_PRIMARY_FENCE, "vk_primary_fence",
                       DB_PROGRESS_KIND_POLL, 8U, 100U * DB_NS_PER_MS_U64,
                       800U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FAIL),
    DB_PROGRESS_POLICY(DB_PROGRESS_VK_ACQUIRE_IMAGE, "vk_acquire_image",
                       DB_PROGRESS_KIND_POLL, 8U, 100U * DB_NS_PER_MS_U64,
                       800U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FAIL),
    DB_PROGRESS_POLICY(DB_PROGRESS_VK_CALIBRATION_READY, "vk_calibration_ready",
                       DB_PROGRESS_KIND_POLL, 1U, DB_NS_PER_MS_U64,
                       DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_SKIP),
    DB_PROGRESS_POLICY(DB_PROGRESS_VK_CANDIDATE_COMPLETE,
                       "vk_candidate_complete", DB_PROGRESS_KIND_POLL, 8U,
                       10U * DB_NS_PER_MS_U64, 80U * DB_NS_PER_MS_U64,
                       DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_VK_WORKER_SLOT_REUSE, "vk_worker_slot_reuse",
                       DB_PROGRESS_KIND_POLL, 1U, DB_NS_PER_MS_U64,
                       DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_GL_UPLOAD_REUSE, "gl_upload_reuse",
                       DB_PROGRESS_KIND_POLL, 8U, DB_NS_PER_MS_U64,
                       8U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_GL_PENDING_SYNC_PROBE,
                       "gl_pending_sync_probe", DB_PROGRESS_KIND_POLL, 1U, 1U,
                       1U, DB_PROGRESS_ACTION_SKIP),
    DB_PROGRESS_POLICY(DB_PROGRESS_GL_SHADOW_SLOT_PROBE, "gl_shadow_slot_probe",
                       DB_PROGRESS_KIND_POLL, 1U, 1U, 1U,
                       DB_PROGRESS_ACTION_SKIP),
    DB_PROGRESS_POLICY(DB_PROGRESS_GLFW_RESIZE, "glfw_resize",
                       DB_PROGRESS_KIND_POLL, 50U, 5U * DB_NS_PER_MS_U64,
                       250U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FAIL),
    DB_PROGRESS_POLICY(DB_PROGRESS_KMS_PAGE_FLIP, "kms_page_flip",
                       DB_PROGRESS_KIND_POLL, 100U, 10U * DB_NS_PER_MS_U64,
                       1000U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FAIL),
    DB_PROGRESS_POLICY(DB_PROGRESS_FRAME_RETRY, "frame_retry",
                       DB_PROGRESS_KIND_RETRY, 8U, 0U, 1000U * DB_NS_PER_MS_U64,
                       DB_PROGRESS_ACTION_FAIL),
    DB_PROGRESS_POLICY(DB_PROGRESS_GL_ERROR_DRAIN, "gl_error_drain",
                       DB_PROGRESS_KIND_DRAIN, 64U, 0U, 0U,
                       DB_PROGRESS_ACTION_SKIP),
    DB_PROGRESS_POLICY(DB_PROGRESS_GL_UPLOAD_PREPARE, "gl_upload_prepare",
                       DB_PROGRESS_KIND_RETRY, 3U, 0U, 0U,
                       DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_CONFORMANCE_HELPER, "conformance_helper",
                       DB_PROGRESS_KIND_PROCESS, 6000U, 10U * DB_NS_PER_MS_U64,
                       60U * DB_NS_PER_SECOND_U64, DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_CONFORMANCE_CHILD_CAPTURE,
                       "conformance_child_capture", DB_PROGRESS_KIND_PROCESS,
                       5500U, 10U * DB_NS_PER_MS_U64,
                       55U * DB_NS_PER_SECOND_U64, DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_CONFORMANCE_PIPE_IO, "conformance_pipe_io",
                       DB_PROGRESS_KIND_PROCESS, 5500U, 10U * DB_NS_PER_MS_U64,
                       55U * DB_NS_PER_SECOND_U64, DB_PROGRESS_ACTION_FALLBACK),
    DB_PROGRESS_POLICY(DB_PROGRESS_CONFORMANCE_REAP, "conformance_reap",
                       DB_PROGRESS_KIND_PROCESS, 25U, 10U * DB_NS_PER_MS_U64,
                       250U * DB_NS_PER_MS_U64, DB_PROGRESS_ACTION_FALLBACK),
};

#undef DB_PROGRESS_POLICY

_Static_assert(sizeof(g_progress_policies) / sizeof(g_progress_policies[0]) ==
                   DB_PROGRESS_PROFILE_COUNT,
               "progress policy registry must cover every profile");

static uint64_t db_progress_now_monotonic(void *user_data) {
    (void)user_data;
    struct timespec ts = {0};
    // libc exposes this through <time.h>, but include-cleaner attributes the
    // feature-test-gated token to an internal bits header.
    // NOLINTNEXTLINE(misc-include-cleaner)
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * DB_NS_PER_SECOND_U64) + (uint64_t)ts.tv_nsec;
}

const char *db_progress_policy_name(db_progress_policy_id_t id) {
    const db_progress_policy_t *const policy = db_progress_policy_get(id);
    return (policy != NULL) ? policy->name : "invalid";
}

const char *db_progress_status_name(db_progress_status_t status) {
    switch (status) {
    case DB_PROGRESS_COMPLETED:
        return "completed";
    case DB_PROGRESS_TIMEOUT:
        return "timeout";
    case DB_PROGRESS_FAILED:
        return "failed";
    case DB_PROGRESS_UNSUPPORTED:
        return "unsupported";
    case DB_PROGRESS_INVALID:
        return "invalid";
    }
    return "unknown";
}

const char *db_progress_action_name(db_progress_action_t action) {
    switch (action) {
    case DB_PROGRESS_ACTION_FAIL:
        return "fail";
    case DB_PROGRESS_ACTION_FALLBACK:
        return "fallback";
    case DB_PROGRESS_ACTION_SKIP:
        return "skip";
    }
    return "unknown";
}

const db_progress_policy_t *db_progress_policy_get(db_progress_policy_id_t id) {
    if ((id < 0) || (id >= DB_PROGRESS_PROFILE_COUNT)) {
        return NULL;
    }
    return &g_progress_policies[(size_t)id];
}

db_progress_outcome_t db_progress_outcome_make(db_progress_status_t status,
                                               uint32_t attempts,
                                               uint64_t elapsed_ns,
                                               uint32_t native_result,
                                               const char *reason) {
    return (db_progress_outcome_t){
        .policy_id = DB_PROGRESS_PROFILE_COUNT,
        .status = status,
        .action = DB_PROGRESS_ACTION_FAIL,
        .attempts = attempts,
        .elapsed_ns = elapsed_ns,
        .native_result = native_result,
        .reason = (reason != NULL) ? reason : db_progress_status_name(status),
    };
}

int db_progress_policy_validate(const db_progress_policy_t *policy) {
    if ((policy == NULL) || (policy->name == NULL) ||
        (policy->id >= DB_PROGRESS_PROFILE_COUNT) ||
        (policy->kind > DB_PROGRESS_KIND_PROCESS) ||
        (policy->max_attempts == 0U)) {
        return 0;
    }
    if (((policy->kind == DB_PROGRESS_KIND_POLL) ||
         (policy->kind == DB_PROGRESS_KIND_PROCESS)) &&
        ((policy->attempt_timeout_ns == 0U) ||
         (policy->total_timeout_ns == 0U))) {
        return 0;
    }
    return 1;
}

db_deadline_t db_deadline_after(uint64_t now_ns, uint64_t duration_ns) {
    const uint64_t expires_ns = db_u64_saturating_add(now_ns, duration_ns);
    return (db_deadline_t){.started_ns = now_ns, .expires_ns = expires_ns};
}

uint64_t db_deadline_remaining_ns(const db_deadline_t *deadline,
                                  uint64_t now_ns) {
    if ((deadline == NULL) || (now_ns >= deadline->expires_ns)) {
        return 0U;
    }
    return deadline->expires_ns - now_ns;
}

int db_deadline_expired(const db_deadline_t *deadline, uint64_t now_ns) {
    return (deadline == NULL) || (now_ns >= deadline->expires_ns);
}

db_progress_outcome_t db_progress_execute_with_clock(
    db_progress_policy_id_t id, db_progress_attempt_fn_t attempt_fn,
    void *user_data, db_progress_now_fn_t now_fn, void *clock_user_data) {
    const db_progress_policy_t *const policy = db_progress_policy_get(id);
    if ((policy == NULL) || (attempt_fn == NULL) || (now_fn == NULL) ||
        (db_progress_policy_validate(policy) == 0) ||
        ((policy->kind != DB_PROGRESS_KIND_POLL) &&
         (policy->kind != DB_PROGRESS_KIND_RETRY))) {
        return db_progress_outcome_make(DB_PROGRESS_INVALID, 0U, 0U, 0U,
                                        "invalid_policy_kind");
    }

    const uint64_t start_ns = now_fn(clock_user_data);
    const db_deadline_t deadline =
        db_deadline_after(start_ns, policy->total_timeout_ns);
    db_progress_outcome_t last =
        db_progress_outcome_make(DB_PROGRESS_TIMEOUT, 0U, 0U, 0U, "timeout");
    for (uint32_t attempt = 0U; attempt < policy->max_attempts; attempt++) {
        const uint64_t before_ns = now_fn(clock_user_data);
        const uint64_t remaining_ns =
            db_deadline_remaining_ns(&deadline, before_ns);
        if ((policy->total_timeout_ns != 0U) && (remaining_ns == 0U)) {
            break;
        }
        const uint64_t attempt_timeout_ns =
            (policy->kind == DB_PROGRESS_KIND_POLL)
                ? DB_MIN(policy->attempt_timeout_ns, remaining_ns)
                : 0U;
        last = attempt_fn(user_data, attempt_timeout_ns);
        last.policy_id = id;
        last.action = policy->action;
        last.attempts = attempt + 1U;
        const uint64_t after_ns = now_fn(clock_user_data);
        last.elapsed_ns = after_ns - start_ns;
        if (last.status != DB_PROGRESS_TIMEOUT) {
            return last;
        }
        if ((policy->total_timeout_ns != 0U) &&
            (db_deadline_expired(&deadline, after_ns) != 0)) {
            break;
        }
    }
    last.status = DB_PROGRESS_TIMEOUT;
    last.policy_id = id;
    last.action = policy->action;
    if (last.reason == NULL) {
        last.reason = "timeout";
    }
    return last;
}

db_progress_outcome_t db_progress_execute(db_progress_policy_id_t id,
                                          db_progress_attempt_fn_t attempt_fn,
                                          void *user_data) {
    return db_progress_execute_with_clock(id, attempt_fn, user_data,
                                          db_progress_now_monotonic, NULL);
}

int db_progress_session_begin(db_progress_session_t *session,
                              db_progress_policy_id_t policy_id,
                              uint64_t now_ns) {
    const db_progress_policy_t *const policy =
        db_progress_policy_get(policy_id);
    if ((session == NULL) || (policy == NULL) ||
        (db_progress_policy_validate(policy) == 0)) {
        return 0;
    }
    return db_progress_session_begin_with_timeout(session, policy_id, now_ns,
                                                  policy->total_timeout_ns);
}

int db_progress_session_begin_with_timeout(db_progress_session_t *session,
                                           db_progress_policy_id_t policy_id,
                                           uint64_t now_ns,
                                           uint64_t timeout_ns) {
    const db_progress_policy_t *const policy =
        db_progress_policy_get(policy_id);
    if ((session == NULL) || (policy == NULL) ||
        (db_progress_policy_validate(policy) == 0) ||
        ((policy->kind == DB_PROGRESS_KIND_PROCESS) && (timeout_ns == 0U))) {
        return 0;
    }
    const uint64_t bounded_timeout_ns =
        (policy->total_timeout_ns == 0U)
            ? timeout_ns
            : DB_MIN(timeout_ns, policy->total_timeout_ns);
    *session = (db_progress_session_t){
        .policy_id = policy_id,
        .deadline = db_deadline_after(now_ns, bounded_timeout_ns),
        .started_ns = now_ns,
        .status = DB_PROGRESS_TIMEOUT,
        .reason = "pending",
        .active = 1,
    };
    return 1;
}

uint64_t db_progress_session_next_timeout(db_progress_session_t *session,
                                          uint64_t now_ns) {
    const db_progress_policy_t *const policy =
        (session != NULL) ? db_progress_policy_get(session->policy_id) : NULL;
    if ((session == NULL) || (session->active == 0) || (policy == NULL) ||
        (session->attempts >= policy->max_attempts)) {
        return 0U;
    }
    const uint64_t remaining_ns =
        db_deadline_remaining_ns(&session->deadline, now_ns);
    if ((policy->total_timeout_ns != 0U) && (remaining_ns == 0U)) {
        return 0U;
    }
    session->attempts++;
    if (policy->attempt_timeout_ns == 0U) {
        return UINT64_MAX;
    }
    return DB_MIN(policy->attempt_timeout_ns, remaining_ns);
}

int db_progress_session_record_retry(db_progress_session_t *session,
                                     uint64_t now_ns, uint32_t native_result,
                                     const char *reason) {
    const db_progress_policy_t *const policy =
        (session != NULL) ? db_progress_policy_get(session->policy_id) : NULL;
    if ((session == NULL) || (session->active == 0) || (policy == NULL)) {
        return 0;
    }
    session->native_result = native_result;
    session->reason = (reason != NULL) ? reason : "pending";
    if ((session->attempts >= policy->max_attempts) ||
        ((policy->total_timeout_ns != 0U) &&
         (db_deadline_expired(&session->deadline, now_ns) != 0))) {
        session->active = 0;
        session->status = DB_PROGRESS_TIMEOUT;
        return 0;
    }
    return 1;
}

db_progress_outcome_t
db_progress_session_outcome(const db_progress_session_t *session,
                            uint64_t now_ns) {
    if (session == NULL) {
        return db_progress_outcome_make(DB_PROGRESS_INVALID, 0U, 0U, 0U,
                                        "missing_session");
    }
    db_progress_outcome_t outcome = db_progress_outcome_make(
        session->status, session->attempts,
        db_u64_saturating_sub(now_ns, session->started_ns),
        session->native_result, session->reason);
    outcome.policy_id = session->policy_id;
    const db_progress_policy_t *const policy =
        db_progress_policy_get(session->policy_id);
    if (policy != NULL) {
        outcome.action = policy->action;
    }
    return outcome;
}

void db_progress_session_complete(db_progress_session_t *session,
                                  db_progress_status_t status,
                                  uint32_t native_result, const char *reason) {
    if (session == NULL) {
        return;
    }
    session->status = status;
    session->native_result = native_result;
    session->reason =
        (reason != NULL) ? reason : db_progress_status_name(status);
    session->active = 0;
}

void db_progress_session_reset(db_progress_session_t *session) {
    if (session != NULL) {
        *session = (db_progress_session_t){0};
    }
}

int db_progress_policy_allows_start(db_progress_policy_id_t policy_id,
                                    uint64_t remaining_ns) {
    const db_progress_policy_t *const policy =
        db_progress_policy_get(policy_id);
    return (policy != NULL) && (policy->attempt_timeout_ns != 0U) &&
           (remaining_ns >= policy->attempt_timeout_ns);
}

db_progress_drain_result_t
db_progress_drain_execute(db_progress_policy_id_t id,
                          db_progress_drain_fn_t drain_fn, void *user_data) {
    const db_progress_policy_t *const policy = db_progress_policy_get(id);
    db_progress_drain_result_t result = {
        .outcome = db_progress_outcome_make(DB_PROGRESS_INVALID, 0U, 0U, 0U,
                                            "invalid_drain"),
    };
    if ((policy == NULL) || (policy->kind != DB_PROGRESS_KIND_DRAIN) ||
        (drain_fn == NULL)) {
        return result;
    }
    result.outcome.policy_id = id;
    result.outcome.action = policy->action;
    const uint64_t started_ns = db_progress_now_monotonic(NULL);
    for (uint32_t attempt = 0U; attempt < policy->max_attempts; attempt++) {
        const db_progress_drain_item_t item = drain_fn(user_data);
        result.outcome.attempts = attempt + 1U;
        result.outcome.native_result = item.native_result;
        if (item.done != 0) {
            result.outcome.status = DB_PROGRESS_COMPLETED;
            result.outcome.reason = "drained";
            result.outcome.elapsed_ns =
                db_progress_now_monotonic(NULL) - started_ns;
            return result;
        }
        result.drained_count++;
    }
    result.outcome.status = DB_PROGRESS_TIMEOUT;
    result.outcome.reason = "drain_limit";
    result.outcome.elapsed_ns = db_progress_now_monotonic(NULL) - started_ns;
    result.truncated = 1;
    return result;
}

void db_progress_log_outcome(const char *backend, const char *operation,
                             db_progress_policy_id_t policy_id,
                             const db_progress_outcome_t *result) {
    const db_progress_policy_t *const policy =
        db_progress_policy_get(policy_id);
    if ((backend == NULL) || (operation == NULL) || (policy == NULL) ||
        (result == NULL) || (result->status == DB_PROGRESS_COMPLETED)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("profile", policy->name),
        DB_LOG_TOKEN("operation", operation),
        DB_LOG_TOKEN("status", db_progress_status_name(result->status)),
        DB_LOG_TOKEN("action", db_progress_action_name(result->action)),
        DB_LOG_U64("attempts", result->attempts),
        DB_LOG_U64("elapsed_ns", result->elapsed_ns),
        DB_LOG_U64("deadline_ns", policy->total_timeout_ns),
        DB_LOG_U64("native_result", result->native_result),
        DB_LOG_TOKEN("reason", result->reason),
    };
    if ((result->status == DB_PROGRESS_FAILED) ||
        (result->action == DB_PROGRESS_ACTION_FAIL)) {
        db_log_error(backend, "wait_outcome", fields,
                     DB_LOG_FIELD_COUNT(fields));
    } else {
        db_log_info(backend, "wait_outcome", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
}
