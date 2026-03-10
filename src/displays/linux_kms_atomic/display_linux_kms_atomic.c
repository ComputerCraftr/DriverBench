#include "display_linux_kms_atomic_common.h"

#include <stdint.h>

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

static void db_kms_render_frame_gl1(uint32_t frame_index,
                                    uint32_t preserved_framebuffer_count) {
    db_display_gl_render_frame_kms(DB_GL_RENDERER_GL1_5_GLES1_1, frame_index,
                                   preserved_framebuffer_count);
}

static void db_kms_render_frame_gl3(uint32_t frame_index,
                                    uint32_t preserved_framebuffer_count) {
    db_display_gl_render_frame_kms(DB_GL_RENDERER_GL3_3, frame_index,
                                   preserved_framebuffer_count);
}

int db_run_linux_kms_atomic(db_api_t api, db_gl_renderer_t renderer,
                            const char *card_path, const db_cli_config_t *cfg) {
    const char *card = (card_path != NULL) ? card_path : "/dev/dri/card0";

    db_dispatch_validate_backend_or_fail(
        BACKEND_NAME_GL, DB_DISPLAY_LINUX_KMS_ATOMIC, api, renderer);

    if (api == DB_API_CPU) {
        return db_kms_atomic_run_cpu(BACKEND_NAME_CPU, db_renderer_name_cpu(),
                                     card, api, cfg);
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
                .render_frame = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                                    ? db_kms_render_frame_gl1
                                    : db_kms_render_frame_gl3,
                .shutdown = renderer_ops.shutdown,
                .work_unit_count = renderer_ops.work_unit_count,
            },
        .renderer_ops = renderer_ops,
    };
    return db_kms_atomic_run(BACKEND_NAME_GL, gl_cfg.renderer_ops.renderer_name,
                             card, renderer, gl_cfg.context_mode,
                             &gl_cfg.vtable, cfg);
}
