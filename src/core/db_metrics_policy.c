#include "db_metrics_policy.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "db_core.h"
#include "db_numeric.h"
#include "db_sort.h"

#define DB_PERCENTILE_MAX 100.0
#define DB_PERCENTILE_P50 50.0
#define DB_PERCENTILE_P95 95.0
#define DB_PERCENTILE_P99 99.0
#define DB_METRIC_EMA_KEEP 0.9
#define DB_METRIC_EMA_NEW 0.1

enum { DB_RUN_METRIC_STORAGE_ARRAY_COUNT = 3U };

static double db_f64_percentile_sorted(const double *values, size_t count,
                                       double percentile) {
    if ((values == NULL) || (count == 0U)) {
        return 0.0;
    }
    if (percentile <= 0.0) {
        return values[0U];
    }
    if (percentile >= DB_PERCENTILE_MAX) {
        return values[count - 1U];
    }
    const double rank =
        (percentile / DB_PERCENTILE_MAX) * DB_TO_F64(count - 1U);
    const size_t index = (size_t)rank;
    const size_t next = (index + 1U < count) ? index + 1U : count - 1U;
    const double fraction = rank - DB_TO_F64(index);
    return (values[index] * (1.0 - fraction)) + (values[next] * fraction);
}

db_f64_sample_ring_status_t db_f64_sample_ring_init(
    db_f64_sample_ring_t *ring,
    // Retained for later writes.
    double *storage, // NOLINT(readability-non-const-parameter)
    size_t capacity) {
    if ((ring == NULL) || (storage == NULL) || (capacity == 0U) ||
        (capacity > DB_METRIC_RECENT_SAMPLE_CAPACITY)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    *ring = (db_f64_sample_ring_t){
        .values = storage,
        .capacity = capacity,
    };
    return DB_F64_SAMPLE_RING_OK;
}

void db_f64_sample_ring_reset(db_f64_sample_ring_t *ring) {
    if (ring == NULL) {
        return;
    }
    ring->count = 0U;
    ring->next_index = 0U;
    ring->total_samples = 0U;
}

db_f64_sample_ring_status_t db_f64_sample_ring_push(db_f64_sample_ring_t *ring,
                                                    double value) {
    if ((ring == NULL) || (ring->values == NULL) || (ring->capacity == 0U) ||
        !isfinite(value) || (value < 0.0)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    ring->values[ring->next_index] = value;
    ring->next_index++;
    if (ring->next_index == ring->capacity) {
        ring->next_index = 0U;
    }
    if (ring->count < ring->capacity) {
        ring->count++;
    }
    ring->total_samples = db_u64_saturating_add(ring->total_samples, 1U);
    return DB_F64_SAMPLE_RING_OK;
}

db_f64_sample_ring_status_t
db_f64_sample_ring_snapshot(const db_f64_sample_ring_t *ring, double *output,
                            size_t output_capacity, size_t *output_count) {
    if ((ring == NULL) || (ring->values == NULL) || (output_count == NULL) ||
        ((ring->count > 0U) && (output == NULL))) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    if (output_capacity < ring->count) {
        return DB_F64_SAMPLE_RING_CAPACITY;
    }
    const size_t oldest =
        (ring->count == ring->capacity) ? ring->next_index : 0U;
    for (size_t index = 0U; index < ring->count; index++) {
        size_t source = oldest + index;
        if (source >= ring->capacity) {
            source -= ring->capacity;
        }
        output[index] = ring->values[source];
    }
    *output_count = ring->count;
    return DB_F64_SAMPLE_RING_OK;
}

db_f64_sample_ring_status_t
db_f64_sample_ring_summarize(const db_f64_sample_ring_t *ring, double *scratch,
                             size_t scratch_capacity,
                             db_f64_sample_summary_t *summary) {
    if ((ring == NULL) || (summary == NULL)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    *summary = (db_f64_sample_summary_t){
        .window_sample_count = ring->count,
        .total_samples = ring->total_samples,
    };
    if (ring->count == 0U) {
        return DB_F64_SAMPLE_RING_OK;
    }
    size_t sample_count = 0U;
    const db_f64_sample_ring_status_t snapshot_status =
        db_f64_sample_ring_snapshot(ring, scratch, scratch_capacity,
                                    &sample_count);
    if (snapshot_status != DB_F64_SAMPLE_RING_OK) {
        return snapshot_status;
    }
    if (db_sort_f64_ascending(scratch, sample_count) != DB_SORT_OK) {
        return DB_F64_SAMPLE_RING_SORT_FAILED;
    }
    summary->p50 =
        db_f64_percentile_sorted(scratch, sample_count, DB_PERCENTILE_P50);
    summary->p95 =
        db_f64_percentile_sorted(scratch, sample_count, DB_PERCENTILE_P95);
    summary->p99 =
        db_f64_percentile_sorted(scratch, sample_count, DB_PERCENTILE_P99);
    return DB_F64_SAMPLE_RING_OK;
}

int db_run_metrics_init(db_run_metrics_t *metrics, int recent_enabled) {
    if ((metrics == NULL) || (metrics->initialized != 0)) {
        return 0;
    }
    *metrics = (db_run_metrics_t){.initialized = 1};
    if (recent_enabled == 0) {
        return 1;
    }
    size_t value_count = 0U;
    if (db_try_mul_size(DB_RUN_METRIC_STORAGE_ARRAY_COUNT,
                        DB_METRIC_RECENT_SAMPLE_CAPACITY, &value_count) == 0) {
        return 0;
    }
    double *const storage = calloc(value_count, sizeof(*storage));
    if (storage == NULL) {
        return 0;
    }
    *metrics = (db_run_metrics_t){
        .storage = storage,
        .scratch = storage + (value_count - DB_METRIC_RECENT_SAMPLE_CAPACITY),
        .initialized = 1,
    };
    if ((db_f64_sample_ring_init(&metrics->frame_total, storage,
                                 DB_METRIC_RECENT_SAMPLE_CAPACITY) !=
         DB_F64_SAMPLE_RING_OK) ||
        (db_f64_sample_ring_init(&metrics->renderer_critical,
                                 storage + DB_METRIC_RECENT_SAMPLE_CAPACITY,
                                 DB_METRIC_RECENT_SAMPLE_CAPACITY) !=
         DB_F64_SAMPLE_RING_OK)) {
        db_run_metrics_shutdown(metrics);
        return 0;
    }
    return 1;
}

void db_run_metrics_shutdown(db_run_metrics_t *metrics) {
    if (metrics == NULL) {
        return;
    }
    free(metrics->storage);
    *metrics = (db_run_metrics_t){0};
}

db_f64_sample_ring_status_t
db_run_metrics_accept_frame(db_run_metrics_t *metrics,
                            const db_completed_frame_metrics_t *frame) {
    if ((metrics == NULL) || (metrics->initialized == 0) || (frame == NULL)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    if (frame->classification != DB_RUN_METRIC_PRODUCTION) {
        metrics->rejected_samples =
            db_u64_saturating_add(metrics->rejected_samples, 1U);
        return DB_F64_SAMPLE_RING_OK;
    }
    if (metrics->storage != NULL) {
        const db_f64_sample_ring_status_t frame_status =
            db_f64_sample_ring_push(&metrics->frame_total,
                                    frame->frame_total_ms);
        if (frame_status != DB_F64_SAMPLE_RING_OK) {
            return frame_status;
        }
    }
    if (metrics->frame_ema_ms <= 0.0) {
        metrics->frame_ema_ms = frame->frame_total_ms;
        metrics->jitter_ema_ms = 0.0;
    } else {
        metrics->frame_ema_ms = (DB_METRIC_EMA_KEEP * metrics->frame_ema_ms) +
                                (DB_METRIC_EMA_NEW * frame->frame_total_ms);
        const double jitter =
            fabs(frame->frame_total_ms - metrics->frame_ema_ms);
        metrics->jitter_ema_ms = (DB_METRIC_EMA_KEEP * metrics->jitter_ema_ms) +
                                 (DB_METRIC_EMA_NEW * jitter);
    }
    if ((metrics->storage != NULL) && (frame->renderer_critical_valid != 0)) {
        const db_f64_sample_ring_status_t renderer_status =
            db_f64_sample_ring_push(&metrics->renderer_critical,
                                    frame->renderer_critical_ms);
        if (renderer_status != DB_F64_SAMPLE_RING_OK) {
            return renderer_status;
        }
    }
    metrics->accepted_samples =
        db_u64_saturating_add(metrics->accepted_samples, 1U);
    return DB_F64_SAMPLE_RING_OK;
}

db_f64_sample_ring_status_t
db_run_metrics_summarize_frame(const db_run_metrics_t *metrics,
                               db_f64_sample_summary_t *summary) {
    if ((metrics == NULL) || (metrics->initialized == 0) || (summary == NULL)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    if (metrics->storage == NULL) {
        *summary = (db_f64_sample_summary_t){0};
        return DB_F64_SAMPLE_RING_OK;
    }
    return db_f64_sample_ring_summarize(&metrics->frame_total, metrics->scratch,
                                        DB_METRIC_RECENT_SAMPLE_CAPACITY,
                                        summary);
}

db_f64_sample_ring_status_t
db_run_metrics_summarize_renderer(const db_run_metrics_t *metrics,
                                  db_f64_sample_summary_t *summary) {
    if ((metrics == NULL) || (metrics->initialized == 0) || (summary == NULL)) {
        return DB_F64_SAMPLE_RING_INVALID;
    }
    if (metrics->storage == NULL) {
        *summary = (db_f64_sample_summary_t){0};
        return DB_F64_SAMPLE_RING_OK;
    }
    return db_f64_sample_ring_summarize(
        &metrics->renderer_critical, metrics->scratch,
        DB_METRIC_RECENT_SAMPLE_CAPACITY, summary);
}
