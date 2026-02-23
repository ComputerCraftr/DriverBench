#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "renderer_benchmark_common.h"

#define DB_GL_PROBE_PREFIX_BYTES 64U
#define DB_GL_MAP_RANGE_PROBE_XOR_SEED 0xA5U

typedef void (*db_gl_generic_proc_t)(void);
typedef db_gl_generic_proc_t (*db_gl_get_proc_address_fn_t)(const char *name);
typedef unsigned int (*db_gl_get_error_fn_t)(void);

typedef struct {
    int use_map_buffer_upload;
    int use_map_range_upload;
    int use_persistent_upload;
    void *persistent_mapped_ptr;
} db_gl_upload_probe_result_t;

typedef struct {
    size_t dst_offset_bytes;
    size_t src_offset_bytes;
    size_t size_bytes;
} db_gl_upload_range_t;

typedef struct {
    float *vertices;
    size_t vertex_stride;
    db_pattern_t pattern;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
    db_gl_upload_probe_result_t upload;
} db_gl_vertex_init_t;

int db_has_gl_extension_token(const char *exts, const char *needle);
int db_parse_gl_version_numbers(const char *version_text, int *major_out,
                                int *minor_out);
int db_gl_version_text_at_least(const char *version_text, int req_major,
                                int req_minor);
int db_gl_is_es_context(const char *version_text);

int db_gl_runtime_supports_vbo(const char *version_text, const char *exts);
int db_gl_runtime_supports_map_buffer_range(const char *version_text,
                                            const char *exts);
int db_gl_runtime_supports_map_buffer(const char *version_text,
                                      const char *exts);
int db_gl_runtime_supports_buffer_storage(const char *version_text,
                                          const char *exts);

void db_gl_clear_errors(db_gl_get_error_fn_t get_error);
size_t db_gl_probe_size(size_t bytes);
void db_gl_fill_probe_pattern(uint8_t *pattern, size_t count);

void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     db_gl_upload_probe_result_t *out);
int db_gl_has_vbo_support(void);
void db_gl_set_proc_address_loader(
    db_gl_get_proc_address_fn_t get_proc_address);

void db_gl_upload_ranges(const void *source_base, size_t total_bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload,
                         const db_gl_upload_range_t *ranges,
                         size_t range_count);

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload);
void db_gl_unmap_current_array_buffer(void);

int db_init_band_vertices_common(db_gl_vertex_init_t *out_state,
                                 size_t vertex_stride);
int db_init_grid_vertices_common(db_gl_vertex_init_t *out_state,
                                 size_t vertex_stride);
int db_init_vertices_for_pattern_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    db_pattern_t pattern, size_t vertex_stride);
int db_init_vertices_for_runtime_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride);
void db_fill_band_vertices_pos_rgb_stride(float *out_vertices,
                                          uint32_t band_count, double time_s,
                                          size_t stride_floats,
                                          size_t color_offset_floats);
void db_update_band_vertices_rgb_stride(float *out_vertices,
                                        uint32_t band_count, double time_s,
                                        size_t stride_floats,
                                        size_t color_offset_floats);

#endif
