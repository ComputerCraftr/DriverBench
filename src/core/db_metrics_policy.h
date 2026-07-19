#ifndef DRIVERBENCH_CORE_DB_METRICS_POLICY_H
#define DRIVERBENCH_CORE_DB_METRICS_POLICY_H

#include <stddef.h>
#include <stdint.h>

enum {
    DB_METRIC_RECENT_SAMPLE_CAPACITY = 4096U,
};

typedef enum {
    DB_F64_SAMPLE_RING_OK = 0,
    DB_F64_SAMPLE_RING_INVALID,
    DB_F64_SAMPLE_RING_CAPACITY,
    DB_F64_SAMPLE_RING_SORT_FAILED,
} db_f64_sample_ring_status_t;

typedef struct {
    double *values;
    size_t capacity;
    size_t count;
    size_t next_index;
    uint64_t total_samples;
} db_f64_sample_ring_t;

typedef struct {
    size_t window_sample_count;
    uint64_t total_samples;
    double p50;
    double p95;
    double p99;
} db_f64_sample_summary_t;

typedef enum {
    DB_RUN_METRIC_PRODUCTION = 0,
    DB_RUN_METRIC_WARMUP,
    DB_RUN_METRIC_QUALIFICATION,
    DB_RUN_METRIC_FAILED,
    DB_RUN_METRIC_ROLLED_BACK,
} db_run_metric_class_t;

typedef struct {
    double frame_total_ms;
    double renderer_critical_ms;
    db_run_metric_class_t classification;
    int renderer_critical_valid;
} db_completed_frame_metrics_t;

typedef struct {
    db_f64_sample_ring_t frame_total;
    db_f64_sample_ring_t renderer_critical;
    double *storage;
    double *scratch;
    double frame_ema_ms;
    double jitter_ema_ms;
    uint64_t accepted_samples;
    uint64_t rejected_samples;
    int initialized;
} db_run_metrics_t;

db_f64_sample_ring_status_t db_f64_sample_ring_init(db_f64_sample_ring_t *ring,
                                                    double *storage,
                                                    size_t capacity);
void db_f64_sample_ring_reset(db_f64_sample_ring_t *ring);
db_f64_sample_ring_status_t db_f64_sample_ring_push(db_f64_sample_ring_t *ring,
                                                    double value);
db_f64_sample_ring_status_t
db_f64_sample_ring_snapshot(const db_f64_sample_ring_t *ring, double *output,
                            size_t output_capacity, size_t *output_count);
db_f64_sample_ring_status_t
db_f64_sample_ring_summarize(const db_f64_sample_ring_t *ring, double *scratch,
                             size_t scratch_capacity,
                             db_f64_sample_summary_t *summary);
int db_run_metrics_init(db_run_metrics_t *metrics, int recent_enabled);
void db_run_metrics_shutdown(db_run_metrics_t *metrics);
db_f64_sample_ring_status_t
db_run_metrics_accept_frame(db_run_metrics_t *metrics,
                            const db_completed_frame_metrics_t *frame);
db_f64_sample_ring_status_t
db_run_metrics_summarize_frame(const db_run_metrics_t *metrics,
                               db_f64_sample_summary_t *summary);
db_f64_sample_ring_status_t
db_run_metrics_summarize_renderer(const db_run_metrics_t *metrics,
                                  db_f64_sample_summary_t *summary);

#endif
