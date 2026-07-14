#include "db_render_ir_surface.h"

#include "db_core.h"
#include "db_frame_plan.h"
#include "db_geometry.h"
#include "db_numeric.h"
#include "db_render_ir.h"
#include "db_render_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const db_pixel_surface_t *surface;
    uint32_t logical_width;
    uint32_t logical_height;
} surface_lowering_t;

static int fill_logical_rect(surface_lowering_t *lowering,
                             db_render_ir_rect_t rect, const double rgba[4]) {
    if ((lowering == NULL) || (lowering->surface == NULL) || (rgba == NULL) ||
        (rect.x < 0) || (rect.y < 0) || (rect.width <= 0) ||
        (rect.height <= 0)) {
        return 0;
    }
    db_grid_block_t logical = {0};
    if (db_render_ir_rect_to_grid_block(rect, lowering->logical_width,
                                        lowering->logical_height,
                                        &logical) == 0) {
        return 0;
    }
    db_damage_block_t pixels = {0};
    if (db_grid_block_to_pixel_block(
            lowering->logical_width, lowering->logical_height, &logical,
            lowering->surface->pixel_width, lowering->surface->pixel_height,
            &pixels) == 0) {
        return 0;
    }
    db_rgb_pixels_fill_damage_block_f64(
        lowering->surface->pixel_width, lowering->surface->pixel_height,
        lowering->surface->pixels, lowering->surface->format, pixels.row_start,
        pixels.row_count, pixels.col_start, pixels.col_count, rgba);
    return 1;
}

static int begin_target(void *context, db_render_ir_resource_id_t target) {
    (void)context;
    return DB_BOOL(target != DB_RENDER_IR_INVALID_ID);
}

static int end_target(void *context, db_render_ir_resource_id_t target) {
    return begin_target(context, target);
}

static int clear_target(void *context, db_render_ir_resource_id_t target,
                        db_render_ir_color_t color) {
    surface_lowering_t *const lowering = (surface_lowering_t *)context;
    (void)target;
    db_render_ir_rect_t target_rect = {0};
    if (db_render_ir_rect_from_extent(lowering->logical_width,
                                      lowering->logical_height,
                                      &target_rect) == 0) {
        return 0;
    }
    return fill_logical_rect(lowering, target_rect, color.rgba);
}

static int fill_rects(void *context, db_render_ir_resource_id_t target,
                      const db_render_ir_fill_t *fills, size_t fill_count) {
    surface_lowering_t *const lowering = (surface_lowering_t *)context;
    (void)target;
    if ((fills == NULL) && (fill_count > 0U)) {
        return 0;
    }
    for (size_t index = 0U; index < fill_count; index++) {
        if (fill_logical_rect(lowering, fills[index].rect,
                              fills[index].color.rgba) == 0) {
            return 0;
        }
    }
    return 1;
}

static int
fill_linear_gradient(void *context, db_render_ir_resource_id_t target,
                     const db_render_ir_linear_gradient_command_t *gradient) {
    surface_lowering_t *const lowering = (surface_lowering_t *)context;
    (void)target;
    if ((gradient == NULL) || (gradient->bounds.height <= 0)) {
        return 0;
    }
    const int64_t row_end_i64 =
        (int64_t)gradient->bounds.y + (int64_t)gradient->bounds.height;
    if ((row_end_i64 < INT32_MIN) || (row_end_i64 > INT32_MAX)) {
        return 0;
    }
    const int32_t row_end = (int32_t)row_end_i64;
    for (int32_t row = gradient->bounds.y; row < row_end; row++) {
        const db_render_ir_color_t color =
            db_render_ir_linear_gradient_color_at(gradient, row);
        if (fill_logical_rect(
                lowering,
                (db_render_ir_rect_t){.x = gradient->bounds.x,
                                      .y = row,
                                      .width = gradient->bounds.width,
                                      .height = 1},
                color.rgba) == 0) {
            return 0;
        }
    }
    return 1;
}

static int upload_image(void *context, db_render_ir_resource_id_t target,
                        const db_render_ir_upload_command_t *upload,
                        db_render_ir_external_binding_view_t bindings) {
    surface_lowering_t *const lowering = (surface_lowering_t *)context;
    const db_render_ir_external_binding_t *const source =
        (upload == NULL) ? NULL
                         : db_render_ir_find_binding(bindings, upload->source);
    (void)target;
    if ((lowering == NULL) || (lowering->surface == NULL) || (source == NULL) ||
        (source->pixels == NULL) ||
        (upload->semantics.replacement != DB_RENDER_IR_UPLOAD_REPLACE_EXACT) ||
        (upload->semantics.filter != DB_RENDER_IR_FILTER_NEAREST) ||
        (upload->semantics.conversion != DB_RENDER_IR_CONVERSION_EXACT) ||
        (db_equal_f64(upload->semantics.opacity, 1.0) == 0) ||
        (source->format != lowering->surface->format) ||
        (upload->source_rect.x != 0) || (upload->source_rect.y != 0) ||
        (upload->destination_x != 0) || (upload->destination_y != 0) ||
        ((uint32_t)upload->source_rect.width != source->width) ||
        ((uint32_t)upload->source_rect.height != source->height) ||
        (source->width != lowering->surface->pixel_width) ||
        (source->height != lowering->surface->pixel_height)) {
        return 0;
    }
    const size_t pixel_bytes = db_pixel_surface_pixel_bytes(lowering->surface);
    size_t row_bytes = 0U;
    size_t source_required_bytes = 0U;
    size_t destination_required_bytes = 0U;
    if ((pixel_bytes == 0U) ||
        (db_try_mul_size(source->width, pixel_bytes, &row_bytes) == 0) ||
        (db_try_strided_size(source->height, source->row_stride_bytes,
                             row_bytes, &source_required_bytes) == 0) ||
        (source_required_bytes > source->size_bytes) ||
        (db_try_mul_size(source->height, row_bytes,
                         &destination_required_bytes) == 0)) {
        return 0;
    }
    for (uint32_t row = 0U; row < source->height; row++) {
        const size_t destination_offset = (size_t)row * row_bytes;
        const size_t source_offset = (size_t)row * source->row_stride_bytes;
        if ((db_size_range_fits(destination_required_bytes, destination_offset,
                                row_bytes) == 0) ||
            (db_size_range_fits(source->size_bytes, source_offset, row_bytes) ==
             0)) {
            return 0;
        }
        memcpy((uint8_t *)lowering->surface->pixels + destination_offset,
               (const uint8_t *)source->pixels + source_offset, row_bytes);
    }
    return 1;
}

db_render_ir_status_t
db_render_ir_rasterize_surface(const db_render_ir_view_t *view,
                               uint32_t logical_width, uint32_t logical_height,
                               const db_pixel_surface_t *surface) {
    return db_render_ir_rasterize_surface_with_bindings(
        view, (db_render_ir_external_binding_view_t){0}, logical_width,
        logical_height, surface);
}

db_render_ir_status_t db_render_ir_rasterize_surface_with_bindings(
    const db_render_ir_view_t *view,
    db_render_ir_external_binding_view_t bindings, uint32_t logical_width,
    uint32_t logical_height, const db_pixel_surface_t *surface) {
    if ((view == NULL) || (surface == NULL) || (surface->pixels == NULL) ||
        (logical_width == 0U) || (logical_height == 0U) ||
        (logical_width > INT32_MAX) || (logical_height > INT32_MAX)) {
        return DB_RENDER_IR_INVALID;
    }
    surface_lowering_t lowering = {
        .surface = surface,
        .logical_width = logical_width,
        .logical_height = logical_height,
    };
    return db_render_ir_lower(view, bindings,
                              &(const db_render_ir_lowering_ops_t){
                                  .begin_target = begin_target,
                                  .end_target = end_target,
                                  .clear = clear_target,
                                  .fill_rects = fill_rects,
                                  .fill_linear_gradient = fill_linear_gradient,
                                  .upload_image = upload_image,
                              },
                              &lowering);
}

db_render_ir_status_t
db_frame_plan_rasterize_reference(const db_frame_plan_t *plan,
                                  const db_pixel_surface_t *surface) {
    if ((plan == NULL) || (surface == NULL) || (plan->rebuild_required == 0) ||
        (surface->pixel_width != plan->pixel_width) ||
        (surface->pixel_height != plan->pixel_height)) {
        return DB_RENDER_IR_INVALID;
    }
    db_render_ir_status_t status = db_render_ir_rasterize_surface_with_bindings(
        &plan->rebuild_ir, plan->external_bindings, plan->grid_cols,
        plan->grid_rows, surface);
    if (status == DB_RENDER_IR_OK) {
        status = db_render_ir_rasterize_surface_with_bindings(
            &plan->update_ir, plan->external_bindings, plan->grid_cols,
            plan->grid_rows, surface);
    }
    return status;
}
