#ifndef RENDERER_OPENGL_COMMON_H
#define RENDERER_OPENGL_COMMON_H

#include <stddef.h>

typedef struct {
    int use_persistent_upload;
    int use_map_range_upload;
    int use_map_buffer_upload;
    void *persistent_mapped_ptr;
} db_gl_upload_probe_result_t;

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     int allow_persistent_upload,
                                     db_gl_upload_probe_result_t *out);
int db_gl_has_vbo_support(void);

void db_gl_upload_mapped_or_subdata(const void *source, size_t bytes,
                                    void *mapped_ptr);

#endif
