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
    const db_grid_block_t *blocks;
    size_t count;
} db_grid_block_view_t;

typedef struct {
    const db_damage_block_t *blocks;
    size_t count;
} db_pixel_block_view_t;

#endif
