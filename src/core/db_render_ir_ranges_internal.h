#ifndef DB_RENDER_IR_RANGES_INTERNAL_H
#define DB_RENDER_IR_RANGES_INTERNAL_H

#include "db_render_ir.h"

// The caller must validate the complete view once before using this hot path.
int db_render_ir_commands_batch_compatible_validated(
    const db_render_ir_view_t *view, const db_render_ir_command_header_t *lhs,
    const db_render_ir_command_header_t *rhs);

#endif
