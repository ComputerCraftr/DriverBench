#ifndef DRIVERBENCH_GL3_H
#define DRIVERBENCH_GL3_H

#include <stdint.h>

#include "../gl_common.h"
#include "core/db_renderer_runtime_contract.h"

#include "../../core/db_frame_plan.h"

void db_gl3_init(const db_renderer_runtime_contract_t *resolved_runtime);
void db_gl3_render_frame(const db_frame_plan_t *plan, int viewport_width_px,
                         int viewport_height_px);
void db_gl3_shutdown(void);
const char *db_gl3_capability_mode(void);
uint32_t db_gl3_work_unit_count(void);
uint64_t db_gl3_state_hash(void);
uint64_t db_gl3_working_hash(void);
void db_gl3_draw_stats(db_renderer_draw_path_stats_t *stats);

#endif
