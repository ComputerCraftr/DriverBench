#ifndef DRIVERBENCH_DB_PROGRESS_POLICY_H
#define DRIVERBENCH_DB_PROGRESS_POLICY_H

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
    DB_PROGRESS_GL_UPLOAD_PREPARE,
    DB_PROGRESS_CONFORMANCE_HELPER,
    DB_PROGRESS_CONFORMANCE_CHILD_CAPTURE,
    DB_PROGRESS_CONFORMANCE_PIPE_IO,
    DB_PROGRESS_CONFORMANCE_REAP,
    DB_PROGRESS_PROFILE_COUNT,
} db_progress_policy_id_t;

typedef enum {
    DB_PROGRESS_KIND_POLL = 0,
    DB_PROGRESS_KIND_RETRY,
    DB_PROGRESS_KIND_DRAIN,
    DB_PROGRESS_KIND_PROCESS,
} db_progress_policy_kind_t;

typedef enum {
    DB_PROGRESS_COMPLETED = 0,
    DB_PROGRESS_TIMEOUT = 1,
    DB_PROGRESS_FAILED = 2,
    DB_PROGRESS_UNSUPPORTED = 3,
    DB_PROGRESS_INVALID = 4,
} db_progress_status_t;

typedef enum {
    DB_PROGRESS_ACTION_FAIL = 0,
    DB_PROGRESS_ACTION_FALLBACK = 1,
    DB_PROGRESS_ACTION_SKIP = 2,
} db_progress_action_t;

typedef struct {
    const char *name;
    uint64_t attempt_timeout_ns;
    uint64_t total_timeout_ns;
    db_progress_policy_id_t id;
    db_progress_policy_kind_t kind;
    uint32_t max_attempts;
    db_progress_action_t action;
} db_progress_policy_t;

typedef struct {
    db_progress_policy_id_t policy_id;
    db_progress_status_t status;
    db_progress_action_t action;
    uint32_t attempts;
    uint64_t elapsed_ns;
    uint32_t native_result;
    const char *reason;
} db_progress_outcome_t;

typedef struct {
    uint64_t started_ns;
    uint64_t expires_ns;
} db_deadline_t;

typedef struct {
    db_progress_policy_id_t policy_id;
    db_deadline_t deadline;
    uint64_t started_ns;
    uint32_t attempts;
    uint32_t native_result;
    db_progress_status_t status;
    const char *reason;
    int active;
} db_progress_session_t;

typedef struct {
    uint32_t native_result;
    int done;
} db_progress_drain_item_t;

typedef struct {
    db_progress_outcome_t outcome;
    uint32_t drained_count;
    int truncated;
} db_progress_drain_result_t;

typedef uint64_t (*db_progress_now_fn_t)(void *user_data);
typedef db_progress_outcome_t (*db_progress_attempt_fn_t)(void *user_data,
                                                          uint64_t timeout_ns);
typedef db_progress_drain_item_t (*db_progress_drain_fn_t)(void *user_data);

const char *db_progress_policy_name(db_progress_policy_id_t id);
const char *db_progress_status_name(db_progress_status_t status);
const char *db_progress_action_name(db_progress_action_t action);
const db_progress_policy_t *db_progress_policy_get(db_progress_policy_id_t id);
db_progress_outcome_t db_progress_execute(db_progress_policy_id_t id,
                                          db_progress_attempt_fn_t attempt_fn,
                                          void *user_data);
db_progress_outcome_t db_progress_execute_with_clock(
    db_progress_policy_id_t id, db_progress_attempt_fn_t attempt_fn,
    void *user_data, db_progress_now_fn_t now_fn, void *clock_user_data);
db_progress_outcome_t db_progress_outcome_make(db_progress_status_t status,
                                               uint32_t attempts,
                                               uint64_t elapsed_ns,
                                               uint32_t native_result,
                                               const char *reason);
int db_progress_policy_validate(const db_progress_policy_t *policy);

int db_progress_session_begin(db_progress_session_t *session,
                              db_progress_policy_id_t policy_id,
                              uint64_t now_ns);
int db_progress_session_begin_with_timeout(db_progress_session_t *session,
                                           db_progress_policy_id_t policy_id,
                                           uint64_t now_ns,
                                           uint64_t timeout_ns);
uint64_t db_progress_session_next_timeout(db_progress_session_t *session,
                                          uint64_t now_ns);
int db_progress_session_record_retry(db_progress_session_t *session,
                                     uint64_t now_ns, uint32_t native_result,
                                     const char *reason);
db_progress_outcome_t
db_progress_session_outcome(const db_progress_session_t *session,
                            uint64_t now_ns);
void db_progress_session_complete(db_progress_session_t *session,
                                  db_progress_status_t status,
                                  uint32_t native_result, const char *reason);
void db_progress_session_reset(db_progress_session_t *session);
int db_progress_policy_allows_start(db_progress_policy_id_t policy_id,
                                    uint64_t remaining_ns);

db_progress_drain_result_t
db_progress_drain_execute(db_progress_policy_id_t id,
                          db_progress_drain_fn_t drain_fn, void *user_data);

db_deadline_t db_deadline_after(uint64_t now_ns, uint64_t duration_ns);
uint64_t db_deadline_remaining_ns(const db_deadline_t *deadline,
                                  uint64_t now_ns);
int db_deadline_expired(const db_deadline_t *deadline, uint64_t now_ns);

void db_progress_log_outcome(const char *backend, const char *operation,
                             db_progress_policy_id_t policy_id,
                             const db_progress_outcome_t *result);

#endif
