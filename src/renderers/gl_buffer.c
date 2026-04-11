#include "../core/db_numeric.h"
#include "core/db_log.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_probe_internal.h"
#include "gl_proc_runtime.h"
#include <stddef.h>

int db_gl_vbo_create_or_zero(unsigned int *out_buffer) {
    db_gl_load_upload_proc_table();
    db_gl_probe_drain_errors();
    if (out_buffer == NULL) {
        return 0;
    }
    *out_buffer = 0U;
    if (g_upload_proc_table.gen_buffers == NULL) {
        DB_RUNTIME_ERROR("renderer_gl_buffer",
                         "db_gl_vbo_create_or_zero: gen_buffers proc is NULL");
        return 0;
    }
    GLuint buffer = 0U;
    g_upload_proc_table.gen_buffers(1, &buffer);
    if (buffer == 0U) {
        GLenum err = db_gl_get_error_value();
        DB_RUNTIME_ERROR("renderer_gl_buffer",
                         "db_gl_vbo_create_or_zero: glGenBuffers returned 0, "
                         "gl_error=0x%04X",
                         (unsigned int)err);
    }
    *out_buffer = (unsigned int)buffer;
    return DB_BOOL(buffer);
}

void db_gl_vbo_delete_if_valid(unsigned int buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_delete_if_valid");
    if ((buffer == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_buffer = (GLuint)buffer;
    g_upload_proc_table.delete_buffers(1, &gl_buffer);
}

// Context capability checks (metadata/procs).
int db_gl_context_has_pbo_upload_procs(void) {
    db_gl_require_upload_proc_table_loaded(
        "db_gl_context_has_pbo_upload_procs");
    return (g_upload_proc_table.bind_buffer != NULL) &&
           (g_upload_proc_table.buffer_data != NULL) &&
           (g_upload_proc_table.buffer_sub_data != NULL) &&
           (g_upload_proc_table.gen_buffers != NULL) &&
           (g_upload_proc_table.delete_buffers != NULL);
}

int db_gl_context_advertises_vbo(void) {
    db_gl_load_upload_proc_table();
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    return db_gl_extensions_advertise_vbo(&runtime);
}

// 3) Wrapper APIs: geometry utilities and texture operations.
void db_gl_quad_init(float *verts) {
    verts[DB_GL_QUAD_V0_X] = -1.0F;
    verts[DB_GL_QUAD_V0_Y] = -1.0F;
    verts[DB_GL_QUAD_V1_X] = 1.0F;
    verts[DB_GL_QUAD_V1_Y] = -1.0F;
    verts[DB_GL_QUAD_V2_X] = -1.0F;
    verts[DB_GL_QUAD_V2_Y] = 1.0F;
    verts[DB_GL_QUAD_V3_X] = 1.0F;
    verts[DB_GL_QUAD_V3_Y] = 1.0F;
}
