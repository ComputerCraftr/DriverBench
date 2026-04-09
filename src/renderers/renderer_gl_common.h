#ifndef DRIVERBENCH_RENDERER_GL_COMMON_H
#define DRIVERBENCH_RENDERER_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../core/db_core.h"
#include "renderer_benchmark_gradient.h"

#define DB_GL_PROBE_PREFIX_BYTES 64U
#define DB_GL_MAP_RANGE_PROBE_XOR_SEED 0xA5U
#define DB_GL_CAPABILITY_MODE_MAX 128U
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
typedef db_gl_generic_proc_t (*db_gl_proc_resolver_fn_t)(const char *name);

typedef struct {
    int use_map_buffer_upload;
    int use_map_range_upload;
    int use_persistent_upload;
    void *persistent_mapped_ptr;
} db_gl_upload_probe_result_t;

typedef enum {
    DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER = 0,
    DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER = 1,
} db_gl_upload_target_t;

typedef enum {
    DB_GL_STREAM_UPLOAD_STORAGE_CLIENT = 0,
    DB_GL_STREAM_UPLOAD_STORAGE_BUFFER_OBJECT = 1,
} db_gl_stream_upload_storage_t;

typedef enum {
    DB_GL_STREAM_UPLOAD_MODE_SUB_DATA = 0,
    DB_GL_STREAM_UPLOAD_MODE_MAP_BUFFER = 1,
    DB_GL_STREAM_UPLOAD_MODE_MAP_RANGE = 2,
    DB_GL_STREAM_UPLOAD_MODE_PERSISTENT = 3,
} db_gl_stream_upload_mode_t;

typedef enum {
    DB_GL_UPLOAD_ROLE_GEOMETRY = 0,
    DB_GL_UPLOAD_ROLE_PRESENT_FULL = 1,
    DB_GL_UPLOAD_ROLE_PRESENT_PARTIAL = 2,
    DB_GL_UPLOAD_ROLE_PRESENT_SLOT_REPAIR = 3,
} db_gl_upload_role_t;

typedef struct {
    db_gl_upload_target_t target;
    db_gl_stream_upload_storage_t requested_storage;
    db_gl_stream_upload_storage_t supported_storage;
    db_gl_stream_upload_storage_t effective_storage;
    db_gl_stream_upload_mode_t requested_mode;
    db_gl_stream_upload_mode_t supported_mode;
    db_gl_stream_upload_mode_t effective_mode;
    int sync_supported;
    int sync_enabled;
} db_gl_stream_upload_capability_t;

typedef struct {
    db_gl_upload_target_t target;
    db_gl_stream_upload_capability_t capability;
    unsigned int buffer;
    void *client_storage;
    void *persistent_mapping;
    void *in_flight_sync;
    size_t reserved_bytes;
    size_t active_bytes;
    int owns_storage;
    int mapping_active;
} db_gl_upload_stream_t;

typedef struct {
    db_gl_upload_probe_result_t probe;
    db_gl_stream_upload_capability_t capability;
    unsigned int buffer;
    int uses_client_arrays;
} db_gl_geometry_stream_init_result_t;

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

typedef enum {
    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA8 = 0,
    DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F = 1,
} db_gl_shadow_present_texture_format_t;

typedef enum {
    DB_GL_RUNTIME_DRAW_FF_RECT_FILL = 0,
    DB_GL_RUNTIME_DRAW_DIRTY_REPLAY = 1,
    DB_GL_RUNTIME_DRAW_FULL_PRESENT = 2,
    DB_GL_RUNTIME_DRAW_SHADOW_FALLBACK = 3,
} db_gl_runtime_draw_mode_t;

typedef enum {
    DB_GL_SHADOW_PRESENT_REPLACE_CONTENTS = 0,
    DB_GL_SHADOW_PRESENT_PRESERVE_SINGLE_SOURCE = 1,
    DB_GL_SHADOW_PRESENT_PRESERVE_RING_COHERENT = 2,
} db_gl_shadow_present_preserve_mode_t;

typedef enum {
    DB_GL_BACKBUFFER_DRAW_DIRTY = 0,
    DB_GL_BACKBUFFER_DRAW_FULL = 1,
} db_gl_backbuffer_draw_mode_t;

typedef enum {
    DB_GL_PRESENT_BUFFER_MODE_AUTO = 0,
    DB_GL_PRESENT_BUFFER_MODE_REPLACE = 1,
    DB_GL_PRESENT_BUFFER_MODE_SINGLE_SOURCE = 2,
    DB_GL_PRESENT_BUFFER_MODE_RING = 3,
} db_gl_present_buffer_mode_t;

typedef struct {
    db_gl_backbuffer_draw_mode_t requested_backbuffer_draw_mode;
    db_gl_present_buffer_mode_t requested_present_buffer_mode;
    int prefer_ring_for_preserved_draw;
    uint32_t preserved_framebuffer_count;
    db_gl_stream_upload_capability_t present_upload;
} db_gl_present_mode_request_t;

typedef struct {
    int valid;
    int downgraded;
    db_gl_backbuffer_draw_mode_t effective_backbuffer_draw_mode;
    db_gl_shadow_present_preserve_mode_t requested_preserve_mode;
    db_gl_shadow_present_preserve_mode_t effective_preserve_mode;
    db_gl_stream_upload_capability_t effective_present_upload;
    const char *reason;
} db_gl_present_mode_resolution_t;

typedef struct {
    db_gl_runtime_draw_mode_t draw_mode;
    int geometry_upload_enabled;
    int geometry_uses_client_arrays;
    db_gl_stream_upload_capability_t geometry_upload;
    db_gl_stream_upload_capability_t full_present_upload;
    db_gl_stream_upload_capability_t partial_present_upload;
    db_gl_shadow_present_preserve_mode_t preserve_mode;
    int backbuffer_replay;
} db_gl_runtime_mode_desc_t;

typedef struct {
    uint64_t full_present_frames;
    uint64_t dirty_geometry_frames;
    uint64_t shadow_fallback_frames;
    uint64_t replay_only_frames;
} db_renderer_draw_path_stats_t;

enum {
    DB_GL_SHADOW_PRESENT_UPLOAD_RING_SLOTS = 2U,
};

typedef struct {
    db_gl_upload_stream_t stream;
    int slot_valid;
    int slot_matches_shadow;
    int slot_matches_presented_texture;
} db_gl_shadow_present_upload_slot_t;

typedef struct {
    int initialized;
    int backing_valid;
    int texture_valid;
    int texture_needs_full_upload;
    int runtime_supports_unpack_row_length_upload;
    int runtime_supports_hdr_present;
    int uses_exact_size_texture;
    db_gl_stream_upload_capability_t requested_full_upload_capability;
    db_gl_stream_upload_capability_t effective_full_upload_capability;
    db_gl_stream_upload_capability_t requested_partial_upload_capability;
    db_gl_stream_upload_capability_t effective_partial_upload_capability;
    db_gl_shadow_present_preserve_mode_t requested_preserve_mode;
    db_gl_shadow_present_preserve_mode_t preserve_mode;
    uint32_t slot_count;
    uint32_t write_slot_index;
    uint32_t present_slot_index;
    db_gl_shadow_present_texture_format_t selected_texture_format;
    db_gl_upload_stream_t unpack_stream;
    db_gl_shadow_present_upload_slot_t
        upload_slots[DB_GL_SHADOW_PRESENT_UPLOAD_RING_SLOTS];
    unsigned int texture;
    uint32_t content_width;
    uint32_t content_height;
    uint32_t texture_width;
    uint32_t texture_height;
    float texcoords[8];
    float vertices[8];
    float colors[DB_RECT_VERTEX_COUNT * 4U];
} db_gl_shadow_present_state_t;

typedef struct {
    db_benchmark_pixel_surface_t pixel_surface;
    size_t total_bytes;
    int uses_pbo;
    int preserve_contents;
    int slot_surface_valid;
    uint32_t slot_index;
} db_gl_shadow_present_full_upload_target_t;

typedef struct {
    db_gl_shadow_present_state_t *state;
    const char *backend;
    uint32_t pixel_width;
    uint32_t pixel_height;
    const void *selected_pixels;
    const db_damage_block_t *damage_blocks;
    size_t damage_block_count;
} db_gl_shadow_present_frame_t;

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

db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_renderer(
    int uses_ff_rect_draw_mode, int uses_history_draw, int has_vbo,
    const db_gl_upload_probe_result_t *upload, int backbuffer_replay);
db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_present(
    const db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_preserve_mode_t preserve_mode);
const char *db_gl_runtime_draw_mode_name(db_gl_runtime_draw_mode_t mode);
const char *db_gl_shadow_present_preserve_mode_name(
    db_gl_shadow_present_preserve_mode_t preserve_mode);
const char *
db_gl_stream_upload_storage_name(db_gl_stream_upload_storage_t storage);
const char *db_gl_stream_upload_mode_name(db_gl_stream_upload_mode_t mode);
const char *
db_gl_stream_upload_name(const db_gl_stream_upload_capability_t *capability,
                         int client_arrays, int upload_enabled);
const char *db_gl_present_buffer_mode_name(db_gl_present_buffer_mode_t mode);
int db_gl_present_buffer_mode_parse(const char *text,
                                    db_gl_present_buffer_mode_t *out_mode);
db_gl_stream_upload_capability_t db_gl_stream_upload_capability_from_probe(
    db_gl_upload_target_t target, const db_gl_upload_probe_result_t *probe,
    int enable_sync);
db_gl_stream_upload_capability_t db_gl_stream_upload_capability_for_role(
    db_gl_upload_target_t target, const db_gl_upload_probe_result_t *probe,
    int enable_sync, db_gl_upload_role_t role,
    db_gl_shadow_present_texture_format_t texture_format);
void db_gl_stream_upload_force_client_fallback(
    db_gl_stream_upload_capability_t *capability, int disable_sync);
int db_gl_stream_upload_uses_buffer_object(
    const db_gl_stream_upload_capability_t *capability);
int db_gl_stream_upload_uses_map_range(
    const db_gl_stream_upload_capability_t *capability);
int db_gl_stream_upload_uses_map_buffer(
    const db_gl_stream_upload_capability_t *capability);
int db_gl_stream_upload_uses_persistent(
    const db_gl_stream_upload_capability_t *capability);
int db_gl_stream_upload_sync_enabled(
    const db_gl_stream_upload_capability_t *capability);
void db_gl_stream_upload_disable_persistent_for_target(
    db_gl_stream_upload_capability_t *capability, db_gl_upload_target_t target);
int db_gl_present_mode_validate_request(
    int is_cpu_api, int is_glfw_window_display, int is_gl1_renderer,
    db_gl_backbuffer_draw_mode_t backbuffer_draw_mode,
    db_gl_present_buffer_mode_t present_buffer_mode, const char **out_reason);
void db_gl_present_mode_resolve(const db_gl_present_mode_request_t *request,
                                db_gl_present_mode_resolution_t *out);
void db_gl_runtime_mode_format_renderer(char *output, size_t output_size,
                                        const db_gl_runtime_mode_desc_t *mode);
void db_gl_runtime_mode_format_present(char *output, size_t output_size,
                                       const db_gl_runtime_mode_desc_t *mode);

int db_gl_context_probe_texture_float_support(void);
int db_gl_context_probe_texture_float_present_support(void);
void db_gl_shadow_present_init_runtime(db_gl_shadow_present_state_t *state,
                                       int prefer_unpack_pbo,
                                       int selected_content_uses_rgba16f);
void db_gl_shadow_present_log_decision(
    const char *backend, const char *present_name, int content_uses_rgba16f,
    int hdr_explicit_requested, const db_gl_shadow_present_state_t *state);
void db_gl_shadow_present_set_preserve_mode(
    db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_preserve_mode_t preserve_mode);
void db_gl_shadow_present_invalidate_presented_texture(
    db_gl_shadow_present_state_t *state, int full_upload_required);
void db_gl_shadow_present_note_shadow_change(
    db_gl_shadow_present_state_t *state, int full_upload_required);
void db_gl_shadow_present_shutdown(db_gl_shadow_present_state_t *state);
void db_gl_shadow_present_prepare_texture(db_gl_shadow_present_state_t *state,
                                          const char *backend,
                                          uint32_t pixel_width,
                                          uint32_t pixel_height);
void db_gl_shadow_present_upload_damage_blocks(
    db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *blocks, size_t block_count);
int db_gl_shadow_present_begin_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_contents,
    db_gl_shadow_present_full_upload_target_t *target);
int db_gl_shadow_present_begin_full_upload_target_slot_offset(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_contents,
    uint32_t slot_offset, db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_repair_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target,
    const db_benchmark_pixel_surface_t *source_surface,
    const db_damage_block_t *damage_blocks, size_t damage_block_count);
void db_gl_shadow_present_sync_preserved_slots(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_benchmark_pixel_surface_t *source_surface,
    const db_damage_block_t *damage_blocks, size_t damage_block_count,
    const db_gl_shadow_present_full_upload_target_t *current_target);
void db_gl_shadow_present_finish_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_present_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_present_replace_pixels(
    db_gl_shadow_present_state_t *state, const char *backend,
    const void *selected_pixels, uint32_t pixel_width, uint32_t pixel_height,
    const db_damage_block_t *damage_blocks, size_t damage_block_count);
void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame);
void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height);
void db_gl_set_viewport_px(int width_px, int height_px);
int db_gl_texture_allocate_rgba(unsigned int texture, int width, int height,
                                unsigned int internal_format,
                                const void *pixels);
int db_gl_texture_create_rgba(unsigned int *out_texture, int width, int height,
                              unsigned int internal_format, const void *pixels);
int db_gl_texture_create_rgba8(unsigned int *out_texture, int width, int height,
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
void db_gl_clear_color_buffer(void);
void db_gl_set_blend_enabled(int enabled);
void db_gl_set_cull_face_enabled(int enabled);
void db_gl_set_depth_test_enabled(int enabled);
void db_gl_set_dither_enabled(int enabled);
void db_gl_set_pack_alignment_1(void);
void db_gl_set_unpack_alignment_1(void);
void db_gl_prepare_textured_present_state(void);
void db_gl_finish_textured_present_state(void);
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
void db_gl_uniform3fv3(int location, const float *xyz);
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
void db_gl_context_probe_stream_upload_capabilities(
    db_gl_upload_target_t target, size_t bytes,
    db_gl_upload_probe_result_t *out);
void db_gl_upload_stream_init(db_gl_upload_stream_t *stream,
                              db_gl_upload_target_t target,
                              db_gl_stream_upload_capability_t capability,
                              unsigned int buffer, int owns_storage);
int db_gl_geometry_stream_init(db_gl_upload_stream_t *stream,
                               db_gl_geometry_stream_init_result_t *result,
                               const char *backend, size_t storage_bytes,
                               const float *probe_seed_vertices,
                               const void *initial_seed_data,
                               size_t initial_seed_bytes,
                               int allow_client_array_fallback);
int db_gl_upload_stream_prepare_storage(db_gl_upload_stream_t *stream,
                                        const char *backend,
                                        size_t required_bytes);
void *db_gl_upload_stream_begin_write(db_gl_upload_stream_t *stream,
                                      size_t offset_bytes, size_t size_bytes);
void db_gl_upload_stream_end_write(db_gl_upload_stream_t *stream);
int db_gl_upload_stream_write(db_gl_upload_stream_t *stream,
                              const char *backend, const void *source,
                              size_t total_bytes, size_t dst_offset_bytes,
                              size_t size_bytes);
const void *db_gl_upload_stream_pointer(const db_gl_upload_stream_t *stream,
                                        size_t byte_offset);
void db_gl_upload_stream_wait(db_gl_upload_stream_t *stream);
void db_gl_upload_stream_record_sync(db_gl_upload_stream_t *stream);
void db_gl_upload_stream_shutdown(db_gl_upload_stream_t *stream);
size_t db_gl_compact_vbo_total_bytes(size_t base_vbo_bytes);
void db_gl_compact_vbo_init_or_fail(const char *backend_name,
                                    db_gl_compact_vbo_state_t *compact,
                                    size_t base_vbo_bytes,
                                    size_t vertex_stride);
void db_gl_compact_vbo_init_standalone_or_fail(
    const char *backend_name, db_gl_compact_vbo_state_t *compact,
    size_t compact_vbo_bytes, size_t vertex_stride);
void db_gl_compact_vbo_free(db_gl_compact_vbo_state_t *compact);
int db_gl_upload_compact_prepared(const db_gl_compact_vbo_state_t *compact,
                                  const db_gl_upload_probe_result_t *upload,
                                  size_t compact_bytes);

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
