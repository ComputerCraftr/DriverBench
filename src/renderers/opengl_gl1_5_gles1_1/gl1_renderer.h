#ifndef DRIVERBENCH_GL1_H
#define DRIVERBENCH_GL1_H

#include <stdint.h>

#include "../gl_common.h"
#include "core/db_renderer_runtime_contract.h"

#include "../../core/db_frame_plan.h"

void db_gl1_init(const db_renderer_runtime_contract_t *resolved_runtime);
void db_gl1_render_frame(const db_frame_plan_t *plan, int viewport_width_px,
                         int viewport_height_px,
                         db_pixel_block_view_t presentation_damage,
                         int force_full_presentation);
void db_gl1_shutdown(void);
const char *db_gl1_capability_mode(void);
uint32_t db_gl1_work_unit_count(void);
uint64_t db_gl1_state_hash(void);
uint64_t db_gl1_working_hash(void);
void db_gl1_draw_stats(db_renderer_draw_path_stats_t *stats);
void db_gl1_execution_report(db_render_execution_report_t *report);

#endif
