#ifndef DRIVERBENCH_CORE_GEOMETRY_H
#define DRIVERBENCH_CORE_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#define DB_CANONICAL_GEOMETRY_BLOCK_CAPACITY 8192U

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
} db_grid_block_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
} db_damage_block_t;

typedef struct {
    uint32_t row_start;
    uint32_t row_count;
    uint32_t col_start;
    uint32_t col_count;
    double rgb[3];
} db_colored_f64_block_t;

typedef struct {
    const db_grid_block_t *blocks;
    size_t count;
} db_grid_block_view_t;

typedef struct {
    const db_colored_f64_block_t *blocks;
    size_t count;
} db_colored_f64_block_view_t;

typedef struct {
    const db_damage_block_t *blocks;
    size_t count;
} db_pixel_block_view_t;

typedef enum {
    DB_GEOMETRY_EXECUTION_NO_OP = 0,
    DB_GEOMETRY_EXECUTION_INCREMENTAL = 1,
    DB_GEOMETRY_EXECUTION_REBUILD = 2,
    DB_GEOMETRY_EXECUTION_FULL_REDRAW = 3,
} db_geometry_execution_operation_t;

typedef struct {
    db_grid_block_view_t logical_damage;
    db_colored_f64_block_view_t current_blocks;
    db_pixel_block_view_t repair_union;
    db_geometry_execution_operation_t operation;
    uint32_t replay_depth;
    int overflowed;
} db_geometry_execution_t;

typedef struct {
    db_colored_f64_block_t *blocks;
    size_t *frame_counts;
    size_t blocks_per_frame;
    uint32_t frame_capacity;
    uint32_t frame_count;
    uint32_t next_frame;
} db_geometry_history_t;

typedef enum {
    DB_GEOMETRY_F64_STATUS_OK = 0,
    DB_GEOMETRY_F64_STATUS_OVERFLOW = 1,
    DB_GEOMETRY_F64_STATUS_INVALID = 2,
} db_geometry_f64_status_t;

db_geometry_f64_status_t db_colored_f64_blocks_compact(
    db_colored_f64_block_view_t input, db_colored_f64_block_t *scratch,
    size_t scratch_capacity, db_colored_f64_block_t *output,
    size_t output_capacity, size_t *output_count);
db_geometry_f64_status_t db_colored_f64_blocks_pixel_union(
    db_colored_f64_block_view_t input, uint32_t grid_cols, uint32_t grid_rows,
    uint32_t pixel_width, uint32_t pixel_height, db_damage_block_t *output,
    size_t output_capacity, size_t *output_count);
size_t db_damage_blocks_from_grid_blocks_or_full(const db_grid_block_t *blocks,
                                                 size_t block_count,
                                                 uint32_t max_rows,
                                                 uint32_t full_width_cols,
                                                 db_damage_block_t *output,
                                                 size_t output_capacity);

void db_geometry_history_init(db_geometry_history_t *history,
                              db_colored_f64_block_t *blocks,
                              size_t *frame_counts, size_t blocks_per_frame,
                              uint32_t frame_capacity);
void db_geometry_history_reset(db_geometry_history_t *history);
db_geometry_f64_status_t db_geometry_history_append(
    db_geometry_history_t *history, db_colored_f64_block_view_t current,
    db_colored_f64_block_t *scratch, size_t scratch_capacity);
db_geometry_f64_status_t db_geometry_history_assemble(
    const db_geometry_history_t *history, uint32_t previous_frame_count,
    db_colored_f64_block_view_t current, db_colored_f64_block_t *output,
    size_t output_capacity, size_t *historical_count, size_t *output_count);

#endif
