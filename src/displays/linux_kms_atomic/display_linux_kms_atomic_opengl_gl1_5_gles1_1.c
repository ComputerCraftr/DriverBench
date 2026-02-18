#include "display_linux_kms_atomic_common.h"

#include "../../renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.h"
#include "../display_gl_runtime_common.h"

#define BACKEND_NAME "display_linux_kms_atomic_opengl_gl1_5_gles1_1"
#define RENDERER_NAME "renderer_opengl_gl1_5_gles1_1"

static void db_runtime_check_gl1_5_gles1_1(const char *backend,
                                           const char *runtime_version,
                                           int runtime_is_gles) {
    if (runtime_is_gles != 0) {
        db_display_validate_gles_1x_runtime_or_fail(backend, runtime_version);
    }
}

int main(int argc, char **argv) {
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

    const db_kms_atomic_renderer_vtable_t renderer = {
        .init = db_renderer_opengl_gl1_5_gles1_1_init,
        .render_frame = db_renderer_opengl_gl1_5_gles1_1_render_frame,
        .shutdown = db_renderer_opengl_gl1_5_gles1_1_shutdown,
        .capability_mode = db_renderer_opengl_gl1_5_gles1_1_capability_mode,
        .work_unit_count = db_renderer_opengl_gl1_5_gles1_1_work_unit_count,
    };

    return db_kms_atomic_run(BACKEND_NAME, RENDERER_NAME, card,
                             DB_KMS_ATOMIC_CONTEXT_GL1_5_OR_GLES1_1, &renderer,
                             db_runtime_check_gl1_5_gles1_1);
}
