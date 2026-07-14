#ifndef DRIVERBENCH_RENDER_IR_H
#define DRIVERBENCH_RENDER_IR_H

#include "db_render_types.h"

#include <stddef.h>
#include <stdint.h>

#define DB_RENDER_IR_INVALID_ID UINT32_MAX

typedef uint32_t db_render_ir_resource_id_t;
typedef uint32_t db_render_ir_region_id_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} db_render_ir_rect_t;

typedef struct {
    double rgba[4];
} db_render_ir_color_t;

typedef enum {
    DB_RENDER_IR_UPLOAD_REPLACE_EXACT = 0,
} db_render_ir_upload_replace_t;

typedef enum {
    DB_RENDER_IR_FILTER_NEAREST = 0,
} db_render_ir_filter_t;

typedef enum {
    DB_RENDER_IR_CONVERSION_EXACT = 0,
} db_render_ir_conversion_t;

typedef enum {
    DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT = 0,
    DB_RENDER_IR_PRIOR_CONTENT_REQUIRED = 1,
} db_render_ir_prior_content_t;

typedef struct {
    db_render_ir_upload_replace_t replacement;
    db_render_ir_filter_t filter;
    db_render_ir_conversion_t conversion;
    db_render_ir_prior_content_t prior_content;
    double opacity;
} db_render_ir_upload_semantics_t;

typedef struct {
    db_render_ir_rect_t rect;
    db_render_ir_color_t color;
} db_render_ir_fill_t;

typedef struct {
    int32_t x_start;
    int32_t x_end;
} db_render_ir_span_t;

typedef struct {
    int32_t y_start;
    int32_t y_end;
    uint32_t first_span;
    uint32_t span_count;
} db_render_ir_band_t;

typedef struct {
    uint32_t first_band;
    uint32_t band_count;
} db_render_ir_region_t;

typedef enum {
    DB_RENDER_IR_RESOURCE_CANONICAL_TARGET = 0,
    DB_RENDER_IR_RESOURCE_RASTER_SOURCE = 1,
} db_render_ir_resource_kind_t;

typedef struct {
    db_render_ir_resource_kind_t kind;
    uint32_t width;
    uint32_t height;
    db_pixel_format_t format;
} db_render_ir_resource_t;

typedef enum {
    DB_RENDER_IR_OP_BEGIN_TARGET = 0,
    DB_RENDER_IR_OP_END_TARGET = 1,
    DB_RENDER_IR_OP_CLEAR = 2,
    DB_RENDER_IR_OP_FILL_RECTS = 3,
    DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT = 4,
    DB_RENDER_IR_OP_UPLOAD_IMAGE = 5,
    DB_RENDER_IR_OP_INVALIDATE_RESOURCE = 6,
} db_render_ir_opcode_t;

enum {
    DB_RENDER_IR_RESERVED_IMAGE_OPCODE = 32,
    DB_RENDER_IR_RESERVED_COMPOSITE_OPCODE = 33,
    DB_RENDER_IR_RESERVED_GLYPH_OPCODE = 34,
    DB_RENDER_IR_RESERVED_PATH_OPCODE = 35,
    DB_RENDER_IR_RESERVED_EFFECT_OPCODE = 36,
};

typedef enum {
    DB_RENDER_IR_ACCESS_NONE = 0,
    DB_RENDER_IR_ACCESS_RENDER_WRITE = 1U << 0U,
    DB_RENDER_IR_ACCESS_TRANSFER_READ = 1U << 1U,
    DB_RENDER_IR_ACCESS_TRANSFER_WRITE = 1U << 2U,
    DB_RENDER_IR_ACCESS_CPU_READ = 1U << 3U,
    DB_RENDER_IR_ACCESS_CPU_WRITE = 1U << 4U,
} db_render_ir_access_t;

typedef enum {
    DB_RENDER_IR_COMPOSITE_SOURCE = 0,
    DB_RENDER_IR_COMPOSITE_SOURCE_OVER = 1,
} db_render_ir_composite_t;

enum {
    DB_RENDER_IR_COMMAND_OPAQUE_SOURCE = 1U << 0U,
    DB_RENDER_IR_COMMAND_FULL_OVERWRITE = 1U << 1U,
};

typedef struct {
    max_align_t alignment;
    uint16_t byte_size;
    uint8_t opcode;
    uint8_t composite;
    uint8_t source_access;
    uint8_t destination_access;
    uint32_t sequence;
    db_render_ir_resource_id_t destination;
    db_render_ir_region_id_t clip_region;
    db_render_ir_region_id_t touched_region;
    db_render_ir_region_id_t full_coverage_region;
    uint32_t flags;
} db_render_ir_command_header_t;

typedef struct {
    db_render_ir_command_header_t header;
    db_render_ir_color_t color;
} db_render_ir_clear_command_t;

typedef struct {
    db_render_ir_command_header_t header;
    uint32_t first_fill;
    uint32_t fill_count;
} db_render_ir_fill_command_t;

typedef struct {
    db_render_ir_command_header_t header;
    db_render_ir_rect_t bounds;
    int32_t axis_start;
    int32_t axis_end;
    uint8_t reverse_stops;
    db_render_ir_color_t start_color;
    db_render_ir_color_t end_color;
} db_render_ir_linear_gradient_command_t;

typedef struct {
    db_render_ir_command_header_t header;
    db_render_ir_resource_id_t source;
    db_render_ir_rect_t source_rect;
    int32_t destination_x;
    int32_t destination_y;
    db_render_ir_upload_semantics_t semantics;
} db_render_ir_upload_command_t;

typedef struct {
    db_render_ir_resource_id_t resource;
    uint64_t generation;
    uint64_t content_revision;
    uint32_t width;
    uint32_t height;
    db_pixel_format_t format;
    size_t row_stride_bytes;
    size_t size_bytes;
    const void *pixels;
} db_render_ir_external_binding_t;

typedef struct {
    const db_render_ir_external_binding_t *bindings;
    size_t count;
} db_render_ir_external_binding_view_t;

typedef union {
    max_align_t alignment;
    db_render_ir_command_header_t header;
    db_render_ir_clear_command_t clear;
    db_render_ir_fill_command_t fills;
    db_render_ir_linear_gradient_command_t linear_gradient;
    db_render_ir_upload_command_t upload;
} db_render_ir_command_t;

typedef enum {
    DB_RENDER_IR_OK = 0,
    DB_RENDER_IR_INVALID = 1,
    DB_RENDER_IR_CAPACITY = 2,
    DB_RENDER_IR_ARITHMETIC_OVERFLOW = 3,
} db_render_ir_status_t;

typedef struct {
    db_render_ir_status_t status;
    db_render_ir_region_id_t damage_region;
    uint64_t damage_area;
    uint64_t eliminated_area;
    uint32_t command_count;
    uint32_t compatible_batch_count;
    uint32_t instance_count;
    uint32_t solid_command_count;
    uint32_t gradient_count;
    uint32_t exact_fallback_instance_count;
    uint32_t partitionable_batch_count;
    int full_coverage;
    int prior_content_required;
    int fragmented;
    int worker_partitionable;
} db_render_ir_metadata_t;

typedef enum {
    DB_RENDER_IR_STREAM_UPDATE = 0,
    DB_RENDER_IR_STREAM_REBUILD = 1,
} db_render_ir_stream_t;

typedef struct {
    db_render_ir_stream_t stream;
    uint32_t first_sequence;
    uint32_t command_count;
    db_render_ir_region_id_t region;
    uint32_t instance_count;
    db_render_ir_prior_content_t prior_content;
    uint8_t opcode;
} db_render_ir_command_range_t;

const char *db_render_ir_status_name(db_render_ir_status_t status);

typedef struct {
    max_align_t *commands;
    size_t command_capacity;
    size_t command_size;
    uint32_t command_count;
    db_render_ir_fill_t *fills;
    size_t fill_capacity;
    size_t fill_count;
    db_render_ir_resource_t *resources;
    size_t resource_capacity;
    size_t resource_count;
    db_render_ir_region_t *regions;
    size_t region_capacity;
    size_t region_count;
    db_render_ir_band_t *bands;
    size_t band_capacity;
    size_t band_count;
    db_render_ir_span_t *spans;
    size_t span_capacity;
    size_t span_count;
    uint32_t next_sequence;
    db_render_ir_status_t status;
} db_render_ir_store_t;

typedef struct {
    const max_align_t *commands;
    size_t command_size;
    uint32_t command_count;
    const db_render_ir_fill_t *fills;
    size_t fill_count;
    const db_render_ir_resource_t *resources;
    size_t resource_count;
    const db_render_ir_region_t *regions;
    size_t region_count;
    const db_render_ir_band_t *bands;
    size_t band_count;
    const db_render_ir_span_t *spans;
    size_t span_count;
} db_render_ir_view_t;

typedef struct {
    const db_render_ir_view_t *view;
    size_t offset;
    db_render_ir_command_t current;
} db_render_ir_iterator_t;

typedef struct {
    db_render_ir_fill_t *primary;
    db_render_ir_fill_t *secondary;
    size_t capacity;
} db_render_ir_optimizer_workspace_t;

typedef struct {
    int (*begin_target)(void *context, db_render_ir_resource_id_t target);
    int (*end_target)(void *context, db_render_ir_resource_id_t target);
    int (*clear)(void *context, db_render_ir_resource_id_t target,
                 db_render_ir_color_t color);
    int (*fill_rects)(void *context, db_render_ir_resource_id_t target,
                      const db_render_ir_fill_t *fills, size_t fill_count);
    int (*fill_linear_gradient)(
        void *context, db_render_ir_resource_id_t target,
        const db_render_ir_linear_gradient_command_t *gradient);
    int (*upload_image)(void *context, db_render_ir_resource_id_t target,
                        const db_render_ir_upload_command_t *upload,
                        db_render_ir_external_binding_view_t bindings);
    int (*invalidate)(void *context, db_render_ir_resource_id_t resource);
} db_render_ir_lowering_ops_t;

void db_render_ir_store_reset(db_render_ir_store_t *store);
db_render_ir_view_t db_render_ir_store_view(const db_render_ir_store_t *store);
db_render_ir_status_t
db_render_ir_add_resource(db_render_ir_store_t *store,
                          const db_render_ir_resource_t *resource,
                          db_render_ir_resource_id_t *resource_id);
db_render_ir_status_t
db_render_ir_add_rect_region(db_render_ir_store_t *store,
                             db_render_ir_rect_t rect,
                             db_render_ir_region_id_t *region_id);
db_render_ir_status_t db_render_ir_add_fill_region(
    db_render_ir_store_t *store, const db_render_ir_fill_t *fills,
    size_t fill_count, db_render_ir_region_id_t *region_id);
db_render_ir_status_t db_render_ir_set_last_command_regions(
    db_render_ir_store_t *store, db_render_ir_region_id_t touched_region,
    db_render_ir_region_id_t full_coverage_region);
db_render_ir_status_t db_render_ir_region_union(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id);
db_render_ir_status_t db_render_ir_region_intersection(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id);
db_render_ir_status_t db_render_ir_region_subtract(
    db_render_ir_store_t *store, db_render_ir_region_id_t lhs,
    db_render_ir_region_id_t rhs, db_render_ir_region_id_t *region_id);
uint64_t db_render_ir_region_area(const db_render_ir_view_t *view,
                                  db_render_ir_region_id_t region_id);
db_render_ir_region_id_t
db_render_ir_final_damage_region(const db_render_ir_view_t *view);
int db_render_ir_final_damage_covers(const db_render_ir_view_t *view,
                                     uint32_t width, uint32_t height);
db_render_ir_status_t
db_render_ir_begin_target(db_render_ir_store_t *store,
                          db_render_ir_resource_id_t destination);
db_render_ir_status_t
db_render_ir_end_target(db_render_ir_store_t *store,
                        db_render_ir_resource_id_t destination);
db_render_ir_status_t db_render_ir_clear(db_render_ir_store_t *store,
                                         db_render_ir_resource_id_t destination,
                                         db_render_ir_color_t color,
                                         db_render_ir_region_id_t clip_region);
db_render_ir_status_t
db_render_ir_fill_rects(db_render_ir_store_t *store,
                        db_render_ir_resource_id_t destination,
                        const db_render_ir_fill_t *fills, size_t fill_count,
                        db_render_ir_region_id_t clip_region);
db_render_ir_status_t db_render_ir_fill_linear_gradient(
    db_render_ir_store_t *store, db_render_ir_resource_id_t destination,
    db_render_ir_rect_t bounds, int32_t axis_start, int32_t axis_end,
    int reverse_stops, db_render_ir_color_t start_color,
    db_render_ir_color_t end_color, db_render_ir_region_id_t clip_region);
db_render_ir_status_t db_render_ir_upload_image(
    db_render_ir_store_t *store, db_render_ir_resource_id_t destination,
    db_render_ir_resource_id_t source, db_render_ir_rect_t source_rect,
    int32_t destination_x, int32_t destination_y,
    db_render_ir_upload_semantics_t semantics);
db_render_ir_status_t
db_render_ir_invalidate_resource(db_render_ir_store_t *store,
                                 db_render_ir_resource_id_t resource);

void db_render_ir_iterator_begin(db_render_ir_iterator_t *iterator,
                                 const db_render_ir_view_t *view);
// The returned command remains valid until the next call using this iterator.
const db_render_ir_command_header_t *
db_render_ir_iterator_next(db_render_ir_iterator_t *iterator);
db_render_ir_status_t db_render_ir_validate(const db_render_ir_view_t *view);
db_render_ir_status_t
db_render_ir_validate_bindings(const db_render_ir_view_t *view,
                               db_render_ir_external_binding_view_t bindings);
db_render_ir_status_t
db_render_ir_optimize(const db_render_ir_view_t *raw,
                      db_render_ir_store_t *optimized,
                      db_render_ir_optimizer_workspace_t workspace);
db_render_ir_status_t
db_render_ir_lower(const db_render_ir_view_t *view,
                   db_render_ir_external_binding_view_t bindings,
                   const db_render_ir_lowering_ops_t *ops, void *context);

// Exact fallback lowering for backends without a conforming semantic-gradient
// path. Semantic-capable backends should consume commands directly.
size_t db_render_ir_rect_count(const db_render_ir_view_t *view);
int db_render_ir_rect_at(const db_render_ir_view_t *view, size_t index,
                         db_render_ir_fill_t *fill);
int db_render_ir_rect_to_grid_block(db_render_ir_rect_t rect,
                                    uint32_t grid_width, uint32_t grid_height,
                                    db_grid_block_t *block);

db_render_ir_status_t
db_render_ir_color_canonicalize(db_render_ir_color_t input,
                                db_render_ir_color_t *output);
const db_render_ir_external_binding_t *
db_render_ir_find_binding(db_render_ir_external_binding_view_t bindings,
                          db_render_ir_resource_id_t resource);

int db_render_ir_rect_is_empty(db_render_ir_rect_t rect);
int db_render_ir_rect_from_extent(uint32_t width, uint32_t height,
                                  db_render_ir_rect_t *result);
int db_render_ir_rect_intersect(db_render_ir_rect_t lhs,
                                db_render_ir_rect_t rhs,
                                db_render_ir_rect_t *result);
uint64_t db_render_ir_rect_area(db_render_ir_rect_t rect);
db_render_ir_color_t db_render_ir_linear_gradient_color_at(
    const db_render_ir_linear_gradient_command_t *gradient,
    int32_t logical_row);
uint64_t db_render_ir_hash(const db_render_ir_view_t *view);
db_render_ir_metadata_t db_render_ir_metadata(const db_render_ir_view_t *view,
                                              db_render_ir_status_t status,
                                              uint32_t target_width,
                                              uint32_t target_height);
size_t db_render_ir_collect_command_ranges(const db_render_ir_view_t *view,
                                           db_render_ir_stream_t stream,
                                           db_render_ir_command_range_t *ranges,
                                           size_t range_capacity,
                                           int *out_overflow);
size_t db_render_ir_command_range_rect_count(
    const db_render_ir_view_t *view, const db_render_ir_command_range_t *range);
int db_render_ir_command_range_rect_at(
    const db_render_ir_view_t *view, const db_render_ir_command_range_t *range,
    size_t index, db_render_ir_fill_t *fill);
size_t db_render_ir_region_copy_grid_blocks(const db_render_ir_view_t *view,
                                            db_render_ir_region_id_t region_id,
                                            db_grid_block_t *output,
                                            size_t output_capacity,
                                            int *out_overflow);

#endif
