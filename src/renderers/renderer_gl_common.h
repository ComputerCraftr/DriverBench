#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "renderer_benchmark_common.h"
#include "renderer_snake_common.h"
#include "renderer_snake_shape_common.h"

#define DB_GL_PROBE_PREFIX_BYTES 64U
#define DB_GL_MAP_RANGE_PROBE_XOR_SEED 0xA5U

typedef void (*db_gl_generic_proc_t)(void);
typedef unsigned int (*db_gl_get_error_fn_t)(void);
typedef db_gl_generic_proc_t (*db_gl_proc_resolver_fn_t)(const char *name);

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
    uint32_t row_unit_width;
    uint32_t row_count_total;
    size_t unit_stride_bytes;
    size_t total_bytes;
    int force_full_upload;
    const db_dirty_row_range_t *dirty_rows;
    size_t dirty_row_count;
    const db_snake_col_span_t *spans;
    size_t span_count;
} db_gl_damage_upload_plan_t;

typedef struct {
    db_pattern_t pattern;
    uint32_t cols;
    uint32_t rows;
    size_t upload_bytes;
    size_t upload_tile_bytes;
    int force_full_upload;
    const db_snake_plan_t *snake_plan;
    uint32_t snake_prev_start;
    uint32_t snake_prev_count;
    uint32_t pattern_seed;
    db_snake_col_span_t *snake_spans;
    size_t snake_scratch_capacity;
    db_snake_shape_row_bounds_t *snake_row_bounds;
    size_t snake_row_bounds_capacity;
    const db_dirty_row_range_t *damage_row_ranges;
    size_t damage_row_count;
    int use_damage_row_ranges;
} db_gl_pattern_upload_collect_t;

typedef struct {
    db_gl_upload_range_t range;
    db_dirty_row_range_t rows;
} db_gl_upload_row_span_t;

typedef void (*db_gl_upload_row_span_apply_fn_t)(
    const db_gl_upload_row_span_t *span, void *user_data);

typedef enum {
    DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER = 0,
    DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER = 1,
} db_gl_upload_target_t;

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

int db_gl_runtime_supports_buffer_storage(const char *version_text,
                                          const char *exts);
int db_gl_runtime_supports_map_buffer(const char *version_text,
                                      const char *exts);
int db_gl_runtime_supports_map_buffer_range(const char *version_text,
                                            const char *exts);
int db_gl_runtime_supports_pbo(const char *version_text, const char *exts);
int db_gl_runtime_supports_vbo(const char *version_text, const char *exts);

void db_gl_clear_errors(db_gl_get_error_fn_t get_error);
size_t db_gl_probe_size(size_t bytes);
void db_gl_fill_probe_pattern(uint8_t *pattern, size_t count);

int db_gl_vbo_bind(unsigned int buffer);
int db_gl_vbo_create_or_zero(unsigned int *out_buffer);
void db_gl_vbo_delete_if_valid(unsigned int buffer);
int db_gl_vbo_init_data(size_t bytes, const void *data, unsigned int usage);
int db_gl_context_supports_pbo_upload(void);
int db_gl_context_supports_vbo(void);
int db_gl_get_viewport_size(int *width_out, int *height_out);
unsigned int db_gl_pbo_create_or_zero(void);
void db_gl_pbo_delete_if_valid(unsigned int pbo);
void db_gl_pbo_unbind_unpack(void);
int db_gl_texture_allocate_rgba(unsigned int texture, int width, int height,
                                unsigned int internal_format,
                                const void *pixels);
int db_gl_texture_create_rgba(unsigned int *out_texture, int width, int height,
                              unsigned int internal_format, const void *pixels);
void db_gl_texture_delete_if_valid(unsigned int *texture);
void db_gl_set_proc_resolver(db_gl_proc_resolver_fn_t resolver);
void db_gl_preload_upload_proc_table(void);
void db_gl_probe_upload_capabilities(size_t bytes,
                                     const float *initial_vertices,
                                     db_gl_upload_probe_result_t *out);
void db_gl_upload_ranges_target(
    const void *source_base, size_t total_bytes,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_gl_upload_target_t target, unsigned int target_buffer,
    int use_persistent_upload, void *persistent_mapped_ptr,
    int use_map_range_upload, int use_map_buffer_upload);

void db_gl_upload_buffer(const void *source, size_t bytes,
                         int use_persistent_upload, void *persistent_mapped_ptr,
                         int use_map_range_upload, int use_map_buffer_upload);
void db_gl_unmap_current_array_buffer(void);
size_t db_gl_collect_row_upload_ranges(
    uint32_t row_unit_width, uint32_t row_count_total, size_t unit_stride_bytes,
    const db_dirty_row_range_t *dirty_ranges, size_t dirty_count,
    db_dirty_row_range_t *out_rows, db_gl_upload_range_t *out_ranges,
    size_t out_capacity);
size_t db_gl_collect_span_upload_ranges(
    uint32_t row_unit_width, size_t dst_unit_stride_bytes,
    size_t src_unit_stride_bytes, const db_snake_col_span_t *spans,
    size_t span_count, db_gl_upload_range_t *out_ranges, size_t out_capacity);
size_t
db_gl_collect_damage_upload_ranges(const db_gl_damage_upload_plan_t *plan,
                                   db_gl_upload_range_t *out_ranges,
                                   size_t out_capacity);
size_t
db_gl_collect_pattern_upload_ranges(const db_gl_pattern_upload_collect_t *ctx,
                                    db_gl_upload_range_t *out_ranges,
                                    size_t out_capacity);
size_t db_gl_for_each_upload_row_span(const char *backend_name,
                                      uint32_t row_unit_width,
                                      const db_gl_upload_range_t *ranges,
                                      size_t range_count,
                                      db_gl_upload_row_span_apply_fn_t apply_fn,
                                      void *user_data);

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
