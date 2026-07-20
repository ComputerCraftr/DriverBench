#include "db_render_ir.h"

#include <stddef.h>
#include <stdint.h>

static int lower_clipped_fill(const db_render_ir_view_t *view,
                              const db_render_ir_command_header_t *command,
                              db_render_ir_fill_t fill,
                              const db_render_ir_lowering_ops_t *ops,
                              void *context) {
    if ((command->clip_region == DB_RENDER_IR_INVALID_ID) ||
        (command->clip_region >= view->region_count)) {
        return (ops->fill_rects != NULL)
                   ? ops->fill_rects(context, command->destination, &fill, 1U)
                   : 0;
    }
    const db_render_ir_region_t region = view->regions[command->clip_region];
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
                ((ops->fill_rects == NULL) ||
                 (ops->fill_rects(context, command->destination,
                                  &(const db_render_ir_fill_t){
                                      .rect = clipped, .color = fill.color},
                                  1U) == 0))) {
                return 0;
            }
        }
    }
    return 1;
}

static int
lower_clipped_gradient(const db_render_ir_view_t *view,
                       const db_render_ir_command_header_t *command,
                       const db_render_ir_linear_gradient_command_t *gradient,
                       const db_render_ir_lowering_ops_t *ops, void *context) {
    if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
        return (ops->fill_linear_gradient != NULL)
                   ? ops->fill_linear_gradient(context, command->destination,
                                               gradient, gradient->bounds)
                   : 0;
    }
    if ((command->clip_region >= view->region_count) ||
        (ops->fill_linear_gradient == NULL)) {
        return 0;
    }
    const db_render_ir_region_t region = view->regions[command->clip_region];
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
                    gradient->bounds,
                    (db_render_ir_rect_t){
                        .x = span.x_start,
                        .y = band.y_start,
                        .width = span.x_end - span.x_start,
                        .height = band.y_end - band.y_start,
                    },
                    &clipped)) {
                if (ops->fill_linear_gradient(context, command->destination,
                                              gradient, clipped) == 0) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

db_render_ir_status_t
db_render_ir_lower(const db_render_ir_view_t *view,
                   db_render_ir_external_binding_view_t bindings,
                   const db_render_ir_lowering_ops_t *ops, void *context) {
    if ((view == NULL) || (ops == NULL) ||
        (db_render_ir_validate_bindings(view, bindings) != DB_RENDER_IR_OK)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        int succeeded = 1;
        switch ((db_render_ir_opcode_t)command->opcode) {
        case DB_RENDER_IR_OP_BEGIN_TARGET:
            succeeded = (ops->begin_target == NULL)
                            ? 1
                            : ops->begin_target(context, command->destination);
            break;
        case DB_RENDER_IR_OP_END_TARGET:
            succeeded = (ops->end_target == NULL)
                            ? 1
                            : ops->end_target(context, command->destination);
            break;
        case DB_RENDER_IR_OP_CLEAR: {
            const db_render_ir_clear_command_t *const clear =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
                succeeded = (ops->clear == NULL)
                                ? 0
                                : ops->clear(context, command->destination,
                                             clear->color);
            } else if (command->destination >= view->resource_count) {
                succeeded = 0;
            } else {
                db_render_ir_rect_t target = {0};
                const db_render_ir_resource_t resource =
                    view->resources[command->destination];
                succeeded =
                    (db_render_ir_rect_from_extent(
                         resource.width, resource.height, &target) != 0) &&
                    lower_clipped_fill(
                        view, command,
                        (db_render_ir_fill_t){.rect = target,
                                              .color = clear->color},
                        ops, context);
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
                succeeded =
                    (ops->fill_rects != NULL)
                        ? ops->fill_rects(context, command->destination,
                                          &view->fills[fills->first_fill],
                                          fills->fill_count)
                        : 0;
            } else {
                succeeded = 1;
                for (uint32_t index = 0U;
                     (succeeded != 0) && (index < fills->fill_count); index++) {
                    succeeded = lower_clipped_fill(
                        view, command, view->fills[fills->first_fill + index],
                        ops, context);
                }
            }
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            succeeded =
                lower_clipped_gradient(view, command, gradient, ops, context);
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_upload_command_t, command);
            db_render_ir_external_binding_t binding = {0};
            succeeded = ((ops->upload_image == NULL) ||
                         (db_render_ir_find_binding(bindings, upload->source,
                                                    &binding) == 0))
                            ? 0
                            : ops->upload_image(context, command->destination,
                                                upload, bindings);
            break;
        }
        case DB_RENDER_IR_OP_INVALIDATE_RESOURCE:
            succeeded = (ops->invalidate == NULL)
                            ? 1
                            : ops->invalidate(context, command->destination);
            break;
        }
        if (succeeded == 0) {
            return DB_RENDER_IR_INVALID;
        }
    }
    return DB_RENDER_IR_OK;
}
