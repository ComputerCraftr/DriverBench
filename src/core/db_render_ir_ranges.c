#include "db_numeric.h"
#include "db_render_ir.h"
#include "db_render_ir_ranges_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static int clip_regions_equal(const db_render_ir_view_t *view,
                              db_render_ir_region_id_t lhs,
                              db_render_ir_region_id_t rhs) {
    if (lhs == rhs) {
        return 1;
    }
    if ((lhs == DB_RENDER_IR_INVALID_ID) || (rhs == DB_RENDER_IR_INVALID_ID)) {
        return 0;
    }
    return db_render_ir_regions_equal(view, lhs, rhs);
}

static int
command_regions_are_valid(const db_render_ir_view_t *view,
                          const db_render_ir_command_header_t *command) {
    const db_render_ir_region_id_t regions[] = {
        command->clip_region,
        command->touched_region,
        command->full_coverage_region,
    };
    for (size_t index = 0U; index < sizeof(regions) / sizeof(regions[0]);
         index++) {
        if (regions[index] == DB_RENDER_IR_INVALID_ID) {
            continue;
        }
        if ((view == NULL) || (regions[index] >= view->region_count) ||
            (view->regions == NULL)) {
            return 0;
        }
    }
    return 1;
}

static size_t command_record_size(uint8_t opcode) {
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
command_shape_is_valid(const db_render_ir_command_header_t *command) {
    if (command == NULL) {
        return 0;
    }
    const size_t expected = command_record_size(command->opcode);
    return DB_BOOL((expected > 0U) && (expected <= UINT16_MAX) &&
                   (command->byte_size == expected));
}

int db_render_ir_commands_batch_compatible_validated(
    const db_render_ir_view_t *view, const db_render_ir_command_header_t *lhs,
    const db_render_ir_command_header_t *rhs) {
    return DB_BOOL(
        (view != NULL) && (lhs != NULL) && (rhs != NULL) &&
        (command_shape_is_valid(lhs) != 0) &&
        (command_shape_is_valid(rhs) != 0) && (view->resources != NULL) &&
        (lhs->destination < view->resource_count) &&
        (rhs->destination < view->resource_count) &&
        (command_regions_are_valid(view, lhs) != 0) &&
        (command_regions_are_valid(view, rhs) != 0) &&
        (lhs->opcode == rhs->opcode) &&
        (lhs->opcode != DB_RENDER_IR_OP_UPLOAD_IMAGE) &&
        (lhs->destination == rhs->destination) &&
        (lhs->composite == rhs->composite) &&
        (lhs->source_access == rhs->source_access) &&
        (lhs->destination_access == rhs->destination_access) &&
        (lhs->flags == rhs->flags) &&
        (lhs->ordering_domain == rhs->ordering_domain) &&
        (lhs->inseparable_group == rhs->inseparable_group) &&
        (clip_regions_equal(view, lhs->clip_region, rhs->clip_region) != 0));
}

int db_render_ir_commands_batch_compatible(
    const db_render_ir_view_t *view, const db_render_ir_command_header_t *lhs,
    const db_render_ir_command_header_t *rhs) {
    if ((view == NULL) || (db_render_ir_validate(view) != DB_RENDER_IR_OK)) {
        return 0;
    }
    return db_render_ir_commands_batch_compatible_validated(view, lhs, rhs);
}

typedef struct {
    db_render_ir_rect_t bounds;
    uint32_t instance_count;
    uint32_t fallback_instance_count;
    int has_bounds;
    int semantic_gradient_eligible;
} command_geometry_t;

static int rect_union(db_render_ir_rect_t lhs, db_render_ir_rect_t rhs,
                      db_render_ir_rect_t *result);

static int region_bounds(const db_render_ir_view_t *view,
                         db_render_ir_region_id_t region_id,
                         db_render_ir_rect_t *bounds) {
    if ((view == NULL) || (bounds == NULL) ||
        (region_id >= view->region_count)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    int have_bounds = 0;
    db_render_ir_rect_t combined = {0};
    for (uint32_t band_offset = 0U; band_offset < region.band_count;
         band_offset++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_offset];
        for (uint32_t span_offset = 0U; span_offset < band.span_count;
             span_offset++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_offset];
            const db_render_ir_rect_t rect = {
                .x = span.x_start,
                .y = band.y_start,
                .width = span.x_end - span.x_start,
                .height = band.y_end - band.y_start,
            };
            if (have_bounds == 0) {
                combined = rect;
                have_bounds = 1;
            } else if (rect_union(combined, rect, &combined) == 0) {
                return 0;
            }
        }
    }
    if (have_bounds != 0) {
        *bounds = combined;
    }
    return have_bounds;
}

static int apply_effective_bounds(const db_render_ir_view_t *view,
                                  const db_render_ir_command_header_t *command,
                                  command_geometry_t *geometry) {
    if (command->touched_region == DB_RENDER_IR_INVALID_ID) {
        return 1;
    }
    if (region_bounds(view, command->touched_region, &geometry->bounds) == 0) {
        return 0;
    }
    geometry->has_bounds = 1;
    return 1;
}

static int rect_union(db_render_ir_rect_t lhs, db_render_ir_rect_t rhs,
                      db_render_ir_rect_t *result) {
    const int64_t x_start = DB_MIN((int64_t)lhs.x, (int64_t)rhs.x);
    const int64_t y_start = DB_MIN((int64_t)lhs.y, (int64_t)rhs.y);
    const int64_t x_end =
        DB_MAX((int64_t)lhs.x + lhs.width, (int64_t)rhs.x + rhs.width);
    const int64_t y_end =
        DB_MAX((int64_t)lhs.y + lhs.height, (int64_t)rhs.y + rhs.height);
    const int64_t width = x_end - x_start;
    const int64_t height = y_end - y_start;
    if ((result == NULL) || (x_start < INT32_MIN) || (x_start > INT32_MAX) ||
        (y_start < INT32_MIN) || (y_start > INT32_MAX) || (width <= 0) ||
        (width > INT32_MAX) || (height <= 0) || (height > INT32_MAX)) {
        return 0;
    }
    *result = (db_render_ir_rect_t){
        .x = (int32_t)x_start,
        .y = (int32_t)y_start,
        .width = (int32_t)width,
        .height = (int32_t)height,
    };
    return 1;
}

static int command_geometry(const db_render_ir_view_t *view,
                            const db_render_ir_command_header_t *command,
                            command_geometry_t *geometry) {
    if ((view == NULL) || (command == NULL) || (geometry == NULL)) {
        return 0;
    }
    *geometry = (command_geometry_t){0};
    switch ((db_render_ir_opcode_t)command->opcode) {
    case DB_RENDER_IR_OP_CLEAR: {
        if (command->destination >= view->resource_count) {
            return 0;
        }
        const db_render_ir_resource_t target =
            view->resources[command->destination];
        if ((target.width > INT32_MAX) || (target.height > INT32_MAX)) {
            return 0;
        }
        geometry->bounds = (db_render_ir_rect_t){
            .width = (int32_t)target.width,
            .height = (int32_t)target.height,
        };
        geometry->instance_count = 1U;
        geometry->fallback_instance_count = 1U;
        geometry->has_bounds = 1;
        return apply_effective_bounds(view, command, geometry);
    }
    case DB_RENDER_IR_OP_FILL_RECTS: {
        const db_render_ir_fill_command_t *const fills =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
        if ((fills->first_fill > view->fill_count) ||
            (fills->fill_count > (view->fill_count - fills->first_fill))) {
            return 0;
        }
        for (uint32_t index = 0U; index < fills->fill_count; index++) {
            const db_render_ir_rect_t rect =
                view->fills[fills->first_fill + index].rect;
            if (geometry->has_bounds == 0) {
                geometry->bounds = rect;
                geometry->has_bounds = 1;
            } else if (rect_union(geometry->bounds, rect, &geometry->bounds) ==
                       0) {
                return 0;
            }
        }
        geometry->instance_count = fills->fill_count;
        geometry->fallback_instance_count = fills->fill_count;
        return apply_effective_bounds(view, command, geometry);
    }
    case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
        const db_render_ir_linear_gradient_command_t *const gradient =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                    command);
        if (gradient->bounds.height <= 0) {
            return 0;
        }
        geometry->bounds = gradient->bounds;
        geometry->instance_count = 1U;
        geometry->fallback_instance_count = (uint32_t)gradient->bounds.height;
        if ((command->clip_region != DB_RENDER_IR_INVALID_ID) &&
            (db_render_ir_region_row_span_count(
                 view, command->clip_region,
                 &geometry->fallback_instance_count) == 0)) {
            return 0;
        }
        geometry->has_bounds = 1;
        geometry->semantic_gradient_eligible =
            DB_BOOL(command->clip_region == DB_RENDER_IR_INVALID_ID);
        return apply_effective_bounds(view, command, geometry);
    }
    case DB_RENDER_IR_OP_UPLOAD_IMAGE:
    case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
    case DB_RENDER_IR_OP_BEGIN_TARGET:
    case DB_RENDER_IR_OP_END_TARGET:
        return 1;
    }
    return 0;
}

static int command_range_extend(db_render_ir_command_range_t *range,
                                const command_geometry_t *geometry,
                                db_render_ir_region_id_t touched_region,
                                const db_render_ir_view_t *view) {
    db_render_ir_rect_t bounds = range->bounds;
    if ((range->command_count == UINT32_MAX) ||
        (geometry->instance_count > (UINT32_MAX - range->instance_count)) ||
        (geometry->fallback_instance_count >
         (UINT32_MAX - range->fallback_instance_count)) ||
        ((range->has_bounds != 0U) && (geometry->has_bounds != 0) &&
         (rect_union(range->bounds, geometry->bounds, &bounds) == 0))) {
        return 0;
    }
    range->command_count++;
    range->instance_count += geometry->instance_count;
    range->fallback_instance_count += geometry->fallback_instance_count;
    range->bounds = bounds;
    if (geometry->has_bounds != 0) {
        range->has_bounds = 1U;
    }
    range->semantic_gradient_eligible =
        (uint8_t)DB_BOOL((range->semantic_gradient_eligible != 0U) &&
                         (geometry->semantic_gradient_eligible != 0));
    if ((range->region != touched_region) &&
        (db_render_ir_regions_equal(view, range->region, touched_region) ==
         0)) {
        range->region = DB_RENDER_IR_INVALID_ID;
    }
    return 1;
}

static db_render_ir_command_range_t
command_range_begin(db_render_ir_stream_t stream,
                    const db_render_ir_command_header_t *command,
                    const command_geometry_t *geometry) {
    db_render_ir_prior_content_t prior = DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT;
    if (command->opcode == DB_RENDER_IR_OP_UPLOAD_IMAGE) {
        prior =
            (DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command))
                ->semantics.prior_content;
    }
    return (db_render_ir_command_range_t){
        .stream = stream,
        .first_sequence = command->sequence,
        .command_count = 1U,
        .region = command->touched_region,
        .instance_count = geometry->instance_count,
        .fallback_instance_count = geometry->fallback_instance_count,
        .ordering_domain = command->ordering_domain,
        .inseparable_group = command->inseparable_group,
        .bounds = geometry->bounds,
        .prior_content = prior,
        .opcode = command->opcode,
        .has_bounds = (uint8_t)DB_BOOL(geometry->has_bounds != 0),
        .semantic_gradient_eligible =
            (uint8_t)DB_BOOL(geometry->semantic_gradient_eligible != 0),
    };
}

static int collect_command_ranges_pass(const db_render_ir_view_t *view,
                                       db_render_ir_stream_t stream,
                                       db_render_ir_command_range_t *ranges,
                                       size_t *out_count) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    db_render_ir_command_header_t previous = {0};
    db_render_ir_command_range_t current = {0};
    int have_previous = 0;
    const db_render_ir_command_header_t *command = NULL;
    size_t count = 0U;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) ||
            (command->opcode == DB_RENDER_IR_OP_END_TARGET)) {
            continue;
        }
        command_geometry_t geometry = {0};
        if (command_geometry(view, command, &geometry) == 0) {
            return 0;
        }
        if ((have_previous != 0) &&
            (db_render_ir_commands_batch_compatible_validated(view, &previous,
                                                              command) != 0)) {
            if (command_range_extend(&current, &geometry,
                                     command->touched_region, view) == 0) {
                return 0;
            }
            previous = *command;
            continue;
        }
        if (have_previous != 0) {
            if (ranges != NULL) {
                ranges[count] = current;
            }
            count++;
        }
        current = command_range_begin(stream, command, &geometry);
        previous = *command;
        have_previous = 1;
    }
    if (have_previous != 0) {
        if (ranges != NULL) {
            ranges[count] = current;
        }
        count++;
    }
    *out_count = count;
    return 1;
}

size_t db_render_ir_collect_command_ranges(const db_render_ir_view_t *view,
                                           db_render_ir_stream_t stream,
                                           db_render_ir_command_range_t *ranges,
                                           size_t range_capacity,
                                           int *out_overflow) {
    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    if ((view == NULL) || ((ranges == NULL) && (range_capacity > 0U)) ||
        (db_render_ir_validate(view) != DB_RENDER_IR_OK)) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    size_t required_count = 0U;
    if ((collect_command_ranges_pass(view, stream, NULL, &required_count) ==
         0) ||
        (required_count > range_capacity)) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    size_t published_count = 0U;
    if (collect_command_ranges_pass(view, stream, ranges, &published_count) ==
        0) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    return published_count;
}
