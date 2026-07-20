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
        (rect.width <= 0) || (rect.height <= 0)) {
        return 0;
    }
    db_render_ir_rect_t logical_extent = {0};
    db_render_ir_rect_t clipped = {0};
    if ((db_render_ir_rect_from_extent(lowering->logical_width,
                                       lowering->logical_height,
                                       &logical_extent) == 0) ||
        (db_render_ir_rect_intersect(rect, logical_extent, &clipped) == 0)) {
        return 1;
    }
    db_grid_block_t logical = {0};
    if (db_render_ir_rect_to_grid_block(clipped, lowering->logical_width,
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
                     const db_render_ir_linear_gradient_command_t *gradient,
                     db_render_ir_rect_t coverage) {
    surface_lowering_t *const lowering = (surface_lowering_t *)context;
    (void)target;
    if ((gradient == NULL) || (coverage.height <= 0)) {
        return 0;
    }
    const int64_t row_end_i64 = (int64_t)coverage.y + (int64_t)coverage.height;
    if ((row_end_i64 < INT32_MIN) || (row_end_i64 > INT32_MAX)) {
        return 0;
    }
    const int32_t row_end = (int32_t)row_end_i64;
    for (int32_t row = coverage.y; row < row_end; row++) {
        const db_render_ir_color_t color =
            db_render_ir_linear_gradient_color_at(gradient, row);
        if (fill_logical_rect(lowering,
                              (db_render_ir_rect_t){.x = coverage.x,
                                                    .y = row,
                                                    .width = coverage.width,
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
    db_render_ir_external_binding_t source_storage = {0};
    const db_render_ir_external_binding_t *const source =
        ((upload != NULL) &&
         (db_render_ir_find_binding(bindings, upload->source,
                                    &source_storage) != 0))
            ? &source_storage
            : NULL;
    (void)target;
    if ((lowering == NULL) || (lowering->surface == NULL) || (source == NULL) ||
        (source->pixels == NULL) ||
        (upload->semantics.replacement != DB_RENDER_IR_UPLOAD_REPLACE_EXACT) ||
        (upload->semantics.filter != DB_RENDER_IR_FILTER_NEAREST) ||
        (upload->semantics.conversion != DB_RENDER_IR_CONVERSION_EXACT) ||
        (db_equal_f64(upload->semantics.opacity, 1.0) == 0) ||
        (source->format != lowering->surface->format) ||
        (lowering->logical_width != lowering->surface->pixel_width) ||
        (lowering->logical_height != lowering->surface->pixel_height)) {
        return 0;
    }
    const size_t pixel_bytes = db_pixel_surface_pixel_bytes(lowering->surface);
    size_t copy_row_bytes = 0U;
    size_t source_row_bytes = 0U;
    size_t destination_row_bytes = 0U;
    size_t source_required_bytes = 0U;
    size_t destination_required_bytes = 0U;
    size_t source_x_bytes = 0U;
    size_t source_y_bytes = 0U;
    size_t source_offset = 0U;
    size_t destination_x_bytes = 0U;
    size_t destination_y_bytes = 0U;
    size_t destination_offset = 0U;
    const size_t copy_rows = (size_t)(uint32_t)upload->source_rect.height;
    if ((pixel_bytes == 0U) ||
        (db_try_mul_size((size_t)(uint32_t)upload->source_rect.width,
                         pixel_bytes, &copy_row_bytes) == 0) ||
        (db_try_mul_size(source->width, pixel_bytes, &source_row_bytes) == 0) ||
        (db_try_mul_size(lowering->surface->pixel_width, pixel_bytes,
                         &destination_row_bytes) == 0) ||
        (db_try_strided_size(source->height, source->row_stride_bytes,
                             source_row_bytes, &source_required_bytes) == 0) ||
        (source_required_bytes > source->size_bytes) ||
        (db_try_mul_size(lowering->surface->pixel_height, destination_row_bytes,
                         &destination_required_bytes) == 0) ||
        (db_try_mul_size((size_t)(uint32_t)upload->source_rect.x, pixel_bytes,
                         &source_x_bytes) == 0) ||
        (db_try_mul_size((size_t)(uint32_t)upload->source_rect.y,
                         source->row_stride_bytes, &source_y_bytes) == 0) ||
        (db_try_add_size(source_y_bytes, source_x_bytes, &source_offset) ==
         0) ||
        (db_try_mul_size((size_t)(uint32_t)upload->destination_x, pixel_bytes,
                         &destination_x_bytes) == 0) ||
        (db_try_mul_size((size_t)(uint32_t)upload->destination_y,
                         destination_row_bytes, &destination_y_bytes) == 0) ||
        (db_try_add_size(destination_y_bytes, destination_x_bytes,
                         &destination_offset) == 0)) {
        return 0;
    }
    size_t source_copy_bytes = 0U;
    size_t destination_copy_bytes = 0U;
    if ((db_try_strided_size(copy_rows, source->row_stride_bytes,
                             copy_row_bytes, &source_copy_bytes) == 0) ||
        (db_try_strided_size(copy_rows, destination_row_bytes, copy_row_bytes,
                             &destination_copy_bytes) == 0) ||
        (source_offset > source->size_bytes) ||
        (source_copy_bytes > (source->size_bytes - source_offset)) ||
        (destination_offset > destination_required_bytes) ||
        (destination_copy_bytes >
         (destination_required_bytes - destination_offset))) {
        return 0;
    }
    const uintptr_t source_address = (uintptr_t)source->pixels;
    const uintptr_t destination_address = (uintptr_t)lowering->surface->pixels;
    if ((source_offset > (UINTPTR_MAX - source_address)) ||
        (destination_offset > (UINTPTR_MAX - destination_address)) ||
        (source_copy_bytes >
         (UINTPTR_MAX - (source_address + source_offset))) ||
        (destination_copy_bytes >
         (UINTPTR_MAX - (destination_address + destination_offset)))) {
        return 0;
    }
    const uint8_t *const source_bytes =
        (const uint8_t *)source->pixels + source_offset;
    uint8_t *const destination_bytes =
        (uint8_t *)lowering->surface->pixels + destination_offset;
    int overlap = 0;
    if ((db_memory_ranges_overlap(source_bytes, source_copy_bytes,
                                  destination_bytes, destination_copy_bytes,
                                  &overlap) == 0) ||
        ((overlap != 0) &&
         (source->row_stride_bytes != destination_row_bytes))) {
        return 0;
    }
    const int bottom_to_top =
        DB_BOOL((overlap != 0) &&
                ((uintptr_t)destination_bytes > (uintptr_t)source_bytes));
    for (size_t row_offset = 0U; row_offset < copy_rows; row_offset++) {
        const size_t row =
            (bottom_to_top != 0) ? copy_rows - 1U - row_offset : row_offset;
        memmove(destination_bytes + (row * destination_row_bytes),
                source_bytes + (row * source->row_stride_bytes),
                copy_row_bytes);
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
