#ifndef DRIVERBENCH_DB_POLL_POLICY_H
#define DRIVERBENCH_DB_POLL_POLICY_H

#include <stdint.h>

typedef enum {
    DB_PROGRESS_VK_PRIMARY_FENCE = 0,
    DB_PROGRESS_VK_ACQUIRE_IMAGE,
    DB_PROGRESS_VK_CALIBRATION_READY,
    DB_PROGRESS_VK_CANDIDATE_COMPLETE,
    DB_PROGRESS_VK_WORKER_SLOT_REUSE,
    DB_PROGRESS_GL_UPLOAD_REUSE,
    DB_PROGRESS_GL_PENDING_SYNC_PROBE,
    DB_PROGRESS_GL_SHADOW_SLOT_PROBE,
    DB_PROGRESS_GLFW_RESIZE,
    DB_PROGRESS_KMS_PAGE_FLIP,
    DB_PROGRESS_FRAME_RETRY,
    DB_PROGRESS_GL_ERROR_DRAIN,
    DB_PROGRESS_PROFILE_COUNT,
} db_progress_policy_id_t;

typedef enum {
    DB_SYNC_WAIT_COMPLETED = 0,
    DB_SYNC_WAIT_TIMEOUT = 1,
    DB_SYNC_WAIT_FAILED = 2,
    DB_SYNC_WAIT_UNSUPPORTED = 3,
    DB_SYNC_WAIT_INVALID = 4,
} db_sync_wait_status_t;

typedef enum {
    DB_SYNC_TIMEOUT_FAIL = 0,
    DB_SYNC_TIMEOUT_FALLBACK = 1,
    DB_SYNC_TIMEOUT_SKIP = 2,
} db_sync_timeout_action_t;

typedef struct {
    const char *name;
    uint64_t attempt_timeout_ns;
    uint64_t total_timeout_ns;
    db_progress_policy_id_t id;
    uint32_t max_attempts;
    db_sync_timeout_action_t timeout_action;
} db_poll_policy_t;

typedef struct {
    db_progress_policy_id_t policy_id;
    db_sync_wait_status_t status;
    uint32_t attempts;
    uint64_t elapsed_ns;
    uint32_t native_result;
    const char *reason;
} db_sync_wait_result_t;

typedef struct {
    uint64_t started_ns;
    uint64_t expires_ns;
} db_deadline_t;

typedef struct {
    db_progress_policy_id_t policy_id;
    uint32_t consecutive_retries;
    db_deadline_t deadline;
    int active;
} db_retry_tracker_t;

typedef uint64_t (*db_progress_now_fn_t)(void *user_data);
typedef db_sync_wait_result_t (*db_progress_attempt_fn_t)(void *user_data,
                                                          uint64_t timeout_ns);

const char *db_progress_policy_name(db_progress_policy_id_t id);
const char *db_sync_wait_status_name(db_sync_wait_status_t status);
const char *db_sync_timeout_action_name(db_sync_timeout_action_t action);
const db_poll_policy_t *db_progress_policy_get(db_progress_policy_id_t id);
db_sync_wait_result_t db_progress_execute(db_progress_policy_id_t id,
                                          db_progress_attempt_fn_t attempt_fn,
                                          void *user_data);
db_sync_wait_result_t db_progress_execute_with_clock(
    db_progress_policy_id_t id, db_progress_attempt_fn_t attempt_fn,
    void *user_data, db_progress_now_fn_t now_fn, void *clock_user_data);
db_sync_wait_result_t db_sync_wait_result_make(db_sync_wait_status_t status,
                                               uint32_t attempts,
                                               uint64_t elapsed_ns,
                                               uint32_t native_result,
                                               const char *reason);
int db_poll_policy_validate(const db_poll_policy_t *policy);

db_deadline_t db_deadline_after(uint64_t now_ns, uint64_t duration_ns);
uint64_t db_deadline_remaining_ns(const db_deadline_t *deadline,
                                  uint64_t now_ns);
int db_deadline_expired(const db_deadline_t *deadline, uint64_t now_ns);

void db_retry_tracker_reset(db_retry_tracker_t *tracker);
int db_retry_tracker_record(db_retry_tracker_t *tracker,
                            db_progress_policy_id_t policy_id, uint64_t now_ns);

void db_progress_log_outcome(const char *backend, const char *operation,
                             db_progress_policy_id_t policy_id,
                             const db_sync_wait_result_t *result);

#endif
