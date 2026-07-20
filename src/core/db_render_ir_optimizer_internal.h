#ifndef DRIVERBENCH_RENDER_IR_OPTIMIZER_INTERNAL_H
#define DRIVERBENCH_RENDER_IR_OPTIMIZER_INTERNAL_H

#include "db_render_ir.h"

#include <stddef.h>

db_render_ir_status_t
db_render_ir_sort_and_merge_fills(db_render_ir_fill_t *fills,
                                  db_render_ir_fill_t *scratch, size_t *count,
                                  db_render_ir_optimizer_stats_t *stats);

db_render_ir_status_t db_render_ir_eliminate_overwrites(
    const db_render_ir_fill_t *input, size_t input_count,
    db_render_ir_optimizer_workspace_t workspace, size_t region_span_capacity,
    db_render_ir_fill_t *output, size_t *output_count);
db_render_ir_status_t db_render_ir_build_fill_region_bounded(
    const db_render_ir_fill_t *fills, size_t fill_count,
    db_render_ir_optimizer_workspace_t workspace, size_t region_span_capacity,
    db_render_ir_store_t *destination,
    db_render_ir_region_id_t *destination_region);

#endif
