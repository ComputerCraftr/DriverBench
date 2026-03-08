#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "renderer_benchmark_common.h"
#include "renderer_history_common.h"
#include "renderer_snake_common.h"

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
#define DB_GL_HISTORY_TARGET_COUNT 2U
#define DB_GL_COLOR_COMPONENT_COUNT DB_VERTEX_COLOR_FLOAT_COUNT
#define DB_GL_COLOR_R_INDEX 0U
#define DB_GL_COLOR_G_INDEX 1U
#define DB_GL_COLOR_B_INDEX 2U
#define DB_GL_COLOR_R_OFFSET DB_VERTEX_POSITION_FLOAT_COUNT
#define DB_GL_COLOR_G_OFFSET                                                   \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_GL_COLOR_G_INDEX)
#define DB_GL_COLOR_B_OFFSET                                                   \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_GL_COLOR_B_INDEX)
#define DB_GL_COLOR_A_OFFSET                                                   \
    (DB_VERTEX_POSITION_FLOAT_COUNT + DB_VERTEX_COLOR_FLOAT_COUNT)

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

db_gl_upload_range_t db_gl_upload_full_range(size_t total_bytes);

typedef struct {
    uint32_t row_unit_width;
    uint32_t row_count_total;
    size_t unit_stride_bytes;
    size_t total_bytes;
    int force_full_upload;
    const db_damage_block_t *dirty_blocks;
    size_t dirty_block_count;
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
    const db_history_snake_scratch_t *snake_scratch;
    const db_damage_block_t *damage_blocks;
    size_t damage_block_count;
    int use_damage_blocks;
} db_gl_pattern_upload_collect_t;

typedef struct {
    db_gl_upload_range_t range;
    db_damage_block_t block;
} db_gl_upload_block_span_t;

typedef int (*db_gl_upload_row_segment_apply_fn_t)(uint32_t row,
                                                   uint32_t col_start,
                                                   uint32_t col_end,
                                                   void *user_data);

typedef void (*db_gl_upload_block_span_apply_fn_t)(
    const db_gl_upload_block_span_t *span, void *user_data);

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

typedef struct {
    int last_viewport_w;
    int last_viewport_h;
} db_gl_viewport_cache_t;

typedef struct {
    unsigned int vbo;
    unsigned int bound_array_buffer;
    size_t vbo_bytes;
} db_gl_buffer_cache_t;

typedef struct {
    float *scratch_vertices;
    size_t scratch_float_capacity;
    size_t vbo_offset_bytes;
    size_t vbo_capacity_bytes;
    size_t first_vertex;
} db_gl_compact_vbo_state_t;

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

int db_gl_extensions_advertise_buffer_storage(const char *version_text,
                                              const char *exts);
int db_gl_extensions_advertise_map_buffer(const char *version_text,
                                          const char *exts);
int db_gl_extensions_advertise_map_buffer_range(const char *version_text,
                                                const char *exts);
int db_gl_extensions_advertise_pbo(const char *version_text, const char *exts);
int db_gl_extensions_advertise_texture_float(const char *version_text,
                                             const char *exts);
int db_gl_extensions_advertise_vbo(const char *version_text, const char *exts);

void db_gl_clear_errors(db_gl_get_error_fn_t get_error);
size_t db_gl_upload_probe_size_bytes(size_t bytes);
void db_gl_upload_probe_fill_pattern(uint8_t *pattern, size_t count);
const char *db_gl_capability_mode_upload_from_probe(
    int has_vbo, const db_gl_upload_probe_result_t *upload);
const char *
db_gl_capability_mode_gl3_shader(const db_gl_upload_probe_result_t *upload,
                                 int uses_history_texture);
const char *db_gl_capability_mode_draw_select(int uses_ff_rect_draw_mode,
                                              int uses_history_draw,
                                              int use_shader_history_draw_name);
const char *db_gl_capability_mode_upload_select(int suppress_upload,
                                                const char *upload_mode);
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
int db_gl_context_has_pbo_upload_procs(void);
int db_gl_context_probe_texture_float_support(void);
int db_gl_context_advertises_vbo(void);
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
int db_gl_texture_allocate_rgba16f(unsigned int texture, int width, int height,
                                   const void *pixels);
int db_gl_texture_create_rgba16f(unsigned int *out_texture, int width,
                                 int height, const void *pixels);
void db_gl_texture_delete_if_valid(unsigned int *texture);
void db_gl_texture_bind_2d(unsigned int texture);
void db_gl_texture_sub_image_2d_rgba(int x_px, int y_px, int width, int height,
                                     const void *pixels);
void db_gl_texture_sub_image_2d_rgba16f(int x_px, int y_px, int width,
                                        int height, const void *pixels);
void db_gl_clear_color_rgba(float red, float green, float blue, float alpha);
void db_gl_clear_color_rgb(float red, float green, float blue);
void db_gl_clear_color_buffer(void);
void db_gl_set_blend_enabled(int enabled);
void db_gl_set_cull_face_enabled(int enabled);
void db_gl_set_depth_test_enabled(int enabled);
void db_gl_set_pack_alignment_1(void);
void db_gl_read_pixels_rgba8(int x_px, int y_px, int width, int height,
                             void *pixels);
void db_gl_read_pixels_rgba16f(int x_px, int y_px, int width, int height,
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
void db_gl_context_probe_upload_capabilities(size_t bytes,
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
size_t db_gl_compact_vbo_total_bytes(size_t base_vbo_bytes);
void db_gl_compact_vbo_init_or_fail(const char *backend_name,
                                    db_gl_compact_vbo_state_t *compact,
                                    size_t base_vbo_bytes,
                                    size_t vertex_stride);
void db_gl_compact_vbo_free(db_gl_compact_vbo_state_t *compact);
int db_gl_compact_copy_ranges_from_vertices(
    const db_gl_upload_range_t *ranges, size_t range_count,
    const float *source_vertices, size_t upload_bytes, size_t vertex_stride,
    db_gl_compact_vbo_state_t *compact, size_t *out_compact_bytes);
int db_gl_upload_compact_prepared(const db_gl_compact_vbo_state_t *compact,
                                  const db_gl_upload_probe_result_t *upload,
                                  size_t compact_bytes);
void db_gl_upload_vbo_damage_ranges(const float *vertices, size_t upload_bytes,
                                    const db_gl_upload_probe_result_t *upload,
                                    const db_gl_upload_range_t *range_storage,
                                    size_t upload_range_count);
void db_gl_draw_dirty_ranges_common(const char *backend_name,
                                    size_t vertex_stride,
                                    uint32_t draw_vertex_count,
                                    const db_gl_upload_range_t *ranges,
                                    size_t range_count);
void db_gl_unmap_current_array_buffer(void);
size_t db_gl_collect_block_upload_ranges(
    uint32_t row_unit_width, uint32_t row_count_total, size_t unit_stride_bytes,
    const db_damage_block_t *dirty_blocks, size_t dirty_block_count,
    db_damage_block_t *out_blocks, db_gl_upload_range_t *out_ranges,
    size_t out_capacity);
size_t
db_gl_collect_pattern_upload_ranges(const db_gl_pattern_upload_collect_t *ctx,
                                    db_gl_upload_range_t *out_ranges,
                                    size_t out_capacity);
int db_gl_for_each_upload_row_segment(
    uint32_t row_unit_width, uint32_t row_count_total,
    size_t src_unit_stride_bytes, size_t dst_unit_stride_bytes,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_gl_upload_row_segment_apply_fn_t apply, void *user_data);
int db_gl_collect_snake_compact_blocks_from_upload_ranges(
    uint32_t row_unit_width, uint32_t row_count_total,
    size_t src_unit_stride_bytes, size_t dst_unit_stride_bytes,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_snake_get_color_bits_cb_t get_color_bits, void *get_color_user_data,
    db_snake_compact_block_t *out_blocks, size_t out_capacity,
    size_t *out_count);
size_t db_gl_for_each_upload_block_span(
    const char *backend_name, uint32_t row_unit_width,
    const db_gl_upload_range_t *ranges, size_t range_count,
    db_gl_upload_block_span_apply_fn_t apply_fn, void *user_data);
size_t db_gl_copy_upload_ranges(const db_gl_upload_range_t *source_ranges,
                                size_t source_count,
                                db_gl_upload_range_t *out_ranges,
                                size_t out_capacity);

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
void db_gl_preload_upload_proc_table(void);

#endif
