#ifndef DRIVERBENCH_CORE_GEOMETRY_BUILDER_H
#define DRIVERBENCH_CORE_GEOMETRY_BUILDER_H

#include "db_geometry.h"

typedef enum {
    DB_GEOMETRY_BUILDER_OK = 0,
    DB_GEOMETRY_BUILDER_OVERFLOW = 1,
    DB_GEOMETRY_BUILDER_INVALID = 2,
} db_geometry_builder_status_t;

typedef struct {
    db_grid_block_t *logical_blocks;
    size_t logical_capacity;
    size_t logical_count;
    db_colored_f64_block_t *colored_blocks;
    size_t colored_capacity;
    size_t colored_count;
    db_geometry_builder_status_t status;
} db_geometry_builder_t;

void db_geometry_builder_reset(db_geometry_builder_t *builder);
int db_geometry_builder_add_damage(db_geometry_builder_t *builder,
                                   const db_grid_block_t *block);
int db_geometry_builder_add_span(db_geometry_builder_t *builder, uint32_t row,
                                 uint32_t col_start, uint32_t col_end,
                                 const double rgb[3]);
int db_geometry_builder_add_block(db_geometry_builder_t *builder,
                                  const db_colored_f64_block_t *block);

#endif
