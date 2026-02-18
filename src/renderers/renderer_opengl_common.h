#ifndef RENDERER_OPENGL_COMMON_H
#define RENDERER_OPENGL_COMMON_H

#include <stddef.h>

typedef struct {
    int use_persistent_upload;
    int use_map_range_upload;
    int use_map_buffer_upload;
    void *persistent_mapped_ptr;
} db_gl_upload_probe_result_t;

typedef struct {
    size_t dst_offset_bytes;
    size_t src_offset_bytes;
    size_t size_bytes;
} db_gl_upload_range_t;

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     int allow_persistent_upload,
                                     db_gl_upload_probe_result_t *out);
int db_gl_has_vbo_support(void);

void db_gl_upload_ranges(const void *source_base, size_t total_bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload,
                         const db_gl_upload_range_t *ranges,
                         size_t range_count);

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload);

#endif
