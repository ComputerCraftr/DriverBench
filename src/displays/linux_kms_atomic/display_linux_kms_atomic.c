#include "display_linux_kms_atomic_common.h"

#include "../../core/db_core.h"
#include "../../driverbench_cli.h"
#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#ifdef DB_HAS_OPENGL_DESKTOP
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../../renderers/renderer_gl_common.h"
#endif
#include "../../renderers/renderer_identity.h"
#include "../display_dispatch.h"
#include "../display_gl_runtime_common.h"

#define BACKEND_NAME_GL "display_linux_kms_atomic_opengl"
#define BACKEND_NAME_CPU "display_linux_kms_atomic_cpu"

static const char *db_kms_backend_name(db_gl_renderer_t renderer) {
    (void)renderer;
    return BACKEND_NAME_GL;
}

static const char *db_kms_renderer_name(db_gl_renderer_t renderer) {
#ifdef DB_HAS_OPENGL_DESKTOP
    return (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
               ? db_renderer_name_opengl_gl1_5_gles1_1()
               : db_renderer_name_opengl_gl3_3();
#else
    (void)renderer;
    return db_renderer_name_opengl_gl1_5_gles1_1();
#endif
}

static void db_runtime_check_gl1(const char *backend,
                                 const char *runtime_version,
                                 int runtime_is_gles) {
    (void)backend;
    (void)runtime_version;
    if (runtime_is_gles != 0) {
        db_display_validate_gles_1x_runtime_or_fail(backend, runtime_version);
    }
}

#ifdef DB_HAS_OPENGL_DESKTOP
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
#endif

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

#ifdef DB_HAS_OPENGL_DESKTOP
    const db_kms_atomic_renderer_vtable_t vtable = {
        .init = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                    ? db_renderer_opengl_gl1_5_gles1_1_init
                    : db_renderer_opengl_gl3_3_init,
        .render_frame = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                            ? db_renderer_opengl_gl1_5_gles1_1_render_frame
                            : db_renderer_opengl_gl3_3_render_frame,
        .shutdown = (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                        ? db_renderer_opengl_gl1_5_gles1_1_shutdown
                        : db_renderer_opengl_gl3_3_shutdown,
        .capability_mode =
            (renderer == DB_GL_RENDERER_GL1_5_GLES1_1)
                ? db_renderer_opengl_gl1_5_gles1_1_capability_mode
                : db_renderer_opengl_gl3_3_capability_mode,
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
#else
    if (renderer != DB_GL_RENDERER_GL1_5_GLES1_1) {
        db_failf("display_linux_kms_atomic",
                 "renderer gl3_3 is not compiled in this build");
    }
    const db_kms_atomic_renderer_vtable_t vtable = {
        .init = db_renderer_opengl_gl1_5_gles1_1_init,
        .render_frame = db_renderer_opengl_gl1_5_gles1_1_render_frame,
        .shutdown = db_renderer_opengl_gl1_5_gles1_1_shutdown,
        .capability_mode = db_renderer_opengl_gl1_5_gles1_1_capability_mode,
        .work_unit_count = db_renderer_opengl_gl1_5_gles1_1_work_unit_count,
    };
    const db_kms_atomic_runtime_check_fn_t runtime_check = db_runtime_check_gl1;
    const db_kms_atomic_context_profile_t context_mode =
        DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1;
#endif

    return db_kms_atomic_run(db_kms_backend_name(renderer),
                             db_kms_renderer_name(renderer), card, context_mode,
                             &vtable, runtime_check, cfg);
}
