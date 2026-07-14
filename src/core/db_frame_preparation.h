#ifndef DRIVERBENCH_CORE_DB_FRAME_PREPARATION_H
#define DRIVERBENCH_CORE_DB_FRAME_PREPARATION_H

#include "core/db_frame_plan.h"
#include "core/db_render_result.h"

#include <stdint.h>

typedef struct {
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_format;
    uint32_t framebuffer_generation;
    uint32_t raw_buffer_age;
    uint32_t replay_depth;
    uint64_t requirements_token;
    uint64_t checkpoint_binding_token;
    db_render_target_strategy_t target_strategy;
    db_frame_rebuild_reason_t rebuild_reason;
    int buffer_age_valid;
    int conversion_required;
    int force_rebuild;
} db_frame_preparation_t;

uint64_t db_frame_preparation_token(const db_frame_preparation_t *preparation);
int db_frame_preparation_matches(const db_frame_plan_t *plan,
                                 const db_frame_preparation_t *preparation);

#endif
