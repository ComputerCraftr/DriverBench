#ifndef DRIVERBENCH_RENDER_IR_SNAPSHOT_H
#define DRIVERBENCH_RENDER_IR_SNAPSHOT_H

#include "db_render_ir.h"

#include <stddef.h>

typedef struct {
    db_render_ir_store_t store;
} db_render_ir_snapshot_t;

typedef db_render_ir_snapshot_t db_render_ir_owned_store_t;

typedef enum {
    DB_RENDER_IR_CLONE_OK = 0,
    DB_RENDER_IR_CLONE_INVALID,
    DB_RENDER_IR_CLONE_CAPACITY,
    DB_RENDER_IR_CLONE_NOT_OPTIMIZED,
    DB_RENDER_IR_CLONE_NOT_REPLAYABLE,
} db_render_ir_clone_status_t;

int db_render_ir_snapshot_init(db_render_ir_snapshot_t *snapshot,
                               size_t command_capacity, size_t fill_capacity,
                               size_t resource_capacity, size_t region_capacity,
                               size_t band_capacity, size_t span_capacity);
void db_render_ir_snapshot_shutdown(db_render_ir_snapshot_t *snapshot);
db_render_ir_status_t
db_render_ir_snapshot_capture(db_render_ir_snapshot_t *snapshot,
                              const db_render_ir_view_t *source);
db_render_ir_view_t
db_render_ir_snapshot_view(const db_render_ir_snapshot_t *snapshot);
int db_render_ir_owned_store_required_bytes(
    size_t command_capacity, size_t fill_capacity, size_t resource_capacity,
    size_t region_capacity, size_t band_capacity, size_t span_capacity,
    size_t *required_bytes);
db_render_ir_clone_status_t
db_render_ir_clone_replayable(const db_render_ir_view_t *source,
                              const db_render_ir_metadata_t *metadata,
                              db_render_ir_owned_store_t *destination);
const char *db_render_ir_clone_status_name(db_render_ir_clone_status_t status);

#endif
