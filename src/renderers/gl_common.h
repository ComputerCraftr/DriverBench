#ifndef DRIVERBENCH_GL_COMMON_H
#define DRIVERBENCH_GL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "core/db_format_contract.h"
#include "core/db_poll_policy.h"
#include "core/db_render_result.h"
#include "core/db_render_types.h"
#include "core/db_renderer_support.h"

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

typedef enum {
    DB_GL_UPLOAD_TARGET_VBO_ARRAY_BUFFER = 0,
    DB_GL_UPLOAD_TARGET_PBO_UNPACK_BUFFER = 1,
    DB_GL_UPLOAD_TARGET_PBO_PACK_BUFFER = 2,
} db_gl_upload_target_t;

typedef enum {
    DB_GL_UPLOAD_FAILURE_NONE = 0,
    DB_GL_UPLOAD_FAILURE_PROBE_REJECTED = 1,
    DB_GL_UPLOAD_FAILURE_STORAGE_ALLOC = 2,
    DB_GL_UPLOAD_FAILURE_MAP_NULL = 3,
    DB_GL_UPLOAD_FAILURE_UNMAP_FAILED = 4,
    DB_GL_UPLOAD_FAILURE_API_UNAVAILABLE = 5,
    DB_GL_UPLOAD_FAILURE_TARGET_ACQUIRE = 6,
    DB_GL_UPLOAD_FAILURE_CANARY_MISMATCH = 7,
} db_gl_upload_failure_reason_t;

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
    DB_GL_UPLOAD_PROBE_STEP_NONE = 0,
    DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE,
    DB_GL_UPLOAD_PROBE_STEP_MAP_RANGE_UNMAP,
    DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER,
    DB_GL_UPLOAD_PROBE_STEP_MAP_BUFFER_UNMAP,
    DB_GL_UPLOAD_PROBE_STEP_CANARY_VERIFY,
    DB_GL_UPLOAD_PROBE_STEP_RESTORE,
} db_gl_upload_probe_step_t;

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
    size_t staging_storage_bytes;
    size_t alignment_bytes;
    int partial_updates_supported;
    int explicit_orphaning_required;
    int mapping_validated;
    int canary_validated;
    int mapping_probe_attempted;
    db_gl_upload_probe_step_t mapping_probe_failure_step;
    uint32_t mapping_probe_gl_error;
    db_gl_upload_failure_reason_t demotion_reason;
} db_gl_stream_upload_capability_t;

typedef struct {
    db_gl_upload_target_t target;
    db_gl_stream_upload_capability_t capability;
    unsigned int buffer;
    void *client_storage;
    void *persistent_mapping;
    void *in_flight_sync;
    size_t buffer_reserved_bytes;
    size_t client_reserved_bytes;
    size_t initialized_storage_bytes;
    size_t hot_path_fixed_capacity_bytes;
    size_t active_bytes;
    int owns_storage;
    int mapping_active;
    uint32_t last_failure_gl_error;
    db_gl_upload_failure_reason_t last_logged_failure_reason;
    db_gl_stream_upload_storage_t last_logged_effective_storage;
    db_gl_stream_upload_mode_t last_logged_effective_mode;
} db_gl_upload_stream_t;

typedef enum {
    DB_GL_DIRTY_GEOMETRY_SOURCE_CURRENT = 0,
    DB_GL_DIRTY_GEOMETRY_SOURCE_HISTORICAL = 1,
    DB_GL_DIRTY_GEOMETRY_SOURCE_ASSEMBLED = 2,
    DB_GL_DIRTY_GEOMETRY_SOURCE_SEED = 3,
    DB_GL_DIRTY_GEOMETRY_SOURCE_FULL_RECOVERY = 4,
} db_gl_dirty_geometry_source_t;

typedef struct {
    uint32_t error_code;
    const char *phase;
    const char *target;
    const char *context;
} db_gl_error_record_t;

enum {
    DB_GL_ERROR_TRACE_CAPACITY = 16U,
    DB_GL_DIRTY_TRACE_DAMAGE_CAPACITY = 128U,
    DB_GL_DIRTY_TRACE_COMPACT_CAPACITY = 512U,
    DB_GL_DIRTY_TRACE_UPLOAD_SPAN_CAPACITY = 128U,
};

typedef struct {
    db_gl_error_record_t records[DB_GL_ERROR_TRACE_CAPACITY];
    size_t count;
} db_gl_error_trace_t;

typedef struct {
    db_damage_block_t block;
    size_t offset_bytes;
    size_t size_bytes;
    const char *source_label;
} db_gl_upload_span_trace_t;

typedef struct {
    db_gl_dirty_geometry_source_t source_kind;
    const db_grid_block_t *logical_damage_blocks;
    size_t logical_damage_block_count;
    size_t emitted_vertex_count;
    size_t emitted_vertex_bytes;
} db_gl_dirty_geometry_payload_t;

typedef struct {
    db_gl_stream_upload_capability_t capability;
} db_gl_geometry_stream_init_result_t;

typedef struct {
    db_gl_stream_upload_capability_t requested_full;
    db_gl_stream_upload_capability_t effective_full;
    db_gl_stream_upload_capability_t requested_partial;
    db_gl_stream_upload_capability_t effective_partial;
} db_gl_present_upload_profile_t;

typedef struct {
    float *vertices;
    size_t vertex_stride;
    uint32_t work_unit_count;
    uint32_t draw_vertex_count;
} db_gl_vertex_init_t;

typedef struct {
    int last_viewport_w;
    int last_viewport_h;
} db_gl_viewport_cache_t;

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
    const db_pixel_surface_t *surface;
    db_gl_shadow_present_texture_format_t format;
    size_t row_stride_bytes;
    size_t total_bytes;
} db_gl_pixel_upload_payload_t;

typedef enum {
    DB_GL_SHADOW_FULL_UPLOAD_TARGET_NONE = 0,
    DB_GL_SHADOW_FULL_UPLOAD_TARGET_MAPPED_PBO = 1,
    DB_GL_SHADOW_FULL_UPLOAD_TARGET_CLIENT_BUFFER_THEN_SUBDATA = 2,
    DB_GL_SHADOW_FULL_UPLOAD_TARGET_DIRECT_CLIENT_TEXTURE_UPLOAD = 3,
} db_gl_shadow_full_upload_target_mode_t;

typedef struct {
    db_gl_pixel_upload_payload_t pixel_payload;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint64_t upload_source_hash;
    uint64_t target_surface_hash;
    uint64_t fallback_source_hash;
    const char *source_label;
    const char *history_source_label;
    const char *target_mode_label;
    const char *upload_mode_label;
    const char *executed_upload_mode_label;
    const char *fallback_mode_label;
    const char *seed_source_label;
    uint32_t slot_index;
    size_t total_bytes;
    int full_upload_attempted;
    int full_upload_executed;
    int seeded_shadow_ring;
    uint32_t required_previous_frames;
    size_t historical_block_count;
    size_t repair_block_count;
    db_gl_upload_span_trace_t
        upload_spans[DB_GL_DIRTY_TRACE_UPLOAD_SPAN_CAPACITY];
    size_t upload_span_count;
    db_gl_error_trace_t error_trace;
} db_gl_shadow_upload_trace_t;

enum {
    DB_GL_MAX_PRESERVED_FRAMEBUFFER_COUNT = 8U,
    DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS = 2U,
    DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT = 2U,
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
    int hdr_output_enabled;
    db_encoded_present_format_t encoded_present_format;
    db_hdr_conversion_implementation_t hdr_conversion;
    int uses_exact_size_texture;
    db_gl_present_upload_profile_t upload_profile;
    db_gl_shadow_present_preserve_mode_t requested_preserve_mode;
    db_gl_shadow_present_preserve_mode_t preserve_mode;
    uint32_t requested_preserved_framebuffer_count;
    uint32_t slot_count;
    uint32_t write_slot_index;
    uint32_t present_slot_index;
    db_gl_shadow_present_texture_format_t selected_texture_format;
    uint32_t *encoded_upload_scratch;
    size_t encoded_upload_scratch_capacity;
    db_gl_upload_stream_t
        unpack_streams[DB_GL_SHADOW_PRESENT_UPLOAD_STREAM_COUNT];
    uint32_t unpack_write_index;
    db_gl_upload_stream_t presentation_quad_stream;
    db_gl_shadow_present_upload_slot_t
        upload_slots[DB_GL_SHADOW_PRESENT_MAX_RING_SLOTS];
    unsigned int texture;
    uint32_t content_width;
    uint32_t content_height;
    uint32_t texture_width;
    uint32_t texture_height;
    float texcoords[8];
    float vertices[8];
    float colors[DB_RECT_VERTEX_COUNT * 4U];
    int presentation_quad_uses_vbo;
    const db_damage_block_t *present_damage_blocks;
    size_t present_damage_block_count;
    int present_damage_full;
    int present_damage_configured;
    db_gl_shadow_upload_trace_t upload_trace;
} db_gl_shadow_present_state_t;

typedef struct {
    db_pixel_surface_t pixel_surface;
    const db_pixel_surface_t *fallback_source_surface;
    size_t total_bytes;
    db_gl_shadow_full_upload_target_mode_t mode;
    int preserve_slot_contents;
    int slot_surface_valid;
    uint32_t slot_index;
} db_gl_shadow_present_full_upload_target_t;

typedef struct {
    db_gl_shadow_present_state_t *state;
    const char *backend;
    uint32_t pixel_width;
    uint32_t pixel_height;
    db_gl_pixel_upload_payload_t source_pixels;
    const db_damage_block_t *damage_blocks;
    size_t damage_block_count;
} db_gl_shadow_present_frame_t;

typedef struct {
    unsigned int vbo;
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

int db_parse_gl_version_numbers(const char *version_text, int *major_out,
                                int *minor_out);
int db_gl_version_text_at_least(const char *version_text, int req_major,
                                int req_minor);
int db_gl_is_es_context(const char *version_text);

db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_renderer(
    db_gl_runtime_draw_mode_t draw_mode, int has_vbo,
    const db_gl_stream_upload_capability_t *upload, int backbuffer_replay);
db_gl_runtime_mode_desc_t db_gl_runtime_mode_desc_present(
    const db_gl_shadow_present_state_t *state,
    db_gl_shadow_present_preserve_mode_t preserve_mode);
const char *db_gl_runtime_draw_mode_name(db_gl_runtime_draw_mode_t mode);
const char *db_gl_shadow_present_preserve_mode_name(
    db_gl_shadow_present_preserve_mode_t preserve_mode);
const char *
db_gl_stream_upload_storage_name(db_gl_stream_upload_storage_t storage);
const char *db_gl_stream_upload_mode_name(db_gl_stream_upload_mode_t mode);
const char *db_gl_upload_target_name(db_gl_upload_target_t target);
const char *
db_gl_upload_failure_reason_name(db_gl_upload_failure_reason_t reason);
const char *db_gl_upload_probe_step_name(db_gl_upload_probe_step_t step);
const char *
db_gl_stream_upload_name(const db_gl_stream_upload_capability_t *capability,
                         int client_arrays, int upload_enabled);
const char *db_gl_present_buffer_mode_name(db_gl_present_buffer_mode_t mode);
int db_gl_present_buffer_mode_parse(const char *text,
                                    db_gl_present_buffer_mode_t *out_mode);
db_gl_stream_upload_capability_t
db_gl_stream_upload_capability_probe(db_gl_upload_target_t target, size_t bytes,
                                     const void *initial_bytes,
                                     int enable_sync);
db_gl_stream_upload_capability_t db_gl_stream_upload_capability_for_role(
    const db_gl_stream_upload_capability_t *base_capability,
    db_gl_upload_role_t role);
void db_gl_stream_upload_force_client_fallback(
    db_gl_stream_upload_capability_t *capability, int disable_sync);
int db_gl_stream_upload_demote(db_gl_stream_upload_capability_t *capability,
                               db_gl_upload_failure_reason_t reason,
                               int disable_sync);
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
void db_gl_present_mode_resolve(const db_gl_present_mode_request_t *request,
                                db_gl_present_mode_resolution_t *out);
void db_gl_runtime_mode_format_renderer(char *output, size_t output_size,
                                        const db_gl_runtime_mode_desc_t *mode);
void db_gl_runtime_mode_format_present(char *output, size_t output_size,
                                       const db_gl_runtime_mode_desc_t *mode);

int db_gl_context_probe_texture_float_support(void);
int db_gl_context_probe_texture_float_present_support(void);
db_gl_pixel_upload_payload_t
db_gl_pixel_upload_payload_from_surface(const db_pixel_surface_t *surface);
void db_gl_error_trace_reset(db_gl_error_trace_t *trace);
size_t db_gl_error_trace_drain(db_gl_error_trace_t *trace, const char *phase,
                               const char *target, const char *context);
void db_gl_error_trace_dump(const db_gl_error_trace_t *trace);
void db_gl_shadow_upload_trace_reset(db_gl_shadow_upload_trace_t *trace);
void db_gl_shadow_upload_trace_capture_pixel_payload(
    db_gl_shadow_upload_trace_t *trace,
    const db_gl_pixel_upload_payload_t *payload);
void db_gl_shadow_upload_trace_capture_full_upload_attempt(
    db_gl_shadow_upload_trace_t *trace, uint32_t slot_index, size_t total_bytes,
    const char *source_label, const char *target_mode_label,
    const char *upload_mode_label);
void db_gl_shadow_upload_trace_note_history(db_gl_shadow_upload_trace_t *trace,
                                            uint32_t required_previous_frames,
                                            size_t historical_block_count,
                                            size_t repair_block_count,
                                            const char *history_source_label);
void db_gl_shadow_upload_trace_note_fallback(db_gl_shadow_upload_trace_t *trace,
                                             const char *fallback_mode_label);
void db_gl_shadow_upload_trace_note_execution(
    db_gl_shadow_upload_trace_t *trace, const char *executed_upload_mode_label);
void db_gl_shadow_upload_trace_note_surface_hashes(
    db_gl_shadow_upload_trace_t *trace, uint64_t upload_source_hash,
    uint64_t target_surface_hash, uint64_t fallback_source_hash);
void db_gl_shadow_upload_trace_note_seed(db_gl_shadow_upload_trace_t *trace,
                                         const char *seed_source_label);
void db_gl_shadow_upload_trace_capture_upload_span(
    db_gl_shadow_upload_trace_t *trace, const db_damage_block_t *block,
    size_t offset_bytes, size_t size_bytes, const char *source_label);
void db_gl_shadow_upload_trace_dump(const db_gl_shadow_upload_trace_t *trace);
uint64_t db_gl_pixel_surface_hash_canonical(const db_pixel_surface_t *surface);
void db_gl_shadow_present_init_runtime(
    db_gl_shadow_present_state_t *state, int prefer_unpack_pbo,
    int enable_full_upload_targets,
    const db_display_resolved_format_config_t *resolved_format,
    uint32_t preserved_framebuffer_count);
void db_gl_shadow_present_log_decision(
    const char *backend, const char *present_name,
    const db_display_resolved_format_config_t *resolved_format,
    const db_gl_shadow_present_state_t *state);
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
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *blocks, size_t block_count);
int db_gl_shadow_present_begin_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_slot_contents,
    db_gl_shadow_present_full_upload_target_t *target);
int db_gl_shadow_present_begin_full_upload_target_slot_offset(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height, int preserve_slot_contents,
    uint32_t slot_offset, db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_repair_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target,
    const db_pixel_surface_t *source_surface,
    db_pixel_block_view_t damage_view);
void db_gl_shadow_present_sync_preserved_slots(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_pixel_surface_t *source_surface, db_pixel_block_view_t damage_view,
    const db_gl_shadow_present_full_upload_target_t *current_target);
int db_gl_shadow_present_finish_full_upload_target(
    db_gl_shadow_present_state_t *state,
    const db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_present_full_upload_target(
    db_gl_shadow_present_state_t *state, const char *backend,
    uint32_t pixel_width, uint32_t pixel_height,
    const db_gl_shadow_present_full_upload_target_t *target);
void db_gl_shadow_present_present_replace_pixels(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *damage_blocks, size_t damage_block_count);
void db_gl_shadow_present_present_replace_pixels_direct_client(
    db_gl_shadow_present_state_t *state, const char *backend,
    const db_gl_pixel_upload_payload_t *source_pixels,
    const db_damage_block_t *damage_blocks, size_t damage_block_count);
void db_gl_shadow_present_frame(const db_gl_shadow_present_frame_t *frame);
void db_gl_shadow_present_draw(db_gl_shadow_present_state_t *state,
                               uint32_t pixel_width, uint32_t pixel_height);
void db_gl_shadow_present_set_draw_damage(db_gl_shadow_present_state_t *state,
                                          db_pixel_block_view_t damage,
                                          int force_full);
void db_gl_set_viewport_px(int width_px, int height_px);
int db_gl_texture_allocate_rgba(unsigned int texture, uint32_t width,
                                uint32_t height, unsigned int internal_format,
                                const void *pixels);
int db_gl_texture_create_rgba(unsigned int *out_texture, uint32_t width,
                              uint32_t height, unsigned int internal_format,
                              const void *pixels);
int db_gl_texture_create_rgba8(unsigned int *out_texture, uint32_t width,
                               uint32_t height, const uint8_t *pixels);
int db_gl_texture_create_rgba16f(unsigned int *out_texture, uint32_t width,
                                 uint32_t height, const uint16_t *pixels);
int db_gl_texture_create_rgb10a2_bt2020_pq(unsigned int *out_texture,
                                           uint32_t width, uint32_t height,
                                           const uint32_t *pixels);
void db_gl_texture_delete_if_valid(unsigned int *texture);
void db_gl_texture_bind_2d(unsigned int texture);
void db_gl_texture_sub_image_2d_rgba(uint32_t x_px, uint32_t y_px,
                                     uint32_t width, uint32_t height,
                                     const uint8_t *pixels);
void db_gl_texture_sub_image_2d_rgba16f(uint32_t x_px, uint32_t y_px,
                                        uint32_t width, uint32_t height,
                                        const uint16_t *pixels);
void db_gl_texture_sub_image_2d_rgb10a2_bt2020_pq(uint32_t x_px, uint32_t y_px,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  const uint32_t *pixels);
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
void db_gl_read_pixels_rgba8(uint32_t x_px, uint32_t y_px, uint32_t width,
                             uint32_t height, uint8_t *pixels);
void db_gl_read_pixels_rgba16f(uint32_t x_px, uint32_t y_px, uint32_t width,
                               uint32_t height, uint16_t *pixels);
void db_gl_set_texture_2d_enabled(int enabled);
void db_gl_set_client_state_vertex_array_enabled(int enabled);
void db_gl_set_client_state_color_array_enabled(int enabled);
void db_gl_set_client_state_texcoord_array_enabled(int enabled);
void db_gl_set_vertex_pointer_2f(size_t stride_bytes, const void *pointer);
void db_gl_set_color_pointer_f(uint32_t component_count, size_t stride_bytes,
                               const void *pointer);
void db_gl_set_texcoord_pointer_2f(size_t stride_bytes, const void *pointer);
void db_gl_draw_arrays_triangles(uint32_t first, uint32_t count);
void db_gl_draw_arrays_triangle_strip(uint32_t first, uint32_t count);
void db_gl_set_scissor_enabled(int enabled);
void db_gl_set_scissor(uint32_t x_px, uint32_t y_px, uint32_t width,
                       uint32_t height);
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
void db_gl_delete_framebuffers(uint32_t count,
                               const unsigned int *framebuffers);
void db_gl_delete_program(unsigned int program);
void db_gl_delete_shader(unsigned int shader);
void db_gl_delete_vertex_arrays(uint32_t count, const unsigned int *arrays);
void db_gl_enable_vertex_attrib_array(unsigned int index);
void db_gl_framebuffer_texture_2d(unsigned int target, unsigned int attachment,
                                  unsigned int textarget, unsigned int texture,
                                  int level);
void db_gl_gen_framebuffers(uint32_t count, unsigned int *framebuffers);
void db_gl_gen_vertex_arrays(uint32_t count, unsigned int *arrays);
void db_gl_get_integerv(unsigned int pname, int *value);
void db_gl_get_program_info_log(unsigned int program, size_t buf_size,
                                int *length, char *log);
void db_gl_get_program_iv(unsigned int program, unsigned int pname, int *value);
void db_gl_get_shader_info_log(unsigned int shader, size_t buf_size,
                               int *length, char *log);
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
uint32_t db_gl_get_error_value(void);
const char *db_gl_get_version_string(void);
const char *db_gl_get_renderer_string(void);
const char *db_gl_get_extensions_string(void);
void db_gl_set_proc_resolver(db_gl_proc_resolver_fn_t resolver);
void db_gl_upload_stream_init(db_gl_upload_stream_t *stream,
                              db_gl_upload_target_t target,
                              db_gl_stream_upload_capability_t capability,
                              unsigned int buffer, int owns_storage);
int db_gl_upload_stream_create_owned_buffer(db_gl_upload_stream_t *stream,
                                            const char *backend);
void db_gl_upload_stream_log_selection(const db_gl_upload_stream_t *stream,
                                       const char *backend, const char *role);
int db_gl_geometry_stream_init(db_gl_upload_stream_t *stream,
                               db_gl_geometry_stream_init_result_t *result,
                               const char *backend, size_t storage_bytes,
                               const float *probe_seed_vertices,
                               const float *initial_vertices,
                               size_t initial_seed_bytes, int enable_sync,
                               int allow_client_array_fallback);
int db_gl_upload_stream_prepare_storage(db_gl_upload_stream_t *stream,
                                        const char *backend,
                                        size_t required_bytes);
uint8_t *db_gl_upload_stream_begin_write(db_gl_upload_stream_t *stream,
                                         const char *backend,
                                         size_t offset_bytes,
                                         size_t size_bytes);
int db_gl_upload_stream_end_write(db_gl_upload_stream_t *stream,
                                  const char *backend);
uint8_t *db_gl_upload_stream_begin_read(db_gl_upload_stream_t *stream,
                                        const char *backend,
                                        size_t offset_bytes, size_t size_bytes);
int db_gl_upload_stream_end_read(db_gl_upload_stream_t *stream,
                                 const char *backend);
int db_gl_upload_stream_bind(const db_gl_upload_stream_t *stream);
int db_gl_upload_stream_unbind_target(db_gl_upload_target_t target);
int db_gl_upload_stream_write(db_gl_upload_stream_t *stream,
                              const char *backend, const void *source,
                              size_t total_bytes, size_t dst_offset_bytes,
                              size_t size_bytes);
const void *db_gl_upload_stream_pointer(const db_gl_upload_stream_t *stream,
                                        size_t byte_offset);
int db_gl_upload_stream_wait(db_gl_upload_stream_t *stream);
db_sync_wait_result_t
db_gl_upload_stream_probe_sync(void *sync, db_progress_policy_id_t policy_id);
void db_gl_upload_stream_record_sync(db_gl_upload_stream_t *stream);
void db_gl_upload_stream_shutdown(db_gl_upload_stream_t *stream);
void db_gl_compact_vbo_init_or_fail(const char *backend_name,
                                    db_gl_compact_vbo_state_t *compact,
                                    size_t base_vbo_bytes,
                                    size_t vertex_stride);
void db_gl_compact_vbo_init_standalone_or_fail(
    const char *backend_name, db_gl_compact_vbo_state_t *compact,
    size_t compact_vbo_bytes, size_t vertex_stride);
void db_gl_compact_vbo_free(db_gl_compact_vbo_state_t *compact);
int db_gl_upload_compact_stream_write(db_gl_upload_stream_t *stream,
                                      const char *backend,
                                      const db_gl_compact_vbo_state_t *compact,
                                      size_t compact_bytes);

int db_init_vertices_for_execution_config(
    const char *backend_name, db_gl_vertex_init_t *out_state,
    const db_renderer_execution_config_t *config, size_t vertex_stride);

static inline const void *db_gl_vbo_offset_ptr(size_t byte_offset) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return (const void *)(uintptr_t)byte_offset;
}

#endif
