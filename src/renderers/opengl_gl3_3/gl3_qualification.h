#ifndef DRIVERBENCH_GL3_QUALIFICATION_H
#define DRIVERBENCH_GL3_QUALIFICATION_H

#include "core/db_conformance_service.h"
#include "core/db_frame_plan.h"
#include "core/db_renderer_diagnostics.h"
#include "renderers/gl_hash_readback.h"

#include <stdint.h>

db_conformance_decision_t db_gl3_qualify_implementation(
    db_pixel_format_t format, uint32_t logical_width, uint32_t logical_height,
    const db_renderer_diagnostic_config_t *diagnostics,
    db_gradient_implementation_t implementation);
int db_gl3_qualify_current_target(const db_frame_plan_t *plan,
                                  db_pixel_format_t format,
                                  db_gl_framebuffer_hash_scratch_t *scratch,
                                  const char *divergence_path);

#endif
