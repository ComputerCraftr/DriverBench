#ifndef DRIVERBENCH_BENCHMARK_CHECKPOINT_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_CHECKPOINT_INTERNAL_H

#include "benchmarks/db_benchmark_emitters.h"
#include "core/db_frame_plan.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t row;
    uint32_t col_start;
    uint32_t col_end;
} db_benchmark_checkpoint_span_t;

typedef struct {
    db_pixel_surface_t surface;
    void *overlay_pixels;
    uint32_t *overlay_generation;
    db_benchmark_checkpoint_span_t *overlay_dirty_spans;
    size_t pixel_count;
    size_t overlay_dirty_span_count;
    size_t overlay_dirty_span_capacity;
    uint64_t dirty_span_search_comparisons;
    uint32_t active_overlay_generation;
    size_t surface_size_bytes;
    size_t allocation_size_bytes;
    uint64_t generation;
    uint64_t content_revision;
    uint32_t committed_frame_index;
    int committed_frame_valid;
    int overlay_valid;
    int enabled;
} db_benchmark_checkpoint_t;

typedef enum {
    DB_BENCHMARK_CHECKPOINT_OK = 0,
    DB_BENCHMARK_CHECKPOINT_CAPACITY,
    DB_BENCHMARK_CHECKPOINT_MEMORY_BUDGET,
    DB_BENCHMARK_CHECKPOINT_ALLOCATION_FAILED,
    DB_BENCHMARK_CHECKPOINT_UNAVAILABLE,
} db_benchmark_checkpoint_status_t;

#define DB_BENCHMARK_CHECKPOINT_MAX_BYTES                                      \
    ((size_t)16U * (size_t)1024U * (size_t)1024U)

db_benchmark_checkpoint_status_t db_benchmark_checkpoint_init(
    db_benchmark_checkpoint_t *checkpoint, uint32_t width, uint32_t height,
    db_pixel_format_t format, const double seed_rgb[3]);
db_benchmark_checkpoint_status_t
db_benchmark_checkpoint_preflight(uint32_t width, uint32_t height,
                                  db_pixel_format_t format,
                                  size_t *out_allocation_size_bytes);
const char *
db_benchmark_checkpoint_status_name(db_benchmark_checkpoint_status_t status);
void db_benchmark_checkpoint_shutdown(db_benchmark_checkpoint_t *checkpoint);
void db_benchmark_checkpoint_overlay_begin(
    db_benchmark_checkpoint_t *checkpoint);
void db_benchmark_checkpoint_overlay_write(
    db_benchmark_checkpoint_t *checkpoint, uint32_t row_start,
    uint32_t row_count, uint32_t col_start, uint32_t col_count,
    const double rgb[3]);
int db_benchmark_checkpoint_overlay_publish(
    db_benchmark_checkpoint_t *checkpoint, db_benchmark_ir_emitter_t *emitter);
void db_benchmark_checkpoint_read_with_overlay(
    const db_benchmark_checkpoint_t *checkpoint, uint32_t row, uint32_t col,
    double out_rgb[3]);
int db_benchmark_checkpoint_commit(db_benchmark_checkpoint_t *checkpoint,
                                   const db_frame_plan_t *plan,
                                   const db_render_result_t *result);

#endif
