#include "db_numeric.h"
#include "db_render_ir.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static int db_render_ir_commands_batch_compatible(
    const db_render_ir_command_header_t *lhs,
    const db_render_ir_command_header_t *rhs) {
    return DB_BOOL((lhs != NULL) && (rhs != NULL) &&
                   (lhs->opcode == rhs->opcode) &&
                   (lhs->destination == rhs->destination) &&
                   (lhs->composite == rhs->composite) &&
                   (lhs->clip_region == rhs->clip_region));
}

static uint32_t db_render_ir_command_instance_count(
    const db_render_ir_command_header_t *command) {
    if (command == NULL) {
        return 0U;
    }
    switch ((db_render_ir_opcode_t)command->opcode) {
    case DB_RENDER_IR_OP_FILL_RECTS:
        return ((const db_render_ir_fill_command_t *)command)->fill_count;
    case DB_RENDER_IR_OP_CLEAR:
    case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT:
    case DB_RENDER_IR_OP_UPLOAD_IMAGE:
    case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
        return 1U;
    case DB_RENDER_IR_OP_BEGIN_TARGET:
    case DB_RENDER_IR_OP_END_TARGET:
        return 0U;
    }
    return 0U;
}

size_t db_render_ir_collect_command_ranges(const db_render_ir_view_t *view,
                                           db_render_ir_stream_t stream,
                                           db_render_ir_command_range_t *ranges,
                                           size_t range_capacity,
                                           int *out_overflow) {
    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    if ((view == NULL) || ((ranges == NULL) && (range_capacity > 0U))) {
        if (out_overflow != NULL) {
            *out_overflow = 1;
        }
        return 0U;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    db_render_ir_command_header_t previous = {0};
    int have_previous = 0;
    const db_render_ir_command_header_t *command = NULL;
    size_t count = 0U;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->opcode == DB_RENDER_IR_OP_BEGIN_TARGET) ||
            (command->opcode == DB_RENDER_IR_OP_END_TARGET)) {
            continue;
        }
        const uint32_t instances = db_render_ir_command_instance_count(command);
        if ((count > 0U) && (have_previous != 0) &&
            (db_render_ir_commands_batch_compatible(&previous, command) != 0)) {
            ranges[count - 1U].command_count++;
            ranges[count - 1U].instance_count += instances;
            previous = *command;
            continue;
        }
        if (count >= range_capacity) {
            if (out_overflow != NULL) {
                *out_overflow = 1;
            }
            return count;
        }
        db_render_ir_prior_content_t prior =
            DB_RENDER_IR_PRIOR_CONTENT_INDEPENDENT;
        if (command->opcode == DB_RENDER_IR_OP_UPLOAD_IMAGE) {
            prior = ((const db_render_ir_upload_command_t *)command)
                        ->semantics.prior_content;
        }
        ranges[count++] = (db_render_ir_command_range_t){
            .stream = stream,
            .first_sequence = command->sequence,
            .command_count = 1U,
            .region = command->touched_region,
            .instance_count = instances,
            .prior_content = prior,
            .opcode = command->opcode,
        };
        previous = *command;
        have_previous = 1;
    }
    return count;
}

size_t db_render_ir_command_range_rect_count(
    const db_render_ir_view_t *view,
    const db_render_ir_command_range_t *range) {
    if ((view == NULL) || (range == NULL)) {
        return 0U;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    size_t count = 0U;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->sequence < range->first_sequence) ||
            (command->sequence >=
             (range->first_sequence + range->command_count))) {
            continue;
        }
        if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            count++;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            count += ((const db_render_ir_fill_command_t *)command)->fill_count;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const int32_t height =
                ((const db_render_ir_linear_gradient_command_t *)command)
                    ->bounds.height;
            if (height > 0) {
                count += (size_t)height;
            }
        }
    }
    return count;
}

int db_render_ir_command_range_rect_at(
    const db_render_ir_view_t *view, const db_render_ir_command_range_t *range,
    size_t index, db_render_ir_fill_t *fill) {
    if ((view == NULL) || (range == NULL) || (fill == NULL)) {
        return 0;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if ((command->sequence < range->first_sequence) ||
            (command->sequence >=
             (range->first_sequence + range->command_count))) {
            continue;
        }
        if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            if (index == 0U) {
                if (command->destination >= view->resource_count) {
                    return 0;
                }
                const db_render_ir_resource_t target =
                    view->resources[command->destination];
                if ((target.width > INT32_MAX) || (target.height > INT32_MAX)) {
                    return 0;
                }
                *fill = (db_render_ir_fill_t){
                    .rect = {.width = (int32_t)target.width,
                             .height = (int32_t)target.height},
                    .color =
                        ((const db_render_ir_clear_command_t *)command)->color,
                };
                return 1;
            }
            index--;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
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
            const size_t rows =
                db_positive_i32_to_size_or_zero(gradient->bounds.height);
            if (index < rows) {
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
            index -= rows;
        }
    }
    return 0;
}
