#ifndef DRIVERBENCH_KMS_RUNNER_H
#define DRIVERBENCH_KMS_RUNNER_H

#include <stdint.h>

#include "../../core/db_frame_contracts.h"
#include "../../core/db_frame_plan.h"
#include "../../driverbench_config.h"
#include "../display_presentation_policy.h"
#include "../display_types.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_result.h"
#include "core/db_renderer_runtime_contract.h"

typedef enum {
    DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1 = 0,
    DB_KMS_ATOMIC_CONTEXT_GL3_3 = 1,
} db_kms_atomic_context_profile_t;

typedef struct {
    const char *(*capability_mode)(void);
    void (*draw_stats)(db_renderer_draw_path_stats_t *stats);
    void (*execution_report)(db_render_execution_report_t *report);
    const db_renderer_qualification_ops_t *qualification_ops;
    void (*init)(const db_renderer_runtime_contract_t *resolved_runtime);
    int (*render_frame)(const db_frame_plan_t *plan,
                        const db_renderer_target_t *target,
                        const db_gl_presentation_frame_t *presentation);
    void (*shutdown)(void);
    uint32_t (*work_unit_count)(void);
} db_kms_atomic_renderer_vtable_t;

int db_kms_atomic_run(const char *backend, const char *renderer_name,
                      const char *card, db_gl_renderer_t gl_renderer,
                      db_kms_atomic_context_profile_t context_profile,
                      const db_kms_atomic_renderer_vtable_t *renderer,
                      const db_cli_config_t *cfg);
int db_kms_atomic_run_cpu(const char *backend, const char *renderer_name,
                          const char *card, db_api_t api,
                          const db_cli_config_t *cfg);

#endif
