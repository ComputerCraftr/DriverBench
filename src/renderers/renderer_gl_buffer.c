#include "renderer_gl_api.h"
#include "renderer_gl_common.h"
#include "renderer_gl_proc_runtime_internal.h"
#include <stddef.h>

// 3) Wrapper APIs: buffer and VBO/VAO operations.
int db_gl_vbo_bind(unsigned int buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_bind");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return 0;
    }
    g_upload_proc_table.bind_buffer(GL_ARRAY_BUFFER, (GLuint)buffer);
    return 1;
}

int db_gl_bind_array_buffer_cached(unsigned int buffer,
                                   unsigned int *cached_buffer) {
    if ((cached_buffer != NULL) && (*cached_buffer == buffer)) {
        return 1;
    }
    if (db_gl_vbo_bind(buffer) == 0) {
        return 0;
    }
    if (cached_buffer != NULL) {
        *cached_buffer = buffer;
    }
    return 1;
}

int db_gl_vbo_create_or_zero(unsigned int *out_buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_create_or_zero");
    if (out_buffer == NULL) {
        return 0;
    }
    *out_buffer = 0U;
    if (g_upload_proc_table.gen_buffers == NULL) {
        return 0;
    }
    GLuint buffer = 0U;
    g_upload_proc_table.gen_buffers(1, &buffer);
    *out_buffer = (unsigned int)buffer;
    return (buffer != 0U) ? 1 : 0;
}

void db_gl_vbo_delete_if_valid(unsigned int buffer) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_delete_if_valid");
    if ((buffer == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_buffer = (GLuint)buffer;
    g_upload_proc_table.delete_buffers(1, &gl_buffer);
}

int db_gl_vbo_init_data(size_t bytes, const void *data, unsigned int usage) {
    db_gl_require_upload_proc_table_loaded("db_gl_vbo_init_data");
    if (g_upload_proc_table.buffer_data == NULL) {
        return 0;
    }
    g_upload_proc_table.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data,
                                    (GLenum)usage);
    return ((g_upload_proc_table.get_error != NULL) &&
            (g_upload_proc_table.get_error() == GL_NO_ERROR))
               ? 1
               : 0;
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

unsigned int db_gl_pbo_create_or_zero(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_create_or_zero");
    if ((g_upload_proc_table.gen_buffers == NULL) ||
        (g_upload_proc_table.delete_buffers == NULL)) {
        return 0U;
    }
    GLuint pbo = 0U;
    g_upload_proc_table.gen_buffers(1, &pbo);
    return (unsigned int)pbo;
}

void db_gl_pbo_delete_if_valid(unsigned int pbo) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_delete_if_valid");
    if ((pbo == 0U) || (g_upload_proc_table.delete_buffers == NULL)) {
        return;
    }
    const GLuint gl_pbo = (GLuint)pbo;
    g_upload_proc_table.delete_buffers(1, &gl_pbo);
}

void db_gl_pbo_unbind_unpack(void) {
    db_gl_require_upload_proc_table_loaded("db_gl_pbo_unbind_unpack");
    if (g_upload_proc_table.bind_buffer == NULL) {
        return;
    }
    g_upload_proc_table.bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0U);
}

unsigned int db_gl_pbo_create_if_usable(int prefer_unpack_pbo) {
    db_gl_load_upload_proc_table();
    if ((prefer_unpack_pbo == 0) ||
        (db_gl_context_has_pbo_upload_procs() == 0)) {
        return 0U;
    }
    const db_gl_runtime_metadata_t runtime = db_gl_runtime_metadata_load();
    if (db_gl_extensions_advertise_pbo(&runtime) == 0) {
        return 0U;
    }
    return db_gl_pbo_create_or_zero();
}
