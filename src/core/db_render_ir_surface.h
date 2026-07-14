#ifndef DRIVERBENCH_RENDER_IR_SURFACE_H
#define DRIVERBENCH_RENDER_IR_SURFACE_H

#include "db_frame_plan.h"
#include "db_render_ir.h"
#include "db_render_types.h"

#include <stdint.h>

db_render_ir_status_t
db_render_ir_rasterize_surface(const db_render_ir_view_t *view,
                               uint32_t logical_width, uint32_t logical_height,
                               const db_pixel_surface_t *surface);
db_render_ir_status_t db_render_ir_rasterize_surface_with_bindings(
    const db_render_ir_view_t *view,
    db_render_ir_external_binding_view_t bindings, uint32_t logical_width,
    uint32_t logical_height, const db_pixel_surface_t *surface);
db_render_ir_status_t
db_frame_plan_rasterize_reference(const db_frame_plan_t *plan,
                                  const db_pixel_surface_t *surface);

#endif
