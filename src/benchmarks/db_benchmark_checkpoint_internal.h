#ifndef DRIVERBENCH_BENCHMARK_CHECKPOINT_INTERNAL_H
#define DRIVERBENCH_BENCHMARK_CHECKPOINT_INTERNAL_H

#include "core/db_frame_plan.h"
#include "core/db_geometry_builder.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    db_pixel_surface_t surface;
    double *canonical_rgb;
    double *overlay_rgb;
    uint32_t *overlay_generation;
    uint32_t *overlay_dirty_indices;
    size_t pixel_count;
    size_t overlay_dirty_count;
    uint32_t active_overlay_generation;
    size_t allocation_size_bytes;
    uint64_t generation;
    uint64_t content_revision;
    uint32_t committed_frame_index;
    int committed_frame_valid;
    int enabled;
} db_benchmark_checkpoint_t;

void db_benchmark_checkpoint_init(db_benchmark_checkpoint_t *checkpoint,
                                  uint32_t width, uint32_t height,
                                  db_pixel_format_t format,
                                  const double seed_rgb[3]);
void db_benchmark_checkpoint_shutdown(db_benchmark_checkpoint_t *checkpoint);
void db_benchmark_checkpoint_publish_seed(
    const db_benchmark_checkpoint_t *checkpoint, db_frame_plan_t *plan);
void db_benchmark_checkpoint_overlay_begin(
    db_benchmark_checkpoint_t *checkpoint);
void db_benchmark_checkpoint_overlay_write(
    db_benchmark_checkpoint_t *checkpoint, const db_colored_f64_block_t *block);
void db_benchmark_checkpoint_overlay_publish(
    db_benchmark_checkpoint_t *checkpoint, db_geometry_builder_t *builder);
void db_benchmark_checkpoint_read_with_overlay(
    const db_benchmark_checkpoint_t *checkpoint, uint32_t row, uint32_t col,
    double out_rgb[3]);
void db_benchmark_checkpoint_commit(db_benchmark_checkpoint_t *checkpoint,
                                    const db_frame_plan_t *plan,
                                    const db_render_result_t *result);

#endif
