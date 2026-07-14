#include "db_render_ir.h"

#include "db_numeric.h"
#include "db_sort.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int colors_equal(db_render_ir_color_t lhs, db_render_ir_color_t rhs) {
    return (db_equal_f64(lhs.rgba[0], rhs.rgba[0]) != 0) &&
           (db_equal_f64(lhs.rgba[1], rhs.rgba[1]) != 0) &&
           (db_equal_f64(lhs.rgba[2], rhs.rgba[2]) != 0) &&
           (db_equal_f64(lhs.rgba[3], rhs.rgba[3]) != 0);
}

static int compare_fills_row_major(const void *lhs_pointer,
                                   const void *rhs_pointer) {
    const db_render_ir_fill_t *const lhs =
        (const db_render_ir_fill_t *)lhs_pointer;
    const db_render_ir_fill_t *const rhs =
        (const db_render_ir_fill_t *)rhs_pointer;
    if (lhs->rect.y != rhs->rect.y) {
        return (lhs->rect.y > rhs->rect.y) - (lhs->rect.y < rhs->rect.y);
    }
    if (lhs->rect.x != rhs->rect.x) {
        return (lhs->rect.x > rhs->rect.x) - (lhs->rect.x < rhs->rect.x);
    }
    if (lhs->rect.height != rhs->rect.height) {
        return (lhs->rect.height > rhs->rect.height) -
               (lhs->rect.height < rhs->rect.height);
    }
    return (lhs->rect.width > rhs->rect.width) -
           (lhs->rect.width < rhs->rect.width);
}

static int append_fill(db_render_ir_fill_t *fills, size_t capacity,
                       size_t *count, db_render_ir_fill_t fill) {
    if ((*count >= capacity) || (db_render_ir_rect_is_empty(fill.rect) != 0)) {
        return 0;
    }
    fills[(*count)++] = fill;
    return 1;
}

static int subtract_rect(db_render_ir_fill_t source,
                         db_render_ir_rect_t occluder,
                         db_render_ir_fill_t *output, size_t capacity,
                         size_t *count) {
    db_render_ir_rect_t overlap = {0};
    if (!db_render_ir_rect_intersect(source.rect, occluder, &overlap)) {
        return append_fill(output, capacity, count, source);
    }
    const int32_t source_right = source.rect.x + source.rect.width;
    const int32_t source_bottom = source.rect.y + source.rect.height;
    const int32_t overlap_right = overlap.x + overlap.width;
    const int32_t overlap_bottom = overlap.y + overlap.height;
    const db_render_ir_rect_t pieces[4] = {
        {.x = source.rect.x,
         .y = source.rect.y,
         .width = source.rect.width,
         .height = overlap.y - source.rect.y},
        {.x = source.rect.x,
         .y = overlap_bottom,
         .width = source.rect.width,
         .height = source_bottom - overlap_bottom},
        {.x = source.rect.x,
         .y = overlap.y,
         .width = overlap.x - source.rect.x,
         .height = overlap.height},
        {.x = overlap_right,
         .y = overlap.y,
         .width = source_right - overlap_right,
         .height = overlap.height},
    };
    for (size_t index = 0U; index < 4U; index++) {
        if ((db_render_ir_rect_is_empty(pieces[index]) == 0) &&
            !append_fill(output, capacity, count,
                         (db_render_ir_fill_t){.rect = pieces[index],
                                               .color = source.color})) {
            return 0;
        }
    }
    return 1;
}

static int subtract_from_list(db_render_ir_fill_t *pieces, size_t capacity,
                              size_t *piece_count,
                              db_render_ir_rect_t occluder) {
    size_t index = 0U;
    while (index < *piece_count) {
        db_render_ir_rect_t overlap = {0};
        if (!db_render_ir_rect_intersect(pieces[index].rect, occluder,
                                         &overlap)) {
            index++;
            continue;
        }
        const db_render_ir_fill_t source = pieces[index];
        pieces[index] = pieces[*piece_count - 1U];
        (*piece_count)--;
        if (!subtract_rect(source, occluder, pieces, capacity, piece_count)) {
            return 0;
        }
    }
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
            *target = command->destination;
        } else if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            const db_render_ir_resource_t resource =
                raw->resources[command->destination];
            const db_render_ir_clear_command_t *const clear =
                (const db_render_ir_clear_command_t *)command;
            db_render_ir_rect_t target_rect = {0};
            if ((db_render_ir_rect_from_extent(resource.width, resource.height,
                                               &target_rect) == 0) ||
                !append_fill(output, capacity, output_count,
                             (db_render_ir_fill_t){.rect = target_rect,
                                                   .color = clear->color})) {
                return 0;
            }
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fills =
                (const db_render_ir_fill_command_t *)command;
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                db_render_ir_fill_t clipped = {0};
                const db_render_ir_fill_t fill =
                    raw->fills[fills->first_fill + index];
                if (clip_to_target(raw, command->destination, fill, &clipped) &&
                    !append_fill(output, capacity, output_count, clipped)) {
                    return 0;
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
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_CLEAR) ||
            (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) ||
            (command->opcode == DB_RENDER_IR_OP_UPLOAD_IMAGE) ||
            (command->opcode == DB_RENDER_IR_OP_INVALIDATE_RESOURCE)) {
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
                    db_render_ir_fill_t *scratch, size_t scratch_capacity) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, raw);
    const db_render_ir_command_header_t *command = NULL;
    db_render_ir_region_id_t damage_region = DB_RENDER_IR_INVALID_ID;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        db_render_ir_status_t status = DB_RENDER_IR_OK;
        db_render_ir_region_id_t command_region = DB_RENDER_IR_INVALID_ID;
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
                (const db_render_ir_clear_command_t *)command;
            status = db_render_ir_clear(optimized, command->destination,
                                        clear->color, DB_RENDER_IR_INVALID_ID);
            if (status == DB_RENDER_IR_OK) {
                const db_render_ir_resource_t resource =
                    raw->resources[command->destination];
                db_render_ir_rect_t target_rect = {0};
                if (db_render_ir_rect_from_extent(
                        resource.width, resource.height, &target_rect) == 0) {
                    return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
                }
                status = set_last_rect_regions(optimized, target_rect,
                                               &command_region);
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fill_command =
                (const db_render_ir_fill_command_t *)command;
            size_t clipped_count = 0U;
            for (uint32_t index = 0U; index < fill_command->fill_count;
                 index++) {
                db_render_ir_fill_t clipped = {0};
                if (clip_to_target(raw, command->destination,
                                   raw->fills[fill_command->first_fill + index],
                                   &clipped)) {
                    if (clipped_count >= scratch_capacity) {
                        return DB_RENDER_IR_CAPACITY;
                    }
                    scratch[clipped_count++] = clipped;
                }
            }
            status = db_render_ir_fill_rects(optimized, command->destination,
                                             scratch, clipped_count,
                                             DB_RENDER_IR_INVALID_ID);
            if ((status == DB_RENDER_IR_OK) && (clipped_count > 0U)) {
                db_render_ir_region_id_t region = DB_RENDER_IR_INVALID_ID;
                status = db_render_ir_add_fill_region(optimized, scratch,
                                                      clipped_count, &region);
                if (status == DB_RENDER_IR_OK) {
                    status = db_render_ir_set_last_command_regions(
                        optimized, region, region);
                    command_region = region;
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
            db_render_ir_fill_t clipped = {0};
            if (!clip_to_target(
                    raw, command->destination,
                    (db_render_ir_fill_t){.rect = gradient->bounds,
                                          .color = gradient->start_color},
                    &clipped)) {
                break;
            }
            if (colors_equal(gradient->start_color, gradient->end_color)) {
                status = db_render_ir_fill_rects(
                    optimized, command->destination,
                    &(const db_render_ir_fill_t){
                        .rect = clipped.rect, .color = gradient->start_color},
                    1U, DB_RENDER_IR_INVALID_ID);
            } else {
                status = db_render_ir_fill_linear_gradient(
                    optimized, command->destination, clipped.rect,
                    gradient->axis_start, gradient->axis_end,
                    gradient->reverse_stops, gradient->start_color,
                    gradient->end_color, DB_RENDER_IR_INVALID_ID);
            }
            if (status == DB_RENDER_IR_OK) {
                status = set_last_rect_regions(optimized, clipped.rect,
                                               &command_region);
            }
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                (const db_render_ir_upload_command_t *)command;
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

static int eliminate_overwrites(db_render_ir_fill_t *input, size_t input_count,
                                db_render_ir_fill_t *scratch, size_t capacity,
                                db_render_ir_fill_t *output,
                                size_t *output_count) {
    size_t visible_count = 0U;
    for (size_t reverse = input_count; reverse > 0U; reverse--) {
        size_t piece_count = 1U;
        scratch[0] = input[reverse - 1U];
        for (size_t occluder = 0U; occluder < visible_count; occluder++) {
            if (!subtract_from_list(scratch, capacity, &piece_count,
                                    output[occluder].rect)) {
                return 0;
            }
            if (piece_count == 0U) {
                break;
            }
        }
        if (piece_count > (capacity - visible_count)) {
            return 0;
        }
        if (piece_count > (SIZE_MAX / sizeof(*output))) {
            return 0;
        }
        const size_t copy_bytes = piece_count * sizeof(*output);
        memcpy(&output[visible_count], scratch, copy_bytes);
        visible_count += piece_count;
    }
    *output_count = visible_count;
    return 1;
}

static int merge_pair(db_render_ir_fill_t *lhs, db_render_ir_fill_t rhs) {
    if (!colors_equal(lhs->color, rhs.color)) {
        return 0;
    }
    const int32_t lhs_right = lhs->rect.x + lhs->rect.width;
    const int32_t rhs_right = rhs.rect.x + rhs.rect.width;
    const int32_t lhs_bottom = lhs->rect.y + lhs->rect.height;
    const int32_t rhs_bottom = rhs.rect.y + rhs.rect.height;
    if ((lhs->rect.y == rhs.rect.y) && (lhs->rect.height == rhs.rect.height) &&
        ((lhs_right == rhs.rect.x) || (rhs_right == lhs->rect.x))) {
        const int32_t x_start = DB_MIN(lhs->rect.x, rhs.rect.x);
        lhs->rect.x = x_start;
        lhs->rect.width = DB_MAX(lhs_right, rhs_right) - x_start;
        return 1;
    }
    if ((lhs->rect.x == rhs.rect.x) && (lhs->rect.width == rhs.rect.width) &&
        ((lhs_bottom == rhs.rect.y) || (rhs_bottom == lhs->rect.y))) {
        const int32_t y_start = DB_MIN(lhs->rect.y, rhs.rect.y);
        lhs->rect.y = y_start;
        lhs->rect.height = DB_MAX(lhs_bottom, rhs_bottom) - y_start;
        return 1;
    }
    return 0;
}

static void merge_exact_color(db_render_ir_fill_t *fills, size_t *count) {
    size_t remaining_merges = *count;
    int changed = 1;
    while ((changed != 0) && (remaining_merges > 0U)) {
        changed = 0;
        for (size_t lhs = 0U; lhs < *count; lhs++) {
            for (size_t rhs = lhs + 1U; rhs < *count; rhs++) {
                if (merge_pair(&fills[lhs], fills[rhs])) {
                    fills[rhs] = fills[*count - 1U];
                    (*count)--;
                    remaining_merges--;
                    changed = 1;
                    break;
                }
            }
            if (changed != 0) {
                break;
            }
        }
    }
}

db_render_ir_status_t
db_render_ir_optimize(const db_render_ir_view_t *raw,
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
    memcpy(optimized->resources, raw->resources, resource_bytes);
    optimized->resource_count = raw->resource_count;
    if (requires_ordered_copy(raw) != 0) {
        return copy_ordered_stream(raw, optimized, workspace.primary,
                                   workspace.capacity);
    }

    size_t raw_fill_count = 0U;
    db_render_ir_resource_id_t target = DB_RENDER_IR_INVALID_ID;
    if (!collect_fills(raw, workspace.primary, workspace.capacity,
                       &raw_fill_count, &target)) {
        return DB_RENDER_IR_CAPACITY;
    }
    size_t visible_count = 0U;
    if (!eliminate_overwrites(workspace.primary, raw_fill_count,
                              workspace.secondary, workspace.capacity,
                              optimized->fills, &visible_count)) {
        return DB_RENDER_IR_CAPACITY;
    }
    if (db_sort_records(optimized->fills, visible_count,
                        sizeof(*optimized->fills),
                        compare_fills_row_major) != DB_SORT_OK) {
        return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }
    merge_exact_color(optimized->fills, &visible_count);
    if (db_sort_records(optimized->fills, visible_count,
                        sizeof(*optimized->fills),
                        compare_fills_row_major) != DB_SORT_OK) {
        return DB_RENDER_IR_ARITHMETIC_OVERFLOW;
    }
    optimized->fill_count = 0U;
    if (db_render_ir_begin_target(optimized, target) != DB_RENDER_IR_OK) {
        return optimized->status;
    }
    if (db_render_ir_fill_rects(optimized, target, optimized->fills,
                                visible_count,
                                DB_RENDER_IR_INVALID_ID) != DB_RENDER_IR_OK) {
        return optimized->status;
    }
    db_render_ir_region_id_t damage_region = DB_RENDER_IR_INVALID_ID;
    db_render_ir_status_t region_status = db_render_ir_add_fill_region(
        optimized, optimized->fills, visible_count, &damage_region);
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
