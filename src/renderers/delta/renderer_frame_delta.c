#include "renderers/delta/renderer_frame_delta.h"

#include <stdint.h>

#include "renderers/renderer_benchmark_gradient.h"
#include "renderers/renderer_benchmark_types.h"

void db_frame_delta_plan_reset(db_frame_delta_plan_t *plan,
                               db_pattern_t benchmark_kind) {
    if (plan == NULL) {
        return;
    }
    *plan = (db_frame_delta_plan_t){
        .benchmark_kind = benchmark_kind,
        .mode = DB_FRAME_DELTA_MODE_NO_OP,
    };
}

int db_frame_delta_plan_is_full_frame(const db_frame_delta_plan_t *plan,
                                      uint32_t rows, uint32_t cols) {
    if ((plan == NULL) || (rows == 0U) || (cols == 0U)) {
        return 0;
    }
    if (plan->mode == DB_FRAME_DELTA_MODE_FULL_REBUILD) {
        return 1;
    }
    if ((plan->logical_damage_block_count == 1U) &&
        (plan->logical_damage_blocks != NULL)) {
        const db_grid_block_t *const block = &plan->logical_damage_blocks[0];
        return (block->row_start == 0U) && (block->row_count == rows) &&
               (block->col_start == 0U) && (block->col_count == cols);
    }
    return 0;
}
