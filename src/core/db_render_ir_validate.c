#include "db_render_ir.h"

#include "db_core.h"

#include "db_geometry.h"
#include "db_render_types.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum { DB_RENDER_IR_MEMORY_RANGE_COUNT = 6U };

typedef struct {
    const void *data;
    size_t size;
} db_render_ir_memory_range_t;

static int view_memory_ranges(
    const db_render_ir_view_t *view,
    db_render_ir_memory_range_t ranges[DB_RENDER_IR_MEMORY_RANGE_COUNT]) {
    size_t fill_bytes = 0U;
    size_t resource_bytes = 0U;
    size_t region_bytes = 0U;
    size_t band_bytes = 0U;
    size_t span_bytes = 0U;
    if ((view == NULL) ||
        (db_try_mul_size(view->fill_count, sizeof(*view->fills), &fill_bytes) ==
         0) ||
        (db_try_mul_size(view->resource_count, sizeof(*view->resources),
                         &resource_bytes) == 0) ||
        (db_try_mul_size(view->region_count, sizeof(*view->regions),
                         &region_bytes) == 0) ||
        (db_try_mul_size(view->band_count, sizeof(*view->bands), &band_bytes) ==
         0) ||
        (db_try_mul_size(view->span_count, sizeof(*view->spans), &span_bytes) ==
         0)) {
        return 0;
    }
    ranges[0] =
        (db_render_ir_memory_range_t){view->commands, view->command_size};
    ranges[1] = (db_render_ir_memory_range_t){view->fills, fill_bytes};
    ranges[2] = (db_render_ir_memory_range_t){view->resources, resource_bytes};
    ranges[3] = (db_render_ir_memory_range_t){view->regions, region_bytes};
    ranges[4] = (db_render_ir_memory_range_t){view->bands, band_bytes};
    ranges[5] = (db_render_ir_memory_range_t){view->spans, span_bytes};
    for (size_t lhs = 0U; lhs < DB_RENDER_IR_MEMORY_RANGE_COUNT; lhs++) {
        int self_overlap = 0;
        if (db_memory_ranges_overlap(ranges[lhs].data, ranges[lhs].size,
                                     ranges[lhs].data, ranges[lhs].size,
                                     &self_overlap) == 0) {
            return 0;
        }
        for (size_t rhs = lhs + 1U; rhs < DB_RENDER_IR_MEMORY_RANGE_COUNT;
             rhs++) {
            int overlap = 0;
            if ((db_memory_ranges_overlap(ranges[lhs].data, ranges[lhs].size,
                                          ranges[rhs].data, ranges[rhs].size,
                                          &overlap) == 0) ||
                (overlap != 0)) {
                return 0;
            }
        }
    }
    return 1;
}

static int store_memory_ranges(
    const db_render_ir_store_t *store,
    db_render_ir_memory_range_t ranges[DB_RENDER_IR_MEMORY_RANGE_COUNT]) {
    size_t fill_bytes = 0U;
    size_t resource_bytes = 0U;
    size_t region_bytes = 0U;
    size_t band_bytes = 0U;
    size_t span_bytes = 0U;
    if ((store == NULL) ||
        (db_try_mul_size(store->fill_capacity, sizeof(*store->fills),
                         &fill_bytes) == 0) ||
        (db_try_mul_size(store->resource_capacity, sizeof(*store->resources),
                         &resource_bytes) == 0) ||
        (db_try_mul_size(store->region_capacity, sizeof(*store->regions),
                         &region_bytes) == 0) ||
        (db_try_mul_size(store->band_capacity, sizeof(*store->bands),
                         &band_bytes) == 0) ||
        (db_try_mul_size(store->span_capacity, sizeof(*store->spans),
                         &span_bytes) == 0)) {
        return 0;
    }
    ranges[0] =
        (db_render_ir_memory_range_t){store->commands, store->command_capacity};
    ranges[1] = (db_render_ir_memory_range_t){store->fills, fill_bytes};
    ranges[2] = (db_render_ir_memory_range_t){store->resources, resource_bytes};
    ranges[3] = (db_render_ir_memory_range_t){store->regions, region_bytes};
    ranges[4] = (db_render_ir_memory_range_t){store->bands, band_bytes};
    ranges[5] = (db_render_ir_memory_range_t){store->spans, span_bytes};
    for (size_t index = 0U; index < DB_RENDER_IR_MEMORY_RANGE_COUNT; index++) {
        int self_overlap = 0;
        if (db_memory_ranges_overlap(ranges[index].data, ranges[index].size,
                                     ranges[index].data, ranges[index].size,
                                     &self_overlap) == 0) {
            return 0;
        }
        for (size_t other = index + 1U; other < DB_RENDER_IR_MEMORY_RANGE_COUNT;
             other++) {
            int overlap = 0;
            if ((db_memory_ranges_overlap(
                     ranges[index].data, ranges[index].size, ranges[other].data,
                     ranges[other].size, &overlap) == 0) ||
                (overlap != 0)) {
                return 0;
            }
        }
    }
    return 1;
}

db_render_ir_storage_relation_t
db_render_ir_view_store_relation(const db_render_ir_view_t *view,
                                 const db_render_ir_store_t *store) {
    db_render_ir_memory_range_t source[DB_RENDER_IR_MEMORY_RANGE_COUNT] = {0};
    db_render_ir_memory_range_t destination[DB_RENDER_IR_MEMORY_RANGE_COUNT] = {
        0};
    if ((view_memory_ranges(view, source) == 0) ||
        (store_memory_ranges(store, destination) == 0)) {
        return DB_RENDER_IR_STORAGE_INVALID;
    }
    int matched_arena = 0;
    for (size_t source_index = 0U;
         source_index < DB_RENDER_IR_MEMORY_RANGE_COUNT; source_index++) {
        for (size_t destination_index = 0U;
             destination_index < DB_RENDER_IR_MEMORY_RANGE_COUNT;
             destination_index++) {
            int overlap = 0;
            if (db_memory_ranges_overlap(
                    source[source_index].data, source[source_index].size,
                    destination[destination_index].data,
                    destination[destination_index].size, &overlap) == 0) {
                return DB_RENDER_IR_STORAGE_INVALID;
            }
            if (overlap == 0) {
                continue;
            }
            if ((source_index != destination_index) ||
                (source[source_index].data !=
                 destination[destination_index].data)) {
                return DB_RENDER_IR_STORAGE_CROSS_ALIAS;
            }
            matched_arena = 1;
        }
    }
    return (matched_arena != 0) ? DB_RENDER_IR_STORAGE_MATCHED_ARENAS
                                : DB_RENDER_IR_STORAGE_DISJOINT;
}

static int validation_align_size(size_t value, size_t *result) {
    if ((result == NULL) ||
        (value > (SIZE_MAX - (DB_RENDER_IR_RECORD_ALIGNMENT - 1U)))) {
        return 0;
    }
    const size_t expanded = value + DB_RENDER_IR_RECORD_ALIGNMENT - 1U;
    *result = expanded & ~(size_t)(DB_RENDER_IR_RECORD_ALIGNMENT - 1U);
    return 1;
}

static int region_fits_resource(const db_render_ir_view_t *view,
                                db_render_ir_region_id_t region_id,
                                db_render_ir_resource_t resource) {
    if (region_id == DB_RENDER_IR_INVALID_ID) {
        return 1;
    }
    if ((view == NULL) || (region_id >= view->region_count)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    for (uint32_t band_offset = 0U; band_offset < region.band_count;
         band_offset++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_offset];
        if ((uint32_t)band.y_end > resource.height) {
            return 0;
        }
        for (uint32_t span_offset = 0U; span_offset < band.span_count;
             span_offset++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_offset];
            if ((uint32_t)span.x_end > resource.width) {
                return 0;
            }
        }
    }
    return 1;
}

db_render_ir_status_t db_render_ir_validate(const db_render_ir_view_t *view) {
    if ((view == NULL) ||
        ((view->commands == NULL) && (view->command_size > 0U)) ||
        ((view->fills == NULL) && (view->fill_count > 0U)) ||
        ((view->resources == NULL) && (view->resource_count > 0U)) ||
        ((view->regions == NULL) && (view->region_count > 0U)) ||
        ((view->bands == NULL) && (view->band_count > 0U)) ||
        ((view->spans == NULL) && (view->span_count > 0U))) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_memory_range_t ranges[DB_RENDER_IR_MEMORY_RANGE_COUNT] = {0};
    if (view_memory_ranges(view, ranges) == 0) {
        return DB_RENDER_IR_INVALID;
    }
    for (size_t index = 0U; index < view->resource_count; index++) {
        const db_render_ir_resource_t resource = view->resources[index];
        if ((resource.width == 0U) || (resource.height == 0U) ||
            (resource.width > INT32_MAX) || (resource.height > INT32_MAX) ||
            (resource.kind > DB_RENDER_IR_RESOURCE_RASTER_SOURCE) ||
            (resource.format > DB_PIXEL_FORMAT_RGBA16F)) {
            return DB_RENDER_IR_INVALID;
        }
    }
    for (size_t region_index = 0U; region_index < view->region_count;
         region_index++) {
        const db_render_ir_region_t region = view->regions[region_index];
        size_t band_end = 0U;
        if ((db_try_add_size((size_t)region.first_band, region.band_count,
                             &band_end) == 0) ||
            (band_end > view->band_count)) {
            return DB_RENDER_IR_INVALID;
        }
        int32_t previous_y_end = -1;
        db_render_ir_band_t previous_band = {0};
        int have_previous_band = 0;
        for (size_t band_index = region.first_band; band_index < band_end;
             band_index++) {
            const db_render_ir_band_t band = view->bands[band_index];
            size_t span_end = 0U;
            if ((band.y_start < 0) || (band.y_end <= band.y_start) ||
                (band.y_start < previous_y_end) || (band.span_count == 0U) ||
                (db_try_add_size((size_t)band.first_span, band.span_count,
                                 &span_end) == 0) ||
                (span_end > view->span_count) || (view->spans == NULL)) {
                return DB_RENDER_IR_INVALID;
            }
            int32_t previous_x_end = -1;
            for (size_t span_index = band.first_span; span_index < span_end;
                 span_index++) {
                const db_render_ir_span_t span = view->spans[span_index];
                if ((span.x_start < 0) || (span.x_end <= span.x_start) ||
                    (span.x_start <= previous_x_end)) {
                    return DB_RENDER_IR_INVALID;
                }
                previous_x_end = span.x_end;
            }
            if ((have_previous_band != 0) && (band.y_start == previous_y_end) &&
                (band.span_count == previous_band.span_count)) {
                int identical = 1;
                for (uint32_t span_offset = 0U; span_offset < band.span_count;
                     span_offset++) {
                    const db_render_ir_span_t current =
                        view->spans[band.first_span + span_offset];
                    const db_render_ir_span_t previous =
                        view->spans[previous_band.first_span + span_offset];
                    if ((current.x_start != previous.x_start) ||
                        (current.x_end != previous.x_end)) {
                        identical = 0;
                        break;
                    }
                }
                if (identical != 0) {
                    return DB_RENDER_IR_INVALID;
                }
            }
            previous_band = band;
            previous_y_end = band.y_end;
            have_previous_band = 1;
        }
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    uint32_t command_count = 0U;
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->sequence != command_count) {
            return DB_RENDER_IR_INVALID;
        }
        command_count++;
        size_t expected_size = 0U;
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            expected_size = sizeof(db_render_ir_command_header_t);
            break;
        case DB_RENDER_IR_OP_CLEAR:
            expected_size = sizeof(db_render_ir_clear_command_t);
            break;
        case DB_RENDER_IR_OP_FILL_RECTS:
            expected_size = sizeof(db_render_ir_fill_command_t);
            break;
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
            expected_size = sizeof(db_render_ir_linear_gradient_command_t);
            break;
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
            expected_size = sizeof(db_render_ir_upload_command_t);
            break;
        default:
            break;
        }
        size_t expected_aligned_size = 0U;
        if ((command->opcode > DB_RENDER_IR_OP_INVALIDATE_RESOURCE) ||
            (expected_size == 0U) ||
            (validation_align_size(expected_size, &expected_aligned_size) ==
             0) ||
            (command->byte_size != expected_aligned_size) ||
            (command->destination >= view->resource_count) ||
            ((command->clip_region != DB_RENDER_IR_INVALID_ID) &&
             (command->clip_region >= view->region_count)) ||
            ((command->touched_region != DB_RENDER_IR_INVALID_ID) &&
             (command->touched_region >= view->region_count)) ||
            ((command->full_coverage_region != DB_RENDER_IR_INVALID_ID) &&
             (command->full_coverage_region >= view->region_count))) {
            return DB_RENDER_IR_INVALID;
        }
        const db_render_ir_resource_t command_destination =
            view->resources[command->destination];
        if ((region_fits_resource(view, command->clip_region,
                                  command_destination) == 0) ||
            (region_fits_resource(view, command->touched_region,
                                  command_destination) == 0) ||
            (region_fits_resource(view, command->full_coverage_region,
                                  command_destination) == 0)) {
            return DB_RENDER_IR_INVALID;
        }
        if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fill_command =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            size_t fill_end = 0U;
            if ((db_try_add_size((size_t)fill_command->first_fill,
                                 fill_command->fill_count, &fill_end) == 0) ||
                (fill_end > view->fill_count)) {
                return DB_RENDER_IR_INVALID;
            }
            for (size_t index = fill_command->first_fill; index < fill_end;
                 index++) {
                int32_t x_end = 0;
                int32_t y_end = 0;
                if (db_render_ir_rect_endpoints(view->fills[index].rect, &x_end,
                                                &y_end) == 0) {
                    return DB_RENDER_IR_INVALID;
                }
            }
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            const db_render_ir_resource_t destination =
                view->resources[command->destination];
            db_grid_block_t checked = {0};
            if ((db_render_ir_rect_is_empty(gradient->bounds) != 0) ||
                (gradient->axis_end < gradient->axis_start) ||
                (gradient->reverse_stops > 1U) ||
                (db_render_ir_rect_to_grid_block(
                     gradient->bounds, destination.width, destination.height,
                     &checked) == 0)) {
                return DB_RENDER_IR_INVALID;
            }
        } else if (command->opcode == DB_RENDER_IR_OP_UPLOAD_IMAGE) {
            const db_render_ir_upload_command_t *const upload =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
            if (upload->source >= view->resource_count) {
                return DB_RENDER_IR_INVALID;
            }
            const db_render_ir_resource_t source =
                view->resources[upload->source];
            const db_render_ir_resource_t destination =
                view->resources[command->destination];
            const db_render_ir_rect_t destination_rect = {
                .x = upload->destination_x,
                .y = upload->destination_y,
                .width = upload->source_rect.width,
                .height = upload->source_rect.height,
            };
            db_grid_block_t checked_source = {0};
            db_grid_block_t checked_destination = {0};
            if ((db_render_ir_rect_to_grid_block(upload->source_rect,
                                                 source.width, source.height,
                                                 &checked_source) == 0) ||
                (db_render_ir_rect_to_grid_block(
                     destination_rect, destination.width, destination.height,
                     &checked_destination) == 0) ||
                (source.format != destination.format) ||
                !isfinite(upload->semantics.opacity) ||
                (upload->semantics.opacity < 0.0) ||
                (upload->semantics.opacity > 1.0) ||
                (upload->semantics.replacement !=
                 DB_RENDER_IR_UPLOAD_REPLACE_EXACT) ||
                (upload->semantics.filter != DB_RENDER_IR_FILTER_NEAREST) ||
                (upload->semantics.conversion !=
                 DB_RENDER_IR_CONVERSION_EXACT)) {
                return DB_RENDER_IR_INVALID;
            }
        }
    }
    if ((iterator.offset != view->command_size) ||
        (command_count != view->command_count)) {
        return DB_RENDER_IR_INVALID;
    }
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t
db_render_ir_validate_bindings(const db_render_ir_view_t *view,
                               db_render_ir_external_binding_view_t bindings) {
    if ((db_render_ir_validate(view) != DB_RENDER_IR_OK) ||
        (bindings.count > DB_RENDER_IR_EXTERNAL_BINDING_CAPACITY) ||
        ((bindings.count > 0U) && (bindings.bindings == NULL))) {
        return DB_RENDER_IR_INVALID;
    }
    for (size_t index = 0U; index < bindings.count; index++) {
        const db_render_ir_external_binding_t *const binding =
            &bindings.bindings[index];
        if ((binding->resource >= view->resource_count) ||
            ((index > 0U) &&
             (bindings.bindings[index - 1U].resource >= binding->resource)) ||
            (binding->pixels == NULL) || (binding->width == 0U) ||
            (binding->height == 0U) || (binding->row_stride_bytes == 0U) ||
            (binding->size_bytes == 0U)) {
            return DB_RENDER_IR_INVALID;
        }
        const db_render_ir_resource_t resource =
            view->resources[binding->resource];
        const size_t pixel_bytes =
            db_pixel_format_bytes_per_pixel(resource.format);
        size_t row_bytes = 0U;
        size_t required_bytes = 0U;
        const uintptr_t pixel_address = (uintptr_t)binding->pixels;
        if ((resource.kind != DB_RENDER_IR_RESOURCE_RASTER_SOURCE) ||
            (resource.width != binding->width) ||
            (resource.height != binding->height) ||
            (resource.format != binding->format) || (pixel_bytes == 0U) ||
            (db_try_mul_size(resource.width, pixel_bytes, &row_bytes) == 0) ||
            (db_try_strided_size(resource.height, binding->row_stride_bytes,
                                 row_bytes, &required_bytes) == 0) ||
            (binding->size_bytes < required_bytes) ||
            (required_bytes > (UINTPTR_MAX - pixel_address))) {
            return DB_RENDER_IR_INVALID;
        }
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode != DB_RENDER_IR_OP_UPLOAD_IMAGE) {
            continue;
        }
        const db_render_ir_upload_command_t *const upload =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
        db_render_ir_external_binding_t binding = {0};
        if (db_render_ir_find_binding(bindings, upload->source, &binding) ==
            0) {
            return DB_RENDER_IR_INVALID;
        }
    }
    return DB_RENDER_IR_OK;
}
