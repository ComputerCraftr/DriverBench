#include "display_linux_kms_atomic_common.h"

#include "../../core/db_core.h"
#include "../../renderers/opengl_gl3_3/renderer_opengl_gl3_3.h"
#include "../../renderers/renderer_gl_common.h"

#define BACKEND_NAME "display_linux_kms_atomic_opengl_gl3_3"
#define RENDERER_NAME "renderer_opengl_gl3_3"

static void db_runtime_check_gl3_3(const char *backend,
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

int main(int argc, char **argv) {
    const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

    const db_kms_atomic_renderer_vtable_t renderer = {
        .init = db_renderer_opengl_gl3_3_init,
        .render_frame = db_renderer_opengl_gl3_3_render_frame,
        .shutdown = db_renderer_opengl_gl3_3_shutdown,
        .capability_mode = db_renderer_opengl_gl3_3_capability_mode,
        .work_unit_count = db_renderer_opengl_gl3_3_work_unit_count,
    };

    return db_kms_atomic_run(BACKEND_NAME, RENDERER_NAME, card,
                             DB_KMS_ATOMIC_CONTEXT_GL3_3, &renderer,
                             db_runtime_check_gl3_3);
}
