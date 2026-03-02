#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "renderer_benchmark_common.h"
#include "renderer_snake_common.h"
#include "renderer_snake_shape_common.h"

#define DB_GL_PROBE_PREFIX_BYTES 64U
#define DB_GL_MAP_RANGE_PROBE_XOR_SEED 0xA5U
#define DB_GL_CAPABILITY_MODE_MAX 128U
#define DB_GL_CAP_DRAW_FF_RECT_FILL "ff_rect_fill"
#define DB_GL_CAP_DRAW_HISTORY_DIRTY "history_dirty_draw"
#define DB_GL_CAP_DRAW_TILES_FULL "tiles_full_draw"
#define DB_GL_CAP_UPLOAD_CLIENT_ARRAY "upload=client_arrays"
#define DB_GL_CAP_UPLOAD_NONE "upload=none"
#define DB_GL_CAP_UPLOAD_VBO "upload=vbo"
#define DB_GL_CAP_UPLOAD_VBO_MAP_BUFFER "upload=vbo_map_buffer"
#define DB_GL_CAP_UPLOAD_VBO_MAP_RANGE "upload=vbo_map_range"
#define DB_GL_CAP_UPLOAD_VBO_PERSISTENT "upload=vbo_persistent"
#define DB_GL_CAP_MODE_OPENGL_SHADER_HISTORY_DIRTY_DRAW                        \
    "opengl_shader_history_dirty_draw"
#define DB_GL_CAP_MODE_OPENGL_SHADER_VBO "opengl_shader_vbo"
#define DB_GL_CAP_MODE_OPENGL_SHADER_VBO_MAP_BUFFER                            \
    "opengl_shader_vbo_map_buffer"
#define DB_GL_CAP_MODE_OPENGL_SHADER_VBO_MAP_RANGE "opengl_shader_vbo_map_range"
#define DB_GL_CAP_MODE_OPENGL_SHADER_VBO_PERSISTENT                            \
    "opengl_shader_vbo_persistent"

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

enum {
    DB_GL_QUAD_V0_X = 0,
    DB_GL_QUAD_V0_Y = 1,
    DB_GL_QUAD_V1_X = 2,
    DB_GL_QUAD_V1_Y = 3,
    DB_GL_QUAD_V2_X = 4,
    DB_GL_QUAD_V2_Y = 5,
    DB_GL_QUAD_V3_X = 6,
    DB_GL_QUAD_V3_Y = 7,
};

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
int db_gl_runtime_supports_texture_float(const char *version_text,
                                         const char *exts);
int db_gl_runtime_supports_vbo(const char *version_text, const char *exts);

void db_gl_clear_errors(db_gl_get_error_fn_t get_error);
size_t db_gl_probe_size(size_t bytes);
void db_gl_fill_probe_pattern(uint8_t *pattern, size_t count);
const char *
db_gl_cap_upload_mode_from_probe(int has_vbo,
                                 const db_gl_upload_probe_result_t *upload);
const char *db_gl_cap_mode_gl3_shader(const db_gl_upload_probe_result_t *upload,
                                      int uses_history_texture);
void db_gl_capability_mode_compose(char *output, size_t output_size,
                                   const char *draw_mode,
                                   const char *upload_mode,
                                   int backbuffer_replay);

int db_gl_vbo_bind(unsigned int buffer);
int db_gl_bind_array_buffer_cached(unsigned int buffer,
                                   unsigned int *cached_buffer);
int db_gl_vbo_create_or_zero(unsigned int *out_buffer);
void db_gl_vbo_delete_if_valid(unsigned int buffer);
int db_gl_vbo_init_data(size_t bytes, const void *data, unsigned int usage);
int db_gl_vbo_init_static_data(size_t bytes, const void *data);
int db_gl_context_supports_pbo_upload(void);
int db_gl_context_supports_vbo(void);
void db_gl_quad_init(float *verts);
void db_gl_set_viewport_px(int width_px, int height_px);
unsigned int db_gl_pbo_create_or_zero(void);
void db_gl_pbo_delete_if_valid(unsigned int pbo);
void db_gl_pbo_unbind_unpack(void);
int db_gl_texture_allocate_rgba(unsigned int texture, int width, int height,
                                unsigned int internal_format,
                                const void *pixels);
int db_gl_texture_create_rgba(unsigned int *out_texture, int width, int height,
                              unsigned int internal_format, const void *pixels);
int db_gl_texture_allocate_rgba8(unsigned int texture, int width, int height,
                                 const void *pixels);
int db_gl_texture_create_rgba8(unsigned int *out_texture, int width, int height,
                               const void *pixels);
int db_gl_texture_allocate_rgba32f(unsigned int texture, int width, int height,
                                   const void *pixels);
int db_gl_texture_create_rgba32f(unsigned int *out_texture, int width,
                                 int height, const void *pixels);
void db_gl_texture_delete_if_valid(unsigned int *texture);
void db_gl_texture_bind_2d(unsigned int texture);
void db_gl_texture_sub_image_2d_rgba(int x_px, int y_px, int width, int height,
                                     const void *pixels);
void db_gl_texture_sub_image_2d_rgba32f(int x_px, int y_px, int width,
                                        int height, const void *pixels);
void db_gl_clear_color_rgba(float red, float green, float blue, float alpha);
void db_gl_clear_color_rgb(float red, float green, float blue);
void db_gl_clear_color_buffer(void);
void db_gl_set_blend_enabled(int enabled);
void db_gl_set_cull_face_enabled(int enabled);
void db_gl_set_depth_test_enabled(int enabled);
void db_gl_set_scissor_enabled(int enabled);
void db_gl_set_scissor_rect(int x_px, int y_px, int width, int height);
void db_gl_set_pack_alignment_1(void);
void db_gl_read_pixels_rgba8(int x_px, int y_px, int width, int height,
                             void *pixels);
void db_gl_set_texture_2d_enabled(int enabled);
void db_gl_set_client_state_vertex_array_enabled(int enabled);
void db_gl_set_client_state_color_array_enabled(int enabled);
void db_gl_set_client_state_texcoord_array_enabled(int enabled);
void db_gl_set_vertex_pointer_2f(int stride_bytes, const void *pointer);
void db_gl_set_color_pointer_f(int component_count, int stride_bytes,
                               const void *pointer);
void db_gl_set_texcoord_pointer_2f(int stride_bytes, const void *pointer);
void db_gl_draw_arrays_triangles(int first, int count);
void db_gl_draw_arrays_triangle_strip(int first, int count);
void db_gl_active_texture(unsigned int texture_unit);
void db_gl_attach_shader(unsigned int program, unsigned int shader);
void db_gl_bind_framebuffer(unsigned int target, unsigned int framebuffer);
void db_gl_bind_vertex_array(unsigned int vao);
void db_gl_blit_framebuffer(int src_x0, int src_y0, int src_x1, int src_y1,
                            int dst_x0, int dst_y0, int dst_x1, int dst_y1,
                            unsigned int mask, unsigned int filter);
unsigned int db_gl_check_framebuffer_status(unsigned int target);
void db_gl_compile_shader(unsigned int shader);
unsigned int db_gl_create_program(void);
unsigned int db_gl_create_shader(unsigned int shader_type);
void db_gl_delete_framebuffers(int count, const unsigned int *framebuffers);
void db_gl_delete_program(unsigned int program);
void db_gl_delete_shader(unsigned int shader);
void db_gl_delete_vertex_arrays(int count, const unsigned int *arrays);
void db_gl_enable_vertex_attrib_array(unsigned int index);
void db_gl_framebuffer_texture_2d(unsigned int target, unsigned int attachment,
                                  unsigned int textarget, unsigned int texture,
                                  int level);
void db_gl_gen_framebuffers(int count, unsigned int *framebuffers);
void db_gl_gen_vertex_arrays(int count, unsigned int *arrays);
void db_gl_get_integerv(unsigned int pname, int *value);
void db_gl_get_program_info_log(unsigned int program, int buf_size, int *length,
                                char *log);
void db_gl_get_program_iv(unsigned int program, unsigned int pname, int *value);
void db_gl_get_shader_info_log(unsigned int shader, int buf_size, int *length,
                               char *log);
void db_gl_get_shader_iv(unsigned int shader, unsigned int pname, int *value);
int db_gl_get_uniform_location(unsigned int program, const char *name);
void db_gl_link_program(unsigned int program);
void db_gl_shader_source_single(unsigned int shader, const char *source);
void db_gl_uniform1i(int location, int value);
void db_gl_uniform1ui(int location, unsigned int value);
void db_gl_uniform3f(int location, float x_val, float y_val, float z_val);
void db_gl_use_program(unsigned int program);
void db_gl_vertex_attrib_pointer_2f(unsigned int index, int stride_bytes,
                                    size_t byte_offset);
void db_gl_vertex_attrib_pointer_3f(unsigned int index, int stride_bytes,
                                    size_t byte_offset);
unsigned int db_gl_get_error_code(void);
const char *db_gl_get_version_string(void);
const char *db_gl_get_renderer_string(void);
const char *db_gl_get_extensions_string(void);
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
int db_gl_row_range_to_scissor_rect(uint32_t row_start, uint32_t row_count,
                                    uint32_t total_rows, int viewport_width,
                                    int viewport_height, int *x_out, int *y_out,
                                    int *width_out, int *height_out);
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

int db_init_grid_vertices_common(db_gl_vertex_init_t *out_state,
                                 db_pattern_t pattern, size_t vertex_stride);

// NOLINTBEGIN(performance-no-int-to-ptr)
static inline const void *db_gl_vbo_offset_ptr(size_t byte_offset) {
    return (const void *)(uintptr_t)byte_offset;
}
// NOLINTEND(performance-no-int-to-ptr)

static inline int db_gl_draw_vertex_count_i32(const char *backend_name,
                                              uint32_t draw_vertex_count) {
    return db_checked_u32_to_i32(backend_name, "draw_vertex_count",
                                 draw_vertex_count);
}

int db_init_vertices_for_pattern_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    db_pattern_t pattern, size_t vertex_stride);
int db_init_vertices_for_runtime_common_with_stride(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_benchmark_runtime_init_t *runtime_state, size_t vertex_stride);
void db_update_grid_vertices_for_bands_rgb_stride(
    float *verts, uint32_t cols, uint32_t rows, uint32_t band_count,
    uint32_t frame_index, size_t stride_floats, size_t color_offset_floats);

#endif
