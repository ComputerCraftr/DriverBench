#include "db_render_ir.h"

#include "db_core.h"
#include "db_geometry.h"
#include "db_hash.h"
#include "db_numeric.h"
#include "db_render_ir_ranges_internal.h"

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
        (iterator->view->commands == NULL) ||
        (iterator->offset >= iterator->view->command_size)) {
        return NULL;
    }
    if ((iterator->view->command_size - iterator->offset) <
        sizeof(iterator->current.header)) {
        iterator->offset = iterator->view->command_size;
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
    size_t command_count = 0U;
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        command_count++;
        if (command->touched_region != DB_RENDER_IR_INVALID_ID) {
            result = command->touched_region;
        }
    }
    if ((iterator.offset != view->command_size) ||
        (command_count != view->command_count)) {
        return DB_RENDER_IR_INVALID_ID;
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
    if ((view == NULL) || (db_render_ir_validate(view) != DB_RENDER_IR_OK)) {
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
        hash = db_fnv1a64_mix_u64(hash, command->ordering_domain);
        hash = db_fnv1a64_mix_u64(hash, command->inseparable_group);
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
    for (size_t region_index = 0U; region_index < view->region_count;
         region_index++) {
        const db_render_ir_region_t region = view->regions[region_index];
        hash = db_fnv1a64_mix_u64(hash, region.band_count);
        for (uint32_t band_offset = 0U; band_offset < region.band_count;
             band_offset++) {
            const db_render_ir_band_t band =
                view->bands[region.first_band + band_offset];
            hash = db_fnv1a64_mix_u64(hash, (uint32_t)band.y_start);
            hash = db_fnv1a64_mix_u64(hash, (uint32_t)band.y_end);
            hash = db_fnv1a64_mix_u64(hash, band.span_count);
            for (uint32_t span_offset = 0U; span_offset < band.span_count;
                 span_offset++) {
                const db_render_ir_span_t span =
                    view->spans[band.first_span + span_offset];
                hash = db_fnv1a64_mix_u64(hash, (uint32_t)span.x_start);
                hash = db_fnv1a64_mix_u64(hash, (uint32_t)span.x_end);
            }
        }
    }
    return hash;
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
    case DB_RENDER_IR_COMPLEXITY_LIMIT:
        return "complexity_limit";
    }
    return "unknown";
}

int db_render_ir_resolve_full_upload(
    const db_render_ir_view_t *view,
    db_render_ir_external_binding_view_t bindings,
    db_render_ir_upload_command_t *output_command,
    db_render_ir_external_binding_t *output_binding) {
    if ((view == NULL) || (output_command == NULL) ||
        (output_binding == NULL) ||
        (db_render_ir_validate_bindings(view, bindings) != DB_RENDER_IR_OK)) {
        return 0;
    }
    db_render_ir_upload_command_t resolved_command = {0};
    db_render_ir_external_binding_t resolved_binding = {0};
    int found = 0;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode != DB_RENDER_IR_OP_UPLOAD_IMAGE) {
            continue;
        }
        if (found != 0) {
            return 0;
        }
        const db_render_ir_upload_command_t *const upload =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
        db_render_ir_external_binding_t binding = {0};
        if ((command->destination >= view->resource_count) ||
            (db_render_ir_find_binding(bindings, upload->source, &binding) ==
             0)) {
            return 0;
        }
        const db_render_ir_resource_t destination =
            view->resources[command->destination];
        if ((upload->source_rect.x != 0) || (upload->source_rect.y != 0) ||
            (upload->destination_x != 0) || (upload->destination_y != 0) ||
            ((uint32_t)upload->source_rect.width != binding.width) ||
            ((uint32_t)upload->source_rect.height != binding.height) ||
            (destination.width != binding.width) ||
            (destination.height != binding.height) ||
            (destination.format != binding.format)) {
            return 0;
        }
        resolved_command = *upload;
        resolved_binding = binding;
        found = 1;
    }
    if (found == 0) {
        return 0;
    }
    *output_command = resolved_command;
    *output_binding = resolved_binding;
    return 1;
}

static int metadata_add_u32(db_render_ir_metadata_t *metadata, uint32_t *field,
                            uint32_t value) {
    if ((metadata == NULL) || (field == NULL) ||
        (value > (UINT32_MAX - *field))) {
        if (metadata != NULL) {
            metadata->status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
            metadata->worker_partitionable = 0;
        }
        return 0;
    }
    *field += value;
    return 1;
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
    if (db_render_ir_validate(view) != DB_RENDER_IR_OK) {
        metadata.status = DB_RENDER_IR_INVALID;
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
        uint64_t span_count = 0U;
        for (uint32_t index = 0U; index < region.band_count; index++) {
            span_count += view->bands[region.first_band + index].span_count;
        }
        metadata.fragmented = DB_BOOL(span_count > region.band_count);
    }

    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    db_render_ir_command_header_t previous = {0};
    int have_previous = 0;
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) ||
            (command->opcode == DB_RENDER_IR_OP_END_TARGET)) {
            continue;
        }
        if ((have_previous == 0) ||
            (db_render_ir_commands_batch_compatible_validated(view, &previous,
                                                              command) == 0)) {
            if (metadata_add_u32(&metadata, &metadata.compatible_batch_count,
                                 1U) == 0) {
                return metadata;
            }
        }
        previous = *command;
        have_previous = 1;
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            if ((metadata_add_u32(&metadata, &metadata.instance_count,
                                  fills->fill_count) == 0) ||
                (metadata_add_u32(&metadata, &metadata.solid_command_count,
                                  1U) == 0)) {
                return metadata;
            }
            break;
        }
        case DB_RENDER_IR_OP_CLEAR:
            if ((metadata_add_u32(&metadata, &metadata.instance_count, 1U) ==
                 0) ||
                (metadata_add_u32(&metadata, &metadata.solid_command_count,
                                  1U) == 0)) {
                return metadata;
            }
            break;
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            uint32_t fallback_instances =
                db_checked_i32_to_u32("render_ir", "gradient_fallback_height",
                                      gradient->bounds.height);
            if ((command->clip_region != DB_RENDER_IR_INVALID_ID) &&
                (db_render_ir_region_row_span_count(
                     view, command->clip_region, &fallback_instances) == 0)) {
                metadata.status = DB_RENDER_IR_ARITHMETIC_OVERFLOW;
                metadata.worker_partitionable = 0;
                return metadata;
            }
            if ((metadata_add_u32(&metadata, &metadata.instance_count, 1U) ==
                 0) ||
                (metadata_add_u32(&metadata, &metadata.gradient_count, 1U) ==
                 0) ||
                (metadata_add_u32(&metadata,
                                  &metadata.exact_fallback_instance_count,
                                  fallback_instances) == 0)) {
                return metadata;
            }
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
            if (metadata_add_u32(&metadata, &metadata.instance_count, 1U) ==
                0) {
                return metadata;
            }
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
        (db_render_ir_validate(view) != DB_RENDER_IR_OK) ||
        ((output == NULL) && (output_capacity > 0U))) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    size_t required_count = 0U;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        if (db_try_add_size(required_count, band.span_count, &required_count) ==
            0) {
            if (out_overflow != NULL) {
                *out_overflow = 1;
            }
            return 0U;
        }
    }
    if (required_count > output_capacity) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    if (required_count == 0U) {
        return 0U;
    }
    if (output == NULL) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    size_t count = 0U;
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
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

int db_render_ir_region_row_span_count(const db_render_ir_view_t *view,
                                       db_render_ir_region_id_t region_id,
                                       uint32_t *count) {
    size_t validated_span_count = 0U;
    if ((count == NULL) || (db_render_ir_region_validate(
                                view, region_id, &validated_span_count) == 0)) {
        return 0;
    }
    (void)validated_span_count;
    uint64_t total = 0U;
    const db_render_ir_region_t region = view->regions[region_id];
    for (uint32_t band_offset = 0U; band_offset < region.band_count;
         band_offset++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_offset];
        const uint64_t rows = (uint32_t)(band.y_end - band.y_start);
        const uint64_t additions = rows * band.span_count;
        if ((additions > UINT32_MAX) || (total > UINT32_MAX - additions)) {
            return 0;
        }
        total += additions;
    }
    *count = (uint32_t)total;
    return 1;
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

void db_render_ir_rect_iterator_begin(db_render_ir_rect_iterator_t *iterator,
                                      const db_render_ir_view_t *view) {
    if (iterator == NULL) {
        return;
    }
    *iterator = (db_render_ir_rect_iterator_t){0};
    db_render_ir_iterator_begin(&iterator->commands, view);
}

static size_t rect_iterator_expected_command_size(uint8_t opcode) {
    switch ((db_render_ir_opcode_t)opcode) {
    case DB_RENDER_IR_OP_BEGIN_TARGET:
    case DB_RENDER_IR_OP_END_TARGET:
    case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
        return DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_command_header_t);
    case DB_RENDER_IR_OP_CLEAR:
        return DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_clear_command_t);
    case DB_RENDER_IR_OP_FILL_RECTS:
        return DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_fill_command_t);
    case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
        return DB_RENDER_IR_ALIGNED_RECORD_SIZE(
            db_render_ir_linear_gradient_command_t);
    case DB_RENDER_IR_OP_UPLOAD_IMAGE:
        return DB_RENDER_IR_ALIGNED_RECORD_SIZE(db_render_ir_upload_command_t);
    }
    return 0U;
}

static int
rect_iterator_command_is_safe(const db_render_ir_view_t *view,
                              const db_render_ir_command_header_t *command) {
    if ((view == NULL) || (command == NULL) ||
        (command->byte_size !=
         rect_iterator_expected_command_size(command->opcode)) ||
        (command->destination >= view->resource_count) ||
        ((view->resource_count > 0U) && (view->resources == NULL))) {
        return 0;
    }
    if (command->clip_region != DB_RENDER_IR_INVALID_ID) {
        size_t ignored_span_count = 0U;
        if (db_render_ir_region_validate(view, command->clip_region,
                                         &ignored_span_count) == 0) {
            return 0;
        }
    }
    if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
        const db_render_ir_fill_command_t *const fills =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
        size_t fill_end = 0U;
        if (((view->fill_count > 0U) && (view->fills == NULL)) ||
            (db_try_add_size(fills->first_fill, fills->fill_count, &fill_end) ==
             0) ||
            (fill_end > view->fill_count)) {
            return 0;
        }
        for (size_t index = fills->first_fill; index < fill_end; index++) {
            int32_t x_end = 0;
            int32_t y_end = 0;
            if (db_render_ir_rect_endpoints(view->fills[index].rect, &x_end,
                                            &y_end) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

void db_render_ir_rect_iterator_begin_command(
    db_render_ir_rect_iterator_t *iterator, const db_render_ir_view_t *view,
    const db_render_ir_command_header_t *command) {
    if (iterator == NULL) {
        return;
    }
    *iterator = (db_render_ir_rect_iterator_t){0};
    if (rect_iterator_command_is_safe(view, command) == 0) {
        return;
    }
    iterator->commands.view = view;
    iterator->commands.offset = view->command_size;
    memcpy(&iterator->commands.current, command, command->byte_size);
    iterator->active_command = &iterator->commands.current.header;
    iterator->active_command_checked = 1;
    if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
        iterator->gradient_row =
            iterator->commands.current.linear_gradient.bounds.y;
    }
}

static int rect_iterator_next_unclipped(db_render_ir_rect_iterator_t *iterator,
                                        db_render_ir_fill_t *fill) {
    for (;;) {
        const db_render_ir_command_header_t *command = iterator->active_command;
        if (command == NULL) {
            command = db_render_ir_iterator_next(&iterator->commands);
            iterator->active_command = command;
            iterator->active_command_checked = 0;
            iterator->fill_index = 0U;
            if (command == NULL) {
                return 0;
            }
            if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
                iterator->gradient_row =
                    (DB_RENDER_IR_COMMAND_AS(
                         db_render_ir_linear_gradient_command_t, command))
                        ->bounds.y;
            }
        }
        if (iterator->active_command_checked == 0) {
            if (rect_iterator_command_is_safe(iterator->commands.view,
                                              command) == 0) {
                iterator->commands.offset =
                    iterator->commands.view->command_size;
                iterator->active_command = NULL;
                return 0;
            }
            iterator->active_command_checked = 1;
        }
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_CLEAR:
            if ((iterator->fill_index == 0U) &&
                (command->destination <
                 iterator->commands.view->resource_count)) {
                const db_render_ir_resource_t target =
                    iterator->commands.view->resources[command->destination];
                db_render_ir_rect_t target_rect = {0};
                iterator->fill_index = 1U;
                if (db_render_ir_rect_from_extent(target.width, target.height,
                                                  &target_rect) != 0) {
                    *fill = (db_render_ir_fill_t){
                        .rect = target_rect,
                        .color = DB_RENDER_IR_COMMAND_AS(
                                     db_render_ir_clear_command_t, command)
                                     ->color,
                    };
                    return 1;
                }
            }
            break;
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            if (iterator->fill_index < fills->fill_count) {
                *fill = iterator->commands.view
                            ->fills[fills->first_fill + iterator->fill_index++];
                return 1;
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            const int64_t row_end =
                (int64_t)gradient->bounds.y + gradient->bounds.height;
            if ((row_end >= INT32_MIN) && (row_end <= INT32_MAX) &&
                (iterator->gradient_row < (int32_t)row_end)) {
                const int32_t row = iterator->gradient_row++;
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
            break;
        }
        case DB_RENDER_IR_OP_BEGIN_TARGET:
        case DB_RENDER_IR_OP_END_TARGET:
        case DB_RENDER_IR_OP_UPLOAD_IMAGE:
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            break;
        }
        iterator->active_command = NULL;
        iterator->active_command_checked = 0;
    }
}

int db_render_ir_rect_iterator_next(db_render_ir_rect_iterator_t *iterator,
                                    db_render_ir_fill_t *fill) {
    if ((iterator == NULL) || (fill == NULL)) {
        return 0;
    }
    for (;;) {
        if (iterator->active_fill_valid == 0) {
            if (rect_iterator_next_unclipped(iterator,
                                             &iterator->active_fill) == 0) {
                return 0;
            }
            iterator->active_fill_valid = 1;
            iterator->clip_band_index = 0U;
            iterator->clip_span_index = 0U;
        }
        const db_render_ir_command_header_t *const command =
            iterator->active_command;
        if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
            *fill = iterator->active_fill;
            iterator->active_fill_valid = 0;
            return 1;
        }
        const db_render_ir_view_t *const view = iterator->commands.view;
        if (command->clip_region >= view->region_count) {
            return 0;
        }
        const db_render_ir_region_t region =
            view->regions[command->clip_region];
        while (iterator->clip_band_index < region.band_count) {
            const db_render_ir_band_t band =
                view->bands[region.first_band + iterator->clip_band_index];
            while (iterator->clip_span_index < band.span_count) {
                const db_render_ir_span_t span =
                    view->spans[band.first_span + iterator->clip_span_index++];
                db_render_ir_rect_t clipped = {0};
                if (db_render_ir_rect_intersect(
                        iterator->active_fill.rect,
                        (db_render_ir_rect_t){
                            .x = span.x_start,
                            .y = band.y_start,
                            .width = span.x_end - span.x_start,
                            .height = band.y_end - band.y_start,
                        },
                        &clipped) != 0) {
                    *fill = (db_render_ir_fill_t){
                        .rect = clipped, .color = iterator->active_fill.color};
                    return 1;
                }
            }
            iterator->clip_band_index++;
            iterator->clip_span_index = 0U;
        }
        iterator->active_fill_valid = 0;
    }
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
