#include "db_render_ir.h"

#include <stddef.h>

static int bindings_valid(const db_render_ir_view_t *view,
                          db_render_ir_external_binding_view_t bindings) {
    if ((bindings.count > 0U) && (bindings.bindings == NULL)) {
        return 0;
    }
    for (size_t index = 0U; index < bindings.count; index++) {
        const db_render_ir_external_binding_t *const binding =
            &bindings.bindings[index];
        if ((binding->resource >= view->resource_count) ||
            (binding->pixels == NULL) || (binding->width == 0U) ||
            (binding->height == 0U) || (binding->row_stride_bytes == 0U) ||
            (binding->size_bytes == 0U)) {
            return 0;
        }
        const db_render_ir_resource_t resource =
            view->resources[binding->resource];
        if ((resource.kind != DB_RENDER_IR_RESOURCE_RASTER_SOURCE) ||
            (resource.width != binding->width) ||
            (resource.height != binding->height) ||
            (resource.format != binding->format)) {
            return 0;
        }
        for (size_t prior = 0U; prior < index; prior++) {
            if (bindings.bindings[prior].resource == binding->resource) {
                return 0;
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
        (bindings_valid(view, bindings) == 0) ||
        (db_render_ir_validate(view) != DB_RENDER_IR_OK)) {
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
                (const db_render_ir_clear_command_t *)command;
            succeeded =
                (ops->clear == NULL)
                    ? 0
                    : ops->clear(context, command->destination, clear->color);
            break;
        }
        case DB_RENDER_IR_OP_FILL_RECTS: {
            const db_render_ir_fill_command_t *const fills =
                (const db_render_ir_fill_command_t *)command;
            succeeded = (ops->fill_rects == NULL)
                            ? 0
                            : ops->fill_rects(context, command->destination,
                                              &view->fills[fills->first_fill],
                                              fills->fill_count);
            break;
        }
        case DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT: {
            const db_render_ir_linear_gradient_command_t *const gradient =
                (const db_render_ir_linear_gradient_command_t *)command;
            succeeded = (ops->fill_linear_gradient == NULL)
                            ? 0
                            : ops->fill_linear_gradient(
                                  context, command->destination, gradient);
            break;
        }
        case DB_RENDER_IR_OP_UPLOAD_IMAGE: {
            const db_render_ir_upload_command_t *const upload =
                (const db_render_ir_upload_command_t *)command;
            succeeded =
                ((ops->upload_image == NULL) ||
                 (db_render_ir_find_binding(bindings, upload->source) == NULL))
                    ? 0
                    : ops->upload_image(context, command->destination, upload,
                                        bindings);
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
