#ifndef DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H
#define DRIVERBENCH_DISPLAY_FRAME_LOOP_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../config/runtime_options.h"
#include "../core/db_alloc_policy.h"
#include "../core/db_core.h"
#include "../core/db_numeric.h"

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

#define DB_DISPLAY_FRAME_SAMPLE_INIT_CAPACITY 1024U
#define DB_DISPLAY_METRIC_EMA_KEEP 0.9
#define DB_DISPLAY_METRIC_EMA_NEW 0.1
#define DB_DISPLAY_PERCENTILE_MAX 100.0
#define DB_DISPLAY_PERCENTILE_P50 50.0
#define DB_DISPLAY_PERCENTILE_P95 95.0
#define DB_DISPLAY_PERCENTILE_P99 99.0

static inline int db_display_dual_metrics_enabled(void) {
    const char *const metrics_mode =
        db_runtime_option_get(DB_RUNTIME_OPT_METRICS_MODE);
    return ((metrics_mode != NULL) && (strcmp(metrics_mode, "dual") == 0)) ? 1
                                                                           : 0;
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
    const double rank = ((pct / 100.0) * (double)(count - 1U));
    const size_t index = (size_t)rank;
    const size_t next = (index + 1U < count) ? (index + 1U) : index;
    const double frac = rank - (double)index;
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
        dual_metrics ? DB_DISPLAY_FRAME_SAMPLE_INIT_CAPACITY : 0U;
    if ((dual_metrics != 0) && (loop->frame_limit > sample_capacity)) {
        sample_capacity = loop->frame_limit;
    }
    double *samples = NULL;
    if (sample_capacity > 0U) {
        samples = (double *)db_alloc_array_or_fail(
            (loop->backend != NULL) ? loop->backend : "display_frame_loop",
            "display_frame_time_samples", sample_capacity, sizeof(double));
    }

    const uint64_t bench_start_ns = db_now_ns_monotonic();
    while (!db_should_stop()) {
        if ((loop->frame_limit > 0U) && (result.frames >= loop->frame_limit)) {
            break;
        }
        if ((loop->should_continue_fn != NULL) &&
            (loop->should_continue_fn(loop->user_data) == 0)) {
            break;
        }

        const uint64_t frame_start_ns = db_now_ns_monotonic();
        if (loop->pre_frame_fn != NULL) {
            loop->pre_frame_fn(loop->user_data, (uint32_t)result.frames);
        }
        const double elapsed_ms =
            (double)(frame_start_ns - bench_start_ns) / DB_NS_PER_MS;
        const db_display_frame_loop_result_t frame_result = loop->frame_fn(
            loop->user_data, (uint32_t)result.frames, elapsed_ms);
        if (frame_result == DB_DISPLAY_FRAME_LOOP_STOP) {
            break;
        }

        db_sleep_to_fps_cap(loop->backend, frame_start_ns, loop->fps_cap);
        const uint64_t frame_end_ns = db_now_ns_monotonic();
        const double frame_total_ms =
            (double)(frame_end_ns - frame_start_ns) / DB_NS_PER_MS;
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
            if (sample_count >= sample_capacity) {
                const size_t new_capacity = db_size_grow_capacity_3_2(
                    sample_capacity, sample_count + 1U,
                    DB_DISPLAY_FRAME_SAMPLE_INIT_CAPACITY);
                if (new_capacity <= sample_capacity) {
                    db_failf((loop->backend != NULL) ? loop->backend
                                                     : "display_frame_loop",
                             "display frame sample capacity overflow");
                }
                db_reserve_array_capacity_or_fail(
                    (void **)&samples, &sample_capacity, new_capacity,
                    DB_DISPLAY_FRAME_SAMPLE_INIT_CAPACITY, sizeof(double),
                    sample_count,
                    (loop->backend != NULL) ? loop->backend
                                            : "display_frame_loop",
                    "display_frame_samples");
            }
            samples[sample_count++] = frame_total_ms;
        }
        if (frame_result != DB_DISPLAY_FRAME_LOOP_RETRY) {
            result.frames++;
        } else {
            result.retries++;
        }
    }

    result.elapsed_ms =
        (double)(db_now_ns_monotonic() - bench_start_ns) / DB_NS_PER_MS;
    if ((samples != NULL) && (sample_count > 0U)) {
        qsort(samples, sample_count, sizeof(double), db_qsort_compare_f64);
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
