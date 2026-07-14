#include "db_render_ir.h"
#include "db_numeric.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { DB_RENDER_IR_ALIGNMENT = _Alignof(max_align_t) };

static int add_size(size_t lhs, size_t rhs, size_t *result) {
    if ((result == NULL) || (lhs > (SIZE_MAX - rhs))) {
        return 0;
    }
    *result = lhs + rhs;
    return 1;
}

static int align_size(size_t value, size_t *result) {
    size_t expanded = 0U;
    if (!add_size(value, DB_RENDER_IR_ALIGNMENT - 1U, &expanded)) {
        return 0;
    }
    *result = expanded & ~(size_t)(DB_RENDER_IR_ALIGNMENT - 1U);
    return 1;
}

static int rect_has_safe_endpoints(db_render_ir_rect_t rect) {
    const int64_t x_end = (int64_t)rect.x + (int64_t)rect.width;
    const int64_t y_end = (int64_t)rect.y + (int64_t)rect.height;
    return DB_BOOL((rect.x >= 0) && (rect.y >= 0) && (rect.width > 0) &&
                   (rect.height > 0) && (x_end <= INT32_MAX) &&
                   (y_end <= INT32_MAX));
}

static db_render_ir_status_t append_command(db_render_ir_store_t *store,
                                            const void *command,
                                            size_t command_size) {
    size_t aligned_size = 0U;
    size_t end = 0U;
    if ((store == NULL) || (command == NULL) ||
        (command_size < sizeof(db_render_ir_command_header_t)) ||
        (command_size > sizeof(db_render_ir_command_t))) {
        return DB_RENDER_IR_INVALID;
    }
    if (!align_size(command_size, &aligned_size) ||
        !add_size(store->command_size, aligned_size, &end)) {
        store->status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
        return store->status;
    }
    if ((aligned_size > UINT16_MAX) || (end > store->command_capacity)) {
        store->status = DB_RENDER_IR_CAPACITY;
        return store->status;
    }
    unsigned char *const destination =
        (unsigned char *)store->commands + store->command_size;
    db_render_ir_command_t encoded = {0};
    memcpy(&encoded, command, command_size);
    encoded.header.byte_size = (uint16_t)aligned_size;
    encoded.header.sequence = store->next_sequence++;
    memset(destination, 0, aligned_size);
    memcpy(destination, &encoded, command_size);
    store->command_size = end;
    store->command_count++;
    return DB_RENDER_IR_OK;
}

static db_render_ir_command_header_t make_header(
    db_render_ir_opcode_t opcode, db_render_ir_resource_id_t destination,
    db_render_ir_composite_t composite, uint32_t flags,
    db_render_ir_region_id_t clip_region, db_render_ir_access_t source_access,
    db_render_ir_access_t destination_access) {
    return (db_render_ir_command_header_t){
        .opcode = (uint8_t)opcode,
        .composite = (uint8_t)composite,
        .source_access = (uint8_t)source_access,
        .destination_access = (uint8_t)destination_access,
        .destination = destination,
        .clip_region = clip_region,
        .touched_region = DB_RENDER_IR_INVALID_ID,
        .full_coverage_region = DB_RENDER_IR_INVALID_ID,
        .flags = flags,
    };
}

void db_render_ir_store_reset(db_render_ir_store_t *store) {
    if (store == NULL) {
        return;
    }
    store->command_size = 0U;
    store->command_count = 0U;
    store->fill_count = 0U;
    store->resource_count = 0U;
    store->region_count = 0U;
    store->band_count = 0U;
    store->span_count = 0U;
    store->next_sequence = 0U;
    store->status = DB_RENDER_IR_OK;
}

db_render_ir_view_t db_render_ir_store_view(const db_render_ir_store_t *store) {
    if (store == NULL) {
        return (db_render_ir_view_t){0};
    }
    return (db_render_ir_view_t){
        .commands = store->commands,
        .command_size = store->command_size,
        .command_count = store->command_count,
        .fills = store->fills,
        .fill_count = store->fill_count,
        .resources = store->resources,
        .resource_count = store->resource_count,
        .regions = store->regions,
        .region_count = store->region_count,
        .bands = store->bands,
        .band_count = store->band_count,
        .spans = store->spans,
        .span_count = store->span_count,
    };
}

db_render_ir_status_t
db_render_ir_add_resource(db_render_ir_store_t *store,
                          const db_render_ir_resource_t *resource,
                          db_render_ir_resource_id_t *resource_id) {
    if ((store == NULL) || (resource == NULL) || (resource_id == NULL) ||
        (resource->width == 0U) || (resource->height == 0U) ||
        (resource->width > INT32_MAX) || (resource->height > INT32_MAX)) {
        return DB_RENDER_IR_INVALID;
    }
    if (store->resource_count >= store->resource_capacity) {
        store->status = DB_RENDER_IR_CAPACITY;
        return store->status;
    }
    if (store->resource_count > UINT32_MAX) {
        store->status = DB_RENDER_IR_CAPACITY;
        return store->status;
    }
    *resource_id = (uint32_t)store->resource_count;
    store->resources[store->resource_count++] = *resource;
    return DB_RENDER_IR_OK;
}

int db_render_ir_rect_is_empty(db_render_ir_rect_t rect) {
    return DB_BOOL((rect.width <= 0) || (rect.height <= 0));
}

int db_render_ir_rect_from_extent(uint32_t width, uint32_t height,
                                  db_render_ir_rect_t *result) {
    if ((result == NULL) || (width > INT32_MAX) || (height > INT32_MAX)) {
        return 0;
    }
    *result = (db_render_ir_rect_t){
        .width = (int32_t)width,
        .height = (int32_t)height,
    };
    return 1;
}

uint64_t db_render_ir_rect_area(db_render_ir_rect_t rect) {
    if (db_render_ir_rect_is_empty(rect) != 0) {
        return 0U;
    }
    return (uint64_t)(uint32_t)rect.width * (uint64_t)(uint32_t)rect.height;
}

int db_render_ir_rect_intersect(db_render_ir_rect_t lhs,
                                db_render_ir_rect_t rhs,
                                db_render_ir_rect_t *result) {
    if (result == NULL) {
        return 0;
    }
    const int64_t lhs_right = (int64_t)lhs.x + lhs.width;
    const int64_t rhs_right = (int64_t)rhs.x + rhs.width;
    const int64_t lhs_bottom = (int64_t)lhs.y + lhs.height;
    const int64_t rhs_bottom = (int64_t)rhs.y + rhs.height;
    const int64_t x_start = DB_MAX((int64_t)lhs.x, (int64_t)rhs.x);
    const int64_t y_start = DB_MAX((int64_t)lhs.y, (int64_t)rhs.y);
    const int64_t x_end = DB_MIN(lhs_right, rhs_right);
    const int64_t y_end = DB_MIN(lhs_bottom, rhs_bottom);
    if ((x_end <= x_start) || (y_end <= y_start) || (x_start < INT32_MIN) ||
        (x_start > INT32_MAX) || (y_start < INT32_MIN) ||
        (y_start > INT32_MAX) || ((x_end - x_start) > INT32_MAX) ||
        ((y_end - y_start) > INT32_MAX)) {
        *result = (db_render_ir_rect_t){0};
        return 0;
    }
    *result = (db_render_ir_rect_t){.x = (int32_t)x_start,
                                    .y = (int32_t)y_start,
                                    .width = (int32_t)(x_end - x_start),
                                    .height = (int32_t)(y_end - y_start)};
    return 1;
}

db_render_ir_status_t
db_render_ir_add_rect_region(db_render_ir_store_t *store,
                             db_render_ir_rect_t rect,
                             db_render_ir_region_id_t *region_id) {
    if ((store == NULL) || (region_id == NULL) ||
        (rect_has_safe_endpoints(rect) == 0)) {
        return DB_RENDER_IR_INVALID;
    }
    if ((store->region_count >= store->region_capacity) ||
        (store->band_count >= store->band_capacity) ||
        (store->span_count >= store->span_capacity) ||
        (store->region_count > UINT32_MAX) ||
        (store->band_count > UINT32_MAX) || (store->span_count > UINT32_MAX)) {
        store->status = DB_RENDER_IR_CAPACITY;
        return store->status;
    }
    *region_id = (uint32_t)store->region_count;
    store->regions[store->region_count++] = (db_render_ir_region_t){
        .first_band = (uint32_t)store->band_count, .band_count = 1U};
    store->bands[store->band_count++] = (db_render_ir_band_t){
        .y_start = rect.y,
        .y_end = rect.y + rect.height,
        .first_span = (uint32_t)store->span_count,
        .span_count = 1U,
    };
    store->spans[store->span_count++] =
        (db_render_ir_span_t){.x_start = rect.x, .x_end = rect.x + rect.width};
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t db_render_ir_set_last_command_regions(
    db_render_ir_store_t *store, db_render_ir_region_id_t touched_region,
    db_render_ir_region_id_t full_coverage_region) {
    if ((store == NULL) || (store->command_count == 0U) ||
        (touched_region >= store->region_count) ||
        (full_coverage_region >= store->region_count)) {
        return DB_RENDER_IR_INVALID;
    }
    size_t offset = 0U;
    size_t last_offset = 0U;
    int has_last = 0;
    while (offset < store->command_size) {
        db_render_ir_command_header_t command = {0};
        const unsigned char *const encoded =
            (const unsigned char *)store->commands + offset;
        memcpy(&command, encoded, sizeof(command));
        if ((command.byte_size < sizeof(command)) ||
            (command.byte_size > (store->command_size - offset))) {
            return DB_RENDER_IR_INVALID;
        }
        last_offset = offset;
        has_last = 1;
        offset += command.byte_size;
    }
    if ((has_last == 0) || (offset != store->command_size)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_command_header_t last = {0};
    unsigned char *const encoded =
        (unsigned char *)store->commands + last_offset;
    memcpy(&last, encoded, sizeof(last));
    last.touched_region = touched_region;
    last.full_coverage_region = full_coverage_region;
    memcpy(encoded, &last, sizeof(last));
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t
db_render_ir_begin_target(db_render_ir_store_t *store,
                          db_render_ir_resource_id_t destination) {
    const db_render_ir_command_header_t command =
        make_header(DB_RENDER_IR_OP_BEGIN_TARGET, destination,
                    DB_RENDER_IR_COMPOSITE_SOURCE, 0U, DB_RENDER_IR_INVALID_ID,
                    DB_RENDER_IR_ACCESS_NONE, DB_RENDER_IR_ACCESS_NONE);
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t
db_render_ir_end_target(db_render_ir_store_t *store,
                        db_render_ir_resource_id_t destination) {
    const db_render_ir_command_header_t command =
        make_header(DB_RENDER_IR_OP_END_TARGET, destination,
                    DB_RENDER_IR_COMPOSITE_SOURCE, 0U, DB_RENDER_IR_INVALID_ID,
                    DB_RENDER_IR_ACCESS_NONE, DB_RENDER_IR_ACCESS_NONE);
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t db_render_ir_clear(db_render_ir_store_t *store,
                                         db_render_ir_resource_id_t destination,
                                         db_render_ir_color_t color,
                                         db_render_ir_region_id_t clip_region) {
    db_render_ir_color_t canonical = {0};
    const db_render_ir_status_t color_status =
        db_render_ir_color_canonicalize(color, &canonical);
    if (color_status != DB_RENDER_IR_OK) {
        return color_status;
    }
    const db_render_ir_clear_command_t command = {
        .header = make_header(DB_RENDER_IR_OP_CLEAR, destination,
                              DB_RENDER_IR_COMPOSITE_SOURCE,
                              DB_RENDER_IR_COMMAND_OPAQUE_SOURCE |
                                  DB_RENDER_IR_COMMAND_FULL_OVERWRITE,
                              clip_region, DB_RENDER_IR_ACCESS_NONE,
                              DB_RENDER_IR_ACCESS_RENDER_WRITE),
        .color = canonical,
    };
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t
db_render_ir_fill_rects(db_render_ir_store_t *store,
                        db_render_ir_resource_id_t destination,
                        const db_render_ir_fill_t *fills, size_t fill_count,
                        db_render_ir_region_id_t clip_region) {
    if ((store == NULL) || ((fills == NULL) && (fill_count > 0U))) {
        return DB_RENDER_IR_INVALID;
    }
    if (fill_count == 0U) {
        return DB_RENDER_IR_OK;
    }
    if ((store->fill_count > store->fill_capacity) ||
        (store->fill_count > UINT32_MAX) || (fill_count > UINT32_MAX) ||
        (fill_count > (store->fill_capacity - store->fill_count))) {
        store->status = DB_RENDER_IR_CAPACITY;
        return store->status;
    }
    const uint32_t first_fill = (uint32_t)store->fill_count;
    for (size_t index = 0U; index < fill_count; index++) {
        db_render_ir_fill_t canonical = fills[index];
        const db_render_ir_status_t color_status =
            db_render_ir_color_canonicalize(fills[index].color,
                                            &canonical.color);
        if ((color_status != DB_RENDER_IR_OK) ||
            (db_render_ir_rect_is_empty(canonical.rect) != 0)) {
            return DB_RENDER_IR_INVALID;
        }
        store->fills[store->fill_count + index] = canonical;
    }
    store->fill_count += fill_count;
    const db_render_ir_fill_command_t command = {
        .header = make_header(DB_RENDER_IR_OP_FILL_RECTS, destination,
                              DB_RENDER_IR_COMPOSITE_SOURCE,
                              DB_RENDER_IR_COMMAND_OPAQUE_SOURCE, clip_region,
                              DB_RENDER_IR_ACCESS_NONE,
                              DB_RENDER_IR_ACCESS_RENDER_WRITE),
        .first_fill = first_fill,
        .fill_count = (uint32_t)fill_count,
    };
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t db_render_ir_fill_linear_gradient(
    db_render_ir_store_t *store, db_render_ir_resource_id_t destination,
    db_render_ir_rect_t bounds, int32_t axis_start, int32_t axis_end,
    int reverse_stops, db_render_ir_color_t start_color,
    db_render_ir_color_t end_color, db_render_ir_region_id_t clip_region) {
    if ((store == NULL) || (db_render_ir_rect_is_empty(bounds) != 0) ||
        (axis_end < axis_start)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_color_t canonical_start = {0};
    db_render_ir_color_t canonical_end = {0};
    if ((db_render_ir_color_canonicalize(start_color, &canonical_start) !=
         DB_RENDER_IR_OK) ||
        (db_render_ir_color_canonicalize(end_color, &canonical_end) !=
         DB_RENDER_IR_OK)) {
        return DB_RENDER_IR_INVALID;
    }
    const db_render_ir_linear_gradient_command_t command = {
        .header = make_header(DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT, destination,
                              DB_RENDER_IR_COMPOSITE_SOURCE,
                              DB_RENDER_IR_COMMAND_OPAQUE_SOURCE, clip_region,
                              DB_RENDER_IR_ACCESS_NONE,
                              DB_RENDER_IR_ACCESS_RENDER_WRITE),
        .bounds = bounds,
        .axis_start = axis_start,
        .axis_end = axis_end,
        .reverse_stops = (uint8_t)DB_BOOL(reverse_stops != 0),
        .start_color = canonical_start,
        .end_color = canonical_end,
    };
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t db_render_ir_upload_image(
    db_render_ir_store_t *store, db_render_ir_resource_id_t destination,
    db_render_ir_resource_id_t source, db_render_ir_rect_t source_rect,
    int32_t destination_x, int32_t destination_y,
    db_render_ir_upload_semantics_t semantics) {
    if ((db_render_ir_rect_is_empty(source_rect) != 0) ||
        !isfinite(semantics.opacity) || (semantics.opacity < 0.0) ||
        (semantics.opacity > 1.0) ||
        (semantics.replacement != DB_RENDER_IR_UPLOAD_REPLACE_EXACT) ||
        (semantics.filter != DB_RENDER_IR_FILTER_NEAREST) ||
        (semantics.conversion != DB_RENDER_IR_CONVERSION_EXACT)) {
        return DB_RENDER_IR_INVALID;
    }
    const db_render_ir_upload_command_t command = {
        .header = make_header(
            DB_RENDER_IR_OP_UPLOAD_IMAGE, destination,
            DB_RENDER_IR_COMPOSITE_SOURCE, DB_RENDER_IR_COMMAND_OPAQUE_SOURCE,
            DB_RENDER_IR_INVALID_ID, DB_RENDER_IR_ACCESS_TRANSFER_READ,
            DB_RENDER_IR_ACCESS_TRANSFER_WRITE),
        .source = source,
        .source_rect = source_rect,
        .destination_x = destination_x,
        .destination_y = destination_y,
        .semantics = semantics,
    };
    return append_command(store, &command, sizeof(command));
}

db_render_ir_status_t
db_render_ir_color_canonicalize(db_render_ir_color_t input,
                                db_render_ir_color_t *output) {
    if (output == NULL) {
        return DB_RENDER_IR_INVALID;
    }
    for (size_t index = 0U; index < 4U; index++) {
        if (!isfinite(input.rgba[index])) {
            return DB_RENDER_IR_INVALID;
        }
        if ((db_f64_to_bits_u64(input.rgba[index]) & INT64_MAX) == 0U) {
            input.rgba[index] = 0.0;
        }
    }
    if ((input.rgba[3] < 0.0) || (input.rgba[3] > 1.0)) {
        return DB_RENDER_IR_INVALID;
    }
    *output = input;
    return DB_RENDER_IR_OK;
}

const db_render_ir_external_binding_t *
db_render_ir_find_binding(db_render_ir_external_binding_view_t bindings,
                          db_render_ir_resource_id_t resource) {
    if (bindings.bindings == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < bindings.count; index++) {
        if (bindings.bindings[index].resource == resource) {
            return &bindings.bindings[index];
        }
    }
    return NULL;
}

db_render_ir_status_t
db_render_ir_invalidate_resource(db_render_ir_store_t *store,
                                 db_render_ir_resource_id_t resource) {
    const db_render_ir_command_header_t command =
        make_header(DB_RENDER_IR_OP_INVALIDATE_RESOURCE, resource,
                    DB_RENDER_IR_COMPOSITE_SOURCE, 0U, DB_RENDER_IR_INVALID_ID,
                    DB_RENDER_IR_ACCESS_NONE, DB_RENDER_IR_ACCESS_NONE);
    return append_command(store, &command, sizeof(command));
}
