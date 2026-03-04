#include "display_linux_kms_atomic_common.h"

#include <stdint.h>

#include "../../core/db_core.h"
#include "../../driverbench_config.h"
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../../renderers/renderer_gl_common.h"
#include "../../renderers/renderer_identity.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"
#include "../display_types.h"

#define BACKEND_NAME_GL "display_linux_kms_atomic_opengl"
#define BACKEND_NAME_CPU "display_linux_kms_atomic_cpu"

static void db_kms_gl1_render_frame_adapter(uint32_t frame_index) {
    db_renderer_opengl_gl1_5_gles1_1_render_frame(frame_index, 0, 0, 1);
}

static void db_kms_gl3_render_frame_adapter(uint32_t frame_index) {
    db_renderer_opengl_gl3_3_render_frame(frame_index, 0, 0);
}

static void db_runtime_check_gl1(const char *backend,
                                 const char *runtime_version,
                                 int runtime_is_gles) {
    if (runtime_is_gles != 0) {
        db_display_validate_gles_1x_runtime_or_fail(backend, runtime_version);
    }
}

static void db_runtime_check_gl3(const char *backend,
                                 const char *runtime_version,
                                 int runtime_is_gles) {
    if (runtime_is_gles != 0) {
        db_failf(backend,
                 "OpenGL ES context is unsupported for this renderer; requires "
                 "desktop OpenGL 3.3+");
    }
    if (!db_gl_version_text_at_least(runtime_version, 3, 3)) {
        db_failf(backend,
                 "Desktop OpenGL %s is unsupported for this renderer; "
                 "requires OpenGL 3.3+",
                 (runtime_version != NULL) ? runtime_version : "(null)");
    }
}

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

    const db_kms_atomic_renderer_vtable_t vtable = {
        .init = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                    ? db_renderer_opengl_gl1_5_gles1_1_init
                    : db_renderer_opengl_gl3_3_init,
        .render_frame = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                            ? db_kms_gl1_render_frame_adapter
                            : db_kms_gl3_render_frame_adapter,
        .shutdown = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                        ? db_renderer_opengl_gl1_5_gles1_1_shutdown
                        : db_renderer_opengl_gl3_3_shutdown,
        .capability_mode =
            (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                ? db_renderer_opengl_gl1_5_gles1_1_capability_mode
                : db_renderer_opengl_gl3_3_capability_mode,
        .draw_stats = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                          ? db_renderer_opengl_gl1_5_gles1_1_draw_stats
                          : db_renderer_opengl_gl3_3_draw_stats,
        .work_unit_count =
            (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                ? db_renderer_opengl_gl1_5_gles1_1_work_unit_count
                : db_renderer_opengl_gl3_3_work_unit_count,
    };

    const db_kms_atomic_runtime_check_fn_t runtime_check =
        (renderer == DB_GL_RENDERER_GL1_5_GLES1_1) ? db_runtime_check_gl1
                                                   : db_runtime_check_gl3;
    const db_kms_atomic_context_profile_t context_mode =
        (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
            ? DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1
            : DB_KMS_ATOMIC_CONTEXT_GL3_3;

    const char *renderer_name = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                                    ? db_renderer_name_opengl_gl1_5_gles1_1()
                                    : db_renderer_name_opengl_gl3_3();
    return db_kms_atomic_run(BACKEND_NAME_GL, renderer_name, card, context_mode,
                             &vtable, runtime_check, cfg);
}
