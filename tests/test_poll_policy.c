#include "support/test_harness.h"

#include "core/db_poll_policy.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t now_ns;
    uint64_t advance_ns;
    uint32_t attempts;
    uint32_t complete_after;
} db_fake_progress_t;

enum {
    DB_TEST_FAKE_START_NS = 100U,
    DB_TEST_FAKE_ADVANCE_NS = 10U,
};

static uint64_t db_fake_now(void *user_data) {
    return ((db_fake_progress_t *)user_data)->now_ns;
}

static db_sync_wait_result_t db_fake_attempt(void *user_data,
                                             uint64_t timeout_ns) {
    db_fake_progress_t *const progress = (db_fake_progress_t *)user_data;
    progress->attempts++;
    progress->now_ns +=
        (progress->advance_ns < timeout_ns) ? progress->advance_ns : timeout_ns;
    const int complete = (progress->complete_after > 0U) &&
                         (progress->attempts >= progress->complete_after);
    return db_sync_wait_result_make(
        complete ? DB_SYNC_WAIT_COMPLETED : DB_SYNC_WAIT_TIMEOUT, 0U, 0U,
        progress->attempts, complete ? "complete" : "pending");
}

static void db_test_progress_policy_registry(db_test_state_t *state) {
    const db_poll_policy_t *const policy =
        db_progress_policy_get(DB_PROGRESS_VK_PRIMARY_FENCE);
    DB_TEST_EXPECT_STR_EQ(state, policy->name, "vk_primary_fence");
    DB_TEST_EXPECT_EQ_U32(state, policy->max_attempts, 8U);
    DB_TEST_EXPECT_EQ_SIZE(state, policy->attempt_timeout_ns, 100000000U);
    DB_TEST_EXPECT_EQ_SIZE(state, policy->total_timeout_ns, 800000000U);
    DB_TEST_EXPECT_EQ_INT(state, policy->timeout_action, DB_SYNC_TIMEOUT_FAIL);
    DB_TEST_EXPECT_TRUE(state, db_poll_policy_validate(policy) != 0);
    DB_TEST_EXPECT_TRUE(
        state, db_progress_policy_get(DB_PROGRESS_PROFILE_COUNT) == NULL);
}

static void db_test_progress_executor_completes(db_test_state_t *state) {
    db_fake_progress_t progress = {
        .now_ns = DB_TEST_FAKE_START_NS,
        .advance_ns = DB_TEST_FAKE_ADVANCE_NS,
        .attempts = 0U,
        .complete_after = 3U,
    };
    const db_sync_wait_result_t result = db_progress_execute_with_clock(
        DB_PROGRESS_GL_UPLOAD_REUSE, db_fake_attempt, &progress, db_fake_now,
        &progress);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_SYNC_WAIT_COMPLETED);
    DB_TEST_EXPECT_EQ_INT(state, result.policy_id, DB_PROGRESS_GL_UPLOAD_REUSE);
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
    const db_sync_wait_result_t result = db_progress_execute_with_clock(
        DB_PROGRESS_GL_UPLOAD_REUSE, db_fake_attempt, &progress, db_fake_now,
        &progress);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_SYNC_WAIT_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, result.attempts, 8U);
}

static void db_test_deadline_and_retry_tracker(db_test_state_t *state) {
    const db_deadline_t deadline = db_deadline_after(100U, 25U);
    DB_TEST_EXPECT_EQ_SIZE(state, db_deadline_remaining_ns(&deadline, 110U),
                           15U);
    DB_TEST_EXPECT_EQ_INT(state, db_deadline_expired(&deadline, 125U), 1);
    const db_deadline_t saturated = db_deadline_after(UINT64_MAX - 5U, 10U);
    DB_TEST_EXPECT_EQ_SIZE(state, saturated.expires_ns, UINT64_MAX);
    DB_TEST_EXPECT_EQ_SIZE(
        state, db_deadline_remaining_ns(&saturated, UINT64_MAX - 2U), 2U);

    db_retry_tracker_t tracker = {0};
    for (uint32_t retry = 0U; retry < 8U; retry++) {
        DB_TEST_EXPECT_EQ_INT(
            state,
            db_retry_tracker_record(&tracker, DB_PROGRESS_FRAME_RETRY, retry),
            1);
    }
    DB_TEST_EXPECT_EQ_INT(
        state, db_retry_tracker_record(&tracker, DB_PROGRESS_FRAME_RETRY, 8U),
        0);
    db_retry_tracker_reset(&tracker);
    DB_TEST_EXPECT_EQ_U32(state, tracker.consecutive_retries, 0U);
}

static void db_test_sync_wait_result_labels(db_test_state_t *state) {
    const db_sync_wait_result_t result =
        db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 3U, 75U, 123U, NULL);
    DB_TEST_EXPECT_EQ_INT(state, result.status, DB_SYNC_WAIT_TIMEOUT);
    DB_TEST_EXPECT_EQ_U32(state, result.attempts, 3U);
    DB_TEST_EXPECT_EQ_SIZE(state, result.elapsed_ns, 75U);
    DB_TEST_EXPECT_EQ_U32(state, result.native_result, 123U);
    DB_TEST_EXPECT_STR_EQ(state, result.reason, "timeout");
    DB_TEST_EXPECT_STR_EQ(
        state, db_sync_timeout_action_name(DB_SYNC_TIMEOUT_SKIP), "skip");
}

unsigned db_poll_policy_test_run_all(void) {
    const db_test_case_t cases[] = {
        {"progress_policy_registry", db_test_progress_policy_registry},
        {"progress_executor_completes", db_test_progress_executor_completes},
        {"progress_executor_honors_attempt_limit",
         db_test_progress_executor_honors_attempt_limit},
        {"deadline_and_retry_tracker", db_test_deadline_and_retry_tracker},
        {"sync_wait_result_labels", db_test_sync_wait_result_labels},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
