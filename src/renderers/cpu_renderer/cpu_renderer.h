#ifndef DRIVERBENCH_CPU_RENDERER_H
#define DRIVERBENCH_CPU_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_runtime_contract.h"

typedef enum {
    DB_CPU_RENDER_TARGET_PRESERVED_SURFACE = 0,
    DB_CPU_RENDER_TARGET_REPLACE_SURFACE = 1,
} db_cpu_render_target_mode_t;

#include "../../core/db_frame_plan.h"

void db_cpu_init(const db_renderer_runtime_contract_t *resolved_runtime);
const db_damage_block_t *db_cpu_render_frame_to_surface_mode(
    const db_frame_plan_t *plan, const db_pixel_surface_t *surface,
    db_cpu_render_target_mode_t target_mode, size_t *out_damage_count);
const db_damage_block_t *
db_cpu_render_frame_to_surface(const db_frame_plan_t *plan,
                               const db_pixel_surface_t *surface,
                               size_t *out_damage_count);
void db_cpu_shutdown(void);

uint32_t db_cpu_work_unit_count(void);
const char *db_cpu_capability_mode(void);
uint64_t db_cpu_state_hash(void);
void db_cpu_execution_report(db_render_execution_report_t *report);

#endif
