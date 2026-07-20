#include "db_render_ir.h"

#include "db_core.h"
#include "db_numeric.h"
#include "db_render_ir_optimizer_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const void *data;
    size_t element_size;
} optimizer_workspace_range_t;

static int
optimizer_storage_is_disjoint(const db_render_ir_view_t *raw,
                              const db_render_ir_store_t *optimized,
                              db_render_ir_optimizer_workspace_t workspace) {
    const optimizer_workspace_range_t ranges[] = {
        {workspace.primary, sizeof(*workspace.primary)},
        {workspace.secondary, sizeof(*workspace.secondary)},
        {workspace.coverage_bands, sizeof(*workspace.coverage_bands)},
        {workspace.coverage_band_scratch,
         sizeof(*workspace.coverage_band_scratch)},
        {workspace.coverage_spans, sizeof(*workspace.coverage_spans)},
        {workspace.coverage_span_scratch,
         sizeof(*workspace.coverage_span_scratch)},
    };
    size_t range_bytes[sizeof(ranges) / sizeof(ranges[0])] = {0};
    for (size_t index = 0U; index < sizeof(ranges) / sizeof(ranges[0]);
         index++) {
        if ((ranges[index].data == NULL) ||
            (db_try_mul_size(workspace.capacity, ranges[index].element_size,
                             &range_bytes[index]) == 0)) {
            return 0;
        }
        int wrapped = 0;
        if (db_memory_ranges_overlap(ranges[index].data, range_bytes[index],
                                     ranges[index].data, range_bytes[index],
                                     &wrapped) == 0) {
            return 0;
        }
        for (size_t other = index + 1U;
             other < sizeof(ranges) / sizeof(ranges[0]); other++) {
            size_t other_bytes = 0U;
            int overlap = 0;
            if ((ranges[other].data == NULL) ||
                (db_try_mul_size(workspace.capacity, ranges[other].element_size,
                                 &other_bytes) == 0) ||
                (db_memory_ranges_overlap(
                     ranges[index].data, range_bytes[index], ranges[other].data,
                     other_bytes, &overlap) == 0) ||
                (overlap != 0)) {
                return 0;
            }
        }
    }

    const db_render_ir_store_t scratch_stores[] = {
        {.fills = workspace.primary, .fill_capacity = workspace.capacity},
        {.fills = workspace.secondary, .fill_capacity = workspace.capacity},
        {.bands = workspace.coverage_bands,
         .band_capacity = workspace.capacity},
        {.bands = workspace.coverage_band_scratch,
         .band_capacity = workspace.capacity},
        {.spans = workspace.coverage_spans,
         .span_capacity = workspace.capacity},
        {.spans = workspace.coverage_span_scratch,
         .span_capacity = workspace.capacity},
    };
    const db_render_ir_view_t scratch_views[] = {
        {.fills = workspace.primary, .fill_count = workspace.capacity},
        {.fills = workspace.secondary, .fill_count = workspace.capacity},
        {.bands = workspace.coverage_bands, .band_count = workspace.capacity},
        {.bands = workspace.coverage_band_scratch,
         .band_count = workspace.capacity},
        {.spans = workspace.coverage_spans, .span_count = workspace.capacity},
        {.spans = workspace.coverage_span_scratch,
         .span_count = workspace.capacity},
    };
    for (size_t index = 0U;
         index < sizeof(scratch_stores) / sizeof(scratch_stores[0]); index++) {
        if ((db_render_ir_view_store_relation(raw, &scratch_stores[index]) !=
             DB_RENDER_IR_STORAGE_DISJOINT) ||
            (db_render_ir_view_store_relation(&scratch_views[index],
                                              optimized) !=
             DB_RENDER_IR_STORAGE_DISJOINT)) {
            return 0;
        }
    }
    return 1;
}

static int colors_equal(db_render_ir_color_t lhs, db_render_ir_color_t rhs) {
    return (db_equal_f64(lhs.rgba[0], rhs.rgba[0]) != 0) &&
           (db_equal_f64(lhs.rgba[1], rhs.rgba[1]) != 0) &&
           (db_equal_f64(lhs.rgba[2], rhs.rgba[2]) != 0) &&
           (db_equal_f64(lhs.rgba[3], rhs.rgba[3]) != 0);
}

static int append_fill(db_render_ir_fill_t *fills, size_t capacity,
                       size_t *count, db_render_ir_fill_t fill) {
    if ((*count >= capacity) || (db_render_ir_rect_is_empty(fill.rect) != 0)) {
        return 0;
    }
    fills[(*count)++] = fill;
    return 1;
}

static int clip_to_target(const db_render_ir_view_t *raw,
                          db_render_ir_resource_id_t destination,
                          db_render_ir_fill_t input,
                          db_render_ir_fill_t *output) {
    if ((destination >= raw->resource_count) || (output == NULL)) {
        return 0;
    }
    const db_render_ir_resource_t resource = raw->resources[destination];
    db_render_ir_rect_t target_rect = {0};
    if (db_render_ir_rect_from_extent(resource.width, resource.height,
                                      &target_rect) == 0) {
        return 0;
    }
    db_render_ir_rect_t clipped = {0};
    if (!db_render_ir_rect_intersect(input.rect, target_rect, &clipped)) {
        return 0;
    }
    *output = (db_render_ir_fill_t){.rect = clipped, .color = input.color};
    return 1;
}

static int region_is_target(const db_render_ir_view_t *view,
                            db_render_ir_region_id_t region_id, uint32_t width,
                            uint32_t height) {
    if ((view == NULL) || (region_id >= view->region_count) ||
        (width > INT32_MAX) || (height > INT32_MAX)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[region_id];
    if (region.band_count != 1U) {
        return 0;
    }
    const db_render_ir_band_t band = view->bands[region.first_band];
    if ((band.y_start != 0) || (band.y_end != (int32_t)height) ||
        (band.span_count != 1U)) {
        return 0;
    }
    const db_render_ir_span_t span = view->spans[band.first_span];
    return DB_BOOL((span.x_start == 0) && (span.x_end == (int32_t)width));
}

static db_render_ir_status_t
import_command_clip(const db_render_ir_view_t *raw,
                    const db_render_ir_command_header_t *command,
                    db_render_ir_store_t *optimized,
                    db_render_ir_region_id_t *clip_region,
                    db_render_ir_optimizer_stats_t *stats) {
    *clip_region = DB_RENDER_IR_INVALID_ID;
    if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
        return DB_RENDER_IR_OK;
    }
    const db_render_ir_status_t status = db_render_ir_region_import(
        raw, command->clip_region, optimized, clip_region);
    if ((status == DB_RENDER_IR_OK) && (stats != NULL)) {
        const db_render_ir_region_t source_region =
            raw->regions[command->clip_region];
        uint64_t span_count = 0U;
        for (uint32_t index = 0U; index < source_region.band_count; index++) {
            span_count +=
                raw->bands[source_region.first_band + index].span_count;
        }
        stats->region_imports++;
        stats->band_comparisons += source_region.band_count;
        stats->span_comparisons += span_count;
    }
    if ((status == DB_RENDER_IR_OK) &&
        (*clip_region != DB_RENDER_IR_INVALID_ID) &&
        (command->destination < raw->resource_count)) {
        const db_render_ir_resource_t target =
            raw->resources[command->destination];
        const db_render_ir_view_t optimized_view =
            db_render_ir_store_view(optimized);
        if (region_is_target(&optimized_view, *clip_region, target.width,
                             target.height) != 0) {
            *clip_region = DB_RENDER_IR_INVALID_ID;
        }
    }
    return status;
}

static int append_fill_clipped_to_region(const db_render_ir_view_t *view,
                                         db_render_ir_region_id_t clip_region,
                                         db_render_ir_fill_t fill,
                                         db_render_ir_fill_t *output,
                                         size_t capacity, size_t *count) {
    if (clip_region == DB_RENDER_IR_INVALID_ID) {
        return append_fill(output, capacity, count, fill);
    }
    if ((view == NULL) || (clip_region >= view->region_count)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[clip_region];
    for (uint32_t band_index = 0U; band_index < region.band_count;
         band_index++) {
        const db_render_ir_band_t band =
            view->bands[region.first_band + band_index];
        for (uint32_t span_index = 0U; span_index < band.span_count;
             span_index++) {
            const db_render_ir_span_t span =
                view->spans[band.first_span + span_index];
            db_render_ir_rect_t clipped = {0};
            if (db_render_ir_rect_intersect(
                    fill.rect,
                    (db_render_ir_rect_t){
                        .x = span.x_start,
                        .y = band.y_start,
                        .width = span.x_end - span.x_start,
                        .height = band.y_end - band.y_start,
                    },
                    &clipped) &&
                !append_fill(output, capacity, count,
                             (db_render_ir_fill_t){.rect = clipped,
                                                   .color = fill.color})) {
                return 0;
            }
        }
    }
    return 1;
}

static db_render_ir_status_t
add_command_region(db_render_ir_store_t *optimized,
                   const db_render_ir_fill_t *fills, size_t fill_count,
                   db_render_ir_region_id_t *command_region,
                   db_render_ir_optimizer_workspace_t workspace) {
    if (fill_count == 0U) {
        *command_region = DB_RENDER_IR_INVALID_ID;
        return DB_RENDER_IR_OK;
    }
    return db_render_ir_build_fill_region_bounded(fills, fill_count, workspace,
                                                  optimized->span_capacity,
                                                  optimized, command_region);
}

static int collect_fills(const db_render_ir_view_t *raw,
                         db_render_ir_fill_t *output, size_t capacity,
                         size_t *output_count,
                         db_render_ir_resource_id_t *target) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, raw);
    const db_render_ir_command_header_t *command = NULL;
    *output_count = 0U;
    *target = DB_RENDER_IR_INVALID_ID;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) {
            if ((*target != DB_RENDER_IR_INVALID_ID) &&
                (*target != command->destination)) {
                return 0;
            }
            *target = command->destination;
        } else if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            const db_render_ir_resource_t resource =
                raw->resources[command->destination];
            const db_render_ir_clear_command_t *const clear =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            db_render_ir_rect_t target_rect = {0};
            if ((db_render_ir_rect_from_extent(resource.width, resource.height,
                                               &target_rect) == 0) ||
                !append_fill_clipped_to_region(
                    raw, command->clip_region,
                    (db_render_ir_fill_t){.rect = target_rect,
                                          .color = clear->color},
                    output, capacity, output_count)) {
                return 0;
            }
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                db_render_ir_fill_t clipped = {0};
                const db_render_ir_fill_t fill =
                    raw->fills[fills->first_fill + index];
                if (clip_to_target(raw, command->destination, fill, &clipped)) {
                    if (!append_fill_clipped_to_region(
                            raw, command->clip_region, clipped, output,
                            capacity, output_count)) {
                        return 0;
                    }
                }
            }
        }
    }
    return *target != DB_RENDER_IR_INVALID_ID;
}

static int requires_ordered_copy(const db_render_ir_view_t *raw) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, raw);
    const db_render_ir_command_header_t *command = NULL;
    db_render_ir_command_header_t domain = {0};
    int have_domain = 0;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) ||
            (command->opcode == DB_RENDER_IR_OP_UPLOAD_IMAGE) ||
            (command->opcode == DB_RENDER_IR_OP_INVALIDATE_RESOURCE)) {
            return 1;
        }
        if ((command->opcode != DB_RENDER_IR_OP_CLEAR) &&
            (command->opcode != DB_RENDER_IR_OP_FILL_RECTS)) {
            continue;
        }
        if (have_domain == 0) {
            domain = *command;
            have_domain = 1;
            continue;
        }
        if ((command->destination != domain.destination) ||
            (command->composite != domain.composite) ||
            (command->source_access != domain.source_access) ||
            (command->destination_access != domain.destination_access) ||
            (command->ordering_domain != domain.ordering_domain) ||
            (command->inseparable_group != domain.inseparable_group)) {
            return 1;
        }
    }
    return 0;
}

static db_render_ir_status_t
set_last_rect_regions(db_render_ir_store_t *store, db_render_ir_rect_t rect,
                      db_render_ir_region_id_t *out_region) {
    db_render_ir_region_id_t region = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t status =
        db_render_ir_add_rect_region(store, rect, &region);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_set_last_command_regions(store, region, region);
    }
    if ((status == DB_RENDER_IR_OK) && (out_region != NULL)) {
        *out_region = region;
    }
    return status;
}

static db_render_ir_status_t
copy_ordered_stream(const db_render_ir_view_t *raw,
                    db_render_ir_store_t *optimized,
                    db_render_ir_optimizer_workspace_t workspace) {
    db_render_ir_fill_t *const scratch = workspace.primary;
    const size_t scratch_capacity = workspace.capacity;
    db_render_ir_optimizer_stats_t *const stats = workspace.stats;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, raw);
    const db_render_ir_command_header_t *command = NULL;
    db_render_ir_region_id_t damage_region = DB_RENDER_IR_INVALID_ID;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        db_render_ir_status_t status = DB_RENDER_IR_OK;
        db_render_ir_region_id_t command_region = DB_RENDER_IR_INVALID_ID;
        db_render_ir_region_id_t clip_region = DB_RENDER_IR_INVALID_ID;
        if ((command->opcode != DB_RENDER_IR_OP_BEGIN_TARGET) &&
            (command->opcode != DB_RENDER_IR_OP_END_TARGET) &&
            (command->opcode != DB_RENDER_IR_OP_UPLOAD_IMAGE) &&
            (command->opcode != DB_RENDER_IR_OP_INVALIDATE_RESOURCE)) {
            status = import_command_clip(raw, command, optimized, &clip_region,
                                         stats);
            if (status != DB_RENDER_IR_OK) {
                return status;
            }
        }
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_BEGIN_TARGET:
            status = db_render_ir_begin_target(optimized, command->destination);
            break;
        case DB_RENDER_IR_OP_END_TARGET:
            status = db_render_ir_end_target(optimized, command->destination);
            if ((status == DB_RENDER_IR_OK) &&
                (damage_region != DB_RENDER_IR_INVALID_ID)) {
                status = db_render_ir_set_last_command_regions(
                    optimized, damage_region, damage_region);
            }
            break;
        case DB_RENDER_IR_OP_CLEAR: {
            const db_render_ir_clear_command_t *const clear =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            const db_render_ir_resource_t resource =
                raw->resources[command->destination];
            db_render_ir_rect_t target_rect = {0};
            if (db_render_ir_rect_from_extent(resource.width, resource.height,
                                              &target_rect) == 0) {
                return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
            }
            size_t clipped_count = 0U;
            const db_render_ir_view_t optimized_view =
                db_render_ir_store_view(optimized);
            if (!append_fill_clipped_to_region(
                    &optimized_view, clip_region,
                    (db_render_ir_fill_t){.rect = target_rect,
                                          .color = clear->color},
                    scratch, scratch_capacity, &clipped_count)) {
                return DB_RENDER_IR_CAPACITY;
            }
            if (clipped_count == 0U) {
                break;
            }
            status =
                db_render_ir_fill_rects(optimized, command->destination,
                                        scratch, clipped_count, clip_region);
            if (status == DB_RENDER_IR_OK) {
                status = add_command_region(optimized, scratch, clipped_count,
                                            &command_region, workspace);
            }
            if (status == DB_RENDER_IR_OK) {
                status = db_render_ir_set_last_command_regions(
                    optimized, command_region, command_region);
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fill_command =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            size_t clipped_count = 0U;
            for (uint32_t index = 0U; index < fill_command->fill_count;
                 index++) {
                db_render_ir_fill_t clipped = {0};
                if (clip_to_target(raw, command->destination,
                                   raw->fills[fill_command->first_fill + index],
                                   &clipped)) {
                    const db_render_ir_view_t optimized_view =
                        db_render_ir_store_view(optimized);
                    if (!append_fill_clipped_to_region(
                            &optimized_view, clip_region, clipped, scratch,
                            scratch_capacity, &clipped_count)) {
                        return DB_RENDER_IR_CAPACITY;
                    }
                }
            }
            if (clipped_count == 0U) {
                break;
            }
            status =
                db_render_ir_fill_rects(optimized, command->destination,
                                        scratch, clipped_count, clip_region);
            if ((status == DB_RENDER_IR_OK) && (clipped_count > 0U)) {
                status = add_command_region(optimized, scratch, clipped_count,
                                            &command_region, workspace);
                if (status == DB_RENDER_IR_OK) {
                    status = db_render_ir_set_last_command_regions(
                        optimized, command_region, command_region);
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            db_render_ir_fill_t clipped = {0};
            if (!clip_to_target(
                    raw, command->destination,
                    (db_render_ir_fill_t){.rect = gradient->bounds,
                                          .color = gradient->start_color},
                    &clipped)) {
                break;
            }
            size_t clipped_count = 0U;
            const db_render_ir_view_t optimized_view =
                db_render_ir_store_view(optimized);
            if (!append_fill_clipped_to_region(
                    &optimized_view, clip_region, clipped, scratch,
                    scratch_capacity, &clipped_count)) {
                return DB_RENDER_IR_CAPACITY;
            }
            if (clipped_count == 0U) {
                break;
            }
            status = add_command_region(optimized, scratch, clipped_count,
                                        &command_region, workspace);
            if (status != DB_RENDER_IR_OK) {
                break;
            }
            if (colors_equal(gradient->start_color, gradient->end_color)) {
                status = db_render_ir_fill_rects(optimized,
                                                 command->destination, scratch,
                                                 clipped_count, clip_region);
            } else {
                status = db_render_ir_fill_linear_gradient(
                    optimized, command->destination, clipped.rect,
                    gradient->axis_start, gradient->axis_end,
                    gradient->reverse_stops, gradient->start_color,
                    gradient->end_color, clip_region);
            }
            if (status == DB_RENDER_IR_OK) {
                status = db_render_ir_set_last_command_regions(
                    optimized, command_region, command_region);
            }
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
            status = db_render_ir_upload_image(
                optimized, command->destination, upload->source,
                upload->source_rect, upload->destination_x,
                upload->destination_y, upload->semantics);
            if (status == DB_RENDER_IR_OK) {
                status = set_last_rect_regions(
                    optimized,
                    (db_render_ir_rect_t){.x = upload->destination_x,
                                          .y = upload->destination_y,
                                          .width = upload->source_rect.width,
                                          .height = upload->source_rect.height},
                    &command_region);
            }
            break;
        }
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            status = db_render_ir_invalidate_resource(optimized,
                                                      command->destination);
            break;
        }
        if (status != DB_RENDER_IR_OK) {
            return status;
        }
        if ((command->opcode != DB_RENDER_IR_OP_BEGIN_TARGET) &&
            (command->opcode != DB_RENDER_IR_OP_END_TARGET)) {
            status = db_render_ir_set_last_command_ordering(
                optimized, command->ordering_domain,
                command->inseparable_group);
            if (status != DB_RENDER_IR_OK) {
                return status;
            }
        }
        if (command_region != DB_RENDER_IR_INVALID_ID) {
            if (damage_region == DB_RENDER_IR_INVALID_ID) {
                damage_region = command_region;
            } else {
                db_render_ir_region_id_t combined = DB_RENDER_IR_INVALID_ID;
                status = db_render_ir_region_union(optimized, damage_region,
                                                   command_region, &combined);
                if (status != DB_RENDER_IR_OK) {
                    return status;
                }
                damage_region = combined;
            }
        }
    }
    return DB_RENDER_IR_OK;
}

static db_render_ir_status_t
optimize_internal(const db_render_ir_view_t *raw,
                  db_render_ir_store_t *optimized,
                  db_render_ir_optimizer_workspace_t workspace) {
    if ((raw == NULL) || (optimized == NULL) || (workspace.primary == NULL) ||
        (workspace.secondary == NULL) || (workspace.capacity == 0U) ||
        (db_render_ir_validate(raw) != DB_RENDER_IR_OK)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_store_reset(optimized);
    if (raw->resource_count > optimized->resource_capacity) {
        return DB_RENDER_IR_CAPACITY;
    }
    if (raw->resource_count > (SIZE_MAX / sizeof(*raw->resources))) {
        return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }
    const size_t resource_bytes = raw->resource_count * sizeof(*raw->resources);
    if (resource_bytes > 0U) {
        memcpy(optimized->resources, raw->resources, resource_bytes);
    }
    optimized->resource_count = raw->resource_count;
    if (requires_ordered_copy(raw) != 0) {
        return copy_ordered_stream(raw, optimized, workspace);
    }

    size_t raw_fill_count = 0U;
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    if (!collect_fills(raw, workspace.primary, workspace.capacity,
                       &raw_fill_count, &target)) {
        return DB_RENDER_IR_CAPACITY;
    }
    size_t visible_count = 0U;
    const db_render_ir_status_t elimination_status =
        db_render_ir_eliminate_overwrites(workspace.primary, raw_fill_count,
                                          workspace, optimized->span_capacity,
                                          optimized->fills, &visible_count);
    if (elimination_status != DB_RENDER_IR_OK) {
        return elimination_status;
    }
    const db_render_ir_status_t sort_status = db_render_ir_sort_and_merge_fills(
        optimized->fills, workspace.primary, &visible_count, workspace.stats);
    if (sort_status != DB_RENDER_IR_OK) {
        return sort_status;
    }
    optimized->fill_count = 0U;
    if (visible_count == 0U) {
        if ((db_render_ir_begin_target(optimized, target) != DB_RENDER_IR_OK) ||
            (db_render_ir_end_target(optimized, target) != DB_RENDER_IR_OK)) {
            return optimized->status;
        }
        return DB_RENDER_IR_OK;
    }
    db_render_ir_region_id_t damage_region = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t region_status =
        db_render_ir_build_fill_region_bounded(
            optimized->fills, visible_count, workspace,
            optimized->span_capacity, optimized, &damage_region);
    if (region_status != DB_RENDER_IR_OK) {
        return region_status;
    }
    if (db_render_ir_begin_target(optimized, target) != DB_RENDER_IR_OK) {
        return optimized->status;
    }
    const db_render_ir_resource_t target_resource =
        optimized->resources[target];
    const db_render_ir_view_t optimized_view =
        db_render_ir_store_view(optimized);
    const db_render_ir_region_id_t effective_clip =
        region_is_target(&optimized_view, damage_region, target_resource.width,
                         target_resource.height)
            ? DB_RENDER_IR_INVALID_ID
            : damage_region;
    if (db_render_ir_fill_rects(optimized, target, optimized->fills,
                                visible_count,
                                effective_clip) != DB_RENDER_IR_OK) {
        return optimized->status;
    }
    if (region_status == DB_RENDER_IR_OK) {
        region_status = db_render_ir_set_last_command_regions(
            optimized, damage_region, damage_region);
    }
    if (region_status != DB_RENDER_IR_OK) {
        return region_status;
    }
    if (db_render_ir_end_target(optimized, target) != DB_RENDER_IR_OK) {
        return optimized->status;
    }
    return DB_RENDER_IR_OK;
}

db_render_ir_status_t
db_render_ir_optimize(const db_render_ir_view_t *raw,
                      db_render_ir_store_t *optimized,
                      db_render_ir_optimizer_workspace_t workspace) {
    if ((raw == NULL) || (optimized == NULL) ||
        (db_render_ir_view_store_relation(raw, optimized) !=
         DB_RENDER_IR_STORAGE_DISJOINT) ||
        (optimizer_storage_is_disjoint(raw, optimized, workspace) == 0)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_optimizer_stats_t local_stats = {0};
    db_render_ir_optimizer_stats_t *const requested_stats = workspace.stats;
    workspace.stats = &local_stats;
    const db_render_ir_status_t status =
        optimize_internal(raw, optimized, workspace);
    if (requested_stats != NULL) {
        *requested_stats = local_stats;
    }
    if ((status == DB_RENDER_IR_OK) &&
        (local_stats.region_imports > raw->command_count)) {
        db_render_ir_store_reset(optimized);
        return DB_RENDER_IR_COMPLEXITY_LIMIT;
    }
    if ((status != DB_RENDER_IR_OK) && (optimized != NULL)) {
        db_render_ir_store_reset(optimized);
    }
    return status;
}
