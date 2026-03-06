#ifndef DRIVERBENCH_RENDERER_GL_UPLOAD_INTERNAL_H
#define DRIVERBENCH_RENDERER_GL_UPLOAD_INTERNAL_H

#include "renderer_gl_api.h"

typedef struct {
    void (*bind_buffer)(GLenum target, GLuint buffer);
    void (*buffer_data)(GLenum target, GLsizeiptr size, const void *data,
                        GLenum usage);
    void (*buffer_sub_data)(GLenum target, GLintptr offset, GLsizeiptr size,
                            const void *data);
    void *(*map_buffer)(GLenum target, GLenum access);
    void *(*map_buffer_range)(GLenum target, GLintptr offset, GLsizeiptr length,
                              GLbitfield access);
    GLboolean (*unmap_buffer)(GLenum target);
} db_gl_upload_ops_t;

void db_gl_internal_require_upload_ready(const char *func_name);
db_gl_upload_ops_t db_gl_internal_get_upload_ops(void);
GLenum db_gl_internal_get_error_value(void);

#endif
