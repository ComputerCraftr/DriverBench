#include "core/db_metrics_policy.h"

#include "core/db_numeric.h"

#include <math.h>
#include <stddef.h>

#include "support/test_harness.h"

enum {
    DB_TEST_RING_CAPACITY = 4U,
};

static const double db_test_sample_10 = 10.0;
static const double db_test_sample_20 = 20.0;
static const double db_test_sample_30 = 30.0;
static const double db_test_sample_40 = 40.0;

static void db_test_ring_empty_and_partial(db_test_state_t *state) {
    double storage[DB_TEST_RING_CAPACITY] = {0.0};
    double snapshot[DB_TEST_RING_CAPACITY] = {0.0};
    db_f64_sample_ring_t ring = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_f64_sample_ring_init(&ring, storage, DB_TEST_RING_CAPACITY),
        DB_F64_SAMPLE_RING_OK);

    size_t count = 1U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_f64_sample_ring_snapshot(
                              &ring, snapshot, DB_TEST_RING_CAPACITY, &count),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_U64(state, count, 0U);

    DB_TEST_EXPECT_EQ_INT(state, db_f64_sample_ring_push(&ring, 1.0),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_INT(state, db_f64_sample_ring_push(&ring, 2.0),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_f64_sample_ring_snapshot(
                              &ring, snapshot, DB_TEST_RING_CAPACITY, &count),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_U64(state, count, 2U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, snapshot[0], 1.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, snapshot[1], 2.0);
}

static void db_test_ring_wrap_and_eviction(db_test_state_t *state) {
    double storage[DB_TEST_RING_CAPACITY] = {0.0};
    double snapshot[DB_TEST_RING_CAPACITY] = {0.0};
    db_f64_sample_ring_t ring = {0};
    (void)db_f64_sample_ring_init(&ring, storage, DB_TEST_RING_CAPACITY);
    for (size_t index = 1U; index <= DB_TEST_RING_CAPACITY + 1U; index++) {
        DB_TEST_EXPECT_EQ_INT(state,
                              db_f64_sample_ring_push(&ring, DB_TO_F64(index)),
                              DB_F64_SAMPLE_RING_OK);
    }
    size_t count = 0U;
    DB_TEST_EXPECT_EQ_INT(state,
                          db_f64_sample_ring_snapshot(
                              &ring, snapshot, DB_TEST_RING_CAPACITY, &count),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_U64(state, count, DB_TEST_RING_CAPACITY);
    DB_TEST_EXPECT_EQ_U64(state, ring.total_samples,
                          DB_TEST_RING_CAPACITY + 1U);
    for (size_t index = 0U; index < DB_TEST_RING_CAPACITY; index++) {
        DB_TEST_EXPECT_DOUBLE_EQUAL(state, snapshot[index],
                                    DB_TO_F64(index + 2U));
    }
}

static void db_test_recent_window_4097_eviction(db_test_state_t *state) {
    static double storage[DB_METRIC_RECENT_SAMPLE_CAPACITY];
    static double snapshot[DB_METRIC_RECENT_SAMPLE_CAPACITY];
    const size_t capacity = DB_METRIC_RECENT_SAMPLE_CAPACITY;
    db_f64_sample_ring_t ring = {0};
    (void)db_f64_sample_ring_init(&ring, storage, capacity);
    for (size_t index = 0U; index <= capacity; index++) {
        DB_TEST_EXPECT_EQ_INT(state,
                              db_f64_sample_ring_push(&ring, DB_TO_F64(index)),
                              DB_F64_SAMPLE_RING_OK);
    }
    size_t count = 0U;
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_f64_sample_ring_snapshot(&ring, snapshot,
                                    DB_METRIC_RECENT_SAMPLE_CAPACITY, &count),
        DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_U64(state, count, DB_METRIC_RECENT_SAMPLE_CAPACITY);
    DB_TEST_EXPECT_EQ_U64(state, ring.total_samples,
                          DB_METRIC_RECENT_SAMPLE_CAPACITY + 1U);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, snapshot[0], 1.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state,
                                snapshot[DB_METRIC_RECENT_SAMPLE_CAPACITY - 1U],
                                DB_TO_F64(DB_METRIC_RECENT_SAMPLE_CAPACITY));
    DB_TEST_EXPECT_TRUE(state, ring.values == storage);
}

static void db_test_ring_summary_and_reset(db_test_state_t *state) {
    double storage[DB_TEST_RING_CAPACITY] = {0.0};
    double scratch[DB_TEST_RING_CAPACITY] = {0.0};
    db_f64_sample_ring_t ring = {0};
    (void)db_f64_sample_ring_init(&ring, storage, DB_TEST_RING_CAPACITY);
    (void)db_f64_sample_ring_push(&ring, db_test_sample_40);
    (void)db_f64_sample_ring_push(&ring, db_test_sample_10);
    (void)db_f64_sample_ring_push(&ring, db_test_sample_30);
    (void)db_f64_sample_ring_push(&ring, db_test_sample_20);

    db_f64_sample_summary_t summary = {0};
    DB_TEST_EXPECT_EQ_INT(state,
                          db_f64_sample_ring_summarize(
                              &ring, scratch, DB_TEST_RING_CAPACITY, &summary),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, summary.p50, 25.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, summary.p95, 38.5);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, summary.p99, 39.7);
    DB_TEST_EXPECT_EQ_U64(state, summary.window_sample_count,
                          DB_TEST_RING_CAPACITY);
    DB_TEST_EXPECT_EQ_U64(state, summary.total_samples, DB_TEST_RING_CAPACITY);

    db_f64_sample_ring_reset(&ring);
    DB_TEST_EXPECT_EQ_U64(state, ring.count, 0U);
    DB_TEST_EXPECT_EQ_U64(state, ring.total_samples, 0U);
    DB_TEST_EXPECT_TRUE(state, ring.values == storage);
    DB_TEST_EXPECT_EQ_U64(state, ring.capacity, DB_TEST_RING_CAPACITY);
}

static void db_test_ring_rejects_invalid_values(db_test_state_t *state) {
    double storage[DB_TEST_RING_CAPACITY] = {0.0};
    db_f64_sample_ring_t ring = {0};
    (void)db_f64_sample_ring_init(&ring, storage, DB_TEST_RING_CAPACITY);
    DB_TEST_EXPECT_EQ_INT(state, db_f64_sample_ring_push(&ring, -1.0),
                          DB_F64_SAMPLE_RING_INVALID);
    DB_TEST_EXPECT_EQ_INT(state, db_f64_sample_ring_push(&ring, nan("")),
                          DB_F64_SAMPLE_RING_INVALID);
    DB_TEST_EXPECT_EQ_INT(state, db_f64_sample_ring_push(&ring, HUGE_VAL),
                          DB_F64_SAMPLE_RING_INVALID);
    DB_TEST_EXPECT_EQ_U64(state, ring.total_samples, 0U);
    DB_TEST_EXPECT_EQ_INT(
        state,
        db_f64_sample_ring_init(&ring, storage,
                                DB_METRIC_RECENT_SAMPLE_CAPACITY + 1U),
        DB_F64_SAMPLE_RING_INVALID);
}

static void
db_test_run_metrics_classification_and_shared_scratch(db_test_state_t *state) {
    db_run_metrics_t metrics = {0};
    DB_TEST_EXPECT_TRUE(state, db_run_metrics_init(&metrics, 1));
    DB_TEST_EXPECT_EQ_INT(state,
                          db_run_metrics_accept_frame(
                              &metrics,
                              &(const db_completed_frame_metrics_t){
                                  .frame_total_ms = 10.0,
                                  .renderer_critical_ms = 4.0,
                                  .classification = DB_RUN_METRIC_PRODUCTION,
                                  .renderer_critical_valid = 1,
                              }),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_INT(state,
                          db_run_metrics_accept_frame(
                              &metrics,
                              &(const db_completed_frame_metrics_t){
                                  .frame_total_ms = 1000.0,
                                  .renderer_critical_ms = 900.0,
                                  .classification = DB_RUN_METRIC_ROLLED_BACK,
                                  .renderer_critical_valid = 1,
                              }),
                          DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_U64(state, metrics.accepted_samples, 1U);
    DB_TEST_EXPECT_EQ_U64(state, metrics.rejected_samples, 1U);
    DB_TEST_EXPECT_EQ_U64(state, metrics.frame_total.count, 1U);
    DB_TEST_EXPECT_EQ_U64(state, metrics.renderer_critical.count, 1U);

    db_f64_sample_summary_t frame_summary = {0};
    db_f64_sample_summary_t renderer_summary = {0};
    DB_TEST_EXPECT_EQ_INT(
        state, db_run_metrics_summarize_frame(&metrics, &frame_summary),
        DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_EQ_INT(
        state, db_run_metrics_summarize_renderer(&metrics, &renderer_summary),
        DB_F64_SAMPLE_RING_OK);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, frame_summary.p50, 10.0);
    DB_TEST_EXPECT_DOUBLE_EQUAL(state, renderer_summary.p50, 4.0);
    DB_TEST_EXPECT_TRUE(state, metrics.scratch != NULL);
    db_run_metrics_shutdown(&metrics);
    DB_TEST_EXPECT_TRUE(state, metrics.storage == NULL);
}

static void
db_test_run_metrics_reinitialization_preserves_owner(db_test_state_t *state) {
    db_run_metrics_t metrics = {0};
    DB_TEST_EXPECT_TRUE(state, db_run_metrics_init(&metrics, 1));
    double *const storage = metrics.storage;
    DB_TEST_EXPECT_TRUE(state, storage != NULL);

    DB_TEST_EXPECT_TRUE(state, db_run_metrics_init(&metrics, 0) == 0);
    DB_TEST_EXPECT_TRUE(state, metrics.storage == storage);
    DB_TEST_EXPECT_EQ_INT(state, metrics.initialized, 1);

    db_run_metrics_shutdown(&metrics);
}

unsigned db_metrics_policy_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"metrics_ring_empty_partial", db_test_ring_empty_and_partial},
        {"metrics_ring_wrap_eviction", db_test_ring_wrap_and_eviction},
        {"metrics_ring_4097_eviction", db_test_recent_window_4097_eviction},
        {"metrics_ring_summary_reset", db_test_ring_summary_and_reset},
        {"metrics_ring_invalid", db_test_ring_rejects_invalid_values},
        {"run_metrics_classification_shared_scratch",
         db_test_run_metrics_classification_and_shared_scratch},
        {"run_metrics_reinitialization_preserves_owner",
         db_test_run_metrics_reinitialization_preserves_owner},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
