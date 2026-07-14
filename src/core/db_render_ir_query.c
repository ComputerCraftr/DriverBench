#include "db_render_ir.h"

#include "db_geometry.h"
#include "db_hash.h"
#include "db_numeric.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void db_render_ir_iterator_begin(db_render_ir_iterator_t *iterator,
                                 const db_render_ir_view_t *view) {
    if (iterator != NULL) {
        *iterator = (db_render_ir_iterator_t){.view = view};
    }
}

const db_render_ir_command_header_t *
db_render_ir_iterator_next(db_render_ir_iterator_t *iterator) {
    if ((iterator == NULL) || (iterator->view == NULL) ||
        (iterator->offset >= iterator->view->command_size)) {
        return NULL;
    }
    const unsigned char *const command =
        (const unsigned char *)iterator->view->commands + iterator->offset;
    memset(&iterator->current, 0, sizeof(iterator->current));
    memcpy(&iterator->current.header, command,
           sizeof(iterator->current.header));
    if ((iterator->current.header.byte_size <
         sizeof(iterator->current.header)) ||
        (iterator->current.header.byte_size > sizeof(iterator->current)) ||
        (iterator->current.header.byte_size >
         (iterator->view->command_size - iterator->offset))) {
        iterator->offset = iterator->view->command_size;
        return NULL;
    }
    memcpy(&iterator->current, command, iterator->current.header.byte_size);
    iterator->offset += iterator->current.header.byte_size;
    return &iterator->current.header;
}

db_render_ir_region_id_t
db_render_ir_final_damage_region(const db_render_ir_view_t *view) {
    if (view == NULL) {
        return DB_RENDER_IR_INVALID_ID;
    }
    db_render_ir_region_id_t result = DB_RENDER_IR_INVALID_ID;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->touched_region != DB_RENDER_IR_INVALID_ID) {
            result = command->touched_region;
        }
    }
    return result;
}

int db_render_ir_final_damage_covers(const db_render_ir_view_t *view,
                                     uint32_t width, uint32_t height) {
    const db_render_ir_region_id_t region =
        db_render_ir_final_damage_region(view);
    if ((region == DB_RENDER_IR_INVALID_ID) || (width == 0U) ||
        (height == 0U)) {
        return 0;
    }
    const uint64_t target_area = (uint64_t)width * (uint64_t)height;
    return DB_BOOL(db_render_ir_region_area(view, region) == target_area);
}

uint64_t db_render_ir_hash(const db_render_ir_view_t *view) {
    if (view == NULL) {
        return 0U;
    }
    uint64_t hash = DB_FNV1A64_OFFSET;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        hash = db_fnv1a64_mix_u64(hash, command->opcode);
        hash = db_fnv1a64_mix_u64(hash, command->composite);
        hash = db_fnv1a64_mix_u64(hash, command->source_access);
        hash = db_fnv1a64_mix_u64(hash, command->destination_access);
        hash = db_fnv1a64_mix_u64(hash, command->sequence);
        hash = db_fnv1a64_mix_u64(hash, command->destination);
        hash = db_fnv1a64_mix_u64(hash, command->clip_region);
        hash = db_fnv1a64_mix_u64(hash, command->touched_region);
        hash = db_fnv1a64_mix_u64(hash, command->full_coverage_region);
        hash = db_fnv1a64_mix_u64(hash, command->flags);
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_CLEAR:
            for (size_t channel = 0U; channel < 4U; channel++) {
                hash = db_fnv1a64_mix_u64(
                    hash, db_f64_to_bits_u64(
                              iterator.current.clear.color.rgba[channel]));
            }
            break;
        case DB_RENDER_IR_OP_FILL_RECTS:
            hash = db_fnv1a64_mix_u64(hash, iterator.current.fills.first_fill);
            hash = db_fnv1a64_mix_u64(hash, iterator.current.fills.fill_count);
            break;
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.bounds.x);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.bounds.y);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.bounds.width);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.bounds.height);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.axis_start);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.linear_gradient.axis_end);
            hash = db_fnv1a64_mix_u64(
                hash, iterator.current.linear_gradient.reverse_stops);
            for (size_t channel = 0U; channel < 4U; channel++) {
                hash = db_fnv1a64_mix_u64(
                    hash, db_f64_to_bits_u64(iterator.current.linear_gradient
                                                 .start_color.rgba[channel]));
                hash = db_fnv1a64_mix_u64(
                    hash, db_f64_to_bits_u64(iterator.current.linear_gradient
                                                 .end_color.rgba[channel]));
            }
            break;
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
            hash = db_fnv1a64_mix_u64(hash, iterator.current.upload.source);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.source_rect.x);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.source_rect.y);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.source_rect.width);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.source_rect.height);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.destination_x);
            hash = db_fnv1a64_mix_u64(
                hash, (uint32_t)iterator.current.upload.destination_y);
            break;
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            break;
        }
    }
    for (size_t index = 0U; index < view->fill_count; index++) {
        const db_render_ir_fill_t fill = view->fills[index];
        hash = db_fnv1a64_mix_u64(hash, (uint32_t)fill.rect.x);
        hash = db_fnv1a64_mix_u64(hash, (uint32_t)fill.rect.y);
        hash = db_fnv1a64_mix_u64(hash, (uint32_t)fill.rect.width);
        hash = db_fnv1a64_mix_u64(hash, (uint32_t)fill.rect.height);
        for (size_t channel = 0U; channel < 4U; channel++) {
            hash = db_fnv1a64_mix_u64(
                hash, db_f64_to_bits_u64(fill.color.rgba[channel]));
        }
    }
    for (size_t index = 0U; index < view->resource_count; index++) {
        const db_render_ir_resource_t resource = view->resources[index];
        hash = db_fnv1a64_mix_u64(hash, resource.kind);
        hash = db_fnv1a64_mix_u64(hash, resource.width);
        hash = db_fnv1a64_mix_u64(hash, resource.height);
        hash = db_fnv1a64_mix_u64(hash, resource.format);
    }
    return hash;
}

static int db_render_ir_commands_batch_compatible(
    const db_render_ir_command_header_t *lhs,
    const db_render_ir_command_header_t *rhs) {
    return DB_BOOL((lhs != NULL) && (rhs != NULL) &&
                   (lhs->opcode == rhs->opcode) &&
                   (lhs->destination == rhs->destination) &&
                   (lhs->composite == rhs->composite) &&
                   (lhs->clip_region == rhs->clip_region));
}

const char *db_render_ir_status_name(db_render_ir_status_t status) {
    switch (status) {
    case DB_RENDER_IR_OK:
        return "ok";
    case DB_RENDER_IR_INVALID:
        return "invalid";
    case DB_RENDER_IR_CAPACITY:
        return "capacity";
    case DB_RENDER_IR_ARITHMETIC_OVERFLOW:
        return "arithmetic_overflow";
    }
    return "unknown";
}

db_render_ir_metadata_t db_render_ir_metadata(const db_render_ir_view_t *view,
                                              db_render_ir_status_t status,
                                              uint32_t target_width,
                                              uint32_t target_height) {
    db_render_ir_metadata_t metadata = {
        .status = status,
        .damage_region = DB_RENDER_IR_INVALID_ID,
        .worker_partitionable = 1,
    };
    if ((view == NULL) || (status != DB_RENDER_IR_OK)) {
        metadata.worker_partitionable = 0;
        return metadata;
    }
    metadata.command_count = view->command_count;
    metadata.damage_region = db_render_ir_final_damage_region(view);
    metadata.damage_area =
        db_render_ir_region_area(view, metadata.damage_region);
    metadata.full_coverage =
        db_render_ir_final_damage_covers(view, target_width, target_height);
    if (metadata.damage_region < view->region_count) {
        const db_render_ir_region_t region =
            view->regions[metadata.damage_region];
        uint32_t span_count = 0U;
        for (uint32_t index = 0U; index < region.band_count; index++) {
            span_count += view->bands[region.first_band + index].span_count;
        }
        metadata.fragmented = DB_BOOL(span_count > region.band_count);
    }

    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *previous = NULL;
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) ||
            (command->opcode == DB_RENDER_IR_OP_END_TARGET)) {
            continue;
        }
        if (db_render_ir_commands_batch_compatible(previous, command) == 0) {
            metadata.compatible_batch_count++;
        }
        previous = command;
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                (const db_render_ir_fill_command_t *)command;
            metadata.instance_count += fills->fill_count;
            metadata.solid_command_count++;
            break;
        }
        case DB_RENDER_IR_OP_CLEAR:
            metadata.instance_count++;
            metadata.solid_command_count++;
            break;
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
            metadata.instance_count++;
            metadata.gradient_count++;
            if (gradient->bounds.height > 0) {
                metadata.exact_fallback_instance_count +=
                    (uint32_t)gradient->bounds.height;
            }
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                (const db_render_ir_upload_command_t *)command;
            metadata.instance_count++;
            if (upload->semantics.prior_content ==
                DB_RENDER_IR_PRIOR_CONTENT_REQUIRED) {
                metadata.prior_content_required = 1;
                metadata.worker_partitionable = 0;
            }
            break;
        }
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            metadata.worker_partitionable = 0;
            break;
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
            break;
        }
    }
    metadata.partitionable_batch_count = (metadata.worker_partitionable != 0)
                                             ? metadata.compatible_batch_count
                                             : 0U;
    return metadata;
}

size_t db_render_ir_region_copy_grid_blocks(const db_render_ir_view_t *view,
                                            db_render_ir_region_id_t region_id,
                                            db_grid_block_t *output,
                                            size_t output_capacity,
                                            int *out_overflow) {
    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    if ((view == NULL) || (region_id >= view->region_count) ||
        ((output == NULL) && (output_capacity > 0U))) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    size_t count = 0U;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
            if (count >= output_capacity) {
                if (out_overflow != NULL) {
                    *out_overflow = 1;
                }
                return count;
            }
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_index];
            output[count++] = (db_grid_block_t){
                .row_start = (uint32_t)band.y_start,
                .row_count = (uint32_t)(band.y_end - band.y_start),
                .col_start = (uint32_t)span.x_start,
                .col_count = (uint32_t)(span.x_end - span.x_start),
            };
        }
    }
    return count;
}

db_render_ir_color_t db_render_ir_linear_gradient_color_at(
    const db_render_ir_linear_gradient_command_t *gradient,
    int32_t logical_row) {
    db_render_ir_color_t color = {0};
    if (gradient == NULL) {
        return color;
    }
    double amount = 0.0;
    if (gradient->axis_end > gradient->axis_start) {
        const int32_t clamped =
            DB_CLAMP(logical_row, gradient->axis_start, gradient->axis_end);
        amount = DB_TO_F64((int64_t)clamped - gradient->axis_start) /
                 DB_TO_F64((int64_t)gradient->axis_end - gradient->axis_start);
    }
    if (gradient->reverse_stops != 0U) {
        amount = 1.0 - amount;
    }
    for (size_t channel = 0U; channel < 4U; channel++) {
        const double start = gradient->start_color.rgba[channel];
        color.rgba[channel] =
            start + ((gradient->end_color.rgba[channel] - start) * amount);
    }
    return color;
}

size_t db_render_ir_rect_count(const db_render_ir_view_t *view) {
    size_t count = 0U;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        size_t added = 0U;
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_FILL_RECTS:
            added = ((const db_render_ir_fill_command_t *)command)->fill_count;
            break;
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
            added = (size_t)((const db_render_ir_linear_gradient_command_t *)
                                 command)
                        ->bounds.height;
            break;
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
        case DB_RENDER_IR_OP_CLEAR:
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            break;
        }
        if (added > (SIZE_MAX - count)) {
            return 0U;
        }
        count += added;
    }
    return count;
}

int db_render_ir_rect_at(const db_render_ir_view_t *view, size_t index,
                         db_render_ir_fill_t *fill) {
    if ((view == NULL) || (fill == NULL)) {
        return 0;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fills =
                (const db_render_ir_fill_command_t *)command;
            if (index < fills->fill_count) {
                *fill = view->fills[fills->first_fill + index];
                return 1;
            }
            index -= fills->fill_count;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
            const size_t row_count = (size_t)gradient->bounds.height;
            if (index < row_count) {
                const int32_t row = gradient->bounds.y + (int32_t)index;
                *fill = (db_render_ir_fill_t){
                    .rect = {.x = gradient->bounds.x,
                             .y = row,
                             .width = gradient->bounds.width,
                             .height = 1},
                    .color =
                        db_render_ir_linear_gradient_color_at(gradient, row),
                };
                return 1;
            }
            index -= row_count;
        }
    }
    return 0;
}

int db_render_ir_rect_to_grid_block(db_render_ir_rect_t rect,
                                    uint32_t grid_width, uint32_t grid_height,
                                    db_grid_block_t *block) {
    if ((block == NULL) || (rect.x < 0) || (rect.y < 0) || (rect.width <= 0) ||
        (rect.height <= 0)) {
        return 0;
    }
    const uint64_t x_end =
        (uint64_t)(uint32_t)rect.x + (uint64_t)(uint32_t)rect.width;
    const uint64_t y_end =
        (uint64_t)(uint32_t)rect.y + (uint64_t)(uint32_t)rect.height;
    if ((x_end > INT32_MAX) || (y_end > INT32_MAX) || (x_end > grid_width) ||
        (y_end > grid_height)) {
        return 0;
    }
    *block = (db_grid_block_t){
        .row_start = (uint32_t)rect.y,
        .row_count = (uint32_t)rect.height,
        .col_start = (uint32_t)rect.x,
        .col_count = (uint32_t)rect.width,
    };
    return 1;
}
