#ifndef DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H
#define DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../config/runtime_options.h"
#include "../core/db_alloc_policy.h"
#include "../core/db_core.h"
#include "../core/db_log.h"
#include "../core/db_metrics_policy.h"
#include "../core/db_numeric.h"
#include "../core/db_poll_policy.h"
#include "../core/db_sort.h"

typedef enum {
    DB_DISPLAY_FRAME_LOOP_CONTINUE = 0,
    DB_DISPLAY_FRAME_LOOP_STOP = 1,
    DB_DISPLAY_FRAME_LOOP_RETRY = 2,
} db_display_frame_loop_result_t;

typedef int (*db_display_frame_loop_should_continue_fn_t)(void *user_data);
typedef void (*db_display_frame_loop_pre_frame_fn_t)(void *user_data,
                                                     uint32_t frame_index);
typedef db_display_frame_loop_result_t (*db_display_frame_loop_frame_fn_t)(
    void *user_data, uint32_t frame_index, double elapsed_ms);

typedef struct {
    const char *backend;
    double fps_cap;
    uint32_t frame_limit;
    uint32_t initial_frame_index;
    void *user_data;
    db_display_frame_loop_should_continue_fn_t should_continue_fn;
    db_display_frame_loop_pre_frame_fn_t pre_frame_fn;
    db_display_frame_loop_frame_fn_t frame_fn;
} db_display_frame_loop_t;

typedef struct {
    uint64_t frames;
    double elapsed_ms;
    uint64_t retries;
    double frame_ema_ms;
    double jitter_ema_ms;
    double frame_p50_ms;
    double frame_p95_ms;
    double frame_p99_ms;
} db_display_frame_loop_run_result_t;

#define DB_DISPLAY_METRIC_EMA_KEEP 0.9
#define DB_DISPLAY_METRIC_EMA_NEW 0.1
#define DB_DISPLAY_PERCENTILE_MAX 100.0
#define DB_DISPLAY_PERCENTILE_P50 50.0
#define DB_DISPLAY_PERCENTILE_P95 95.0
#define DB_DISPLAY_PERCENTILE_P99 99.0

static inline int db_display_dual_metrics_enabled(void) {
    const char *const metrics_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_METRICS_MODE);
    return (metrics_mode != NULL) && (strcmp(metrics_mode, "dual") == 0);
}

static inline uint32_t db_display_frame_index_u32(const char *backend,
                                                  uint64_t frame_index) {
    return db_checked_u64_to_u32((backend != NULL) ? backend
                                                   : "display_frame_loop",
                                 "frame_index", frame_index);
}

static inline double db_display_percentile_sorted(const double *samples,
                                                  size_t count, double pct) {
    if ((samples == NULL) || (count == 0U)) {
        return 0.0;
    }
    if (pct <= 0.0) {
        return samples[0U];
    }
    if (pct >= DB_DISPLAY_PERCENTILE_MAX) {
        return samples[count - 1U];
    }
    const double rank = (pct / 100.0) * DB_TO_F64(count - 1U);
    const size_t index = (size_t)rank;
    const size_t next = DB_MIN(index + 1U, count - 1U);
    const double frac = rank - DB_TO_F64(index);
    return (samples[index] * (1.0 - frac)) + (samples[next] * frac);
}

static inline db_display_frame_loop_run_result_t
db_display_run_frame_loop(const db_display_frame_loop_t *loop) {
    db_display_frame_loop_run_result_t result = {
        .frames = 0U,
        .elapsed_ms = 0.0,
        .retries = 0U,
        .frame_ema_ms = 0.0,
        .jitter_ema_ms = 0.0,
        .frame_p50_ms = 0.0,
        .frame_p95_ms = 0.0,
        .frame_p99_ms = 0.0,
    };
    if ((loop == NULL) || (loop->frame_fn == NULL)) {
        return result;
    }

    const int dual_metrics = db_display_dual_metrics_enabled();
    size_t sample_count = 0U;
    size_t sample_capacity =
        dual_metrics ? DB_METRIC_SAMPLE_INITIAL_CAPACITY : 0U;
    if ((dual_metrics != 0) && (loop->frame_limit > sample_capacity)) {
        sample_capacity =
            DB_MIN(loop->frame_limit, DB_METRIC_SAMPLE_MAX_CAPACITY);
    }
    double *samples = NULL;
    if (sample_capacity > 0U) {
        samples = (double *)db_malloc_or_fail(
            (loop->backend != NULL) ? loop->backend : "display_frame_loop",
            "display_frame_time_samples", sample_capacity, sizeof(double));
    }

    const uint64_t bench_start_ns = db_now_ns_monotonic();
    db_retry_tracker_t retry_tracker = {0};
    while (!db_should_stop()) {
        if ((loop->frame_limit > 0U) && (result.frames >= loop->frame_limit)) {
            break;
        }
        if ((loop->should_continue_fn != NULL) &&
            (loop->should_continue_fn(loop->user_data) == 0)) {
            break;
        }

        const uint64_t frame_start_ns = db_now_ns_monotonic();
        const uint32_t frame_index = db_display_frame_index_u32(
            loop->backend, (uint64_t)loop->initial_frame_index + result.frames);
        if (loop->pre_frame_fn != NULL) {
            loop->pre_frame_fn(loop->user_data, frame_index);
        }
        const double elapsed_ms =
            DB_TO_F64(frame_start_ns - bench_start_ns) / DB_NS_PER_MS;
        const db_display_frame_loop_result_t frame_result =
            loop->frame_fn(loop->user_data, frame_index, elapsed_ms);
        if (frame_result == DB_DISPLAY_FRAME_LOOP_STOP) {
            break;
        }

        db_sleep_to_fps_cap(loop->backend, frame_start_ns, loop->fps_cap);
        const uint64_t frame_end_ns = db_now_ns_monotonic();
        const double frame_total_ms =
            DB_TO_F64(frame_end_ns - frame_start_ns) / DB_NS_PER_MS;
        if (result.frame_ema_ms <= 0.0) {
            result.frame_ema_ms = frame_total_ms;
            result.jitter_ema_ms = 0.0;
        } else {
            result.frame_ema_ms =
                (DB_DISPLAY_METRIC_EMA_KEEP * result.frame_ema_ms) +
                (DB_DISPLAY_METRIC_EMA_NEW * frame_total_ms);
            const double jitter = fabs(frame_total_ms - result.frame_ema_ms);
            result.jitter_ema_ms =
                (DB_DISPLAY_METRIC_EMA_KEEP * result.jitter_ema_ms) +
                (DB_DISPLAY_METRIC_EMA_NEW * jitter);
        }
        if (samples != NULL) {
            if ((sample_count >= sample_capacity) &&
                (sample_capacity < DB_METRIC_SAMPLE_MAX_CAPACITY)) {
                const size_t new_capacity = db_size_grow_capacity_3_2(
                    sample_capacity, sample_count + 1U,
                    DB_METRIC_SAMPLE_INITIAL_CAPACITY);
                const size_t bounded_capacity =
                    DB_MIN(new_capacity, (size_t)DB_METRIC_SAMPLE_MAX_CAPACITY);
                if (bounded_capacity <= sample_capacity) {
                    DB_RUNTIME_FAIL((loop->backend != NULL)
                                        ? loop->backend
                                        : "display_frame_loop",
                                    "display frame sample capacity overflow");
                }
                db_reserve_array_capacity_or_fail(
                    (void **)&samples, &sample_capacity, bounded_capacity,
                    DB_METRIC_SAMPLE_INITIAL_CAPACITY, sizeof(double),
                    (loop->backend != NULL) ? loop->backend
                                            : "display_frame_loop",
                    "display_frame_samples");
            }
            if (sample_count < sample_capacity) {
                samples[sample_count++] = frame_total_ms;
            }
        }
        if (frame_result != DB_DISPLAY_FRAME_LOOP_RETRY) {
            db_retry_tracker_reset(&retry_tracker);
            result.frames++;
        } else {
            result.retries++;
            const uint64_t retry_now_ns = db_now_ns_monotonic();
            if (db_retry_tracker_record(&retry_tracker, DB_PROGRESS_FRAME_RETRY,
                                        retry_now_ns) == 0) {
                db_sync_wait_result_t retry_result = db_sync_wait_result_make(
                    DB_SYNC_WAIT_TIMEOUT, retry_tracker.consecutive_retries,
                    retry_now_ns - retry_tracker.deadline.started_ns, 0U,
                    "no_frame_progress");
                retry_result.policy_id = DB_PROGRESS_FRAME_RETRY;
                db_progress_log_outcome(
                    (loop->backend != NULL) ? loop->backend
                                            : "display_frame_loop",
                    "frame_retry", DB_PROGRESS_FRAME_RETRY, &retry_result);
                break;
            }
        }
    }

    result.elapsed_ms =
        DB_TO_F64(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    if ((samples != NULL) && (sample_count > 0U)) {
        if (db_sort_f64_ascending(samples, sample_count) != DB_SORT_OK) {
            free(samples);
            const db_log_field_t fields[] = {
                DB_LOG_TOKEN("code", "sort_failed"),
                DB_LOG_TOKEN("operation", "frame_metric_percentiles"),
                DB_LOG_U64("sample_count", sample_count),
            };
            db_log_fail(
                (loop->backend != NULL) ? loop->backend : "display_frame_loop",
                "frame_metric_failure", fields, DB_LOG_FIELD_COUNT(fields));
        }
        result.frame_p50_ms = db_display_percentile_sorted(
            samples, sample_count, DB_DISPLAY_PERCENTILE_P50);
        result.frame_p95_ms = db_display_percentile_sorted(
            samples, sample_count, DB_DISPLAY_PERCENTILE_P95);
        result.frame_p99_ms = db_display_percentile_sorted(
            samples, sample_count, DB_DISPLAY_PERCENTILE_P99);
    }
    free(samples);
    return result;
}

#endif
