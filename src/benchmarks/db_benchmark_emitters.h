#ifndef DRIVERBENCH_CORE_BENCHMARK_EMITTERS_H
#define DRIVERBENCH_CORE_BENCHMARK_EMITTERS_H

#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_geometry.h"
#include "core/db_geometry_builder.h"

typedef db_geometry_builder_status_t db_block_emitter_status_t;
typedef db_geometry_builder_t db_block_emitter_sink_t;
#define DB_BLOCK_EMITTER_STATUS_OK DB_GEOMETRY_BUILDER_OK
#define DB_BLOCK_EMITTER_STATUS_OVERFLOW DB_GEOMETRY_BUILDER_OVERFLOW
#define DB_BLOCK_EMITTER_STATUS_INVALID DB_GEOMETRY_BUILDER_INVALID

void db_block_emitter_sink_reset(db_block_emitter_sink_t *sink);
db_block_emitter_status_t
db_benchmark_emit_bands(uint32_t cols, uint32_t rows, uint32_t band_count,
                        uint32_t frame_index, db_block_emitter_sink_t *sink);
db_block_emitter_status_t
db_benchmark_emit_gradient(uint32_t cols, uint32_t rows,
                           const db_gradient_damage_plan_t *plan,
                           int full_frame, db_block_emitter_sink_t *sink);
db_block_emitter_status_t db_benchmark_emit_grid_state_damage(
    uint32_t cols, uint32_t rows, db_grid_block_view_t damage,
    const double *tile_rgb, size_t tile_count, db_block_emitter_sink_t *sink);

#endif
