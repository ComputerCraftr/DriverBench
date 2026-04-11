#include "db_poll_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "db_log.h"

#define DB_NS_PER_MS_U64 1000000ULL
#define DB_NS_PER_SECOND_U64 1000000000ULL

static const db_poll_policy_t g_progress_policies[] = {
    {DB_PROGRESS_VK_PRIMARY_FENCE, "vk_primary_fence", 8U,
     100U * DB_NS_PER_MS_U64, 800U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FAIL},
    {DB_PROGRESS_VK_ACQUIRE_IMAGE, "vk_acquire_image", 8U,
     100U * DB_NS_PER_MS_U64, 800U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FAIL},
    {DB_PROGRESS_VK_CALIBRATION_READY, "vk_calibration_ready", 1U,
     DB_NS_PER_MS_U64, DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_SKIP},
    {DB_PROGRESS_VK_CANDIDATE_COMPLETE, "vk_candidate_complete", 8U,
     10U * DB_NS_PER_MS_U64, 80U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FALLBACK},
    {DB_PROGRESS_VK_WORKER_SLOT_REUSE, "vk_worker_slot_reuse", 1U,
     DB_NS_PER_MS_U64, DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FALLBACK},
    {DB_PROGRESS_GL_UPLOAD_REUSE, "gl_upload_reuse", 8U, DB_NS_PER_MS_U64,
     8U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FALLBACK},
    {DB_PROGRESS_GL_PENDING_SYNC_PROBE, "gl_pending_sync_probe", 1U, 1U, 1U,
     DB_SYNC_TIMEOUT_SKIP},
    {DB_PROGRESS_GL_SHADOW_SLOT_PROBE, "gl_shadow_slot_probe", 1U, 1U, 1U,
     DB_SYNC_TIMEOUT_SKIP},
    {DB_PROGRESS_GLFW_RESIZE, "glfw_resize", 50U, 5U * DB_NS_PER_MS_U64,
     250U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FAIL},
    {DB_PROGRESS_KMS_PAGE_FLIP, "kms_page_flip", 100U, 10U * DB_NS_PER_MS_U64,
     1000U * DB_NS_PER_MS_U64, DB_SYNC_TIMEOUT_FAIL},
    {DB_PROGRESS_FRAME_RETRY, "frame_retry", 8U, 0U, 1000U * DB_NS_PER_MS_U64,
     DB_SYNC_TIMEOUT_FAIL},
    {DB_PROGRESS_GL_ERROR_DRAIN, "gl_error_drain", 64U, 0U, 0U,
     DB_SYNC_TIMEOUT_SKIP},
};

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
    const db_poll_policy_t *const policy = db_progress_policy_get(id);
    return (policy != NULL) ? policy->name : "invalid";
}

const char *db_sync_wait_status_name(db_sync_wait_status_t status) {
    switch (status) {
    case DB_SYNC_WAIT_COMPLETED:
        return "completed";
    case DB_SYNC_WAIT_TIMEOUT:
        return "timeout";
    case DB_SYNC_WAIT_FAILED:
        return "failed";
    case DB_SYNC_WAIT_UNSUPPORTED:
        return "unsupported";
    case DB_SYNC_WAIT_INVALID:
        return "invalid";
    }
    return "unknown";
}

const char *db_sync_timeout_action_name(db_sync_timeout_action_t action) {
    switch (action) {
    case DB_SYNC_TIMEOUT_FAIL:
        return "fail";
    case DB_SYNC_TIMEOUT_FALLBACK:
        return "fallback";
    case DB_SYNC_TIMEOUT_SKIP:
        return "skip";
    }
    return "unknown";
}

const db_poll_policy_t *db_progress_policy_get(db_progress_policy_id_t id) {
    if ((id < 0) || (id >= DB_PROGRESS_PROFILE_COUNT)) {
        return NULL;
    }
    return &g_progress_policies[(size_t)id];
}

db_sync_wait_result_t db_sync_wait_result_make(db_sync_wait_status_t status,
                                               uint32_t attempts,
                                               uint64_t elapsed_ns,
                                               uint32_t native_result,
                                               const char *reason) {
    return (db_sync_wait_result_t){
        .policy_id = DB_PROGRESS_PROFILE_COUNT,
        .status = status,
        .attempts = attempts,
        .elapsed_ns = elapsed_ns,
        .native_result = native_result,
        .reason = (reason != NULL) ? reason : db_sync_wait_status_name(status),
    };
}

int db_poll_policy_validate(const db_poll_policy_t *policy) {
    if ((policy == NULL) || (policy->name == NULL) ||
        (policy->id >= DB_PROGRESS_PROFILE_COUNT) ||
        (policy->max_attempts == 0U)) {
        return 0;
    }
    if ((policy->attempt_timeout_ns == 0U) &&
        (policy->total_timeout_ns == 0U) &&
        (policy->id != DB_PROGRESS_GL_ERROR_DRAIN)) {
        return 0;
    }
    return 1;
}

db_deadline_t db_deadline_after(uint64_t now_ns, uint64_t duration_ns) {
    const uint64_t expires_ns =
        (UINT64_MAX - now_ns < duration_ns) ? UINT64_MAX : now_ns + duration_ns;
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

db_sync_wait_result_t db_progress_execute_with_clock(
    db_progress_policy_id_t id, db_progress_attempt_fn_t attempt_fn,
    void *user_data, db_progress_now_fn_t now_fn, void *clock_user_data) {
    const db_poll_policy_t *const policy = db_progress_policy_get(id);
    if ((policy == NULL) || (attempt_fn == NULL) || (now_fn == NULL) ||
        (db_poll_policy_validate(policy) == 0) ||
        (policy->attempt_timeout_ns == 0U)) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_INVALID, 0U, 0U, 0U,
                                        "invalid_policy");
    }

    const uint64_t start_ns = now_fn(clock_user_data);
    const db_deadline_t deadline =
        db_deadline_after(start_ns, policy->total_timeout_ns);
    db_sync_wait_result_t last =
        db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U, 0U, "timeout");
    for (uint32_t attempt = 0U; attempt < policy->max_attempts; attempt++) {
        const uint64_t before_ns = now_fn(clock_user_data);
        const uint64_t remaining_ns =
            db_deadline_remaining_ns(&deadline, before_ns);
        if (remaining_ns == 0U) {
            break;
        }
        const uint64_t attempt_timeout_ns =
            (policy->attempt_timeout_ns < remaining_ns)
                ? policy->attempt_timeout_ns
                : remaining_ns;
        last = attempt_fn(user_data, attempt_timeout_ns);
        last.policy_id = id;
        last.attempts = attempt + 1U;
        const uint64_t after_ns = now_fn(clock_user_data);
        last.elapsed_ns = after_ns - start_ns;
        if (last.status != DB_SYNC_WAIT_TIMEOUT) {
            return last;
        }
        if (db_deadline_expired(&deadline, after_ns) != 0) {
            break;
        }
    }
    last.status = DB_SYNC_WAIT_TIMEOUT;
    last.policy_id = id;
    last.reason = "timeout";
    return last;
}

db_sync_wait_result_t db_progress_execute(db_progress_policy_id_t id,
                                          db_progress_attempt_fn_t attempt_fn,
                                          void *user_data) {
    return db_progress_execute_with_clock(id, attempt_fn, user_data,
                                          db_progress_now_monotonic, NULL);
}

void db_retry_tracker_reset(db_retry_tracker_t *tracker) {
    if (tracker != NULL) {
        *tracker = (db_retry_tracker_t){0};
    }
}

int db_retry_tracker_record(db_retry_tracker_t *tracker,
                            db_progress_policy_id_t policy_id,
                            uint64_t now_ns) {
    const db_poll_policy_t *const policy = db_progress_policy_get(policy_id);
    if ((tracker == NULL) || (policy == NULL)) {
        return 0;
    }
    if ((tracker->active == 0) || (tracker->policy_id != policy_id)) {
        *tracker = (db_retry_tracker_t){
            .policy_id = policy_id,
            .consecutive_retries = 0U,
            .deadline = db_deadline_after(now_ns, policy->total_timeout_ns),
            .active = 1,
        };
    }
    tracker->consecutive_retries++;
    return (tracker->consecutive_retries <= policy->max_attempts) &&
           (db_deadline_expired(&tracker->deadline, now_ns) == 0);
}

void db_progress_log_outcome(const char *backend, const char *operation,
                             db_progress_policy_id_t policy_id,
                             const db_sync_wait_result_t *result) {
    const db_poll_policy_t *const policy = db_progress_policy_get(policy_id);
    if ((backend == NULL) || (operation == NULL) || (policy == NULL) ||
        (result == NULL) || (result->status == DB_SYNC_WAIT_COMPLETED)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("profile", policy->name),
        DB_LOG_TOKEN("operation", operation),
        DB_LOG_TOKEN("status", db_sync_wait_status_name(result->status)),
        DB_LOG_TOKEN("action",
                     db_sync_timeout_action_name(policy->timeout_action)),
        DB_LOG_U64("attempts", result->attempts),
        DB_LOG_U64("elapsed_ns", result->elapsed_ns),
        DB_LOG_U64("deadline_ns", policy->total_timeout_ns),
        DB_LOG_U64("native_result", result->native_result),
        DB_LOG_TOKEN("reason", result->reason),
    };
    if ((result->status == DB_SYNC_WAIT_FAILED) ||
        (policy->timeout_action == DB_SYNC_TIMEOUT_FAIL)) {
        db_log_error(backend, "wait_outcome", fields,
                     DB_LOG_FIELD_COUNT(fields));
    } else {
        db_log_info(backend, "wait_outcome", fields,
                    DB_LOG_FIELD_COUNT(fields));
    }
}
