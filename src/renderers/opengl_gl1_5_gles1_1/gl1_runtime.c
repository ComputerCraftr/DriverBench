#include "gl1_internal.h"

#include <stddef.h>

#include "../gl_common.h"

void db_gl1_refresh_capability_mode(void) {
    const db_gl_runtime_draw_mode_t draw_mode =
        (g_state.runtime.backbuffer_draw_full != 0)
            ? DB_GL_RUNTIME_DRAW_FULL_PRESENT
            : DB_GL_RUNTIME_DRAW_DIRTY_REPLAY;
    const db_gl_runtime_mode_desc_t mode =
        db_gl_runtime_mode_desc_renderer(draw_mode, 0, NULL, 0);
    db_gl_runtime_mode_format_renderer(
        g_state.telemetry.capability_mode,
        sizeof(g_state.telemetry.capability_mode), &mode);
}

int db_gl1_init_runtime(const db_renderer_execution_config_t *runtime_state) {
    if (runtime_state == NULL) {
        return 0;
    }
    g_state.runtime = *runtime_state;
    return 1;
}
