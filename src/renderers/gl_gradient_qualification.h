#ifndef DRIVERBENCH_RENDERERS_GL_GRADIENT_QUALIFICATION_H
#define DRIVERBENCH_RENDERERS_GL_GRADIENT_QUALIFICATION_H

#include "core/db_gradient_divergence.h"
#include "core/db_render_types.h"
#include "renderers/gl_hash_readback.h"

int db_gl_qualify_current_framebuffer(
    const char *backend, const db_pixel_surface_t *reference,
    db_gl_framebuffer_hash_scratch_t *scratch,
    const db_gradient_compare_context_t *context, const char *divergence_path);

#endif
