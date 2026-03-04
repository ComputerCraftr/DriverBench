#include "display_linux_kms_atomic_common.h"

#include "../../core/db_core.h"
#include "../../driverbench_config.h"
#include "../../renderers/renderer_identity.h"
#include "../display_dispatch.h"
#include "../display_gl_renderer_select_common.h"
#include "../display_gl_runtime_common.h"
#include "../display_types.h"

#define BACKEND_NAME_GL "display_linux_kms_atomic_opengl"
#define BACKEND_NAME_CPU "display_linux_kms_atomic_cpu"

typedef struct {
    db_kms_atomic_context_profile_t context_mode;
    db_kms_atomic_renderer_vtable_t vtable;
    const db_display_gl_renderer_ops_t renderer_ops;
} db_kms_gl_renderer_config_t;

int db_run_linux_kms_atomic(db_api_t api, db_gl_renderer_t renderer,
                            const char *card_path, const db_cli_config_t *cfg) {
    const char *card = (card_path != NULL) ? card_path : "/dev/dri/card0";

    if (api == DB_API_CPU) {
        return db_kms_atomic_run_cpu(BACKEND_NAME_CPU, db_renderer_name_cpu(),
                                     card, api, cfg);
    }

    if (api != DB_API_OPENGL) {
        db_failf("display_linux_kms_atomic",
                 "requested linux_kms_atomic display is incompatible with "
                 "api=%d in this build",
                 (int)api);
    }

    const db_display_gl_renderer_ops_t renderer_ops =
        db_display_gl_select_renderer_ops(renderer);
    const db_display_gl_context_policy_t context_policy =
        db_display_gl_context_policy_for_renderer(renderer);
    const db_kms_gl_renderer_config_t gl_cfg = {
        .context_mode = (context_policy.allow_gles1_1_fallback != 0)
                            ? DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1
                            : DB_KMS_ATOMIC_CONTEXT_GL3_3,
        .vtable =
            (db_kms_atomic_renderer_vtable_t){
                .capability_mode = renderer_ops.runtime_capability_mode,
                .draw_stats = renderer_ops.draw_stats,
                .init = renderer_ops.init,
                .render_frame = renderer_ops.render_frame_kms,
                .shutdown = renderer_ops.shutdown,
                .work_unit_count = renderer_ops.work_unit_count,
            },
        .renderer_ops = renderer_ops,
    };
    return db_kms_atomic_run(BACKEND_NAME_GL, gl_cfg.renderer_ops.renderer_name,
                             card, renderer, gl_cfg.context_mode,
                             &gl_cfg.vtable, cfg);
}
