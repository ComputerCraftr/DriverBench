#ifndef DRIVERBENCH_VK_FRAME_FINALIZE_H
#define DRIVERBENCH_VK_FRAME_FINALIZE_H

#include "core/db_frame_plan.h"
#include "vk_internal.h"
#include "vk_runtime_internal.h"

typedef struct {
    const db_frame_plan_t *plan;
    const db_vk_execution_plan_t *execution_plan;
    const uint32_t *frame_work_units;
    uint64_t frame_start_ns;
    size_t lookup_word_count;
    uint32_t gpu_count;
    uint32_t grid_tiles_drawn;
    int backing_index;
    int use_offscreen_target;
    int frame_full_draw;
    int frame_dirty_draw;
    db_gradient_implementation_t gradient_implementation;
} db_vk_frame_finalize_input_t;

db_vk_frame_result_t
db_vk_finalize_frame(const db_vk_frame_finalize_input_t *input);

#endif
