#ifndef RENDERER_OPENGL_CAPABILITIES_H
#define RENDERER_OPENGL_CAPABILITIES_H

#include <stddef.h>

typedef struct {
    int use_map_range_upload;
    int use_persistent_upload;
    void *persistent_mapped_ptr;
} db_gl3_upload_probe_result_t;

typedef struct {
    int has_vbo;
    int use_map_range_upload;
    int use_map_buffer_upload;
} db_gl15_upload_probe_result_t;

int db_gl15_has_vbo_support(void);
void db_gl15_probe_upload_capabilities(size_t bytes, const float *initial_vertices,
                                       db_gl15_upload_probe_result_t *out);

void db_gl3_probe_upload_capabilities(size_t bytes, const float *initial_vertices,
                                      db_gl3_upload_probe_result_t *out);

#endif
