#ifndef DRIVERBENCH_CORE_BENCHMARK_EMITTERS_H
#define DRIVERBENCH_CORE_BENCHMARK_EMITTERS_H

#include "benchmarks/db_benchmark_types_internal.h"
#include "core/db_geometry.h"
#include "core/db_render_ir.h"

typedef enum {
    DB_BENCHMARK_IR_EMITTER_OK = 0,
    DB_BENCHMARK_IR_EMITTER_CAPACITY,
    DB_BENCHMARK_IR_EMITTER_INVALID,
} db_benchmark_ir_emitter_status_t;

typedef struct {
    db_grid_block_t *logical_blocks;
    size_t logical_capacity;
    size_t logical_count;
    db_render_ir_fill_t *fills;
    size_t fill_capacity;
    size_t fill_count;
    db_benchmark_ir_emitter_status_t status;
} db_benchmark_ir_emitter_t;

void db_benchmark_ir_emitter_reset(db_benchmark_ir_emitter_t *emitter);
int db_benchmark_ir_emitter_add_damage(db_benchmark_ir_emitter_t *emitter,
                                       const db_grid_block_t *block);
int db_benchmark_ir_emitter_add_rect(db_benchmark_ir_emitter_t *emitter,
                                     uint32_t row_start, uint32_t row_count,
                                     uint32_t col_start, uint32_t col_count,
                                     const double rgb[3]);
int db_benchmark_ir_emitter_add_span(db_benchmark_ir_emitter_t *emitter,
                                     uint32_t row, uint32_t col_start,
                                     uint32_t col_end, const double rgb[3]);

db_benchmark_ir_emitter_status_t
db_benchmark_emit_bands(uint32_t cols, uint32_t rows, uint32_t band_count,
                        uint32_t frame_index,
                        db_benchmark_ir_emitter_t *emitter);
db_benchmark_ir_emitter_status_t
db_benchmark_emit_gradient(uint32_t cols, uint32_t rows,
                           const db_gradient_damage_plan_t *plan,
                           int full_frame, db_benchmark_ir_emitter_t *emitter);
db_benchmark_ir_emitter_status_t
db_benchmark_emit_grid_state_damage(uint32_t cols, uint32_t rows,
                                    db_grid_block_view_t damage,
                                    const double *tile_rgb, size_t tile_count,
                                    db_benchmark_ir_emitter_t *emitter);

#endif
