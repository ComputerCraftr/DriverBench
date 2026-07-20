#ifndef DRIVERBENCH_GL3_QUALIFICATION_H
#define DRIVERBENCH_GL3_QUALIFICATION_H

#include "core/db_conformance.h"
#include "core/db_frame_plan.h"
#include "core/db_qualification_contracts.h"
#include "core/db_render_types.h"
#include "renderers/gl_hash_readback.h"

#include <stdint.h>

int db_gl3_describe_qualification(
    db_pixel_format_t format, uint32_t logical_width, uint32_t logical_height,
    db_gradient_implementation_t forced_implementation, int diagnostic_forced,
    int exact_lookup_available, uint64_t exact_lookup_hash,
    db_renderer_qualification_descriptor_store_t *store);
int db_gl3_qualify_current_target(const db_frame_plan_t *plan,
                                  db_pixel_format_t format,
                                  db_gl_framebuffer_hash_scratch_t *scratch,
                                  const char *divergence_path);

#endif
