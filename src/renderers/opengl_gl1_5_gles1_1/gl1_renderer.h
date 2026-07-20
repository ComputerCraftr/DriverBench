#ifndef DRIVERBENCH_GL1_H
#define DRIVERBENCH_GL1_H

#include <stdint.h>

#include "core/db_geometry.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_renderer_runtime_contract.h"

#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"

void db_gl1_init(const db_renderer_runtime_contract_t *resolved_runtime);
int db_gl1_render_frame(const db_frame_plan_t *plan,
                        const db_renderer_target_t *target,
                        int viewport_width_px, int viewport_height_px,
                        db_pixel_block_view_t presentation_damage,
                        int force_full_presentation);
void db_gl1_shutdown(void);
const char *db_gl1_capability_mode(void);
uint32_t db_gl1_work_unit_count(void);
uint64_t db_gl1_state_hash(void);
uint64_t db_gl1_working_hash(void);
void db_gl1_draw_stats(db_renderer_draw_path_stats_t *stats);
void db_gl1_execution_report(db_render_execution_report_t *report);
void db_gl1_replay_preflight_facts(db_render_target_strategy_t *strategy,
                                   uint64_t *target_generation,
                                   int *direct_window_lineage_valid);
void db_gl1_finalize_frame(int commit,
                           db_render_target_strategy_t target_strategy,
                           uint64_t target_generation);
const db_renderer_qualification_ops_t *db_gl1_qualification_ops(void);

#endif
