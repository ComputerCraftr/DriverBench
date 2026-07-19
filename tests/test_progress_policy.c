#include "support/test_harness.h"

#include "core/db_numeric.h"
#include "core/db_progress_policy.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t now_ns;
    uint64_t advance_ns;
    uint32_t attempts;
    uint32_t complete_after;
} db_fake_progress_t;

typedef struct {
    uint32_t count;
    uint32_t done_after;
} db_fake_drain_t;

enum {
    DB_TEST_FAKE_START_NS = 100U,
    DB_TEST_FAKE_ADVANCE_NS = 10U,
    DB_TEST_NATIVE_FAILURE_RESULT = 9U,
    DB_TEST_RETRY_LIMIT_LAST_INDEX = 7U,
};

static uint64_t db_fake_now(void *user_data) {
    return ((db_fake_progress_t *)user_data)->now_ns;
}

static db_progress_outcome_t db_fake_attempt(void *user_data,
                                             uint64_t timeout_ns) {
    db_fake_progress_t *const progress = (db_fake_progress_t *)user_data;
    progress->attempts++;
    progress->now_ns += DB_MIN(progress->advance_ns, timeout_ns);
    const int complete = (progress->complete_after > 0U) &&
                         (progress->attempts >= progress->complete_after);
    return db_progress_outcome_make(
        complete ? DB_PROGRESS_COMPLETED : DB_PROGRESS_TIMEOUT, 0U, 0U,
        progress->attempts, complete ? "complete" : "pending");
}

static db_progress_drain_item_t db_fake_drain(void *user_data) {
    db_fake_drain_t *const drain = (db_fake_drain_t *)user_data;
    drain->count++;
    return (db_progress_drain_item_t){
        .native_result = drain->count,
        .done = (drain->done_after != 0U) && (drain->count > drain->done_after),
    };
}

static void db_test_progress_policy_registry(db_test_state_t *state) {
    const db_progress_policy_t *const policy =
        db_progress_policy_get(DB_PROGRESS_VK_PRIMARY_FENCE);
    DB_TEST_EXPECT_STR_EQ(state, policy->name, "vk_primary_fence");
    DB_TEST_EXPECT_EQ_U32(state, policy->max_attempts, 8U);
    DB_TEST_EXPECT_EQ_SIZE(state, policy->attempt_timeout_ns, 100000000U);
    DB_TEST_EXPECT_EQ_SIZE(state, policy->total_timeout_ns, 800000000U);
    DB_TEST_EXPECT_EQ_INT(state, policy->action, DB_PROGRESS_ACTION_FAIL);
    DB_TEST_EXPECT_EQ_INT(state, policy->kind, DB_PROGRESS_KIND_POLL);
    DB_TEST_EXPECT_TRUE(state, db_progress_policy_validate(policy) != 0);
    DB_TEST_EXPECT_TRUE(
        state, db_progress_policy_get(DB_PROGRESS_PROFILE_COUNT) == NULL);
    const db_progress_policy_t *const pipe_policy =
        db_progress_policy_get(DB_PROGRESS_CONFORMANCE_PIPE_IO);
    DB_TEST_EXPECT_STR_EQ(state, pipe_policy->name, "conformance_pipe_io");
    DB_TEST_EXPECT_EQ_SIZE(state, pipe_policy->attempt_timeout_ns, 10000000U);
    DB_TEST_EXPECT_EQ_SIZE(state, pipe_policy->total_timeout_ns,
                           UINT64_C(55000000000));
}

static void db_test_progress_drain(db_test_state_t *state) {
    db_fake_drain_t finite = {.done_after = 3U};
    const db_progress_drain_result_t finite_result = db_progress_drain_execute(
        DB_PROGRESS_GL_ERROR_DRAIN, db_fake_drain, &finite);
    DB_TEST_EXPECT_EQ_INT(state, finite_result.outcome.status,
                          DB_PROGRESS_COMPLETED);
    DB_TEST_EXPECT_EQ_INT(state, finite_result.outcome.policy_id,
                          DB_PROGRESS_GL_ERROR_DRAIN);
    DB_TEST_EXPECT_EQ_U32(state, finite_result.drained_count, 3U);
    DB_TEST_EXPECT_EQ_INT(state, finite_result.truncated, 0);

    db_fake_drain_t endless = {0};
    const db_progress_drain_result_t bounded_result = db_progress_drain_execute(
        DB_PROGRESS_GL_ERROR_DRAIN, db_fake_drain, &endless);
    DB_TEST_EXPECT_EQ_INT(state, bounded_result.outcome.status,
                          DB_PROGRESS_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, bounded_result.drained_count, 64U);
    DB_TEST_EXPECT_EQ_INT(state, bounded_result.truncated, 1);
}

static void db_test_progress_session_deadline(db_test_state_t *state) {
    db_progress_session_t session = {0};
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_progress_session_begin_with_timeout(
            &session, DB_PROGRESS_CONFORMANCE_HELPER, 100U, 25U),
        1);
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_progress_session_next_timeout(&session, 100U), 25U);
    DB_TEST_EXPECT_EQ_INT(
        state, db_progress_session_record_retry(&session, 125U, 7U, "pending"),
        0);
    const db_progress_outcome_t timeout =
        db_progress_session_outcome(&session, 125U);
    DB_TEST_EXPECT_EQ_INT(state, timeout.policy_id,
                          DB_PROGRESS_CONFORMANCE_HELPER);
    DB_TEST_EXPECT_EQ_INT(state, timeout.status, DB_PROGRESS_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, timeout.native_result, 7U);

    db_progress_session_complete(&session, DB_PROGRESS_FAILED,
                                 DB_TEST_NATIVE_FAILURE_RESULT,
                                 "native_failure");
    const db_progress_outcome_t failed =
        db_progress_session_outcome(&session, 130U);
    DB_TEST_EXPECT_EQ_INT(state, failed.status, DB_PROGRESS_FAILED);
    DB_TEST_EXPECT_STR_EQ(state, failed.reason, "native_failure");

    DB_TEST_EXPECT_EQ_INT(
        state,
        db_progress_session_begin_with_timeout(
            &session, DB_PROGRESS_CONFORMANCE_REAP, 200U, UINT64_MAX),
        1);
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_deadline_remaining_ns(&session.deadline, 200U), 250000000U);
}

static void db_test_progress_executor_completes(db_test_state_t *state) {
    db_fake_progress_t progress = {
        .now_ns = DB_TEST_FAKE_START_NS,
        .advance_ns = DB_TEST_FAKE_ADVANCE_NS,
        .attempts = 0U,
        .complete_after = 3U,
    };
    const db_progress_outcome_t result = db_progress_execute_with_clock(
        DB_PROGRESS_GL_UPLOAD_REUSE, db_fake_attempt, &progress, db_fake_now,
        &progress);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_PROGRESS_COMPLETED);
    DB_TEST_EXPECT_EQ_INT(state, result.policy_id, DB_PROGRESS_GL_UPLOAD_REUSE);
    DB_TEST_EXPECT_EQ_INT(state, result.action, DB_PROGRESS_ACTION_FALLBACK);
    DB_TEST_EXPECT_EQ_U32(state, result.attempts, 3U);
    DB_TEST_EXPECT_EQ_SIZE(state, result.elapsed_ns, 30U);
}

static void
db_test_progress_executor_honors_attempt_limit(db_test_state_t *state) {
    db_fake_progress_t progress = {
        .now_ns = 0U,
        .advance_ns = 1U,
        .attempts = 0U,
        .complete_after = 0U,
    };
    const db_progress_outcome_t result = db_progress_execute_with_clock(
        DB_PROGRESS_GL_UPLOAD_REUSE, db_fake_attempt, &progress, db_fake_now,
        &progress);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_PROGRESS_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, result.attempts, 8U);
}

static void db_test_deadline_and_retry_session(db_test_state_t *state) {
    const db_deadline_t deadline = db_deadline_after(100U, 25U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_deadline_remaining_ns(&deadline, 110U),
                           15U);
    DB_TEST_EXPECT_EQ_INT(state, db_deadline_expired(&deadline, 125U), 1);
    const db_deadline_t saturated = db_deadline_after(UINT64_MAX - 5U, 10U);
    DB_TEST_EXPECT_EQ_SIZE(state, saturated.expires_ns, UINT64_MAX);
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_deadline_remaining_ns(&saturated, UINT64_MAX - 2U), 2U);

    db_progress_session_t session = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_progress_session_begin(&session, DB_PROGRESS_FRAME_RETRY, 0U),
        1);
    for (uint32_t retry = 0U; retry < DB_TEST_RETRY_LIMIT_LAST_INDEX; retry++) {
        DB_TEST_EXPECT_TRUE(
            state, db_progress_session_next_timeout(&session, retry) != 0U);
        DB_TEST_EXPECT_EQ_INT(
            state,
            db_progress_session_record_retry(&session, retry, 0U, "retry"), 1);
    }
    DB_TEST_EXPECT_TRUE(state,
                        db_progress_session_next_timeout(
                            &session, DB_TEST_RETRY_LIMIT_LAST_INDEX) != 0U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_progress_session_record_retry(
            &session, DB_TEST_RETRY_LIMIT_LAST_INDEX, 0U, "retry"),
        0);
    db_progress_session_reset(&session);
    DB_TEST_EXPECT_EQ_U32(state, session.attempts, 0U);
}

static void db_test_progress_outcome_labels(db_test_state_t *state) {
    const db_progress_outcome_t result =
        db_progress_outcome_make(DB_PROGRESS_TIMEOUT, 3U, 75U, 123U, NULL);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_PROGRESS_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, result.attempts, 3U);
    DB_TEST_EXPECT_EQ_SIZE(state, result.elapsed_ns, 75U);
    DB_TEST_EXPECT_EQ_U32(state, result.native_result, 123U);
    DB_TEST_EXPECT_STR_EQ(state, result.reason, "timeout");
    DB_TEST_EXPECT_STR_EQ(
        state, db_progress_action_name(DB_PROGRESS_ACTION_SKIP), "skip");
}

unsigned db_progress_policy_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"progress_policy_registry", db_test_progress_policy_registry},
        {"progress_executor_completes", db_test_progress_executor_completes},
        {"progress_executor_honors_attempt_limit",
         db_test_progress_executor_honors_attempt_limit},
        {"progress_drain", db_test_progress_drain},
        {"progress_session_deadline", db_test_progress_session_deadline},
        {"deadline_and_retry_session", db_test_deadline_and_retry_session},
        {"progress_outcome_labels", db_test_progress_outcome_labels},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
