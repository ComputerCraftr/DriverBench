#include "db_render_ir.h"

#include "db_core.h"

#include "db_geometry.h"
#include "db_render_types.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum { DB_RENDER_IR_VALIDATION_ALIGNMENT = _Alignof(max_align_t) };

static int validation_align_size(size_t value, size_t *result) {
    if ((result == NULL) ||
        (value > (SIZE_MAX - (DB_RENDER_IR_VALIDATION_ALIGNMENT - 1U)))) {
        return 0;
    }
    const size_t expanded = value + DB_RENDER_IR_VALIDATION_ALIGNMENT - 1U;
    *result = expanded & ~(size_t)(DB_RENDER_IR_VALIDATION_ALIGNMENT - 1U);
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
        const size_t band_end = (size_t)region.first_band + region.band_count;
        if (band_end > view->band_count) {
            return DB_RENDER_IR_INVALID;
        }
        int32_t previous_y_end = -1;
        for (size_t band_index = region.first_band; band_index < band_end;
             band_index++) {
            const db_render_ir_band_t band = view->bands[band_index];
            const size_t span_end = (size_t)band.first_span + band.span_count;
            if ((band.y_start < 0) || (band.y_end <= band.y_start) ||
                (band.y_start < previous_y_end) ||
                (span_end > view->span_count)) {
                return DB_RENDER_IR_INVALID;
            }
            previous_y_end = band.y_end;
            int32_t previous_x_end = -1;
            for (size_t span_index = band.first_span; span_index < span_end;
                 span_index++) {
                const db_render_ir_span_t span = view->spans[span_index];
                if ((span.x_start < 0) || (span.x_end <= span.x_start) ||
                    (span.x_start < previous_x_end)) {
                    return DB_RENDER_IR_INVALID;
                }
                previous_x_end = span.x_end;
            }
        }
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    uint32_t command_count = 0U;
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
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
                (const db_render_ir_fill_command_t *)command;
            const size_t fill_end =
                (size_t)fill_command->first_fill + fill_command->fill_count;
            if (fill_end > view->fill_count) {
                return DB_RENDER_IR_INVALID;
            }
            const db_render_ir_resource_t destination =
                view->resources[command->destination];
            for (size_t index = fill_command->first_fill; index < fill_end;
                 index++) {
                db_grid_block_t checked = {0};
                if (db_render_ir_rect_to_grid_block(
                        view->fills[index].rect, destination.width,
                        destination.height, &checked) == 0) {
                    return DB_RENDER_IR_INVALID;
                }
            }
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
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
                (const db_render_ir_upload_command_t *)command;
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
    if (db_render_ir_validate(view) != DB_RENDER_IR_OK) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode != DB_RENDER_IR_OP_UPLOAD_IMAGE) {
            continue;
        }
        const db_render_ir_upload_command_t *const upload =
            (const db_render_ir_upload_command_t *)command;
        const db_render_ir_external_binding_t *const binding =
            db_render_ir_find_binding(bindings, upload->source);
        const db_render_ir_resource_t source = view->resources[upload->source];
        const size_t pixel_bytes = (source.format == DB_PIXEL_FORMAT_RGBA16F)
                                       ? DB_RGBA16F_BYTES_PER_PIXEL
                                       : DB_RGBA8_BYTES_PER_PIXEL;
        size_t row_bytes = 0U;
        size_t required_bytes = 0U;
        if ((binding == NULL) ||
            (db_try_mul_size(source.width, pixel_bytes, &row_bytes) == 0) ||
            (db_try_strided_size(source.height, binding->row_stride_bytes,
                                 row_bytes, &required_bytes) == 0) ||
            (binding->resource != upload->source) ||
            (binding->pixels == NULL) || (binding->width != source.width) ||
            (binding->height != source.height) ||
            (binding->format != source.format) ||
            (binding->size_bytes < required_bytes)) {
            return DB_RENDER_IR_INVALID;
        }
    }
    return DB_RENDER_IR_OK;
}
